#pragma once
#include "config.h"
#include <lvgl.h>

// ── CYD — Cheap Yellow Display (SPI ILI9341, 320x240) ────────────
// Hardware: ESP32-2432S028R
// SPI pins: MOSI=13, SCLK=14, MISO=12, CS=15, DC=2, RST=4
// Backlight: GPIO 21 (active HIGH, manual control — LEDC PWM fails on this pin)

#include <LovyanGFX.hpp>

#ifndef BLACK
#define BLACK 0x0000
#endif

class _CYDDisplay : public lgfx::LGFX_Device {
  lgfx::Panel_ILI9341 _panel;
  lgfx::Bus_SPI       _bus;

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
      cfg.offset_rotation = 4;  // Horizontal mirror fix for this panel
      _panel.config(cfg);
    }
    setPanel(&_panel);
  }
};

static _CYDDisplay *_tft = nullptr;

static void displayInit() {
  // Backlight ON — manual GPIO (PWM LEDC fails on this board)
  pinMode(GPIO_NUM_21, OUTPUT);
  digitalWrite(GPIO_NUM_21, HIGH);

  _tft = new _CYDDisplay();
  _tft->begin();
  _tft->fillScreen(BLACK);
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
