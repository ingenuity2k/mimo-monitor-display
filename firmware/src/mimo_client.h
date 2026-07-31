#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <esp_heap_caps.h>
#include "config.h"

struct MiMoData {
  float credits_used;
  float credits_limit;
  float burn_rate;      // credits per hour (or $/hr if burn_type=="balance")
  float percent_used;
  float balance;        // USD balance
  String burn_type;     // "credits" or "balance"
  String last_updated;
  String plan_expires_at;
  String scraper_error; // upstream error from scraper (e.g. token expired)
  String error;
  bool valid;
};

// ── Heap health ───────────────────────────────────────────────────
struct HeapStats {
  size_t freeKB;
  float fragPct;  // 0-100
};

// Check heap health. Reboots if largest contiguous block < 20KB.
// Call BEFORE each SSL request.
void checkHeapHealth() {
  size_t freeHeap = ESP.getFreeHeap();
  size_t largestBlock = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
  float frag = (freeHeap > 0) ? (1.0f - ((float)largestBlock / (float)freeHeap)) * 100.0f : 0.0f;

  Serial.printf("[Heap] Free: %uKB | Largest: %uKB | Frag: %.0f%%\n",
                freeHeap / 1024, largestBlock / 1024, frag);

  if (largestBlock < 20480) {  // < 20KB
    Serial.printf("[Heap] CRITICAL — largest block %u bytes, rebooting!\n", largestBlock);
    delay(100);
    ESP.restart();
  }
}

// Get heap stats for display
HeapStats getHeapStats() {
  HeapStats stats;
  stats.freeKB = ESP.getFreeHeap() / 1024;
  size_t largestBlock = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
  size_t freeHeap = ESP.getFreeHeap();
  stats.fragPct = (freeHeap > 0) ? (1.0f - ((float)largestBlock / (float)freeHeap)) * 100.0f : 0.0f;
  return stats;
}

// Periodic heap integrity check — coalesces adjacent free blocks
static int fetchCount = 0;
void maybeCheckHeapIntegrity() {
  fetchCount++;
  if (fetchCount % 10 == 0) {
    Serial.println("[Heap] Running integrity check (coalesce free blocks)...");
    heap_caps_check_integrity_all(true);
  }
}

// ── Time ──────────────────────────────────────────────────────────
// Get current time as HH:MM string
String getTimeHHMM() {
  time_t now;
  struct tm info;
  time(&now);
  localtime_r(&now, &info);
  char buf[6];
  snprintf(buf, sizeof(buf), "%02d:%02d", info.tm_hour, info.tm_min);
  return String(buf);
}

// ── WiFi ──────────────────────────────────────────────────────────
// Connect to WiFi, returns true on success
bool wifiConnect() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.printf("[WiFi] Connecting to %s", WIFI_SSID);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("\n[WiFi] Connected! IP: %s\n", WiFi.localIP().toString().c_str());
    return true;
  }

  Serial.println("\n[WiFi] Connection failed!");
  return false;
}

// Reconnect WiFi if disconnected. Returns true if connected.
bool wifiEnsure() {
  if (WiFi.status() == WL_CONNECTED) return true;
  Serial.println("[WiFi] Reconnecting...");
  WiFi.disconnect();
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
    delay(500);
    Serial.print(".");
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("\n[WiFi] Reconnected! IP: %s\n", WiFi.localIP().toString().c_str());
    return true;
  }
  Serial.println("\n[WiFi] Reconnect failed!");
  return false;
}

// ── Fetch ─────────────────────────────────────────────────────────
// Fetch current MiMo data from Supabase
// Retries up to 3 times on failure. After repeated failures, resets WiFi.
static int fetchConsecutiveFails = 0;

MiMoData fetchMiMoData() {
  MiMoData data = {};
  data.valid = false;

  if (WiFi.status() != WL_CONNECTED) {
    data.error = "[" + getTimeHHMM() + "] WiFi disconnected";
    Serial.println("[Fetch] No WiFi");
    return data;
  }

  String url = String(SUPABASE_URL) + MIMO_ENDPOINT;
  int code = 0;
  String payload;

  for (int attempt = 1; attempt <= 3; attempt++) {
    checkHeapHealth();  // reboot if heap too fragmented for SSL
    HTTPClient http;
    http.begin(url);
    http.setConnectTimeout(8000);
    http.setTimeout(10000);
    http.addHeader("apikey", SUPABASE_KEY);
    http.addHeader("Authorization", String("Bearer ") + SUPABASE_KEY);

    code = http.GET();
    if (code == 200) {
      payload = http.getString();
      http.end();
      maybeCheckHeapIntegrity();
      break;
    }

    Serial.printf("[Fetch] HTTP %d (attempt %d/3)\n", code, attempt);
    data.error = "[" + getTimeHHMM() + "] HTTP " + String(code);
    http.end();

    if (attempt < 3) delay(1000 * attempt);  // backoff: 1s, 2s
  }

  if (code != 200) {
    fetchConsecutiveFails++;
    Serial.printf("[Fetch] Failed after 3 attempts (streak: %d)\n", fetchConsecutiveFails);

    // After 3 consecutive full failures (9 total attempts), reset WiFi
    if (fetchConsecutiveFails >= 3) {
      Serial.println("[Fetch] Too many failures — resetting WiFi...");
      WiFi.disconnect(true);
      delay(500);
      wifiConnect();
      fetchConsecutiveFails = 0;
    }
    return data;
  }

  fetchConsecutiveFails = 0;

  // Parse JSON array (first row)
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, payload);
  if (err || doc.size() == 0) {
    data.error = "[" + getTimeHHMM() + "] JSON: " + String(err.c_str());
    Serial.printf("[Fetch] JSON error: %s\n", err.c_str());
    return data;
  }

  JsonObject row = doc[0];
  data.credits_used  = row["credits_used"] | 0.0f;
  data.credits_limit = row["credits_limit"] | 0.0f;
  data.burn_rate     = row["burn_rate_per_hour"] | 0.0f;
  data.balance       = row["balance"] | 0.0f;
  data.burn_type     = row["burn_type"] | "credits";
  data.last_updated  = row["updated_at"] | "unknown";
  data.plan_expires_at = row["plan_expires_at"] | "";
  data.scraper_error = row["error"] | "";

  if (data.credits_limit > 0) {
    data.percent_used = (data.credits_used / data.credits_limit) * 100.0f;
  }

  data.valid = true;
  Serial.printf("[Fetch] OK — used: %.0f, limit: %.0f, rate: %.1f/h\n",
                data.credits_used, data.credits_limit, data.burn_rate);
  return data;
}
