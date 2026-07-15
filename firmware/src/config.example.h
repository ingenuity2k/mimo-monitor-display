#pragma once

// ── WiFi ──────────────────────────────────────────────────────────
#define WIFI_SSID     "YOUR_WIFI_SSID"
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"

// ── Supabase ──────────────────────────────────────────────────────
// Get these from your Supabase project: Settings → API
#define SUPABASE_URL  "https://YOUR_PROJECT.supabase.co"
#define SUPABASE_KEY  "YOUR_ANON_KEY"

// ── MiMo Monitor ─────────────────────────────────────────────────
// Table created by the Supabase migration
#define MIMO_TABLE    "mimo_current"
#define MIMO_ENDPOINT "/rest/v1/" MIMO_TABLE "?select=*"

// ── Refresh interval ─────────────────────────────────────────────
#define REFRESH_INTERVAL_MS 300000  // 5 minutes

// ── Display ───────────────────────────────────────────────────────
#define SCREEN_WIDTH  320
#define SCREEN_HEIGHT 240
