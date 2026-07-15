#!/usr/bin/env node
// refresh-cookies.js — Extract Xiaomi cookies from Chrome & push to Supabase
//
// Usage: SUPABASE_PAT=sbp_xxx node refresh-cookies.js
//
// What it does:
//   1. Finds & refreshes the platform.xiaomimimo.com tab in Chrome
//   2. Waits for the page to load and JS cookies to settle
//   3. Reads & decrypts cookies from Chrome's local cookie DB
//   4. Pushes them to Supabase Management API as a secret
//
// Requirements:
//   - Google Chrome (macOS only — uses AppleScript)
//   - Node.js
//   - SUPABASE_PAT — Supabase Personal Access Token
//     Get one at: https://supabase.com/dashboard/account/tokens

const chrome = require("chrome-cookies-secure");
const path = require("path");
const fs = require("fs");

// Load .env if present
const envPath = path.join(__dirname, ".env");
if (fs.existsSync(envPath)) {
  for (const line of fs.readFileSync(envPath, "utf8").split("\n")) {
    const match = line.match(/^([^#=]+)=(.*)$/);
    if (match && !process.env[match[1].trim()]) {
      process.env[match[1].trim()] = match[2].trim();
    }
  }
}

// --- Config ---
const SUPABASE_PAT = process.env.SUPABASE_PAT;
const SUPABASE_PROJECT_REF = process.env.SUPABASE_PROJECT_REF;
const COOKIE_URL = "https://platform.xiaomimimo.com";
const REQUIRED_COOKIES = [
  "cookie-preferences",
  "api-platform_serviceToken",
  "userId",
  "api-platform_slh",
  "api-platform_ph",
];

const { execSync } = require("child_process");

if (!SUPABASE_PAT) {
  console.error("❌ SUPABASE_PAT env var is required.");
  console.error("   Get one at: https://supabase.com/dashboard/account/tokens");
  console.error("   Usage: SUPABASE_PAT=sbp_xxx node refresh-cookies.js");
  process.exit(1);
}

if (!SUPABASE_PROJECT_REF) {
  console.error("❌ SUPABASE_PROJECT_REF env var is required.");
  console.error("   Find it in your Supabase project URL: https://supabase.com/dashboard/project/YOUR_REF");
  console.error("   Usage: SUPABASE_PROJECT_REF=abcdef node refresh-cookies.js");
  process.exit(1);
}

function refreshChromeTab() {
  const appleScript = `
    tell application "Google Chrome"
      set found to false
      repeat with w in windows
        set tabIndex to 0
        repeat with t in tabs of w
          set tabIndex to tabIndex + 1
          if URL of t contains "platform.xiaomimimo.com" then
            set active tab index of w to tabIndex
            set index of w to 1
            reload t
            set found to true
            exit repeat
          end if
        end repeat
        if found then exit repeat
      end repeat
      if not found then
        open location "${COOKIE_URL}"
      end if
      activate
    end tell
  `;
  try {
    execSync(`osascript -e '${appleScript.replace(/'/g, "'\\''")}'`, {
      stdio: "pipe",
    });
    return true;
  } catch {
    return false;
  }
}

function waitForPageLoad(maxWaitSec = 30) {
  const checkScript = `
    tell application "Google Chrome"
      repeat with w in windows
        repeat with t in tabs of w
          if URL of t contains "platform.xiaomimimo.com" then
            return loading of t
          end if
        end repeat
      end repeat
      return false
    end tell
  `;
  return new Promise((resolve) => {
    const start = Date.now();
    const poll = () => {
      try {
        const result = execSync(`osascript -e '${checkScript.replace(/'/g, "'\\''")}'`, {
          stdio: "pipe",
        }).toString().trim();
        if (result === "false" || Date.now() - start > maxWaitSec * 1000) {
          resolve();
          return;
        }
      } catch {
        resolve();
        return;
      }
      setTimeout(poll, 1000);
    };
    poll();
  });
}

// Strip surrounding quotes from cookie values if present
function stripQuotes(val) {
  if (typeof val === "string" && val.startsWith('"') && val.endsWith('"')) {
    return val.slice(1, -1);
  }
  return val;
}

async function main() {
  console.log("🍪 Chrome Cookie → Supabase refresher\n");

  // 1. Refresh the platform tab in Chrome to get fresh cookies
  console.log("🌐 Refreshing platform tab in Chrome...");
  const refreshed = refreshChromeTab();
  if (refreshed) {
    console.log("   Tab refreshed, waiting for page to fully load...");
    await waitForPageLoad(30);
    console.log("   Page loaded, waiting 20s for JS cookies to settle...");
    await new Promise((r) => setTimeout(r, 20000));
  } else {
    console.log("   ⚠️  Could not refresh Chrome tab (not running?), proceeding with cached cookies...");
  }

  // 2. Read & decrypt cookies from Chrome DB
  console.log("🔍 Reading cookies from Chrome...");
  const cookies = await chrome.getCookiesPromised(COOKIE_URL, "object");

  // 3. Check required cookies
  const missing = REQUIRED_COOKIES.filter((c) => !cookies[c]);
  if (missing.length > 0) {
    console.error(`❌ Missing required cookies: ${missing.join(", ")}`);
    console.error(`   Found: ${Object.keys(cookies).join(", ")}`);
    console.error(`   Make sure you're logged in to ${COOKIE_URL} in Chrome.`);
    process.exit(1);
  }

  const cookieString = REQUIRED_COOKIES.map(
    (c) => `${c}=${stripQuotes(cookies[c])}`
  ).join("; ");
  console.log(`✅ Found ${REQUIRED_COOKIES.length} cookies`);

  // 4. Push to Supabase Management API
  console.log("🚀 Pushing to Supabase...");
  const resp = await fetch(
    `https://api.supabase.com/v1/projects/${SUPABASE_PROJECT_REF}/secrets`,
    {
      method: "POST",
      headers: {
        Authorization: `Bearer ${SUPABASE_PAT}`,
        "Content-Type": "application/json",
      },
      body: JSON.stringify([{ name: "XIAOMI_COOKIES", value: cookieString }]),
    }
  );

  if (!resp.ok) {
    const body = await resp.text();
    console.error(`❌ Supabase API error (${resp.status}): ${body}`);
    process.exit(1);
  }

  console.log("✅ XIAOMI_COOKIES updated in Supabase secrets");
  console.log("🎉 Done! Next scrape will use the fresh cookies.");
}

main().catch((err) => {
  console.error(`❌ ${err.message}`);
  process.exit(1);
});
