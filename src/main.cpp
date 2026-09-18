#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <WiFi.h>
#include <SPI.h>
#include <XPT2046_Touchscreen.h>
#include "display_config.h"
#include "secrets.h"
#include "weather_client.h"
#include "spotify_client.h"
#include "scene.h"
#include "animation.h"

static Arduino_DataBus *bus = new Arduino_ESP32SPI(
    TFT_DC, TFT_CS, TFT_SCK, TFT_MOSI, TFT_MISO);

// Standard 240x320 ILI9341 (not IPS) -- PANEL_ROTATION handles the
// portrait->landscape swap, so no custom width/height/offset args are
// needed here (unlike the original board's nonstandard 172-wide ST7789
// panel, which needed those).
static Arduino_GFX *gfx = new Arduino_ILI9341(bus, TFT_RST, PANEL_ROTATION, false);

// The weather view's animated background (gradient + drifting glow +
// particles) redraws every ~150ms -- direct-to-panel drawing there is
// visibly torn/flickery (confirmed on-device) since each shape is its own
// SPI transaction. A single full-screen offscreen buffer would fix that,
// but this chip is a plain ESP32-D0WD-V3 with no PSRAM, and 320x240 RGB565
// (153,600 bytes) doesn't fit in one contiguous block on this heap
// (confirmed on-device: largest free block was ~110KB at boot, even before
// WiFi). Instead, the weather view is composed and flushed in 3 horizontal
// bands, each with its own small offscreen buffer (320x80 = 51,200 bytes,
// comfortably under that ~110KB ceiling) -- animDraw()/drawOverlay() take a
// yOffset to translate the full scene's coordinates into each band. The
// Spotify view doesn't need this: it only redraws once per view-visit (plus
// an occasional small marquee-line redraw), so it draws straight to `gfx`.
static const int BAND_COUNT = 3;
static const int BAND_H = PANEL_HEIGHT / BAND_COUNT;
static Arduino_Canvas *band[BAND_COUNT];

// Touch is on its own SPI bus (separate pins from the display), per this
// board's wiring -- a second SPIClass instance keeps it independent of the
// display's bus.
static SPIClass touchSPI(VSPI);
static XPT2046_Touchscreen ts(TOUCH_CS, TOUCH_IRQ);

static const unsigned long POLL_INTERVAL_MS = 60000;
static const unsigned long FRAME_INTERVAL_MS = 150;  // ~6.7fps ambient animation -- still reads as alive, meaningfully less continuous SPI/CPU work than 10fps
static unsigned long lastPollAt = 0;
static unsigned long lastSpotifyPollAt = 0;
static unsigned long lastFrameAt = 0;
static WeatherData lastGood;
static bool haveGoodFrame = false;

static SpotifyData lastGoodSpotify;
static uint16_t spotifyArtBuffer[SPOTIFY_ART_PIXELS];  // zero-initialized (black) until the first successful art fetch
static char lastFetchedArtId[40] = "";

enum ViewMode { VIEW_WEATHER, VIEW_SPOTIFY };
static ViewMode currentView = VIEW_WEATHER;
static unsigned long viewSwitchedAt = 0;
static const unsigned long VIEW_INTERVAL_MS = 60000;  // alternate every 1 minute
static bool spotifyDrawnForThisVisit = false;

// Any tap anywhere on the panel manually switches views -- deliberately not
// using touch coordinates at all, which sidesteps resistive-touch
// calibration entirely. A short cooldown stops a held finger (or a noisy
// resistive-touch IRQ) from rapid-toggling.
static unsigned long lastTouchToggleAt = 0;
static const unsigned long TOUCH_COOLDOWN_MS = 400;

static unsigned long nextWifiRetryAt = 0;
static const unsigned long WIFI_RETRY_INTERVAL_MS = 15000;

static void switchView() {
  currentView = (currentView == VIEW_WEATHER) ? VIEW_SPOTIFY : VIEW_WEATHER;
  viewSwitchedAt = millis();
  spotifyDrawnForThisVisit = false;
  Serial.printf("[view] switched to %s\n", currentView == VIEW_WEATHER ? "weather" : "spotify");
}

static void pollTouch() {
  if (!ts.touched()) return;
  unsigned long now = millis();
  if (now - lastTouchToggleAt < TOUCH_COOLDOWN_MS) return;
  lastTouchToggleAt = now;
  Serial.println("[touch] tap -- manual view switch");
  switchView();
}

// Only ever calls WiFi.begin() while STA is idle/disconnected, throttled to
// once per WIFI_RETRY_INTERVAL_MS -- calling begin() again while a previous
// attempt is still resolving makes the ESP-IDF WiFi driver throw
// "cannot set config" and never complete the handshake.
static void beginWiFiAttempt() {
  WiFi.disconnect();
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  nextWifiRetryAt = millis() + WIFI_RETRY_INTERVAL_MS;
}

// Blocking version used once at boot so the very first frame either shows
// the live weather or a clear "still connecting" status instead of racing
// the first poll against an unresolved WiFi connection.
static void connectWiFiBlocking() {
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(true);

  renderStatus(gfx, "CONNECTING WIFI", WIFI_SSID);
  beginWiFiAttempt();

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) {
    delay(250);
  }

  if (WiFi.status() != WL_CONNECTED) {
    renderStatus(gfx, "WIFI FAILED", "RETRYING...");
    Serial.println("[wifi] connect timed out");
  } else {
    Serial.printf("[wifi] connected, IP=%s\n", WiFi.localIP().toString().c_str());
  }
}

static void pollWeather() {
  WeatherData data;
  bool ok = fetchWeather(data);
  Serial.printf("[weather] fetch %s (temp_c=%.1f humidity=%.1f pressure=%.1f wind=%.1f)\n",
                ok ? "OK" : "FAILED", data.tempC, data.humidityPct, data.pressureHpa, data.windSpeedMph);

  if (ok) {
    lastGood = data;
    haveGoodFrame = true;
    animSetTargets(lastGood.tempC, lastGood.windSpeedMph, lastGood.pressureTrend.direction);
  }
  // On failure, keep animating/rendering the last good snapshot rather than
  // blanking, matching the PWA's own graceful-degradation behavior.
}

static void pollSpotify() {
  SpotifyData data;
  bool ok = fetchSpotifyNowPlaying(data);
  Serial.printf("[spotify] fetch %s (playing=%d track=%s)\n",
                ok ? "OK" : "FAILED", data.isPlaying, data.track);

  if (!ok) return;
  lastGoodSpotify = data;

  // Only re-fetch the ~12.8KB art blob when the track's art actually
  // changed -- mirrors the backend's own "don't re-encode art you already
  // have" optimization.
  if (data.isPlaying && data.artId[0] != '\0' && strcmp(data.artId, lastFetchedArtId) != 0) {
    bool artOk = fetchSpotifyArt(spotifyArtBuffer);
    Serial.printf("[spotify] art fetch %s (id=%s)\n", artOk ? "OK" : "FAILED", data.artId);
    if (artOk) {
      strncpy(lastFetchedArtId, data.artId, sizeof(lastFetchedArtId) - 1);
      lastFetchedArtId[sizeof(lastFetchedArtId) - 1] = '\0';
    }
  }
}

void setup() {
  Serial.begin(115200);

  pinMode(TFT_BL, OUTPUT);
  gfx->begin();

  for (int i = 0; i < BAND_COUNT; i++) {
    band[i] = new Arduino_Canvas(PANEL_WIDTH, BAND_H, gfx, 0, i * BAND_H);
    // GFX_SKIP_OUTPUT_BEGIN: gfx->begin() already ran above -- re-running it
    // per band would needlessly re-send the panel's full init sequence.
    if (!band[i]->begin(GFX_SKIP_OUTPUT_BEGIN)) {
      Serial.printf("[display] band %d allocation failed\n", i);
    }
  }

  analogWrite(TFT_BL, 230);  // ~90% brightness, avoids overheating the panel

  touchSPI.begin(TOUCH_SCK, TOUCH_MISO, TOUCH_MOSI, TOUCH_CS);
  ts.begin(touchSPI);

  animInit();

  renderStatus(gfx, "BACKYARD WEATHER", "STARTING...");

  if (WiFi.status() != WL_CONNECTED) {
    connectWiFiBlocking();
  }

  pollWeather();
  pollSpotify();
  lastPollAt = millis();
  lastSpotifyPollAt = millis();
  lastFrameAt = millis();
  viewSwitchedAt = millis();
}

void loop() {
  static bool wasConnected = true;  // connectWiFiBlocking() already ran before the first loop() call
  bool isConnected = WiFi.status() == WL_CONNECTED;
  if (isConnected != wasConnected) {
    Serial.printf("[wifi] %s\n", isConnected ? "reconnected" : "connection lost");
    wasConnected = isConnected;
  }

  if (!isConnected && millis() > nextWifiRetryAt) {
    beginWiFiAttempt();
  }

  pollTouch();

  if (millis() - lastPollAt >= POLL_INTERVAL_MS) {
    pollWeather();
    lastPollAt = millis();
  }

  if (millis() - lastSpotifyPollAt >= POLL_INTERVAL_MS) {
    pollSpotify();
    lastSpotifyPollAt = millis();
  }

  if (millis() - viewSwitchedAt >= VIEW_INTERVAL_MS) {
    switchView();
  }

  if (haveGoodFrame && millis() - lastFrameAt >= FRAME_INTERVAL_MS) {
    float dt = (millis() - lastFrameAt) / 1000.0f;
    lastFrameAt = millis();

    if (currentView == VIEW_SPOTIFY) {
      // Album art + track title are effectively static for the length of a
      // 1-minute visit -- draw once. The artist line keeps ticking after
      // that in case it's long enough to need the scrolling marquee.
      if (!spotifyDrawnForThisVisit) {
        drawSpotifyScreen(gfx, lastGoodSpotify, spotifyArtBuffer);
        spotifyDrawnForThisVisit = true;
      } else {
        spotifyMarqueeTick(gfx, dt);
      }
    } else {
      animAdvance(dt);
      for (int i = 0; i < BAND_COUNT; i++) {
        int yOffset = i * BAND_H;
        animDraw(band[i], yOffset);
        drawOverlay(band[i], yOffset, lastGood);
        band[i]->flush();
      }
    }
  } else if (!haveGoodFrame) {
    renderStatus(gfx, "WAITING FOR DATA", WEATHER_API_HOST);
    delay(500);
  }

  // Without this, the loop spins as fast as the CPU allows checking millis()
  // between ticks -- pegging the core at 100% continuously with no benefit,
  // since nothing here needs sub-millisecond response. This yield lets the
  // idle task (and the chip's power-management/light-sleep opportunities)
  // actually run, which is the single biggest lever on running-hot heat.
  delay(2);
}
