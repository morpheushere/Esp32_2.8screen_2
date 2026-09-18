# ESP32 2.8" Ambient Weather + Spotify Display

A port of [ESP32_experiment1](https://github.com/morpheushere/ESP32_experiment1) (originally built for a Waveshare ESP32-C6-LCD-1.47, 172x320 portrait) to a **Cheap Yellow Display (ESP32-2432S028R)**, a 2.8" 240x320 ILI9341 board run landscape (320x240) here, with its resistive touchscreen added for manual view switching.

Alternates every minute between:

- **Backyard weather** -- live temperature/humidity/pressure/wind pulled from a home weather station, with a temperature-driven animated background (color gradient, drifting glow, wind-scaled particle field), sparkline trend rows, and the onboard RGB LED pulsing in the same temperature color at a heartbeat-like rate that quickens with heat.
- **Spotify now-playing** -- a phone-lock-screen style view: current album art scaled to cover the whole panel, with track/artist text overlaid on a scrim (long artist names scroll via a marquee), plus a progress bar.

Tap the screen anywhere to jump between views immediately instead of waiting for the 60s auto-rotation.

Both views pull from a self-hosted Flask backend (the [strava-heatmap-pwa](https://github.com/morpheushere/strava-heatmap-pwa) project) that already aggregates a home InfluxDB weather station and the Spotify API -- this firmware is just a small, low-power satellite display for data that backend already collects.

## Hardware

- Board: Cheap Yellow Display (ESP32-2432S028R) -- ESP32-D0WD-V3, 4MB flash, 240x320 ILI9341 SPI LCD (run landscape, 320x240), resistive XPT2046 touchscreen, discrete (non-addressable) RGB status LED, CH340 USB-serial bridge.
- Display wiring (`include/display_config.h`): SCK=14, MOSI=13, MISO=12, DC=2, CS=15, no hardware RST (tied to EN), BL=21 (PWM backlight, active HIGH).
- Touch wiring, on its own SPI bus (`include/display_config.h`): SCK=25, MOSI=32, MISO=39, CS=33, IRQ=36.
- RGB LED: R=4, G=16, B=17 -- active LOW (common anode).
- No extra components required -- runs entirely off the board's USB power.

Pinout differs from the original C6 board (different panel driver, no shared bus with touch, no addressable LED) -- see the "Differences from the original project" section below.

## Architecture

- `src/main.cpp` -- boot sequence, WiFi connect/retry, poll timers, view-rotation state machine, touch-driven manual view switch, the ~6.7fps animation loop
- `src/animation.cpp` -- temperature/wind-driven background gradient, drifting glow, particle field, and onboard RGB LED pulse (`include/animation.h`)
- `src/weather_client.cpp` / `src/spotify_client.cpp` -- HTTP+JSON clients for the backend's `/api/weather/current` and `/api/spotify/now-playing` + `/api/spotify/art.raw` endpoints (unchanged from the original project -- pure HTTP/JSON, no display coupling)
- `src/scene.cpp` -- all drawing: the weather placard (numeral + condition pill + unit label in a left column, stat rows with sparklines in a right column) and the Spotify screen (bilinear-scaled album art background, scrim, marquee text, progress bar)
- `include/colors.h` -- the temperature-to-color gradient (unchanged from the original project, ported from the backend PWA's own ambient scene)
- `include/display_config.h` -- board pin/panel constants

Album art arrives from the backend pre-converted to raw 80x80 RGB565 pixels (no JPEG decoder needed on-device).

## Differences from the original project

| | Original (Waveshare C6) | This port (CYD 2.8") |
|---|---|---|
| Panel | 172x320 portrait ST7789 | 320x240 landscape ILI9341 |
| Layout | Single vertical stack (pill/numeral/unit label/3 stat rows) | Left column (numeral) + right column (stat rows) |
| Touch | None | XPT2046 resistive -- any tap toggles the current view |
| Status LED | Single WS2811 addressable RGB (FastLED) | 3 discrete PWM-driven LEDs, active low |
| USB | Native USB-Serial-JTOG | CH340 USB-UART bridge |
| PlatformIO platform | `pioarduino` fork (C6 needs it for Arduino support) | Official `espressif32` (classic ESP32 is fully supported) |

## Setup

1. Copy `include/secrets.h.example` to `include/secrets.h` and fill in:
   - `WIFI_SSID` / `WIFI_PASSWORD` -- your home network
   - `WEATHER_API_HOST` / `WEATHER_API_PORT` -- your backend's address (the strava-heatmap-pwa nginx container)
   - The `WEATHER_API_PATH`, `SPOTIFY_API_PATH`, `SPOTIFY_ART_PATH` values shouldn't need to change unless the backend's routes do

   `secrets.h` is gitignored -- never commit it.

2. This project uses [PlatformIO](https://platformio.org/). Build and flash:

   ```
   pio run --target upload
   ```

   Confirm `upload_port`/`monitor_port` in `platformio.ini` match the board's current USB-serial enumeration (`ls /dev/cu.*` on macOS) before flashing.

3. Watch boot/status logs over serial at 115200 baud (`pio device monitor`).

## Requirements on the backend side

Same as the original project -- a reachable `strava-heatmap-pwa` instance serving `/api/weather/current`, `/api/spotify/now-playing`, and `/api/spotify/art.raw` (80x80 raw RGB565).
