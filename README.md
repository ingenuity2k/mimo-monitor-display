# MiMo Monitor Display

Physical desk monitor for Xiaomi MiMo API token usage. Shows live credit and subscription plan token consumption, burn rate, balance, and quota progress on a **CYD (Cheap Yellow Display)** ESP32 board.

<p align="center">
  <img src="docs/images/hero.png" alt="MiMo Monitor Display" width="600">
</p>

## What It Does

- Fetches data from your Supabase backend (which scrapes the MiMo API every 5 minutes)
- Displays: credits used, percentage, burn rate, balance, progress bar
- **Expected usage bar** — shows where you *should* be based on billing cycle progress
- **Color coding** — purple = under budget, red = over budget
- Auto-refreshes every 5 minutes over WiFi
- **Touch to refresh** — tap the screen to fetch fresh data instantly
- **Smart retry** — exponential backoff (30s → 60s → 120s) on failures, with WiFi auto-reconnect
- **Error timestamps** — every error shows [HH:MM] so you know when it happened
- **Memory health monitoring** — preemptive reboot if heap fragmentation gets critical (largest block < 20KB)
- **Heap stats display** — free memory + fragmentation ratio in footer, color-coded (blue/orange/red)
- **Error view** — shows on repeated HTTP failures or WiFi down (touch to reboot)
- **Periodic heap defrag** — coalesces free blocks every 10 fetches to slow fragmentation
- Clock synced via NTP

## Hardware You Need

| Component | Model | Price | Where |
|-----------|-------|-------|-------|
| **CYD** | ESP32-2432S028R | ~$10-15 | AliExpress, Amazon |
| **USB-C cable** | Any | — | — |

Search for: **"ESP32-2432S028R"** or **"Cheap Yellow Display"**

Specs: 2.8" TFT (ILI9341), 320×240, ESP32-WROOM-32 (520KB SRAM, 4MB Flash), USB-C, resistive touch (XPT2046).

## Setup Overview

1. **Set up Supabase backend** — database + edge function scraper
2. **Configure and flash ESP32** — WiFi + Supabase credentials
3. **Done** — live data on your desk

---

## Step 1: Supabase Backend

### 1.1 Create a Supabase Project

1. Go to [supabase.com](https://supabase.com), create a free account
2. Create a new project (any region)
3. Note your **Project URL** and **anon key** (Settings → API)

### 1.2 Create the Database Tables

Go to **SQL Editor** in your Supabase dashboard and run:

```sql
-- Paste contents of: supabase/migrations/001_initial_schema.sql
```

Then:

```sql
-- Paste contents of: supabase/migrations/002_add_burn_type.sql
```

This creates two tables:
- `mimo_scrapes` — history of every scrape (one row per scrape)
- `mimo_current` — single row with latest data (what the ESP32 reads)

### 1.3 Get Your MiMo Cookies

The scraper needs your Xiaomi session cookies to access the MiMo API.

1. Go to [platform.xiaomimimo.com](https://platform.xiaomimimo.com) and **log in**
2. Open browser **DevTools** (F12) → **Application** → **Cookies**
3. Click on `platform.xiaomimimo.com`
4. Select **all** cookies (Ctrl+A / Cmd+A)
5. Right-click → **Copy** → **Copy all as Netscape** or just copy the name=value pairs as a single string

The cookie string looks like:
```
cookie1=value1; cookie2=value2; cookie3=value3; ...
```

> **Note:** These cookies expire periodically (usually after 24h). When they do, the scraper will fail and you'll get an ntfy alert. Just repeat this step to refresh or utilize the refresh-cookies.js script to make it easy.

### 1.4 Set Supabase Secrets

In your Supabase dashboard → **Settings** → **Edge Functions** → **Secrets**, add:

| Key | Value |
|-----|-------|
| `XIAOMI_COOKIES` | Your cookie string from step 1.3 |

Or via CLI:

```bash
supabase secrets set XIAOMI_COOKIES="cookie1=value1; cookie2=value2; ..."
```

### 1.5 Deploy the Edge Function

```bash
# Install Supabase CLI (if you haven't)
npm install -g supabase

# Login
supabase login

# Link to your project
supabase link --project-ref YOUR_PROJECT_REF

# Deploy the scraper
supabase functions deploy mimo-scraper
```

### 1.6 Schedule the Scraper

The scraper needs to run every 5 minutes. Choose one:

**Option A: Supabase pg_cron (recommended)**

Run this in the SQL Editor:

```sql
-- Enable pg_cron
CREATE EXTENSION IF NOT EXISTS pg_cron;

-- Schedule every 5 minutes
SELECT cron.schedule(
  'mimo-scraper',
  '*/5 * * * *',
  $$
  SELECT net.http_post(
    url := 'https://YOUR_PROJECT_REF.supabase.co/functions/v1/mimo-scraper',
    headers := '{"Authorization": "Bearer YOUR_SERVICE_ROLE_KEY"}'::jsonb
  );
  $$
);
```

Find your service role key in: Settings → API → `service_role` key (⚠️ keep this secret!).

**Option B: External cron**

Use [cron-job.org](https://cron-job.org), [UptimeRobot](https://uptimerobot.com), or any cron service:

- **URL:** `https://YOUR_PROJECT_REF.supabase.co/functions/v1/mimo-scraper`
- **Method:** POST
- **Header:** `Authorization: Bearer YOUR_SERVICE_ROLE_KEY`
- **Interval:** Every 5 minutes

### 1.7 Verify It's Working

Call the edge function manually:

```bash
curl -X POST https://YOUR_PROJECT_REF.supabase.co/functions/v1/mimo-scraper \
  -H "Authorization: Bearer YOUR_SERVICE_ROLE_KEY"
```

Check your `mimo_current` table — it should have one row with your data.

---

## Step 2: ESP32 Firmware

### 2.1 Install PlatformIO

- **VS Code:** Install the [PlatformIO extension](https://platformio.org/install/ide?install=vscode)
- **CLI:** `pip install platformio`

### 2.2 Configure

1. Copy the example config:

   ```bash
   cp firmware/src/config.example.h firmware/src/config.h
   ```

2. Edit `firmware/src/config.h`:

   ```c
   #define WIFI_SSID     "YourWiFiName" # The CYD only supports 2.4Ghz networks!
   #define WIFI_PASSWORD "YourWiFiPassword"
   #define SUPABASE_URL  "https://your-project.supabase.co"
   #define SUPABASE_KEY  "your-anon-key"
   ```

3. Edit the timezone in `firmware/src/main.cpp` — search for `CET-1CEST` (appears in 3 places) and replace with your timezone.

   Find your POSIX timezone string: https://github.com/nayarsystems/posix_tz_db/blob/master/zones.csv

   Examples:
   - US Eastern: `EST5EDT,M3.2.0,M11.1.0`
   - US Pacific: `PST8PDT,M3.2.0,M11.1.0`
   - UK: `GMT0BST,M3.5.0/1,M10.5.0`
   - Japan: `JST-9`
   - Australia: `AEST-10AEDT,M10.1.0,M4.1.0/3`

### 2.3 Flash

1. Connect the CYD via USB-C
2. Flash:

   ```bash
   cd firmware
   pio run -e cyd --target upload
   ```

3. If upload fails, **hold the BOOT button** on the CYD while PlatformIO tries to connect, then release when flashing starts.

### 2.4 Monitor (Optional)

```bash
pio device monitor -e cyd
```

You should see:
```
=== MiMo Monitor Display (CYD) ===
[Display] OK
[WiFi] Connecting to YourWiFi...
[WiFi] Connected! IP: 192.168.x.x
[Fetch] OK — used: 25310000000, limit: 38000000000, rate: 850000.0/h
```

---

## Step 3: Enjoy

The display shows:

| Element | Description |
|---------|-------------|
| **Title** | "MiMo Monitor" in purple |
| **Clock** | Current time (top right) |
| **Credits used** | Large number (e.g., "25.31B") |
| **Percentage** | Of your plan quota |
| **Progress bar** | Purple = under budget, red = over budget |
| **Darker bar** | Expected usage based on billing cycle |
| **Burn rate** | Credits or dollars per hour |
| **Balance** | Remaining USD credit |
| **Last updated** | Timestamp |
| **Memory stats** | Free heap (KB) + fragmentation ratio, color-coded |
| **Touch** | Tap anywhere to refresh instantly |

---

## Architecture

```
┌──────────────┐     ┌─────────────────┐     ┌──────────────┐
│  Xiaomi API  │────▶│  Supabase Edge  │────▶│   Supabase   │
│  (MiMo)      │     │  Function       │     │   Database   │
└──────────────┘     └─────────────────┘     └──────┬───────┘
                                                     │ REST API
                                              ┌──────▼───────┐
                                              │  ESP32 CYD   │
                                              │  (display)   │
                                              └──────────────┘
```

- **Supabase Edge Function** scrapes the Xiaomi MiMo API every 5 minutes
- **Supabase Database** stores scrape history + current status
- **ESP32 CYD** reads `mimo_current` table via REST API and displays it

## ntfy Alerts

The scraper sends a notification to `ntfy.sh/my-mimo-monitor` on the first failure each day (usually means cookies expired).

Subscribe:
- **Web:** https://ntfy.sh/my-mimo-monitor
- **App:** Search for "my-mimo-monitor" in the [ntfy app](https://ntfy.sh/)
- **CLI:** `curl -s ntfy.sh/my-mimo-monitor`
> **Note:** You probably want to use a custom topic, so you don't get notifications of other people's setup. Edit the supabase/functions/mimo-scraper/index.ts file (NTFY_TOPIC) and re-deploy the edge function.

## Refreshing Cookies

Xiaomi cookies expire periodically. When the scraper starts failing (you'll get an ntfy alert), run:

```bash
# Set your Supabase Personal Access Token and project ref
export SUPABASE_PAT=sbp_xxx
export SUPABASE_PROJECT_REF=your_project_ref

node scripts/refresh-cookies.js
```

This script:
1. Finds and refreshes the MiMo platform tab in Chrome
2. Reads & decrypts cookies from Chrome's local cookie DB
3. Pushes them to Supabase Management API as a secret

No manual copy-paste needed. Requires Google Chrome on macOS as-is.

**Get a Supabase PAT:** https://supabase.com/dashboard/account/tokens

## Troubleshooting

| Problem | Fix |
|---------|-----|
| "WiFi failed — check config.h" | Check SSID/password. CYD only supports 2.4GHz. |
| "Fetch failed — retrying..." | Error now shows specifics (e.g. `[23:42] HTTP 401`). Check Supabase URL + anon key. Verify `mimo_current` table has data. Auto-retries with backoff. |
| Display shows error view | Repeated HTTP failures or WiFi down for 5+ min. Touch screen to reboot. Memory issues auto-reboot preemptively. |
| White/blank display | Power cycle. Backlight is on GPIO 21 (handled automatically). |
| Cookie expired | Run `node scripts/refresh-cookies.js` (auto-extracts from Chrome), or manually: log in to [platform.xiaomimimo.com](https://platform.xiaomimimo.com), copy cookies, run `supabase secrets set XIAOMI_COOKIES="..."`. |
| Upload fails | Hold BOOT button while PlatformIO connects. |
| `LV_COLOR_DEPTH` error | Make sure `lv_conf.h` exists at both `firmware/src/lv_conf.h` and `firmware/lv_conf.h`. |

## Customization

- **Refresh interval:** Change `REFRESH_INTERVAL_MS` in `config.h` (default: 300000 = 5 min)
- **Colors:** Edit hex values in `main.cpp` → `createUI()` function
- **Layout:** Adjust `lv_obj_align()` positions in `createUI()`
- **Billing cycle:** The expected usage bar assumes 30 days. Adjust the `30.0f` value in `updateUI()` if your plan differs.

## Credits

- [LVGL](https://lvgl.io/) — UI framework
- [LovyanGFX](https://github.com/lovyan03/LovyanGFX) — display driver
- [PlatformIO](https://platformio.org/) — build system
- [ArduinoJson](https://arduinojson.org/) — JSON parsing

## Support

If you enjoy this project, consider buying me a coffee ☕

[![Buy Me A Coffee](https://img.shields.io/badge/Buy%20Me%20A%20Coffee-FFDD00?style=for-the-badge&logo=buymeacoffee&logoColor=black)](https://buymeacoffee.com/ingenuity2k)

## License

MIT — see [LICENSE](LICENSE).
