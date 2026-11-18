# ckb-keyboard-s3

A **Nervos CKB** node status macro pad for the [LilyGo T-Keyboard S3](https://github.com/Xinyuan-LilyGO/T-Keyboard-S3). Each of the four keycap displays shows one live metric from your own CKB node:

```
 USB-C at the bottom
 ┌─────────┬─────────┐
 │  BLOCK  │  EPOCH  │
 ├─────────┼─────────┤
 │  PEERS  │  HASH   │
 └─────────┴─────────┘
```

Press any key to cycle the colour themes.

## How it works

Every 6 seconds (about one CKB block) the board makes JSON-RPC calls over HTTP to your node:

| Key | RPC | Shown as |
|---|---|---|
| BLOCK | `get_tip_block_number` | Tip height, compacted (e.g. `18.74M`) |
| EPOCH | `get_current_epoch` | Current epoch number |
| PEERS | `get_peers` | Connected peer count |
| HASH | `get_blockchain_info` | Rough network hashrate estimate (see known issue) |

The four displays share one SPI bus. The firmware selects each one with its own chip-select pin and redraws it in turn.

## Build and flash

Requires [PlatformIO](https://platformio.org/).

1. Edit `src/config.h`:

   ```cpp
   #define WIFI_SSID     "YOUR_SSID"
   #define WIFI_PASS     "YOUR_PASSWORD"
   #define CKB_RPC_HOST  "192.168.x.x"   // your node's LAN address
   #define CKB_RPC_PORT  8114
   #define UPDATE_MS     6000
   ```

2. Make sure your node's RPC accepts LAN requests: set `listen_address = "0.0.0.0:8114"` under `[rpc]` in `ckb.toml`. Only do this on a trusted network.

3. Flash:

   ```bash
   pio run -e ckb-keyboard -t upload
   pio device monitor        # 115200 baud
   ```

Dependencies (installed by PlatformIO): `GFX Library for Arduino` ≥ 1.4.7 and `ArduinoJson` 7.

## Hardware notes

Pin map for T-Keyboard S3 V1, from `src/main.cpp`:

| Function | GPIO |
|---|---|
| Display SCK / MOSI / DC / RST / BL | 47 / 48 / 45 / 38 / 39 |
| Display CS, keys 1–4 | 12 / 13 / 14 / 21 |
| Keys 1–4 | 10 / 9 / 46 / 3 |
| WS2812 LEDs (14) | 11 |

Key 4 is on GPIO 3, which is also a boot strap pin. It is read only after `setup()` and has no interrupt attached.

## Known issue

The HASH key currently always shows `0MH`. The code reads `result.epoch.compact_target` from `get_blockchain_info`. In that response `epoch` is a plain hex string, so the field does not exist. The response does include a `difficulty` field, and hashrate ≈ `difficulty / average block time`. A fix would read `difficulty`, or take `compact_target` from the tip header (`get_tip_header`).

## Status

Early, single-commit project. BLOCK, EPOCH and PEERS are working; HASH is not (see above).
