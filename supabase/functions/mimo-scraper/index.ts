// MiMo Monitor — Supabase Edge Function
// Scrapes Xiaomi MiMo platform for usage/credit data, computes burn rate
// Sends ntfy alert on first failure per day
//
// Required Supabase secrets:
//   XIAOMI_COOKIES — full cookie string from platform.xiaomimimo.com
//
// Deploy: supabase functions deploy mimo-scraper
// Schedule: pg_cron every 5 minutes (see README)

const XIAOMI_BASE = "https://platform.xiaomimimo.com/api/v1";
const BURN_RATE_WINDOW_HOURS = 4;
const NTFY_TOPIC = "my-mimo-monitor";

// --- Supabase REST helpers ---

function supabaseHeaders(apiKey: string) {
  return {
    "apikey": apiKey,
    "Content-Type": "application/json",
    "Accept": "application/json",
    "Prefer": "return=minimal",
  };
}

async function supabaseSelect(url: string, apiKey: string, table: string, query: string) {
  const resp = await fetch(`${url}/rest/v1/${table}?${query}`, {
    headers: { ...supabaseHeaders(apiKey), "Prefer": "return=representation" },
  });
  if (!resp.ok) return null;
  return resp.json();
}

async function supabaseInsert(url: string, apiKey: string, table: string, row: Record<string, unknown>) {
  return fetch(`${url}/rest/v1/${table}`, {
    method: "POST",
    headers: supabaseHeaders(apiKey),
    body: JSON.stringify(row),
  });
}

async function supabaseUpsert(url: string, apiKey: string, table: string, row: Record<string, unknown>, onConflict: string) {
  const headers = { ...supabaseHeaders(apiKey), "Prefer": "resolution=merge-duplicates,return=minimal" };
  return fetch(`${url}/rest/v1/${table}?on_conflict=${onConflict}`, {
    method: "POST",
    headers,
    body: JSON.stringify(row),
  });
}

// --- Xiaomi API helpers ---

function xiaomiHeaders(cookies: string): HeadersInit {
  return {
    "accept": "*/*",
    "accept-language": "en",
    "content-type": "application/json",
    "cookie": cookies,
    "referer": "https://platform.xiaomimimo.com/console/",
    "x-timezone": "CET",
  };
}

async function xiaomiFetch<T>(path: string, cookies: string): Promise<T | null> {
  try {
    const resp = await fetch(`${XIAOMI_BASE}${path}`, {
      headers: xiaomiHeaders(cookies),
    });
    if (!resp.ok) return null;
    const json = await resp.json();
    if (json.code !== 0) return null;
    return json.data as T;
  } catch {
    return null;
  }
}

// --- ntfy alert (first failure per day) ---

async function maybeSendFailureAlert(
  supabaseUrl: string,
  supabaseKey: string,
  errorMsg: string,
): Promise<void> {
  try {
    const todayStart = new Date();
    todayStart.setUTCHours(0, 0, 0, 0);

    const existingErrors = await supabaseSelect(
      supabaseUrl, supabaseKey, "mimo_scrapes",
      `select=id&error=not.is.null&scraped_at=gte.${todayStart.toISOString()}&limit=1`
    );

    if (existingErrors && existingErrors.length >= 1) {
      return; // Already had failures today, skip alert
    }

    await fetch(`https://ntfy.sh/${NTFY_TOPIC}`, {
      method: "POST",
      headers: {
        "Title": "MiMo Monitor: Scrape Failed",
        "Tags": "warning",
        "Priority": "high",
      },
      body: `MiMo token scraper failed at ${new Date().toISOString()}\n\n${errorMsg}\n\nRefresh your Xiaomi cookies in Supabase secrets.`,
    });
  } catch {
    // Don't fail the whole function if ntfy is down
  }
}

// --- Main handler ---

Deno.serve(async (_req) => {
  try {
    const supabaseUrl = Deno.env.get("SUPABASE_URL")!;
    const supabaseKey = Deno.env.get("SUPABASE_SERVICE_ROLE_KEY")!;
    const cookies = Deno.env.get("XIAOMI_COOKIES")!;

    if (!cookies) {
      return new Response(JSON.stringify({ error: "XIAOMI_COOKIES not set" }), { status: 500 });
    }

    // Fetch all three Xiaomi endpoints in parallel
    const [balanceData, planDetail, planUsage] = await Promise.all([
      xiaomiFetch<Record<string, unknown>>("/balance", cookies),
      xiaomiFetch<Record<string, unknown>>("/tokenPlan/detail", cookies),
      xiaomiFetch<Record<string, unknown>>("/tokenPlan/usage", cookies),
    ]);

    const anySuccess = balanceData || planDetail || planUsage;
    const errorMsg = anySuccess ? null : "All Xiaomi API calls failed (token expired?)";

    if (errorMsg) {
      await maybeSendFailureAlert(supabaseUrl, supabaseKey, errorMsg);
    }

    // Extract values
    const balance = balanceData ? parseFloat(balanceData.balance as string) : null;
    const currency = (balanceData?.currency as string) ?? "USD";
    const giftBalance = balanceData ? parseFloat(balanceData.giftBalance as string) : null;
    const cashBalance = balanceData ? parseFloat(balanceData.cashBalance as string) : null;

    const planCode = (planDetail?.planCode as string) ?? null;
    const planName = (planDetail?.planName as string) ?? null;
    const planExpiresAt = planDetail?.currentPeriodEnd
      ? new Date(planDetail.currentPeriodEnd as string).toISOString()
      : null;
    const autoRenew = (planDetail?.enableAutoRenew as boolean) ?? false;

    const usageItems = planUsage?.usage?.items as Array<{ name: string; used: number; limit: number; percent: number }> | undefined;
    const usageItem = usageItems?.find(i => i.name === "plan_total_token");
    const creditsUsed = usageItem?.used ?? null;
    const creditsLimit = usageItem?.limit ?? null;
    const creditsPercent = (creditsUsed !== null && creditsLimit && creditsLimit > 0)
      ? creditsUsed / creditsLimit
      : (usageItem?.percent ?? null);

    // Compute burn rate from recent scrapes
    let burnRatePerHour: number | null = null;
    let estimatedHoursRemaining: number | null = null;
    let burnType: string | null = null;

    // Insert scrape record
    await supabaseInsert(supabaseUrl, supabaseKey, "mimo_scrapes", {
      balance, currency, gift_balance: giftBalance, cash_balance: cashBalance,
      plan_code: planCode, plan_name: planName, plan_expires_at: planExpiresAt,
      auto_renew: autoRenew, credits_used: creditsUsed, credits_limit: creditsLimit,
      credits_percent: creditsPercent, burn_type: burnType, error: errorMsg,
    });

    if (creditsUsed !== null || balance !== null) {
      const windowStart = new Date(Date.now() - BURN_RATE_WINDOW_HOURS * 3600 * 1000).toISOString();
      const recentScrapes = await supabaseSelect(
        supabaseUrl, supabaseKey, "mimo_scrapes",
        `select=scraped_at,credits_used,balance&scraped_at=gte.${windowStart}&credits_used=not.is.null&order=scraped_at.asc`
      );

      if (recentScrapes && recentScrapes.length >= 2) {
        const first = recentScrapes[0];
        const last = recentScrapes[recentScrapes.length - 1];
        const timeDeltaMs = new Date(last.scraped_at).getTime() - new Date(first.scraped_at).getTime();
        const timeDeltaHours = timeDeltaMs / 3600000;

        if (timeDeltaHours > 0) {
          const creditDelta = ((last.credits_used ?? 0) as number) - ((first.credits_used ?? 0) as number);
          const balanceDelta = ((first.balance ?? 0) as number) - ((last.balance ?? 0) as number);

          if (balanceDelta > 0.001) {
            burnRatePerHour = balanceDelta / timeDeltaHours;
            burnType = "balance";
          } else if (creditDelta > 0) {
            burnRatePerHour = creditDelta / timeDeltaHours;
            burnType = "credits";
            const remaining = (creditsLimit ?? 0) - creditsUsed;
            estimatedHoursRemaining = burnRatePerHour > 0 ? remaining / burnRatePerHour : null;
          } else {
            burnRatePerHour = 0;
            estimatedHoursRemaining = null;
          }
        }
      }
    }

    // Get scrape count
    const countResp = await fetch(
      `${supabaseUrl}/rest/v1/mimo_scrapes?select=id`, {
        headers: { ...supabaseHeaders(supabaseKey), "Prefer": "count=exact" },
      }
    );
    const scrapeCountHeader = countResp.headers.get("content-range");
    const scrapeCount = scrapeCountHeader ? parseInt(scrapeCountHeader.split("/")[1]) || 0 : 0;

    // Upsert current status
    const now = new Date().toISOString();
    if (errorMsg) {
      // API failed — only update error + timestamp, preserve last known good data
      await supabaseUpsert(supabaseUrl, supabaseKey, "mimo_current", {
        id: 1, last_scraped_at: now, error: errorMsg,
      }, "id");
    } else {
      // Success — write all fields, clear error
      await supabaseUpsert(supabaseUrl, supabaseKey, "mimo_current", {
        id: 1, last_scraped_at: now, error: null,
        balance, currency, gift_balance: giftBalance, cash_balance: cashBalance,
        plan_code: planCode, plan_name: planName, plan_expires_at: planExpiresAt,
        auto_renew: autoRenew, credits_used: creditsUsed, credits_limit: creditsLimit,
        credits_percent: creditsPercent, burn_rate_per_hour: burnRatePerHour,
        estimated_hours_remaining: estimatedHoursRemaining, burn_type: burnType,
        scrape_count: scrapeCount, updated_at: now,
      }, "id");
    }

    return new Response(JSON.stringify({
      ok: true,
      balance, planCode, creditsUsed, creditsLimit, creditsPercent,
      burnRatePerHour, burnType, estimatedHoursRemaining, scrapeCount,
      error: errorMsg,
    }), {
      headers: { "Content-Type": "application/json" },
      status: 200,
    });
  } catch (err) {
    try {
      const supabaseUrl = Deno.env.get("SUPABASE_URL")!;
      const supabaseKey = Deno.env.get("SUPABASE_SERVICE_ROLE_KEY")!;
      await maybeSendFailureAlert(supabaseUrl, supabaseKey, `Uncaught error: ${String(err)}`);
    } catch { /* ignore */ }
    return new Response(JSON.stringify({ error: String(err) }), { status: 500 });
  }
});
