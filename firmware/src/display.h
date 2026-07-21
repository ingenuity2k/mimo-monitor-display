#pragma once
#include "config.h"
#include <lvgl.h>

// ── Waveshare ESP32-S3-Touch-LCD-5B (RGB parallel, 1024x600) ────
#if defined(WAVESHARE_5B)

#include <Arduino_GFX_Library.h>

// 16-bit RGB parallel bus for ESP32-S3
static Arduino_ESP32RGBPanel *_rgbBus = new Arduino_ESP32RGBPanel(
  5,   // DE
  3,   // VSYNC
  46,  // HSYNC
  7,   // PCLK
  1,   // R3
  2,   // R4
  42,  // R5
  41,  // R6
  40,  // R7
  39,  // G2
  0,   // G3
  45,  // G4
  48,  // G5
  47,  // G6
  21,  // G7
  14,  // B3
  38,  // B4
  18,  // B5
  17,  // B6
  10,  // B7
  // hsync_polarity, hsync_front_porch, hsync_pulse_width, hsync_back_porch
  0, 160, 30, 160,
  // vsync_polarity, vsync_front_porch, vsync_pulse_width, vsync_back_porch
  0, 12, 2, 23,
  // pclk_active_neg, prefer_speed
  1, 16000000
);

static Arduino_RGB_Display *_tft = new Arduino_RGB_Display(
  1024, 600, _rgbBus, 0 /* rotation */, true /* auto_flush */
);

static void displayInit() {
  _tft->begin();
  _tft->fillScreen(BLACK);
}

// LVGL flush callback
static void lvFlushCb(lv_display_t *disp, const lv_area_t *area, uint8_t *pxMap) {
  uint32_t w = area->x2 - area->x1 + 1;
  uint32_t h = area->y2 - area->y1 + 1;
  _tft->draw16bitBeRGBBitmap(area->x1, area->y1, (uint16_t *)pxMap, w, h);
  lv_display_flush_ready(disp);
}

// ── CYD — Cheap Yellow Display (SPI ILI9341, 320x240) ────────────
#elif defined(CYD)

#include <LovyanGFX.hpp>

#ifndef BLACK
#define BLACK 0x0000
#endif

class _CYDDisplay : public lgfx::LGFX_Device {
  lgfx::Panel_ILI9341 _panel;
  lgfx::Bus_SPI       _bus;
  lgfx::Light_PWM     _light;
  lgfx::Touch_XPT2046 _touch;

public:
  _CYDDisplay() {
    {
      auto cfg = _bus.config();
      cfg.spi_host   = VSPI_HOST;
      cfg.spi_mode   = 0;
      cfg.freq_write = 40000000;
      cfg.pin_sclk   = GPIO_NUM_14;
      cfg.pin_mosi   = GPIO_NUM_13;
      cfg.pin_miso   = GPIO_NUM_12;
      cfg.pin_dc     = GPIO_NUM_2;
      _bus.config(cfg);
      _panel.setBus(&_bus);
    }
    {
      auto cfg = _panel.config();
      cfg.pin_cs   = GPIO_NUM_15;
      cfg.pin_rst  = GPIO_NUM_4;
      cfg.pin_busy = -1;
      cfg.memory_width  = 320;
      cfg.memory_height = 240;
      cfg.panel_width   = 320;
      cfg.panel_height  = 240;
      cfg.offset_rotation = 4;
      _panel.config(cfg);
    }
    // Skip Light_PWM — LEDC conflicts on this board. Backlight driven manually.
    setPanel(&_panel);

    // XPT2046 resistive touch on separate SPI pins
    {
      auto cfg = _touch.config();
      cfg.spi_host   = HSPI_HOST;
      cfg.pin_sclk   = GPIO_NUM_25;
      cfg.pin_mosi   = GPIO_NUM_32;
      cfg.pin_miso   = GPIO_NUM_39;
      cfg.pin_cs     = GPIO_NUM_33;
      cfg.pin_int    = GPIO_NUM_36;
      cfg.freq       = 1000000;
      cfg.x_min      = 300;
      cfg.x_max      = 3900;
      cfg.y_min      = 400;
      cfg.y_max      = 3900;
      cfg.offset_rotation = 4;
      _touch.config(cfg);
      _panel.setTouch(&_touch);
    }
  }
};

static _CYDDisplay *_tft = nullptr;

static void displayInit() {
  pinMode(GPIO_NUM_21, OUTPUT);
  digitalWrite(GPIO_NUM_21, LOW);

  _tft = new _CYDDisplay();
  _tft->begin();
  _tft->fillScreen(BLACK);

  // Backlight ON after screen is black — no flashbang
  digitalWrite(GPIO_NUM_21, HIGH);
}

// LVGL flush callback
static void lvFlushCb(lv_display_t *disp, const lv_area_t *area, uint8_t *pxMap) {
  uint32_t w = area->x2 - area->x1 + 1;
  uint32_t h = area->y2 - area->y1 + 1;
  _tft->startWrite();
  _tft->setAddrWindow(area->x1, area->y1, w, h);
  _tft->writePixels((lgfx::rgb565_t *)pxMap, w * h);
  _tft->endWrite();
  lv_display_flush_ready(disp);
}

#else
  #error "Define WAVESHARE_5B or CYD in platformio.ini build_flags"
#endif
