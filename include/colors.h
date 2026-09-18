#pragma once
#include <Arduino.h>

// Ported 1:1 from strava-heatmap-pwa/pwa/src/weather-scene.js (TEMP_STOPS)
// and index.html's --bauhaus-* custom properties, so the LCD reads as the
// same ambient display family as the phone/PWA dashboard.

// Arduino_GFX.h already defines an identical RGB565 macro when included --
// only define our own when compiled standalone (e.g. without the GFX lib).
#ifndef RGB565
#define RGB565(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))
#endif

static const uint16_t BAUHAUS_RED    = RGB565(0xE4, 0x03, 0x2E);
static const uint16_t BAUHAUS_YELLOW = RGB565(0xFF, 0xC9, 0x00);
static const uint16_t BAUHAUS_BLUE   = RGB565(0x00, 0x5E, 0xB8);
static const uint16_t COLOR_WHITE    = RGB565(0xFF, 0xFF, 0xFF);
static const uint16_t COLOR_BLACK    = RGB565(0x00, 0x00, 0x00);

struct TempStop {
  float tempC;
  uint8_t r, g, b;
};

// -10=deep indigo, 0=cold blue, 12=cool blue, 20=teal, 26=amber, 34=hot red
static const TempStop TEMP_STOPS[] = {
  {-10, 27, 31, 59},
  {0, 46, 111, 149},
  {12, 46, 134, 171},
  {20, 78, 205, 196},
  {26, 255, 159, 28},
  {34, 255, 90, 54},
};
static const int TEMP_STOPS_COUNT = sizeof(TEMP_STOPS) / sizeof(TEMP_STOPS[0]);

inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }

// Returns an RGB565 color for a given Celsius temperature, matching the
// PWA's gradient exactly (clamped at the ends, linear-interpolated between).
inline uint16_t tempToColor565(float tempC) {
  if (isnan(tempC)) return RGB565(20, 24, 40);
  if (tempC <= TEMP_STOPS[0].tempC) {
    const TempStop &s = TEMP_STOPS[0];
    return RGB565(s.r, s.g, s.b);
  }
  if (tempC >= TEMP_STOPS[TEMP_STOPS_COUNT - 1].tempC) {
    const TempStop &s = TEMP_STOPS[TEMP_STOPS_COUNT - 1];
    return RGB565(s.r, s.g, s.b);
  }
  for (int i = 0; i < TEMP_STOPS_COUNT - 1; i++) {
    const TempStop &s0 = TEMP_STOPS[i];
    const TempStop &s1 = TEMP_STOPS[i + 1];
    if (tempC >= s0.tempC && tempC <= s1.tempC) {
      float t = (tempC - s0.tempC) / (s1.tempC - s0.tempC);
      uint8_t r = round(lerpf(s0.r, s1.r, t));
      uint8_t g = round(lerpf(s0.g, s1.g, t));
      uint8_t b = round(lerpf(s0.b, s1.b, t));
      return RGB565(r, g, b);
    }
  }
  const TempStop &s = TEMP_STOPS[TEMP_STOPS_COUNT - 1];
  return RGB565(s.r, s.g, s.b);
}

// Also returns the raw RGB so callers can lighten it for the glow effect.
inline void tempToColorRGB(float tempC, uint8_t &r, uint8_t &g, uint8_t &b) {
  if (isnan(tempC)) { r = 20; g = 24; b = 40; return; }
  if (tempC <= TEMP_STOPS[0].tempC) {
    r = TEMP_STOPS[0].r; g = TEMP_STOPS[0].g; b = TEMP_STOPS[0].b; return;
  }
  if (tempC >= TEMP_STOPS[TEMP_STOPS_COUNT - 1].tempC) {
    const TempStop &s = TEMP_STOPS[TEMP_STOPS_COUNT - 1];
    r = s.r; g = s.g; b = s.b; return;
  }
  for (int i = 0; i < TEMP_STOPS_COUNT - 1; i++) {
    const TempStop &s0 = TEMP_STOPS[i];
    const TempStop &s1 = TEMP_STOPS[i + 1];
    if (tempC >= s0.tempC && tempC <= s1.tempC) {
      float t = (tempC - s0.tempC) / (s1.tempC - s0.tempC);
      r = round(lerpf(s0.r, s1.r, t));
      g = round(lerpf(s0.g, s1.g, t));
      b = round(lerpf(s0.b, s1.b, t));
      return;
    }
  }
}

inline uint8_t lightenChannel(uint8_t c, float amount) {
  return (uint8_t)round(c + (255 - c) * amount);
}
