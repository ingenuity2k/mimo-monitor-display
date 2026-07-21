#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
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
  String error;
  bool valid;
};

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

// Fetch current MiMo data from Supabase
MiMoData fetchMiMoData() {
  MiMoData data = {};
  data.valid = false;

  if (WiFi.status() != WL_CONNECTED) {
    data.error = "[" + getTimeHHMM() + "] WiFi disconnected";
    Serial.println("[Fetch] No WiFi");
    return data;
  }

  HTTPClient http;
  String url = String(SUPABASE_URL) + MIMO_ENDPOINT;
  http.begin(url);
  http.addHeader("apikey", SUPABASE_KEY);
  http.addHeader("Authorization", String("Bearer ") + SUPABASE_KEY);

  int code = http.GET();
  if (code != 200) {
    data.error = "[" + getTimeHHMM() + "] HTTP " + String(code);
    Serial.printf("[Fetch] HTTP %d\n", code);
    http.end();
    return data;
  }

  String payload = http.getString();
  http.end();

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

  if (data.credits_limit > 0) {
    data.percent_used = (data.credits_used / data.credits_limit) * 100.0f;
  }

  data.valid = true;
  Serial.printf("[Fetch] OK — used: %.0f, limit: %.0f, rate: %.1f/h\n",
                data.credits_used, data.credits_limit, data.burn_rate);
  return data;
}
