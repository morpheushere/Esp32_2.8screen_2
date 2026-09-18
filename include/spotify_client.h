#pragma once
#include <Arduino.h>

// Must match the backend's spotify_client.py ART_SIZE exactly -- it was
// sized down from an initial 120 after measuring this exact class of
// ESP32's real free heap once WiFi is connected (fragmentation leaves only
// ~19-20KB in the largest contiguous block). 80x80 RGB565 = 12800 bytes.
#define SPOTIFY_ART_SIZE 80
#define SPOTIFY_ART_PIXELS (SPOTIFY_ART_SIZE * SPOTIFY_ART_SIZE)
#define SPOTIFY_ART_BYTES (SPOTIFY_ART_PIXELS * 2)

struct SpotifyData {
  bool valid;       // false if the fetch/parse failed
  bool isPlaying;
  char track[64];
  char artist[64];
  char album[64];
  char artId[40];
  long progressMs;
  long durationMs;
};

// GETs SPOTIFY_API_PATH and parses the now-playing JSON. Returns false
// (out.valid = false, isPlaying = false) on any network/parse failure --
// never crashes, matching every other client's graceful-degradation style.
bool fetchSpotifyNowPlaying(SpotifyData &out);

// GETs SPOTIFY_ART_PATH (raw little-endian RGB565, no header, exactly
// SPOTIFY_ART_BYTES long) straight into `buffer` (must hold at least
// SPOTIFY_ART_PIXELS uint16_t values). Returns false on any failure or if
// the response size doesn't match exactly.
bool fetchSpotifyArt(uint16_t *buffer);
