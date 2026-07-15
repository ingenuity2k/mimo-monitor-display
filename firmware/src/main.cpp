#include <Arduino.h>
#include <lvgl.h>
#include <time.h>
#include "config.h"
#include "display.h"
#include "mimo_client.h"

// ── Display ──────────────────────────────────────────────────────
LGFX_CYD tft;

static lv_disp_draw_buf_t draw_buf;
static lv_color_t buf1[SCREEN_WIDTH * 10];
static lv_disp_drv_t disp_drv;

// ── UI elements ──────────────────────────────────────────────────
static lv_obj_t *lbl_credits;
static lv_obj_t *lbl_percent;
static lv_obj_t *lbl_burn;
static lv_obj_t *lbl_balance;
static lv_obj_t *lbl_updated;
static lv_obj_t *lbl_clock;
static lv_obj_t *bar_progress;

// ── Flush callback ───────────────────────────────────────────────
void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
  uint32_t w = area->x2 - area->x1 + 1;
  uint32_t h = area->y2 - area->y1 + 1;
  tft.startWrite();
  tft.setAddrWindow(area->x1, area->y1, w, h);
  tft.pushPixels((uint16_t *)&color_p->full, w * h, true);
  tft.endWrite();
  lv_disp_flush_ready(disp);
}

// ── Helper: format large numbers ─────────────────────────────────
String formatCredits(float val) {
  if (val >= 1e9)  return String(val / 1e9, 1) + "B";
  if (val >= 1e6)  return String(val / 1e6, 1) + "M";
  if (val >= 1e3)  return String(val / 1e3, 1) + "K";
  return String(val, 0);
}

// ── Build UI (CYD layout) ────────────────────────────────────────
void createUI() {
  lv_obj_t *scr = lv_scr_act();
  lv_obj_set_style_bg_color(scr, lv_color_hex(0x1a1a2e), 0);

  // Title
  lv_obj_t *title = lv_label_create(scr);
  lv_label_set_text(title, "MiMo Monitor");
  lv_obj_set_style_text_color(title, lv_color_hex(0xe94560), 0);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
  lv_obj_align(title, LV_ALIGN_TOP_LEFT, 10, 8);

  // Clock (top right)
  lbl_clock = lv_label_create(scr);
  lv_label_set_text(lbl_clock, "--:--");
  lv_obj_set_style_text_color(lbl_clock, lv_color_hex(0x888888), 0);
  lv_obj_set_style_text_font(lbl_clock, &lv_font_montserrat_16, 0);
  lv_obj_align(lbl_clock, LV_ALIGN_TOP_RIGHT, -10, 10);

  // Credits used — large
  lbl_credits = lv_label_create(scr);
  lv_label_set_text(lbl_credits, "--");
  lv_obj_set_style_text_color(lbl_credits, lv_color_hex(0x00d2ff), 0);
  lv_obj_set_style_text_font(lbl_credits, &lv_font_montserrat_28, 0);
  lv_obj_align(lbl_credits, LV_ALIGN_TOP_LEFT, 10, 42);

  // Percentage
  lbl_percent = lv_label_create(scr);
  lv_label_set_text(lbl_percent, "--%");
  lv_obj_set_style_text_color(lbl_percent, lv_color_hex(0xaaaaaa), 0);
  lv_obj_set_style_text_font(lbl_percent, &lv_font_montserrat_16, 0);
  lv_obj_align(lbl_percent, LV_ALIGN_TOP_RIGHT, -10, 50);

  // Progress bar
  bar_progress = lv_bar_create(scr);
  lv_obj_set_size(bar_progress, 300, 16);
  lv_bar_set_range(bar_progress, 0, 100);
  lv_bar_set_value(bar_progress, 0, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(bar_progress, lv_color_hex(0x2d2d44), 0);
  lv_obj_set_style_bg_color(bar_progress, lv_color_hex(0x6c3483), LV_PART_INDICATOR);
  lv_obj_align(bar_progress, LV_ALIGN_TOP_LEFT, 10, 82);

  // Burn rate
  lbl_burn = lv_label_create(scr);
  lv_label_set_text(lbl_burn, "Burn: --/hr");
  lv_obj_set_style_text_color(lbl_burn, lv_color_hex(0xf39c12), 0);
  lv_obj_set_style_text_font(lbl_burn, &lv_font_montserrat_16, 0);
  lv_obj_align(lbl_burn, LV_ALIGN_TOP_LEFT, 10, 115);

  // Balance
  lbl_balance = lv_label_create(scr);
  lv_label_set_text(lbl_balance, "Balance: $--");
  lv_obj_set_style_text_color(lbl_balance, lv_color_hex(0x2ecc71), 0);
  lv_obj_set_style_text_font(lbl_balance, &lv_font_montserrat_16, 0);
  lv_obj_align(lbl_balance, LV_ALIGN_TOP_LEFT, 10, 142);

  // Last updated
  lbl_updated = lv_label_create(scr);
  lv_label_set_text(lbl_updated, "Updated: --");
  lv_obj_set_style_text_color(lbl_updated, lv_color_hex(0x666666), 0);
  lv_obj_set_style_text_font(lbl_updated, &lv_font_montserrat_12, 0);
  lv_obj_align(lbl_updated, LV_ALIGN_TOP_LEFT, 10, 175);
}

// ── Update UI with fresh data ────────────────────────────────────
void updateUI(const MimoData &d) {
  if (!d.valid) {
    lv_label_set_text_fmt(lbl_credits, "ERR");
    lv_label_set_text_fmt(lbl_percent, "%s", d.error.c_str());
    return;
  }

  lv_label_set_text(lbl_credits, formatCredits(d.credits_used).c_str());
  lv_label_set_text_fmt(lbl_percent, "%.1f%%", d.percentage);

  // Progress bar: set value and color based on budget status
  lv_bar_set_value(bar_progress, (int)d.percentage, LV_ANIM_ON);
  if (d.percentage > 100.0) {
    // Over budget — red
    lv_obj_set_style_bg_color(bar_progress, lv_color_hex(0xe74c3c), LV_PART_INDICATOR);
  } else {
    // Under budget — purple
    lv_obj_set_style_bg_color(bar_progress, lv_color_hex(0x6c3483), LV_PART_INDICATOR);
  }

  // Burn rate
  if (d.burn_type == "per_hour") {
    lv_label_set_text_fmt(lbl_burn, "Burn: %s/hr", formatCredits(d.burn_rate).c_str());
  } else {
    lv_label_set_text_fmt(lbl_burn, "Burn: %s/day", formatCredits(d.burn_rate).c_str());
  }

  // Balance
  lv_label_set_text_fmt(lbl_balance, "Balance: $%.2f", d.balance_usd);

  // Updated time — extract just HH:MM:SS from ISO timestamp
  String ts = d.updated_at;
  int tIdx = ts.indexOf('T');
  if (tIdx > 0 && ts.length() > tIdx + 8) {
    lv_label_set_text_fmt(lbl_updated, "Updated: %s", ts.substring(tIdx + 1, tIdx + 9).c_str());
  } else {
    lv_label_set_text_fmt(lbl_updated, "Updated: %s", ts.c_str());
  }
}

// ── Update clock ─────────────────────────────────────────────────
void updateClock() {
  time_t now;
  struct tm info;
  time(&now);
  localtime_r(&now, &info);
  lv_label_set_text_fmt(lbl_clock, "%02d:%02d", info.tm_hour, info.tm_min);
}

// ── Setup ────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== MiMo Monitor Display ===");

  // Init display
  tft.begin();
  tft.setRotation(1);
  tft.setBrightness(200);
  Serial.println("Display initialized");

  // Init LVGL
  lv_init();
  lv_disp_draw_buf_init(&draw_buf, buf1, NULL, SCREEN_WIDTH * 10);
  lv_disp_drv_init(&disp_drv);
  disp_drv.flush_cb = my_disp_flush;
  disp_drv.draw_buf = &draw_buf;
  disp_drv.hor_res  = SCREEN_WIDTH;
  disp_drv.ver_res  = SCREEN_HEIGHT;
  lv_disp_drv_register(&disp_drv);

  // Build UI
  createUI();

  // WiFi
  if (!wifiConnect()) {
    lv_label_set_text(lbl_credits, "ERR");
    lv_label_set_text(lbl_percent, "WiFi failed — check config.h");
    while (true) delay(1000);
  }

  // NTP time sync
  // Change to your timezone: https://github.com/nayarsystems/posix_tz_db/blob/master/zones.csv
  configTzTime("CET-1CEST,M3.5.0,M10.5.0/3", "pool.ntp.org", "time.nist.gov");
  Serial.println("Waiting for NTP sync...");
  time_t now;
  int retries = 0;
  do {
    delay(500);
    time(&now);
    retries++;
  } while (now < 1700000000 && retries < 20);
  Serial.printf("Time synced: %ld\n", now);
}

// ── Loop ─────────────────────────────────────────────────────────
void loop() {
  static unsigned long lastFetch = 0;
  unsigned long now = millis();

  // Fetch data on first run and every REFRESH_INTERVAL_MS
  if (lastFetch == 0 || (now - lastFetch >= REFRESH_INTERVAL_MS)) {
    Serial.println("Fetching MiMo data...");
    MimoData data = fetchMimoData();
    updateUI(data);
    if (data.valid) {
      Serial.printf("  Credits: %.0f (%.1f%%)\n", data.credits_used, data.percentage);
      Serial.printf("  Balance: $%.2f  Burn: %.2f/hr\n", data.balance_usd, data.burn_rate);
    } else {
      Serial.printf("  Error: %s\n", data.error.c_str());
    }
    lastFetch = now;
  }

  updateClock();
  lv_timer_handler();
  delay(1000);
}
