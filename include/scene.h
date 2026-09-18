#pragma once
#include <Arduino_GFX_Library.h>
#include "weather_client.h"
#include "spotify_client.h"

// Draws the condition pill, big temp numeral, unit label, and the three stat
// rows (with sparklines) on top of whatever's already in `target`, a buffer
// representing panel rows [yOffset, yOffset + target->height()) of the full
// PANEL_WIDTH x PANEL_HEIGHT scene. Does NOT touch the background --
// animation.cpp owns that. This board has no room for one full-screen
// offscreen buffer, so the weather view's animated frames are composed and
// flushed in bands (see main.cpp) -- call once per band with the same data,
// using animation.cpp's animDraw() for the same band just before it.
// Elements outside a given band are naturally clipped away by Arduino_GFX's
// own bounds checking.
void drawOverlay(Arduino_GFX *target, int yOffset, const WeatherData &data);

// Full "now playing" screen: album art cover-cropped to fill the entire
// panel as the background (phone-lock-screen style, not a small inset),
// with track/artist text overlaid on a scrim near the bottom for legibility
// against arbitrary art colors. artBuffer must hold SPOTIFY_ART_PIXELS
// pixels already fetched via fetchSpotifyArt(); draws a neutral "nothing
// playing" screen instead if data.isPlaying is false. Unlike the weather
// view, this is drawn straight to the live panel (see main.cpp) -- it's
// only redrawn once per view-visit (plus an occasional small marquee-line
// redraw), not every animation tick, so it doesn't need banded buffering.
void drawSpotifyScreen(Arduino_GFX *gfx, const SpotifyData &data, const uint16_t *artBuffer);

// Call every animation tick while the Spotify view is showing (after the
// initial drawSpotifyScreen() call). No-ops (returns false) unless the
// current artist's name is too long to fit and needs to scroll -- redraws
// only the artist line, not the album art, so this is cheap even when
// called every tick. Returns true when it redrew something.
bool spotifyMarqueeTick(Arduino_GFX *gfx, float dtSeconds);

// Simple full-screen status text for boot/connecting/error states, before
// the first successful weather frame. Drawn straight to the live panel --
// it's static (no animation), so a single direct fillScreen doesn't flicker.
void renderStatus(Arduino_GFX *gfx, const char *line1, const char *line2 = nullptr);
