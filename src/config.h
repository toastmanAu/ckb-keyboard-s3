#pragma once

// ── WiFi ─────────────────────────────────────────────────────────
#define WIFI_SSID     "YOUR_SSID"
#define WIFI_PASS     "YOUR_PASSWORD"

// ── CKB Node RPC ─────────────────────────────────────────────────
// Point at your local node — ckbnode is 192.168.68.87
#define CKB_RPC_HOST  "192.168.68.87"
#define CKB_RPC_PORT  8114

// ── Update interval ──────────────────────────────────────────────
#define UPDATE_MS     6000   // poll every 6s (one block ~6s)
