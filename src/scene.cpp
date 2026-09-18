#include "scene.h"
#include "colors.h"
#include "display_config.h"
#include <math.h>

// ---- layout constants -- landscape 320x240 (this board's native panel is
// 240x320 portrait ILI9341, run rotated). First-pass numbers, ported from
// the original 172x320-portrait layout's vertical stack (pill -> numeral ->
// unit label -> 3 stacked stat rows); tune against the real panel after the
// first live frame. -----------------------------------------------------
static const int SCREEN_W = PANEL_WIDTH;
static const int SCREEN_H = PANEL_HEIGHT;

// Left column: big numeral + condition pill + unit label. Right column: the
// three stat rows, stacked -- the extra width landscape gives us goes into
// putting these side by side instead of the original's single narrow stack.
static const int LEFT_W = 185;
static const int RIGHT_X = 190;

static const int NUMERAL_TEXT_SIZE = 5;  // 6*5=30px per glyph cell
static const int NUMERAL_Y = 78;
static const int UNIT_LABEL_Y = 134;

static const int STAT_ROW_Y[3] = {48, 112, 176};
static const int STAT_ROW_X = RIGHT_X;
static const int SPARK_W = 30;
static const int SPARK_H = 14;

enum DotShape { DOT_CIRCLE, DOT_SQUARE, DOT_TRIANGLE };

// ---- small text helpers ---------------------------------------------------

static int glyphAdvance(int textSize, int extraGapPx = 0) {
  return 6 * textSize + extraGapPx;
}

static int textWidthTracked(const char *s, int textSize, int extraGapPx = 0) {
  return (int)strlen(s) * glyphAdvance(textSize, extraGapPx);
}

// Draws letter-spaced (tracked) text -- the default GFX font has no built-in
// tracking, and the PWA's caption labels rely on generous letter-spacing
// (0.18em-0.3em) for that Bauhaus-poster look.
static void drawTracked(Arduino_GFX *gfx, int x, int y, const char *s, int textSize,
                         uint16_t color, int extraGapPx = 0) {
  gfx->setTextSize(textSize);
  gfx->setTextColor(color);
  int advance = glyphAdvance(textSize, extraGapPx);
  int cx = x;
  for (const char *p = s; *p; p++) {
    gfx->setCursor(cx, y);
    gfx->print(*p);
    cx += advance;
  }
}

// Centers text within [x0, x0+w) rather than always the full screen -- the
// landscape layout needs text centered within the left numeral column, not
// across the whole 320px panel.
static void drawCentered(Arduino_GFX *gfx, int x0, int w, int y, const char *s, int textSize,
                          uint16_t color, int extraGapPx = 0) {
  int tw = textWidthTracked(s, textSize, extraGapPx);
  int x = x0 + (w - tw) / 2;
  if (x < x0) x = x0;
  drawTracked(gfx, x, y, s, textSize, color, extraGapPx);
}

// Picks the largest text size (down to minSize) whose tracked width fits
// within maxWidth -- the big numeral's string length varies (temps can be
// negative/3-digit, prices vary by symbol), so a fixed size risks overflow.
static int pickFittingTextSize(const char *s, int maxWidth, int maxSize, int minSize = 3) {
  for (int sz = maxSize; sz >= minSize; sz--) {
    if (textWidthTracked(s, sz) <= maxWidth) return sz;
  }
  return minSize;
}

// ---- condition pill ---------------------------------------------------

static void drawConditionPill(Arduino_GFX *gfx, int y, const char *label) {
  const int textSize = 1;
  const int gap = 2;
  int textW = textWidthTracked(label, textSize, gap);
  int padX = 6, padY = 4;
  int w = textW + padX * 2;
  int h = 8 * textSize + padY * 2;
  int x = SCREEN_W - w - 8;
  gfx->fillRect(x, y, w, h, BAUHAUS_RED);
  drawTracked(gfx, x + padX, y + padY, label, textSize, COLOR_WHITE, gap);
}

// ---- sparkline --------------------------------------------------------

static void drawSparkline(Arduino_GFX *gfx, int x, int y, int w, int h, const Trend &trend) {
  if (trend.count < 2) {
    // no trend data yet -- a faint flat dashed line as a placeholder
    for (int dx = 0; dx < w; dx += 4) {
      gfx->drawPixel(x + dx, y + h / 2, RGB565(120, 120, 120));
    }
    return;
  }

  float minV = trend.series[0], maxV = trend.series[0];
  for (int i = 1; i < trend.count; i++) {
    if (trend.series[i] < minV) minV = trend.series[i];
    if (trend.series[i] > maxV) maxV = trend.series[i];
  }
  float range = (maxV - minV);
  if (range < 0.0001f) range = 1.0f;

  uint16_t color = BAUHAUS_YELLOW;  // steady, or unknown direction
  if (strcmp(trend.direction, "rising") == 0) color = BAUHAUS_RED;
  else if (strcmp(trend.direction, "falling") == 0) color = BAUHAUS_BLUE;

  const int pad = 3;
  float stepX = (float)(w - pad * 2) / (trend.count - 1);

  int prevX = 0, prevY = 0;
  for (int i = 0; i < trend.count; i++) {
    int px = x + pad + (int)round(i * stepX);
    int py = y + h - pad - (int)round(((trend.series[i] - minV) / range) * (h - pad * 2));
    if (i > 0) gfx->drawLine(prevX, prevY, px, py, color);
    prevX = px;
    prevY = py;
  }
  gfx->fillCircle(prevX, prevY, 2, color);
}

// ---- stat rows ----------------------------------------------------------

static void drawDot(Arduino_GFX *gfx, int cx, int cy, DotShape shape, uint16_t color) {
  switch (shape) {
    case DOT_CIRCLE:
      gfx->fillCircle(cx, cy, 6, color);
      break;
    case DOT_SQUARE:
      gfx->fillRect(cx - 5, cy - 5, 10, 10, color);
      break;
    case DOT_TRIANGLE:
      gfx->fillTriangle(cx, cy - 7, cx - 7, cy + 5, cx + 7, cy + 5, color);
      break;
  }
}

static void drawStatRow(Arduino_GFX *gfx, int y, DotShape shape, uint16_t dotColor,
                         bool hasValue, float value, const char *unit, const char *label,
                         const Trend &trend) {
  int x = STAT_ROW_X;
  int rowMidY = y + 8;

  drawDot(gfx, x + 6, rowMidY, shape, dotColor);
  x += 20;

  char valueStr[16];
  if (hasValue) {
    snprintf(valueStr, sizeof(valueStr), "%.1f%s", value, unit);
  } else {
    snprintf(valueStr, sizeof(valueStr), "--%s", unit);
  }
  const int valueTextSize = 2;
  gfx->setTextSize(valueTextSize);
  gfx->setTextColor(COLOR_WHITE);
  gfx->setCursor(x, y);
  gfx->print(valueStr);
  x += textWidthTracked(valueStr, valueTextSize) + 6;

  drawSparkline(gfx, x, y - 2, SPARK_W, SPARK_H, trend);
  x += SPARK_W + 6;

  drawTracked(gfx, x, y + 4, label, 1, RGB565(230, 230, 230), 1);
}

// Truncates src to fit within maxWidthPx at the given text size, appending
// "..." when it doesn't fit -- track/artist names are arbitrary length and
// need a hard cutoff at small sizes rather than shrinking arbitrarily small
// like the numeral does.
static void truncateToFit(char *dest, size_t destSize, const char *src, int textSize, int maxWidthPx) {
  int maxChars = maxWidthPx / glyphAdvance(textSize);
  if (maxChars < 1) maxChars = 1;
  int len = (int)strlen(src);
  if (len <= maxChars) {
    strncpy(dest, src, destSize - 1);
    dest[destSize - 1] = '\0';
    return;
  }
  int keep = maxChars - 3;
  if (keep < 1) keep = 1;
  if ((size_t)keep > destSize - 4) keep = destSize - 4;
  strncpy(dest, src, keep);
  strcpy(dest + keep, "...");
}

// ---- public entry points ------------------------------------------------

// Draws the pill/numeral/unit label (left column) and stat rows (right
// column) on top of whatever background is already in `target` (a band of
// an animated frame drawn by animation.cpp's animDraw() for this same
// yOffset) -- this function never touches the background itself. Every
// absolute layout Y constant is translated by -yOffset into target's local
// coordinate space; anything landing outside target's own bounds is
// naturally clipped away by Arduino_GFX.
void drawOverlay(Arduino_GFX *target, int yOffset, const WeatherData &data) {
  drawConditionPill(target, 8 - yOffset, conditionLabel(data));

  // Big numeral: Fahrenheit, matching the PWA's default unit. Centered
  // within the left column, not the full panel width, so it doesn't run
  // into the stat-row column on the right.
  char tempStr[8] = "--";
  if (data.hasTemp) {
    float tempF = data.tempC * 9.0f / 5.0f + 32.0f;
    snprintf(tempStr, sizeof(tempStr), "%.1f", tempF);
  }
  int sz = pickFittingTextSize(tempStr, LEFT_W - 22, NUMERAL_TEXT_SIZE);  // leave room for the degree dot
  int numeralW = textWidthTracked(tempStr, sz);
  int numeralX = (LEFT_W - numeralW - 18) / 2;
  if (numeralX < 4) numeralX = 4;
  drawTracked(target, numeralX, NUMERAL_Y - yOffset, tempStr, sz, COLOR_WHITE);
  target->fillCircle(numeralX + numeralW + 8, NUMERAL_Y + 4 - yOffset, 5, BAUHAUS_YELLOW);

  drawCentered(target, 0, LEFT_W, UNIT_LABEL_Y - yOffset, "FAHRENHEIT", 1, RGB565(255, 255, 255), 3);

  drawStatRow(target, STAT_ROW_Y[0] - yOffset, DOT_CIRCLE, BAUHAUS_RED,
              data.hasHumidity, data.humidityPct, "%", "RH", data.humidityTrend);
  drawStatRow(target, STAT_ROW_Y[1] - yOffset, DOT_SQUARE, BAUHAUS_BLUE,
              data.hasPressure, data.pressureHpa, "", "HPA", data.pressureTrend);
  drawStatRow(target, STAT_ROW_Y[2] - yOffset, DOT_TRIANGLE, BAUHAUS_YELLOW,
              data.hasWind, data.windSpeedMph, "", "MPH", data.windTrend);
}

static inline void unpack565(uint16_t c, uint8_t &r, uint8_t &g, uint8_t &b) {
  r = (c >> 11) & 0x1F;
  g = (c >> 5) & 0x3F;
  b = c & 0x1F;
  r = (r << 3) | (r >> 2);  // 5->8 bit
  g = (g << 2) | (g >> 4);  // 6->8 bit
  b = (b << 3) | (b >> 2);  // 5->8 bit
}

// Cover-crops the SPOTIFY_ART_SIZE square art to fill the whole panel --
// scales up by whichever factor covers the larger-relative-to-art dimension
// (width, on this landscape panel; height on the original portrait one),
// then center-crops the resulting rectangle on the other axis. Generalized
// from the original's height-only version so this works for either
// orientation. Bilinear-sampled per destination pixel rather than a
// nearest-neighbor block fill -- an 80px source stretched 4x reads visibly
// blocky otherwise. Drawn once per view-visit, so the extra per-pixel cost
// here is a one-time thing, not a per-animation-frame one.
static void drawArtBackground(Arduino_GFX *gfx, const uint16_t *artBuffer) {
  const float scaleX = (float)SCREEN_W / SPOTIFY_ART_SIZE;
  const float scaleY = (float)SCREEN_H / SPOTIFY_ART_SIZE;
  const float scale = scaleX > scaleY ? scaleX : scaleY;  // cover both dimensions
  const float scaledW = SPOTIFY_ART_SIZE * scale;
  const float scaledH = SPOTIFY_ART_SIZE * scale;
  const float cropX = (scaledW - SCREEN_W) / 2.0f;
  const float cropY = (scaledH - SCREEN_H) / 2.0f;

  for (int dy = 0; dy < SCREEN_H; dy++) {
    float syf = (dy + cropY) / scale;
    int sy0 = (int)syf;
    if (sy0 < 0) sy0 = 0;
    int sy1 = sy0 + 1 < SPOTIFY_ART_SIZE ? sy0 + 1 : sy0;
    float fy = syf - sy0;

    for (int dx = 0; dx < SCREEN_W; dx++) {
      float sxf = (dx + cropX) / scale;
      int sx0 = (int)sxf;
      if (sx0 < 0) sx0 = 0;
      int sx1 = sx0 + 1 < SPOTIFY_ART_SIZE ? sx0 + 1 : sx0;
      float fx = sxf - sx0;

      uint8_t r00, g00, b00, r10, g10, b10, r01, g01, b01, r11, g11, b11;
      unpack565(artBuffer[sy0 * SPOTIFY_ART_SIZE + sx0], r00, g00, b00);
      unpack565(artBuffer[sy0 * SPOTIFY_ART_SIZE + sx1], r10, g10, b10);
      unpack565(artBuffer[sy1 * SPOTIFY_ART_SIZE + sx0], r01, g01, b01);
      unpack565(artBuffer[sy1 * SPOTIFY_ART_SIZE + sx1], r11, g11, b11);

      float w00 = (1 - fx) * (1 - fy), w10 = fx * (1 - fy), w01 = (1 - fx) * fy, w11 = fx * fy;
      uint8_t r = (uint8_t)(r00 * w00 + r10 * w10 + r01 * w01 + r11 * w11);
      uint8_t g = (uint8_t)(g00 * w00 + g10 * w10 + g01 * w01 + g11 * w11);
      uint8_t b = (uint8_t)(b00 * w00 + b10 * w10 + b01 * w01 + b11 * w11);

      gfx->writePixel(dx, dy, RGB565(r, g, b));
    }
  }
}

// ---- spotify layout + artist marquee -------------------------------------

// Shorter than the original (92px on a 320-tall portrait panel) since this
// panel is only 240px tall overall -- tune against the live panel.
static const int SPOTIFY_SCRIM_H = 78;
static const uint16_t SPOTIFY_SCRIM_COLOR = RGB565(10, 10, 14);
static const int SPOTIFY_TEXT_X = 14;
static const int SPOTIFY_ARTIST_LINE_H = 12;

// Artist names routinely run longer than the panel at a readable size --
// rather than truncating with "...", scroll the full name back and forth so
// it's all readable over time. Track title stays truncated (shorter, less
// often cut off, and one moving line is enough).
static bool marqueeActive = false;
static char marqueeText[64] = "";
static float marqueeOffset = 0.0f;
static float marqueeMaxOffset = 0.0f;
static float marqueeTimer = 0.0f;
enum MarqueeState { M_PAUSE_START, M_SCROLLING, M_PAUSE_END };
static MarqueeState marqueeState = M_PAUSE_START;
static const float MARQUEE_PAUSE_S = 1.5f;
static const float MARQUEE_SPEED_PX_S = 30.0f;
static int marqueeArtistY = 0;

static void drawArtistLine(Arduino_GFX *gfx, float offset) {
  // Clear the row first (erases whatever was drawn there last tick), then
  // draw the full text at the scrolled offset, then re-mask the right
  // margin -- the canvas's own left/right bounds checks clip anything that
  // scrolls past the panel edges, so no explicit left mask is needed.
  gfx->fillRect(0, marqueeArtistY - 2, SCREEN_W, SPOTIFY_ARTIST_LINE_H, SPOTIFY_SCRIM_COLOR);
  drawTracked(gfx, SPOTIFY_TEXT_X - (int)offset, marqueeArtistY, marqueeText, 1, RGB565(210, 210, 210), 1);
  gfx->fillRect(SCREEN_W - SPOTIFY_TEXT_X, marqueeArtistY - 2, SPOTIFY_TEXT_X, SPOTIFY_ARTIST_LINE_H, SPOTIFY_SCRIM_COLOR);
}

// Advances the marquee (if active) and redraws just the artist line -- not
// the album art or track title -- so this stays cheap enough to call every
// animation tick. Returns true if it redrew anything (caller should flush).
bool spotifyMarqueeTick(Arduino_GFX *gfx, float dtSeconds) {
  if (!marqueeActive) return false;

  marqueeTimer += dtSeconds;
  switch (marqueeState) {
    case M_PAUSE_START:
      if (marqueeTimer >= MARQUEE_PAUSE_S) { marqueeState = M_SCROLLING; marqueeTimer = 0; }
      break;
    case M_SCROLLING:
      marqueeOffset += MARQUEE_SPEED_PX_S * dtSeconds;
      if (marqueeOffset >= marqueeMaxOffset) { marqueeOffset = marqueeMaxOffset; marqueeState = M_PAUSE_END; marqueeTimer = 0; }
      break;
    case M_PAUSE_END:
      if (marqueeTimer >= MARQUEE_PAUSE_S) { marqueeState = M_PAUSE_START; marqueeTimer = 0; marqueeOffset = 0; }
      break;
  }

  drawArtistLine(gfx, marqueeOffset);
  return true;
}

void drawSpotifyScreen(Arduino_GFX *gfx, const SpotifyData &data, const uint16_t *artBuffer) {
  marqueeActive = false;

  if (!data.isPlaying) {
    gfx->fillScreen(COLOR_BLACK);
    drawCentered(gfx, 0, SCREEN_W, SCREEN_H / 2 - 6, "NOTHING PLAYING", 1, RGB565(150, 150, 150), 2);
    return;
  }

  drawArtBackground(gfx, artBuffer);

  // Bottom scrim -- no alpha blending on this panel, so a solid dark bar
  // stands in for the gradient fade a real "now playing" screen would use,
  // guaranteeing the text reads regardless of the art's own colors.
  gfx->fillRect(0, SCREEN_H - SPOTIFY_SCRIM_H, SCREEN_W, SPOTIFY_SCRIM_H, SPOTIFY_SCRIM_COLOR);

  char trackFit[40];
  truncateToFit(trackFit, sizeof(trackFit), data.track, 2, SCREEN_W - 2 * SPOTIFY_TEXT_X);

  int textY = SCREEN_H - SPOTIFY_SCRIM_H + 12;
  drawTracked(gfx, SPOTIFY_TEXT_X, textY, trackFit, 2, COLOR_WHITE);

  marqueeArtistY = textY + 24;
  strncpy(marqueeText, data.artist, sizeof(marqueeText) - 1);
  marqueeText[sizeof(marqueeText) - 1] = '\0';
  int artistWidth = textWidthTracked(marqueeText, 1, 1);
  int availableWidth = SCREEN_W - 2 * SPOTIFY_TEXT_X;
  marqueeMaxOffset = artistWidth > availableWidth ? (artistWidth - availableWidth) : 0;
  marqueeOffset = 0;
  marqueeTimer = 0;
  marqueeState = M_PAUSE_START;
  marqueeActive = marqueeMaxOffset > 0;
  drawArtistLine(gfx, 0);

  if (data.durationMs > 0) {
    int barY = SCREEN_H - 16;
    int barW = SCREEN_W - 28;
    float frac = (float)data.progressMs / (float)data.durationMs;
    if (frac < 0) frac = 0;
    if (frac > 1) frac = 1;
    gfx->fillRect(14, barY, barW, 4, RGB565(70, 70, 76));
    gfx->fillRect(14, barY, (int)(barW * frac), 4, BAUHAUS_YELLOW);
  }
}

void renderStatus(Arduino_GFX *gfx, const char *line1, const char *line2) {
  gfx->fillScreen(COLOR_BLACK);
  drawCentered(gfx, 0, SCREEN_W, SCREEN_H / 2 - 10, line1, 1, COLOR_WHITE, 1);
  if (line2) drawCentered(gfx, 0, SCREEN_W, SCREEN_H / 2 + 10, line2, 1, RGB565(180, 180, 180), 1);
}
