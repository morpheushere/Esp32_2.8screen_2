#include "animation.h"
#include "colors.h"
#include "display_config.h"
#include <Arduino.h>
#include <math.h>
#include <string.h>

// ---- onboard discrete RGB LED (Cheap Yellow Display factory wiring) ------
// Unlike the Waveshare C6 board's single WS2811 addressable LED, this board
// has three plain PWM-driven LEDs (LED_R_PIN/LED_G_PIN/LED_B_PIN), active
// LOW (common anode) -- 0 duty is full brightness, 255 duty is off.

// ---- layout (mirrors scene.cpp's glow position) --------------------------
static const int SCREEN_W = PANEL_WIDTH;
static const int SCREEN_H = PANEL_HEIGHT;
static const int GLOW_CY = SCREEN_H - 25;
static const int GLOW_MAX_R = 130;
static const int GLOW_STEPS = 16;

// ---- particles -------------------------------------------------------
struct Particle {
  float x, y;
  float size;
  float speed;
  float twinkle;
};

static const int BASE_PARTICLES = 22;
static const int MAX_PARTICLES = 50;
static Particle particles[MAX_PARTICLES];
static int activeParticles = BASE_PARTICLES;

static float randf() { return (float)random(0, 10000) / 10000.0f; }

static void spawnParticle(Particle &p) {
  p.x = randf() * SCREEN_W;
  p.y = randf() * SCREEN_H;
  p.size = 1.0f + randf() * 1.6f;
  p.speed = 0.4f + randf() * 0.8f;
  p.twinkle = randf() * 6.28f;
}

// ---- eased state -- polled values are targets, these are what's drawn ----
static float currentTempC = 15.0f, targetTempC = 15.0f;
static float currentWind = 0.0f, targetWind = 0.0f;
static char pressureDirection[8] = "";

static float t = 0.0f;          // drives the flow field + glow drift path
static float glowPhase = 0.0f;  // pressure "breathing" phase
static float ledPhase = 0.0f;   // LED pulse phase

// Computed once per frame by animAdvance(), consumed by animDraw() -- lets
// animDraw() be called multiple times (once per band) against a single
// consistent snapshot of "what this frame looks like" without redoing the
// physics or re-rolling twinkle/movement each band pass.
static uint8_t bgR = 20, bgG = 24, bgB = 40;
static int glowCx = 0, glowCy = 0, glowMaxR = 0;

void animInit() {
  randomSeed(analogRead(0));
  for (int i = 0; i < MAX_PARTICLES; i++) spawnParticle(particles[i]);

  pinMode(LED_R_PIN, OUTPUT);
  pinMode(LED_G_PIN, OUTPUT);
  pinMode(LED_B_PIN, OUTPUT);
  analogWrite(LED_R_PIN, 255);  // off (active low)
  analogWrite(LED_G_PIN, 255);
  analogWrite(LED_B_PIN, 255);
}

void animSetTargets(float tempC, float windSpeedMph, const char *pressureDir) {
  targetTempC = tempC;
  targetWind = windSpeedMph;
  strncpy(pressureDirection, pressureDir ? pressureDir : "", sizeof(pressureDirection) - 1);
  pressureDirection[sizeof(pressureDirection) - 1] = '\0';
}

static void updateLED(uint8_t r, uint8_t g, uint8_t b, float dtSeconds) {
  // Pulse rate scales with temperature: slow calm breathing when cold,
  // quickening toward a faster pulse as it gets hotter -- same idea as a
  // heartbeat picking up. Range picked for -10C..34C, matching TEMP_STOPS.
  float tNorm = (currentTempC - (-10.0f)) / (34.0f - (-10.0f));
  if (tNorm < 0) tNorm = 0;
  if (tNorm > 1) tNorm = 1;
  float pulseHz = lerpf(0.22f, 1.0f, tNorm);

  ledPhase += dtSeconds * pulseHz * 2.0f * PI;
  float brightness = 0.35f + 0.65f * (0.5f + 0.5f * sinf(ledPhase));

  // Active-low LEDs: invert (scaled channel) so higher brightness means
  // lower PWM duty.
  analogWrite(LED_R_PIN, 255 - (uint8_t)(r * brightness));
  analogWrite(LED_G_PIN, 255 - (uint8_t)(g * brightness));
  analogWrite(LED_B_PIN, 255 - (uint8_t)(b * brightness));
}

void animAdvance(float dtSeconds) {
  const float k = 0.06f;  // per-tick smoothing toward target (frame-rate dependent, tuned for ~10fps)
  currentTempC += (targetTempC - currentTempC) * k;
  currentWind += (targetWind - currentWind) * k;

  t += dtSeconds * 0.35f;

  float pulseSpeed = strcmp(pressureDirection, "rising") == 0    ? 2.4f
                      : strcmp(pressureDirection, "falling") == 0 ? 0.9f
                                                                   : 1.5f;
  glowPhase += dtSeconds * pulseSpeed;
  float pulse = 0.85f + 0.15f * sinf(glowPhase);

  tempToColorRGB(currentTempC, bgR, bgG, bgB);
  updateLED(bgR, bgG, bgB, dtSeconds);

  // Drifting glow -- a slow Lissajous wander instead of a fixed point, same
  // idea as the PWA's independent second layer of motion.
  glowCx = SCREEN_W / 2 + (int)(sinf(t * 0.8f) * SCREEN_W * 0.22f);
  glowCy = GLOW_CY + (int)(cosf(t * 0.55f) * 24.0f);
  glowMaxR = (int)(GLOW_MAX_R * pulse);

  // Particle count scales gently with wind -- calm days stay near the base
  // count so the scene is never static, windy days get a busier field.
  activeParticles = BASE_PARTICLES + (int)(currentWind * 1.1f);
  if (activeParticles > MAX_PARTICLES) activeParticles = MAX_PARTICLES;
  if (activeParticles < BASE_PARTICLES) activeParticles = BASE_PARTICLES;

  float windMag = 0.35f + currentWind * 0.05f;  // always > 0: baseline drift even when calm

  for (int i = 0; i < activeParticles; i++) {
    Particle &p = particles[i];
    float angle = sinf(p.x * 0.02f + t) + cosf(p.y * 0.02f - t * 0.8f);
    float spd = p.speed * windMag;
    p.x += cosf(angle) * spd;
    p.y += sinf(angle) * spd;

    if (p.x < -4) p.x = SCREEN_W + 4;
    else if (p.x > SCREEN_W + 4) p.x = -4;
    if (p.y < -4) p.y = SCREEN_H + 4;
    else if (p.y > SCREEN_H + 4) p.y = -4;

    p.twinkle += 0.12f;
  }
}

void animDraw(Arduino_GFX *target, int yOffset) {
  int bandH = target->height();

  target->fillScreen(RGB565(bgR, bgG, bgB));

  for (int i = GLOW_STEPS; i >= 1; i--) {
    float ft = (float)i / GLOW_STEPS;
    float amount = (1.0f - ft) * 0.35f;
    uint8_t lr = lightenChannel(bgR, amount);
    uint8_t lg = lightenChannel(bgG, amount);
    uint8_t lb = lightenChannel(bgB, amount);
    // Same absolute center/radius drawn into every band -- Arduino_GFX
    // clips to the band's own local bounds, so together the bands
    // reconstruct one seamless circle even though no single band holds
    // the whole thing in memory at once.
    target->fillCircle(glowCx, glowCy - yOffset, (int)(glowMaxR * ft), RGB565(lr, lg, lb));
  }

  uint8_t pr = lightenChannel(bgR, 0.7f);
  uint8_t pg = lightenChannel(bgG, 0.7f);
  uint8_t pb = lightenChannel(bgB, 0.7f);

  for (int i = 0; i < activeParticles; i++) {
    Particle &p = particles[i];
    int localY = (int)p.y - yOffset;
    if (localY < -4 || localY > bandH + 4) continue;  // not in this band, skip the work

    float twinkleAmt = 0.5f + 0.5f * sinf(p.twinkle);
    uint8_t tr = lightenChannel(pr, twinkleAmt * 0.3f);
    uint8_t tg = lightenChannel(pg, twinkleAmt * 0.3f);
    uint8_t tb = lightenChannel(pb, twinkleAmt * 0.3f);

    target->fillCircle((int)p.x, localY, (int)p.size, RGB565(tr, tg, tb));
  }
}
