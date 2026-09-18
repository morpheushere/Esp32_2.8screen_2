# ESP32 2.8" Ambient Weather + Spotify + Claude Approval Display

A 4-tab LVGL dashboard for a **Cheap Yellow Display (ESP32-2432S028R)**, a 2.8" 240x320 ILI9341 board with resistive XPT2046 touch. This merges two earlier projects into one that actually runs on this exact board:

- [ESP32_experiment1](https://github.com/morpheushere/ESP32_experiment1) -- the original ambient weather/Spotify display concept (built for a different board, a Waveshare ESP32-C6-LCD-1.47)
- [esp32_experiment2_2.8inch](https://github.com/morpheushere/esp32_experiment2_2.8inch) -- a from-scratch LVGL/LovyanGFX rebuild of that concept specifically for this CYD board, which also added Calendar and Claude-approval tabs. **This repo's firmware is that project**, carried over as-is (its rendering stack, tab structure, and hard-won heap/network fixes all apply directly here).

## Tabs

1. **Weather** -- live temperature/humidity/pressure/wind, animated temperature-gradient background (drifting glow, wind-scaled particles), sparkline trend rows, onboard RGB LED pulsing in the temperature color.
2. **Spotify** -- now-playing view: album art filling the panel, track/artist text on a scrim (marquee for long names), progress bar, playback controls.
3. **Calendar** -- upcoming events from the backend's calendar cache.
4. **Claude** -- shows a pending Claude Code permission request (project, tool, command summary) with **Accept**/**Deny** buttons, bridged from a `PermissionRequest` hook running on the Mac driving Claude Code. Shows "No pending decisions" when nothing's waiting.

Swipe or tap the top tab bar to switch. Each tab's background polling only runs while it's the active tab.

All four pull from the same self-hosted Flask backend (the [strava-heatmap-pwa](https://github.com/morpheushere/strava-heatmap-pwa) project's `api/` service on a home NAS) -- this firmware only reads/writes that backend's already-cached JSON endpoints.

## Hardware

- Board: Cheap Yellow Display (ESP32-2432S028R) -- ESP32-D0WD-V3, 4MB flash, 240x320 ILI9341 SPI panel (run landscape via `setRotation(1)`), resistive XPT2046 touch on its own SPI bus, discrete RGB status LED, CH340 USB-serial bridge.
- Pin config lives in `include/LGFX_CYD.hpp` (LovyanGFX device class) -- display SCK=14/MOSI=13/MISO=12/DC=2/CS=15/BL=21, touch SCK=25/MOSI=32/MISO=39/CS=33/IRQ=36, RGB LED R=4/G=16/B=17 (active low, see `src/rgb_led.cpp`).
- No extra components required -- runs entirely off the board's USB power.

## Architecture

- `src/main.cpp` -- boots the display, builds the tabview, and ticks each tab's client every loop() iteration
- `src/display_init.cpp` -- LovyanGFX + LVGL wiring (draw buffers, flush/touch callbacks)
- `src/ui_tabview.cpp` -- the 4-tab `lv_tabview`, pausing/resuming each tab's background polling on tab switch
- `src/tab_weather.cpp` + `src/weather_client.cpp` (+ `temp_gradient.cpp`, `rgb_led.cpp`) -- weather tab and its animated scene
- `src/tab_spotify.cpp` + `src/spotify_client.cpp` -- Spotify tab
- `src/tab_calendar.cpp` + `src/calendar_client.cpp` -- Calendar tab
- `src/tab_claude.cpp` + `src/claude_approval_client.cpp` -- Claude approval tab
- `src/http_json.cpp`, `src/network_health.cpp` -- shared bounded-read JSON helper and a consecutive-failure watchdog restart, both added after live heap-fragmentation issues broke sockets on this device
- `src/bauhaus_colors.h` -- shared palette across every tab (ported from the PWA/original project)

## Setup

1. Copy `include/secrets.h.example` to `include/secrets.h` and fill in your WiFi SSID/password and `API_BASE_URL` (the NAS backend's host:port). `secrets.h` is gitignored.
2. This project uses [PlatformIO](https://platformio.org/). Build and flash:
   ```
   pio run --target upload
   ```
   Confirm `upload_port`/`monitor_port` in `platformio.ini` match this board's current USB-serial enumeration (`ls /dev/cu.*` on macOS).
3. Watch boot/status logs over serial at 115200 baud (`pio device monitor`).

## Enabling the Claude approval tab

The Claude tab is read/write against the backend's mailbox queue (`api/claude_approval.py` + the `/api/claude/pending*` routes in `strava-heatmap-pwa`), but the piece that actually creates a pending entry is a Claude Code hook running on whichever machine drives Claude Code -- **not part of this firmware repo**. That hook script is `~/.claude/hooks/remote_approval.py` (a `PermissionRequest` hook: posts a pending entry, polls for up to ~280s, fails safe to deny on timeout or if the backend's unreachable). It needs to be registered in that machine's `~/.claude/settings.json` under a `hooks` entry for the `PermissionRequest` event -- it isn't currently wired up there, so the feature is inactive until that's added back.

## Requirements on the backend side

A reachable `strava-heatmap-pwa` instance serving the weather/Spotify/calendar cache endpoints plus `/api/claude/pending` (list/create), `/api/claude/pending/<id>` (get), and `/api/claude/pending/<id>/decide` (POST `{"decision": "allow"|"deny"}`).
