/*
 * ckb-keyboard-s3 — CKB Node Status Macro Pad
 * ============================================
 * Displays live Nervos CKB node stats on a LilyGo T-Keyboard S3.
 * Each of the 4 keycap displays shows one metric, live-updating
 * via JSON-RPC to your local CKB node.
 *
 * KEY LAYOUT (USB-C at bottom):
 *   [KEY1: BLOCK]  [KEY2: EPOCH]
 *   [KEY3: PEERS]  [KEY4: HASH ]
 *
 * Press any key to cycle colour themes.
 *
 * Board: LilyGo T-Keyboard S3 (ESP32-S3R8, 16MB, 8MB PSRAM)
 * Flash:  pio run -e ckb-keyboard -t upload
 */

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Arduino_GFX_Library.h>

#include "config.h"

// ── Pin definitions (T-Keyboard S3 V1) ───────────────────────────
#define DISP_SCK   47
#define DISP_MOSI  48
#define DISP_DC    45
#define DISP_RST   38
#define DISP_BL    39

#define CS1  12   // KEY1 display
#define CS2  13   // KEY2 display
#define CS3  14   // KEY3 display
#define CS4  21   // KEY4 display

#define KEY1_PIN  10
#define KEY2_PIN   9
#define KEY3_PIN  46
#define KEY4_PIN   3   // also BOOT0 — only read after setup()

#define WS2812_PIN 11
#define NUM_LEDS   14

// ── Display setup ────────────────────────────────────────────────
Arduino_DataBus *bus;
Arduino_GFX    *gfx;

const int8_t CS_PINS[4] = { CS1, CS2, CS3, CS4 };

void selectDisplay(int idx) {
    for (int i = 0; i < 4; i++)
        digitalWrite(CS_PINS[i], (i == idx) ? LOW : HIGH);
}

void selectAll() {
    for (int i = 0; i < 4; i++) digitalWrite(CS_PINS[i], LOW);
}

void deselectAll() {
    for (int i = 0; i < 4; i++) digitalWrite(CS_PINS[i], HIGH);
}

// ── Colours ──────────────────────────────────────────────────────
#define BLACK   0x0000
#define WHITE   0xFFFF
#define CYAN    0x07FF
#define GREEN   0x07E0
#define ORANGE  0xFD20
#define MAGENTA 0xF81F
#define GRAY    0x8410
#define DKGRAY  0x2104

struct Theme { uint16_t bg; uint16_t label; uint16_t colors[4]; };

Theme themes[] = {
    // bg      label   block    epoch    peers    hash
    { DKGRAY, GRAY,  { CYAN,   MAGENTA, GREEN,  ORANGE } },  // dark
    { 0x000F, GRAY,  { 0x07FF, 0xF81F, 0x07E0, 0xFD20 } },  // midnight
    { 0x0010, GRAY,  { 0xFFFF, 0xFFE0, 0x07E0, 0xFD20 } },  // mono
};
int themeIdx = 0;
#define NUM_THEMES (sizeof(themes)/sizeof(themes[0]))

// ── Node data ─────────────────────────────────────────────────────
struct NodeData {
    uint64_t blockNumber;
    uint64_t epoch;
    int      peers;
    float    hashrateMhs;   // estimated from difficulty (rough)
    bool     valid;
};

NodeData nodeData = {};
uint32_t lastUpdate = 0;
bool     wifiOk = false;

// ── RPC helpers ──────────────────────────────────────────────────
String rpcCall(const char *method, const char *params = "[]") {
    HTTPClient http;
    http.begin("http://" CKB_RPC_HOST ":" + String(CKB_RPC_PORT));
    http.addHeader("Content-Type", "application/json");
    String body = "{\"id\":1,\"jsonrpc\":\"2.0\",\"method\":\"";
    body += method;
    body += "\",\"params\":";
    body += params;
    body += "}";
    int code = http.POST(body);
    String resp = (code == 200) ? http.getString() : "";
    http.end();
    return resp;
}

uint64_t hexToU64(const char *hex) {
    if (!hex || hex[0] == '\0') return 0;
    const char *s = (hex[0] == '0' && (hex[1] == 'x' || hex[1] == 'X')) ? hex + 2 : hex;
    return strtoull(s, nullptr, 16);
}

void fetchNodeData() {
    NodeData d = {};

    // get_tip_block_number
    String r = rpcCall("get_tip_block_number");
    if (r.length() > 0) {
        JsonDocument doc;
        if (!deserializeJson(doc, r)) {
            const char *bn = doc["result"];
            if (bn) d.blockNumber = hexToU64(bn);
        }
    }

    // get_current_epoch
    r = rpcCall("get_current_epoch");
    if (r.length() > 0) {
        JsonDocument doc;
        if (!deserializeJson(doc, r)) {
            const char *ep = doc["result"]["number"];
            if (ep) d.epoch = hexToU64(ep);
        }
    }

    // get_peers
    r = rpcCall("get_peers");
    if (r.length() > 0) {
        JsonDocument doc;
        if (!deserializeJson(doc, r)) {
            d.peers = doc["result"].as<JsonArray>().size();
        }
    }

    // get_blockchain_info — compact_target → rough hashrate estimate
    r = rpcCall("get_blockchain_info");
    if (r.length() > 0) {
        JsonDocument doc;
        if (!deserializeJson(doc, r)) {
            const char *ct = doc["result"]["epoch"]["compact_target"];
            if (ct) {
                // Very rough: target = max/difficulty, H/s ~ difficulty * 2^32 / blocktime
                // Just show difficulty as a relative number for now
                uint32_t target = (uint32_t)hexToU64(ct);
                if (target > 0) {
                    // compact_target → difficulty approximation
                    uint32_t exp = (target >> 24) & 0xFF;
                    uint32_t mant = target & 0x00FFFFFF;
                    float diff = (float)0x1000000 / (float)mant;
                    for (int i = 0; i < (int)exp - 3; i++) diff *= 256.0f;
                    d.hashrateMhs = diff * 4294967296.0f / 6.0f / 1e9f; // GH/s approx
                }
            }
        }
    }

    d.valid = (d.blockNumber > 0);
    nodeData = d;
}

// ── Display drawing ───────────────────────────────────────────────
void drawMetric(int idx, const char *label, const char *value, uint16_t valCol) {
    Theme &t = themes[themeIdx];
    selectDisplay(idx);

    gfx->fillScreen(t.bg);

    // Label — small, top quarter
    gfx->setTextSize(1);
    gfx->setTextColor(t.label);
    int lw = strlen(label) * 6;
    gfx->setCursor((128 - lw) / 2, 24);
    gfx->print(label);

    // Divider line
    gfx->drawFastHLine(14, 42, 100, t.label);

    // Value — auto-size to fit
    uint8_t vSize = 3;
    int vw = strlen(value) * 6 * vSize;
    if (vw > 120) { vSize = 2; vw = strlen(value) * 12; }
    if (vw > 120) { vSize = 1; vw = strlen(value) * 6; }
    gfx->setTextSize(vSize);
    gfx->setTextColor(valCol);
    gfx->setCursor((128 - vw) / 2, 60 + (vSize == 3 ? 4 : vSize == 2 ? 10 : 18));
    gfx->print(value);

    deselectAll();
}

void drawAll() {
    Theme &t = themes[themeIdx];

    if (!wifiOk) {
        // WiFi connecting splash
        for (int i = 0; i < 4; i++) {
            selectDisplay(i);
            gfx->fillScreen(BLACK);
            gfx->setTextColor(GRAY);
            gfx->setTextSize(1);
            gfx->setCursor(20, 56);
            gfx->print("connecting...");
            deselectAll();
        }
        return;
    }

    if (!nodeData.valid) {
        for (int i = 0; i < 4; i++) {
            selectDisplay(i);
            gfx->fillScreen(BLACK);
            gfx->setTextColor(0xF800);
            gfx->setTextSize(1);
            gfx->setCursor(30, 56);
            gfx->print("no data");
            deselectAll();
        }
        return;
    }

    char buf[16];

    // KEY1: Block height — show as e.g. "18.74M"
    if (nodeData.blockNumber >= 1000000)
        snprintf(buf, sizeof(buf), "%.2fM", nodeData.blockNumber / 1000000.0f);
    else
        snprintf(buf, sizeof(buf), "%llu", (unsigned long long)nodeData.blockNumber);
    drawMetric(0, "BLOCK", buf, t.colors[0]);

    // KEY2: Epoch
    snprintf(buf, sizeof(buf), "%llu", (unsigned long long)nodeData.epoch);
    drawMetric(1, "EPOCH", buf, t.colors[1]);

    // KEY3: Peers
    snprintf(buf, sizeof(buf), "%d", nodeData.peers);
    drawMetric(2, "PEERS", buf, t.colors[2]);

    // KEY4: Estimated hashrate
    if (nodeData.hashrateMhs >= 1.0f)
        snprintf(buf, sizeof(buf), "%.1fGH", nodeData.hashrateMhs);
    else
        snprintf(buf, sizeof(buf), "%.0fMH", nodeData.hashrateMhs * 1000.0f);
    drawMetric(3, "HASH", buf, t.colors[3]);
}

// ── Key handling ─────────────────────────────────────────────────
volatile bool anyKeyPressed = false;

void IRAM_ATTR onKey() { anyKeyPressed = true; }

// ── Setup ────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);

    // CS pins
    for (int i = 0; i < 4; i++) {
        pinMode(CS_PINS[i], OUTPUT);
        digitalWrite(CS_PINS[i], HIGH);
    }

    // Key pins
    pinMode(KEY1_PIN, INPUT_PULLUP);
    pinMode(KEY2_PIN, INPUT_PULLUP);
    pinMode(KEY3_PIN, INPUT_PULLUP);
    // KEY4 (GPIO3/BOOT) — read-only after boot, no interrupt to avoid issues
    attachInterrupt(digitalPinToInterrupt(KEY1_PIN), onKey, FALLING);
    attachInterrupt(digitalPinToInterrupt(KEY2_PIN), onKey, FALLING);
    attachInterrupt(digitalPinToInterrupt(KEY3_PIN), onKey, FALLING);

    // Display init
    bus = new Arduino_ESP32SPI(DISP_DC, GFX_NOT_DEFINED, DISP_SCK, DISP_MOSI, GFX_NOT_DEFINED);
    gfx = new Arduino_GC9107(bus, DISP_RST, 0, true, 128, 128, 2, 1);

    // Reset all displays simultaneously
    pinMode(DISP_RST, OUTPUT);
    selectAll();
    digitalWrite(DISP_RST, LOW); delay(10);
    digitalWrite(DISP_RST, HIGH); delay(120);
    gfx->begin();
    gfx->fillScreen(BLACK);
    deselectAll();

    // Backlight
    ledcSetup(1, 2000, 8);
    ledcAttachPin(DISP_BL, 1);
    ledcWrite(1, 220);

    // WiFi
    Serial.printf("Connecting to %s\n", WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    drawAll();  // show "connecting..."

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 40) {
        delay(500);
        Serial.print(".");
        attempts++;
    }

    if (WiFi.status() == WL_CONNECTED) {
        wifiOk = true;
        Serial.printf("\nConnected: %s\n", WiFi.localIP().toString().c_str());
    } else {
        Serial.println("\nWiFi failed — will retry in loop");
    }
}

// ── Loop ─────────────────────────────────────────────────────────
void loop() {
    // Key press → cycle theme
    if (anyKeyPressed) {
        anyKeyPressed = false;
        delay(50);  // debounce
        themeIdx = (themeIdx + 1) % NUM_THEMES;
        drawAll();
    }

    // Reconnect WiFi if dropped
    if (!wifiOk || WiFi.status() != WL_CONNECTED) {
        WiFi.reconnect();
        delay(2000);
        wifiOk = (WiFi.status() == WL_CONNECTED);
    }

    // Poll node on interval
    if (wifiOk && (millis() - lastUpdate > UPDATE_MS || lastUpdate == 0)) {
        lastUpdate = millis();
        fetchNodeData();
        drawAll();
        Serial.printf("Block: %llu  Epoch: %llu  Peers: %d\n",
            (unsigned long long)nodeData.blockNumber,
            (unsigned long long)nodeData.epoch,
            nodeData.peers);
    }
}
