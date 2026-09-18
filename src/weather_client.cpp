#include "weather_client.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "secrets.h"

static void parseTrend(JsonVariant v, Trend &t) {
  t.direction[0] = '\0';
  t.count = 0;
  if (v.isNull()) return;

  const char *dir = v["direction"] | "";
  strncpy(t.direction, dir, sizeof(t.direction) - 1);
  t.direction[sizeof(t.direction) - 1] = '\0';

  JsonArrayConst series = v["series"];
  if (series.isNull()) return;
  for (JsonVariantConst s : series) {
    if (t.count >= TREND_SERIES_MAX) break;
    t.series[t.count++] = s.as<float>();
  }
}

bool fetchWeather(WeatherData &out) {
  memset(&out, 0, sizeof(out));
  out.valid = false;

  if (WiFi.status() != WL_CONNECTED) return false;

  HTTPClient http;
  String url = String("http://") + WEATHER_API_HOST + ":" + WEATHER_API_PORT + WEATHER_API_PATH;
  http.setTimeout(8000);
  if (!http.begin(url)) return false;

  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    http.end();
    return false;
  }

  String payload = http.getString();
  http.end();

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, payload);
  if (err) return false;

  // API returns {} when InfluxDB is unreachable -- not an error, just no data yet.
  if (!doc["temp_c"].isNull()) {
    out.hasTemp = true;
    out.tempC = doc["temp_c"].as<float>();
  }
  if (!doc["humidity_pct"].isNull()) {
    out.hasHumidity = true;
    out.humidityPct = doc["humidity_pct"].as<float>();
  }
  if (!doc["pressure_hpa"].isNull()) {
    out.hasPressure = true;
    out.pressureHpa = doc["pressure_hpa"].as<float>();
  }
  if (!doc["wind_speed"].isNull()) {
    out.hasWind = true;
    out.windSpeedMph = doc["wind_speed"].as<float>();
  }
  if (!doc["rainfall"].isNull()) {
    out.hasRainfall = true;
    out.rainfall = doc["rainfall"].as<float>();
  }

  parseTrend(doc["temp_trend"], out.tempTrend);
  parseTrend(doc["humidity_trend"], out.humidityTrend);
  parseTrend(doc["pressure_trend"], out.pressureTrend);
  parseTrend(doc["wind_trend"], out.windTrend);

  out.valid = out.hasTemp;  // temp is the primary display value, same as the PWA
  return out.valid;
}

const char *conditionLabel(const WeatherData &data) {
  if (data.hasRainfall && data.rainfall > 0.1) return "RAIN";
  if (data.hasWind && data.windSpeedMph > 20) return "WINDY";
  if (strcmp(data.pressureTrend.direction, "rising") == 0) return "PRESSURE RISING";
  if (strcmp(data.pressureTrend.direction, "falling") == 0) return "PRESSURE FALLING";
  return "CLEAR";
}
