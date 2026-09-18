#pragma once
#include <Arduino_GFX_Library.h>

// Seeds particles and sets up the onboard RGB LED. Call once from setup().
void animInit();

// Called after each successful weather poll -- updates the values the
// animation eases toward, without touching anything on screen itself.
// pressureDirection is "rising" | "falling" | "steady" | "" (unknown).
void animSetTargets(float tempC, float windSpeedMph, const char *pressureDirection);

// Advances the animation's physics/eased state by dtSeconds (temperature/
// wind easing, particle movement, glow drift, LED pulse) -- call exactly
// once per composed frame, before any animDraw() calls for that frame.
void animAdvance(float dtSeconds);

// Draws the CURRENT (already-advanced) background gradient, drifting glow,
// and particle field into `target`, which represents panel rows
// [yOffset, yOffset + target->height()) of the full PANEL_WIDTH x
// PANEL_HEIGHT scene -- this board has no room for one full-screen offscreen
// buffer, so the frame is composed and flushed in bands instead. Call once
// per band, all using the same animAdvance()'d state, then flush `target`.
void animDraw(Arduino_GFX *target, int yOffset);
