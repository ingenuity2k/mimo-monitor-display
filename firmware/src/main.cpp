/*
 * ESP32 Token Display — MiMo Burn Rate Monitor
 * Supports:
 *   - Waveshare ESP32-S3-Touch-LCD-5B (1024x600, RGB parallel)
 *   - CYD ESP32-2432S028R (320x240, SPI ILI9341)
 *
 * Build: pio run -e waveshare_5b | pio run -e cyd
 */
#include <Arduino.h>
#include <lvgl.h>
#include "config.h"
#include "display.h"
#include "mimo_client.h"

// ── LVGL buffers ─────────────────────────────────────────────────
static lv_display_t *lvDisp = NULL;

static void lv_init_display() {
  lv_init();
  lvDisp = lv_display_create(SCREEN_WIDTH, SCREEN_HEIGHT);
  lv_display_set_flush_cb(lvDisp, lvFlushCb);

#if defined(BOARD_HAS_PSRAM)
  // Double buffer in PSRAM (Waveshare has 8MB)
  size_t buf_sz = SCREEN_WIDTH * 40 * sizeof(lv_color_t);
  static lv_color_t *buf1 = (lv_color_t *)ps_malloc(buf_sz);
  static lv_color_t *buf2 = (lv_color_t *)ps_malloc(buf_sz);
  lv_display_set_buffers(lvDisp, buf1, buf2, buf_sz, LV_DISPLAY_RENDER_MODE_PARTIAL);
#else
  // Single buffer in internal RAM (CYD — 48KB from lv_conf.h is plenty for 320x240)
  size_t buf_sz = SCREEN_WIDTH * 20 * sizeof(lv_color_t);
  static lv_color_t *buf1 = (lv_color_t *)malloc(buf_sz);
  lv_display_set_buffers(lvDisp, buf1, NULL, buf_sz, LV_DISPLAY_RENDER_MODE_PARTIAL);
#endif
}

// ── UI Elements ──────────────────────────────────────────────────
static lv_obj_t *lblTitle, *lblCredits, *lblPercent, *lblRate;
static lv_obj_t *lblStatus, *barUsage, *lblBalance, *lblClock;
static lv_obj_t *barExpected;
static lv_timer_t *refrTimer = NULL;

// Force LVGL to process invalidated areas (known LVGL 9 redraw bug)
static void forceRedraw() {
  if (!refrTimer) refrTimer = lv_display_get_refr_timer(lvDisp);
  if (refrTimer) lv_timer_ready(refrTimer);
}

// ── UI Layout — Waveshare (1024x600) ─────────────────────────────
#if defined(WAVESHARE_5B)

static void createUI() {
  lv_obj_t *scr = lv_screen_active();
  lv_obj_set_style_bg_color(scr, lv_color_hex(0x0a0a0a), 0);

  // Title
  lblTitle = lv_label_create(scr);
  lv_label_set_text(lblTitle, "MiMo Token Monitor");
  lv_obj_set_style_text_color(lblTitle, lv_color_hex(0x7c3aed), 0);
  lv_obj_set_style_text_font(lblTitle, &lv_font_montserrat_24, 0);
  lv_obj_align(lblTitle, LV_ALIGN_TOP_MID, 0, 30);

  // Credits used
  lblCredits = lv_label_create(scr);
  lv_label_set_text(lblCredits, "---");
  lv_obj_set_style_text_color(lblCredits, lv_color_hex(0xffffff), 0);
  lv_obj_set_style_text_font(lblCredits, &lv_font_montserrat_28, 0);
  lv_obj_align(lblCredits, LV_ALIGN_CENTER, 0, -50);

  // Percent
  lblPercent = lv_label_create(scr);
  lv_label_set_text(lblPercent, "--%");
  lv_obj_set_style_text_color(lblPercent, lv_color_hex(0xa1a1a6), 0);
  lv_obj_set_style_text_font(lblPercent, &lv_font_montserrat_20, 0);
  lv_obj_align(lblPercent, LV_ALIGN_CENTER, 0, -10);

  // Progress bar
  barUsage = lv_bar_create(scr);
  lv_obj_set_size(barUsage, SCREEN_WIDTH - 120, 16);
  lv_obj_align(barUsage, LV_ALIGN_CENTER, 0, 30);
  lv_bar_set_range(barUsage, 0, 100);
  lv_bar_set_value(barUsage, 0, LV_ANIM_ON);
  lv_obj_set_style_bg_color(barUsage, lv_color_hex(0x7c3aed), 0);
  lv_obj_set_style_bg_color(barUsage, lv_color_hex(0xc4b5fd), LV_PART_INDICATOR);

  // Burn rate
  lblRate = lv_label_create(scr);
  lv_label_set_text(lblRate, "--- /hour");
  lv_obj_set_style_text_color(lblRate, lv_color_hex(0x6e6e73), 0);
  lv_obj_set_style_text_font(lblRate, &lv_font_montserrat_16, 0);
  lv_obj_align(lblRate, LV_ALIGN_CENTER, 0, 70);

  // Balance
  lblBalance = lv_label_create(scr);
  lv_label_set_text(lblBalance, "");
  lv_obj_set_style_text_color(lblBalance, lv_color_hex(0xa1a1a6), 0);
  lv_obj_set_style_text_font(lblBalance, &lv_font_montserrat_16, 0);
  lv_obj_align(lblBalance, LV_ALIGN_CENTER, 0, 100);

  // Status
  lblStatus = lv_label_create(scr);
  lv_label_set_text(lblStatus, "Connecting...");
  lv_obj_set_style_text_color(lblStatus, lv_color_hex(0x6e6e73), 0);
  lv_obj_set_style_text_font(lblStatus, &lv_font_montserrat_12, 0);
  lv_obj_align(lblStatus, LV_ALIGN_BOTTOM_MID, 0, -15);

  // Clock
  lblClock = lv_label_create(scr);
  lv_label_set_text(lblClock, "--:--");
  lv_obj_set_style_text_color(lblClock, lv_color_hex(0x6e6e73), 0);
  lv_obj_set_style_text_font(lblClock, &lv_font_montserrat_20, 0);
  lv_obj_align(lblClock, LV_ALIGN_TOP_RIGHT, -30, 30);
}

// ── UI Layout — CYD (320x240) ────────────────────────────────────
#elif defined(CYD)

static void createUI() {
  lv_obj_t *scr = lv_screen_active();
  lv_obj_set_style_bg_color(scr, lv_color_hex(0x0a0a0a), 0);

  // Title — compact
  lblTitle = lv_label_create(scr);
  lv_label_set_text(lblTitle, "MiMo Monitor");
  lv_obj_set_style_text_color(lblTitle, lv_color_hex(0x7c3aed), 0);
  lv_obj_set_style_text_font(lblTitle, &lv_font_montserrat_16, 0);
  lv_obj_align(lblTitle, LV_ALIGN_TOP_MID, 0, 8);

  // Clock — top right, small
  lblClock = lv_label_create(scr);
  lv_label_set_text(lblClock, "--:--");
  lv_obj_set_style_text_color(lblClock, lv_color_hex(0x6e6e73), 0);
  lv_obj_set_style_text_font(lblClock, &lv_font_montserrat_12, 0);
  lv_obj_align(lblClock, LV_ALIGN_TOP_RIGHT, -8, 8);

  // Credits used — big number, center-ish
  lblCredits = lv_label_create(scr);
  lv_label_set_text(lblCredits, "---");
  lv_obj_set_style_text_color(lblCredits, lv_color_hex(0xffffff), 0);
  lv_obj_set_style_text_font(lblCredits, &lv_font_montserrat_24, 0);
  lv_obj_align(lblCredits, LV_ALIGN_CENTER, 0, -40);

  // Percent
  lblPercent = lv_label_create(scr);
  lv_label_set_text(lblPercent, "--%");
  lv_obj_set_style_text_color(lblPercent, lv_color_hex(0xa1a1a6), 0);
  lv_obj_set_style_text_font(lblPercent, &lv_font_montserrat_16, 0);
  lv_obj_align(lblPercent, LV_ALIGN_CENTER, 0, -14);

  // Progress bar — full width minus margins
  barExpected = lv_bar_create(scr);
  lv_obj_set_size(barExpected, SCREEN_WIDTH - 40, 10);
  lv_obj_align(barExpected, LV_ALIGN_CENTER, 0, 10);
  lv_bar_set_range(barExpected, 0, 100);
  lv_bar_set_value(barExpected, 0, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(barExpected, lv_color_hex(0x2a2a2e), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(barExpected, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_bg_color(barExpected, lv_color_hex(0x3b1f6e), LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(barExpected, LV_OPA_COVER, LV_PART_INDICATOR);

  barUsage = lv_bar_create(scr);
  lv_obj_set_size(barUsage, SCREEN_WIDTH - 40, 10);
  lv_obj_align(barUsage, LV_ALIGN_CENTER, 0, 10);
  lv_bar_set_range(barUsage, 0, 100);
  lv_bar_set_value(barUsage, 0, LV_ANIM_OFF);
  lv_obj_set_style_bg_opa(barUsage, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_bg_color(barUsage, lv_color_hex(0xc4b5fd), LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(barUsage, LV_OPA_COVER, LV_PART_INDICATOR);

  // Burn rate — below bar
  lblRate = lv_label_create(scr);
  lv_label_set_text(lblRate, "--- /hr");
  lv_obj_set_style_text_color(lblRate, lv_color_hex(0x6e6e73), 0);
  lv_obj_set_style_text_font(lblRate, &lv_font_montserrat_14, 0);
  lv_obj_align(lblRate, LV_ALIGN_CENTER, 0, 36);

  // Balance
  lblBalance = lv_label_create(scr);
  lv_label_set_text(lblBalance, "");
  lv_obj_set_style_text_color(lblBalance, lv_color_hex(0xa1a1a6), 0);
  lv_obj_set_style_text_font(lblBalance, &lv_font_montserrat_14, 0);
  lv_obj_align(lblBalance, LV_ALIGN_CENTER, 0, 56);

  // Status — bottom
  lblStatus = lv_label_create(scr);
  lv_label_set_text(lblStatus, "Connecting...");
  lv_obj_set_style_text_color(lblStatus, lv_color_hex(0x6e6e73), 0);
  lv_obj_set_style_text_font(lblStatus, &lv_font_montserrat_12, 0);
  lv_obj_align(lblStatus, LV_ALIGN_BOTTOM_MID, 0, -8);
}

#endif

// ── Data update ──────────────────────────────────────────────────
static void updateUI(const MiMoData &d) {
  if (!d.valid) {
    lv_label_set_text(lblStatus, d.error.c_str());
    forceRedraw();
    return;
  }

  char buf[64];

  // Credits used
  if (d.credits_used >= 1e9)
    snprintf(buf, sizeof(buf), "%.2fB", d.credits_used / 1e9);
  else if (d.credits_used >= 1e6)
    snprintf(buf, sizeof(buf), "%.1fM", d.credits_used / 1e6);
  else
    snprintf(buf, sizeof(buf), "%.0f", d.credits_used);
  lv_label_set_text(lblCredits, buf);

  // Percent
  float pct = d.percent_used;
  snprintf(buf, sizeof(buf), "%.1f%%", pct);
  lv_label_set_text(lblPercent, buf);

  // Expected usage based on billing cycle progress
  if (d.plan_expires_at.length() >= 19) {
    struct tm exp_tm = {};
    const char *e = d.plan_expires_at.c_str();
    exp_tm.tm_year = atoi(e) - 1900;
    exp_tm.tm_mon  = atoi(e+5) - 1;
    exp_tm.tm_mday = atoi(e+8);
    exp_tm.tm_hour = atoi(e+11);
    exp_tm.tm_min  = atoi(e+14);
    exp_tm.tm_sec  = atoi(e+17);
    setenv("TZ", "UTC0", 1); tzset();
    time_t exp_epoch = mktime(&exp_tm);
    setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1); tzset();

    time_t now_epoch = time(NULL);
    float days_remaining = (float)(exp_epoch - now_epoch) / 86400.0f;
    float days_elapsed = 30.0f - days_remaining;
    if (days_elapsed < 0) days_elapsed = 0;
    float expected_pct = (days_elapsed / 30.0f) * 100.0f;
    if (expected_pct > 100) expected_pct = 100;

    lv_bar_set_value(barExpected, (int32_t)expected_pct, LV_ANIM_OFF);

    // Over budget = red, under = purple
    if (pct > expected_pct) {
      lv_obj_set_style_text_color(lblPercent, lv_color_hex(0xff4444), 0);
      lv_obj_set_style_bg_color(barUsage, lv_color_hex(0xff4444), LV_PART_INDICATOR);
      lv_obj_set_style_bg_opa(barUsage, LV_OPA_COVER, LV_PART_INDICATOR);
    } else {
      lv_obj_set_style_text_color(lblPercent, lv_color_hex(0xa1a1a6), 0);
      lv_obj_set_style_bg_color(barUsage, lv_color_hex(0x8b5cf6), LV_PART_INDICATOR);
      lv_obj_set_style_bg_opa(barUsage, LV_OPA_COVER, LV_PART_INDICATOR);
    }
  } else {
    lv_bar_set_value(barExpected, 0, LV_ANIM_OFF);
    lv_obj_set_style_text_color(lblPercent, lv_color_hex(0xa1a1a6), 0);
    lv_obj_set_style_bg_color(barUsage, lv_color_hex(0x8b5cf6), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(barUsage, LV_OPA_COVER, LV_PART_INDICATOR);
  }

  lv_bar_set_value(barUsage, (int32_t)pct, LV_ANIM_OFF);

  // Burn rate
  if (d.burn_type == "balance") {
    snprintf(buf, sizeof(buf), "burn: $%.2f/hr", d.burn_rate);
  } else if (d.burn_rate >= 1e6) {
    snprintf(buf, sizeof(buf), "burn: %.1fM/hr", d.burn_rate / 1e6);
  } else if (d.burn_rate >= 1e3) {
    snprintf(buf, sizeof(buf), "burn: %.1fK/hr", d.burn_rate / 1e3);
  } else if (d.burn_rate > 0) {
    snprintf(buf, sizeof(buf), "burn: %.0f/hr", d.burn_rate);
  } else {
    snprintf(buf, sizeof(buf), "burn: idle");
  }
  lv_label_set_text(lblRate, buf);

  // Balance
  snprintf(buf, sizeof(buf), "credit: $%.2f", d.balance);
  lv_label_set_text(lblBalance, buf);

  // Status — format timestamp as dd.mm.yyyy hh:mm in Europe/Zurich
  {
    const char *s = d.last_updated.c_str();
    if (strlen(s) >= 19) {
      // Parse UTC ISO 8601
      struct tm utc_tm = {};
      utc_tm.tm_year = atoi(s) - 1900;
      utc_tm.tm_mon  = atoi(s+5) - 1;
      utc_tm.tm_mday = atoi(s+8);
      utc_tm.tm_hour = atoi(s+11);
      utc_tm.tm_min  = atoi(s+14);
      utc_tm.tm_sec  = atoi(s+17);
      // mktime treats input as local — set TZ to UTC temporarily
      setenv("TZ", "UTC0", 1);
      tzset();
      time_t epoch = mktime(&utc_tm);
      // Restore Europe/Zurich
      setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
      tzset();
      struct tm *local = localtime(&epoch);
      snprintf(buf, sizeof(buf), "Updated: %02d.%02d.%04d %02d:%02d",
               local->tm_mday, local->tm_mon + 1, local->tm_year + 1900,
               local->tm_hour, local->tm_min);
    } else {
      snprintf(buf, sizeof(buf), "Updated: %s", s);
    }
  }
  lv_label_set_text(lblStatus, buf);

  forceRedraw();
}

// ── Clock ────────────────────────────────────────────────────────
static uint32_t lastClockUpdate = 0;

static void updateClock() {
  if (millis() - lastClockUpdate < 1000) return;
  lastClockUpdate = millis();

  time_t now = time(NULL);
  struct tm *t = localtime(&now);
  if (t && t->tm_year > 120) {
    char buf[16];
    strftime(buf, sizeof(buf), "%H:%M", t);
    lv_label_set_text(lblClock, buf);
    forceRedraw();
  }
}

// ── Loop state (declared before setup so initial fetch can set timer) ──
static uint32_t lastFetch = 0;
static uint32_t retryDelay = 0;
static int consecutiveFails = 0;
static bool wifiOk = false;

// ── Touch state ──────────────────────────────────────────────────
#define TOUCH_COOLDOWN_MS 3000
static bool touchActive = false;
static uint32_t lastTouchFetch = 0;

// ── Setup ────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(500);  // Short delay — long delays trigger task watchdog reset on ESP32
#if defined(WAVESHARE_5B)
  Serial.println("\n=== MiMo Token Display (Waveshare 5B) ===");
#else
  Serial.println("\n=== MiMo Token Display (CYD) ===");
#endif

  // Display + LVGL
  displayInit();
  lv_init_display();
  createUI();
  Serial.println("[Display] OK");

  // WiFi
  lv_label_set_text(lblStatus, "Connecting to WiFi...");
  forceRedraw();
  bool wifiOk = wifiConnect();
  if (!wifiOk) {
    lv_label_set_text(lblStatus, "WiFi failed — check config.h");
    forceRedraw();
  } else {
    lv_label_set_text(lblStatus, "WiFi connected, syncing time...");
    forceRedraw();
    // NTP after WiFi
    configTzTime("CET-1CEST,M3.5.0,M10.5.0/3", "pool.ntp.org", "time.nist.gov");
    // Wait for NTP (up to 5s)
    for (int i = 0; i < 10; i++) {
      time_t now = time(NULL);
      struct tm *t = localtime(&now);
      if (t && t->tm_year > 120) break;
      delay(500);
    }
    lv_label_set_text(lblStatus, "Fetching MiMo data...");
    forceRedraw();
    // First fetch immediately
    MiMoData d = fetchMiMoData();
    updateUI(d);
    lastFetch = millis();  // reset loop timer
  }
}

// ── Loop ─────────────────────────────────────────────────────────
void loop() {
  lv_timer_handler();
  updateClock();

  uint32_t interval = (consecutiveFails > 0) ? retryDelay : REFRESH_INTERVAL_MS;

  // Touch → immediate manual fetch
  int16_t tx, ty;
  bool touched = _tft->getTouch(&tx, &ty);
  if (touched && !touchActive && (millis() - lastTouchFetch > TOUCH_COOLDOWN_MS)) {
    Serial.println("[Touch] Manual fetch triggered");
    lv_label_set_text(lblStatus, "Refreshing...");
    forceRedraw();
    lv_timer_handler();
    delay(500);
    lastTouchFetch = millis();
    lastFetch = millis();
    consecutiveFails = 0;
    retryDelay = 0;
    MiMoData d = fetchMiMoData();
    updateUI(d);
  }
  touchActive = touched;

  if (millis() - lastFetch >= interval) {
    lastFetch = millis();

    // WiFi reconnect only on retry
    if (consecutiveFails > 0) {
      wifiEnsure();
    }

    MiMoData d = fetchMiMoData();
    updateUI(d);

    if (d.valid) {
      consecutiveFails = 0;
      retryDelay = 0;
    } else {
      consecutiveFails++;
      uint32_t backoff = 30000 * (1 << min(consecutiveFails - 1, 3));
      retryDelay = (backoff < REFRESH_INTERVAL_MS) ? backoff : REFRESH_INTERVAL_MS;
      Serial.printf("  Error (fail #%d, retry in %lus): %s\n",
                    consecutiveFails, retryDelay / 1000, d.error.c_str());
    }
  }

  delay(5);
}
