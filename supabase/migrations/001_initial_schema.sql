-- MiMo Monitor Schema
-- Stores Xiaomi MiMo API scrape history and computes burn rates

-- History table: one row per scrape
CREATE TABLE IF NOT EXISTS mimo_scrapes (
    id uuid DEFAULT gen_random_uuid() PRIMARY KEY,
    scraped_at timestamptz DEFAULT now() NOT NULL,
    -- Pay-as-you-go balance
    balance numeric(12,4),
    currency text DEFAULT 'USD',
    gift_balance numeric(12,4),
    cash_balance numeric(12,4),
    -- Token plan details
    plan_code text,           -- e.g. "pro", "lite", "standard", "max"
    plan_name text,           -- e.g. "Pro"
    plan_expires_at timestamptz,
    auto_renew boolean DEFAULT false,
    -- Token plan usage
    credits_used bigint,      -- raw credits consumed
    credits_limit bigint,     -- total credits in plan
    credits_percent numeric(6,4), -- 0.0 to 1.0
    -- Error tracking
    error text                -- non-null if scrape failed
);

-- Index for burn rate queries (last 24h of scrapes)
CREATE INDEX IF NOT EXISTS idx_mimo_scrapes_scraped_at ON mimo_scrapes (scraped_at DESC);

-- Current status table: single row, upserted each scrape
CREATE TABLE IF NOT EXISTS mimo_current (
    id int PRIMARY KEY DEFAULT 1 CHECK (id = 1), -- enforce single row
    last_scraped_at timestamptz,
    -- Balance
    balance numeric(12,4),
    currency text DEFAULT 'USD',
    gift_balance numeric(12,4),
    cash_balance numeric(12,4),
    -- Plan
    plan_code text,
    plan_name text,
    plan_expires_at timestamptz,
    auto_renew boolean DEFAULT false,
    -- Usage
    credits_used bigint,
    credits_limit bigint,
    credits_percent numeric(6,4),
    -- Computed
    burn_rate_per_hour numeric(12,2),  -- credits/hour based on last 4h window
    estimated_hours_remaining numeric(10,2),
    -- Meta
    scrape_count int DEFAULT 0,        -- total scrapes in history
    updated_at timestamptz DEFAULT now()
);

-- Seed the single row
INSERT INTO mimo_current (id) VALUES (1) ON CONFLICT DO NOTHING;
