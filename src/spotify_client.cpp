#include "spotify_client.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "secrets.h"

bool fetchSpotifyNowPlaying(SpotifyData &out) {
  memset(&out, 0, sizeof(out));
  out.valid = false;

  if (WiFi.status() != WL_CONNECTED) return false;

  HTTPClient http;
  String url = String("http://") + WEATHER_API_HOST + ":" + WEATHER_API_PORT + SPOTIFY_API_PATH;
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

  out.isPlaying = doc["is_playing"] | false;
  if (out.isPlaying) {
    strncpy(out.track, doc["track"] | "", sizeof(out.track) - 1);
    strncpy(out.artist, doc["artist"] | "", sizeof(out.artist) - 1);
    strncpy(out.album, doc["album"] | "", sizeof(out.album) - 1);
    strncpy(out.artId, doc["art_id"] | "", sizeof(out.artId) - 1);
    out.progressMs = doc["progress_ms"] | 0L;
    out.durationMs = doc["duration_ms"] | 0L;
  }
  out.valid = true;
  return true;
}

bool fetchSpotifyArt(uint16_t *buffer) {
  if (WiFi.status() != WL_CONNECTED) return false;

  HTTPClient http;
  String url = String("http://") + WEATHER_API_HOST + ":" + WEATHER_API_PORT + SPOTIFY_ART_PATH;
  http.setTimeout(8000);
  if (!http.begin(url)) return false;

  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    http.end();
    return false;
  }

  int len = http.getSize();
  if (len != SPOTIFY_ART_BYTES) {
    http.end();
    return false;
  }

  WiFiClient *stream = http.getStreamPtr();
  size_t got = stream->readBytes((uint8_t *)buffer, SPOTIFY_ART_BYTES);
  http.end();
  return got == SPOTIFY_ART_BYTES;
}
