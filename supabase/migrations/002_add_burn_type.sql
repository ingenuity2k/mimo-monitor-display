-- Add burn_type to track whether burn rate is credits or balance
ALTER TABLE mimo_scrapes ADD COLUMN IF NOT EXISTS burn_type text; -- 'credits' | 'balance' | null
ALTER TABLE mimo_current ADD COLUMN IF NOT EXISTS burn_type text;
