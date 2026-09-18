#pragma once
#include <Arduino.h>

#define TREND_SERIES_MAX 10

struct Trend {
  char direction[8];  // "rising" | "falling" | "steady" | "" (unknown)
  float series[TREND_SERIES_MAX];
  int count;
};

struct WeatherData {
  bool valid;          // false if the fetch/parse failed -- caller should keep showing the last good frame
  bool hasTemp;
  float tempC;
  bool hasHumidity;
  float humidityPct;
  bool hasPressure;
  float pressureHpa;
  bool hasWind;
  float windSpeedMph;
  bool hasRainfall;
  float rainfall;
  Trend tempTrend;
  Trend humidityTrend;
  Trend pressureTrend;
  Trend windTrend;
};

// GETs WEATHER_API_PATH from the configured host/port and parses it into
// `out`. Returns false (leaving `out.valid = false`) on any network or
// parse failure -- never throws/crashes, matching the PWA's own
// graceful-degradation behavior against the same endpoint.
bool fetchWeather(WeatherData &out);

// "RAIN" | "WINDY" | "PRESSURE RISING" | "PRESSURE FALLING" | "CLEAR"
// Ported from strava-heatmap-pwa/pwa/src/main.js conditionLabel().
const char *conditionLabel(const WeatherData &data);
