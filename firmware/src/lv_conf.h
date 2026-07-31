/*
 * LVGL configuration — ESP32 Token Display
 * See: https://docs.lvgl.io/master/porting/project.html
 */
#pragma once

// Color depth: 16-bit (RGB565)
#define LV_COLOR_DEPTH 16

// Use dark theme
#define LV_USE_THEME_DEFAULT 1
#define LV_THEME_DEFAULT_DARK 1

// ── Font sizes ───────────────────────────────────────────────────
// Enable only the sizes we use (saves flash)
#define LV_FONT_MONTSERRAT_10 1
#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_24 1
#define LV_FONT_MONTSERRAT_28 1

#define LV_FONT_DEFAULT &lv_font_montserrat_14

// ── Widgets ──────────────────────────────────────────────────────
#define LV_USE_LABEL    1
#define LV_USE_BAR      1
#define LV_USE_LINE     1
#define LV_USE_CHART    1
#define LV_USE_BTN      1
#define LV_USE_ARC      1
#define LV_USE_IMG      1

// Disable unused widgets to save flash
#define LV_USE_ANIMIMG  0
#define LV_USE_CALENDAR 0
#define LV_USE_CANVAS   0
#define LV_USE_DROPDOWN 0
#define LV_USE_KEYBOARD 0
#define LV_USE_LIST     0
#define LV_USE_MENU     0
#define LV_USE_ROLLER   0
#define LV_USE_SLIDER   0
#define LV_USE_SPAN     0
#define LV_USE_SPINBOX  0
#define LV_USE_SPINNER  0
#define LV_USE_SWITCH   0
#define LV_USE_TABLE    0
#define LV_USE_TEXTAREA 0
#define LV_USE_TILEVIEW 0
#define LV_USE_WIN      0

// Memory — use PSRAM if available
#if defined(BOARD_HAS_PSRAM)
  #define LV_MEM_CUSTOM 1
  #define LV_MEM_CUSTOM_INCLUDE <stdlib.h>
  #define LV_MEM_CUSTOM_ALLOC   ps_malloc
  #define LV_MEM_CUSTOM_FREE    free
  #define LV_MEM_CUSTOM_REALLOC ps_realloc
#else
  #define LV_MEM_SIZE (48 * 1024)  // 48KB internal
#endif

// Tick source
#define LV_USE_TICK_CUSTOM 1
#define LV_TICK_CUSTOM_INCLUDE <Arduino.h>
#define LV_TICK_CUSTOM_SYS_TIME_EXPR (millis())

// Logging
#define LV_USE_LOG 1
#define LV_LOG_LEVEL LV_LOG_LEVEL_WARN
