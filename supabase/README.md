# Supabase Backend Setup

This directory contains the database migrations and edge function that power the MiMo Monitor Display.

## Prerequisites

- A [Supabase](https://supabase.com) account (free tier works)
- [Supabase CLI](https://supabase.com/docs/guides/cli) installed (`npm install -g supabase`)
- A Xiaomi MiMo account with active API credits/subscription

## Quick Start

### 1. Create a Supabase Project

1. Go to [supabase.com](https://supabase.com) and sign in
2. Create a new project (any region)
3. Note your **Project URL** and **anon key** from Settings → API

### 2. Run Migrations

**Option A: SQL Editor (easiest)**

1. Open SQL Editor in your Supabase dashboard
2. Copy and run the contents of `migrations/001_initial_schema.sql`
3. Copy and run the contents of `migrations/002_add_burn_type.sql`

**Option B: Supabase CLI**

```bash
supabase link --project-ref YOUR_PROJECT_REF
supabase db push
```

### 3. Get Xiaomi Cookies

The scraper authenticates to the MiMo API using your browser cookies:

1. Go to [platform.xiaomimimo.com](https://platform.xiaomimimo.com) and log in
2. Open DevTools (F12) → Application → Cookies
3. Select all cookies for `platform.xiaomimimo.com`
4. Copy them as a single string: `key1=value1; key2=value2; ...`

> **Note:** Cookies expire periodically (usually after a few days/weeks).
> When the scraper starts failing, refresh your cookies by logging in
> again and repeating this step.

### 4. Set Supabase Secrets

**Via Dashboard:**
1. Go to Settings → Edge Functions → Secrets
2. Add `XIAOMI_COOKIES` with your cookie string

**Via CLI:**
```bash
supabase secrets set XIAOMI_COOKIES="your_cookie_string_here"
```

### 5. Deploy the Edge Function

```bash
supabase login
supabase link --project-ref YOUR_PROJECT_REF
supabase functions deploy mimo-scraper
```

### 6. Schedule the Scraper

The scraper needs to run every 5 minutes to keep data fresh.

**Option A: Supabase pg_cron (recommended)**

Run in SQL Editor:
```sql
-- Enable pg_cron extension
CREATE EXTENSION IF NOT EXISTS pg_cron;

-- Schedule the scraper every 5 minutes
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

**Option B: External cron (e.g., cron-job.org)**

- Create a cron job that POSTs to `https://YOUR_PROJECT_REF.supabase.co/functions/v1/mimo-scraper` every 5 minutes
- Add header: `Authorization: Bearer YOUR_SERVICE_ROLE_KEY`

### 7. Verify

Call the edge function manually:
```bash
curl -X POST https://YOUR_PROJECT_REF.supabase.co/functions/v1/mimo-scraper \
  -H "Authorization: Bearer YOUR_SERVICE_ROLE_KEY"
```

Check the `mimo_current` table in your Supabase dashboard — it should have one row with your data.

## Database Schema

### `mimo_scrapes` table

One row per scrape — full history.

| Column | Type | Description |
|--------|------|-------------|
| id | uuid | Primary key |
| scraped_at | timestamptz | When the scrape happened |
| balance | numeric | Pay-as-you-go USD balance |
| currency | text | Currency (default: USD) |
| gift_balance | numeric | Gift balance |
| cash_balance | numeric | Cash balance |
| plan_code | text | Plan code (e.g., "pro") |
| plan_name | text | Plan name (e.g., "Pro") |
| plan_expires_at | timestamptz | When current billing cycle ends |
| auto_renew | boolean | Whether auto-renew is enabled |
| credits_used | bigint | Credits consumed this cycle |
| credits_limit | bigint | Total credits in plan |
| credits_percent | numeric | Usage percentage (0.0–1.0) |
| burn_type | text | "credits" or "balance" |
| error | text | Non-null if scrape failed |

### `mimo_current` table

Single row — latest data, upserted each scrape. This is what the ESP32 reads.

| Column | Type | Description |
|--------|------|-------------|
| id | int | Always 1 (enforced singleton) |
| last_scraped_at | timestamptz | Last successful scrape |
| balance | numeric | Current USD balance |
| credits_used | bigint | Credits consumed |
| credits_limit | bigint | Total credits in plan |
| credits_percent | numeric | Usage percentage |
| burn_rate_per_hour | numeric | Burn rate (credits or $/hr) |
| estimated_hours_remaining | numeric | Hours until quota exhausted |
| burn_type | text | "credits" or "balance" |
| scrape_count | int | Total scrapes in history |
| updated_at | timestamptz | Last upsert timestamp |

## Troubleshooting

### Edge function returns 500 / "XIAOMI_COOKIES not set"
- Check that `XIAOMI_COOKIES` is set in Supabase secrets
- Cookies may have expired — get fresh ones from your browser

### No data in table
- Check edge function logs: `supabase functions logs mimo-scraper`
- Verify the cron job is running
- Test the function manually with curl (step 7)

### ntfy alerts
The scraper sends failure notifications to `ntfy.sh/mimo-monitor` on the first failure each day. Subscribe in the [ntfy app](https://ntfy.sh/) or with:
```bash
curl -s ntfy.sh/mimo-monitor
```
