#pragma once

// Cheap Yellow Display -- ESP32-2432S028R, 2.8" 240x320 ILI9341 SPI panel.
// Pins confirmed against the board's published pinout (Random Nerd
// Tutorials' ESP32-2432S028R pin reference); re-verify against the board's
// silkscreen/schematic if this turns out to be a different CYD sub-revision.

// ---- TFT panel (own SPI bus) ----------------------------------------
#define TFT_SCK  14
#define TFT_MOSI 13
#define TFT_MISO 12
#define TFT_DC   2
#define TFT_CS   15
#define TFT_RST  GFX_NOT_DEFINED  // no GPIO -- tied to the ESP32's EN pin in hardware
#define TFT_BL   21               // backlight, active HIGH, PWM-capable

// Panel is a 240x320 ILI9341 driven in landscape for this project.
#define PANEL_WIDTH   320
#define PANEL_HEIGHT  240
// Native portrait is rotation 0 (USB connector at the bottom). Rotation 1
// turns it 90 degrees so the connector ends up on the left; if the first
// live frame comes up upside down or mirrored, switch this to 3.
#define PANEL_ROTATION 1

// ---- XPT2046 resistive touch (separate SPI bus from the panel) --------
#define TOUCH_SCK  25
#define TOUCH_MOSI 32
#define TOUCH_MISO 39
#define TOUCH_CS   33
#define TOUCH_IRQ  36

// ---- onboard discrete RGB status LED (active LOW / common anode) ------
#define LED_R_PIN 4
#define LED_G_PIN 16
#define LED_B_PIN 17
