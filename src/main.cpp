// WiFi status board: one colored bar per network in WIFI_NETS.
// Per network: scan -> connect -> IP -> tiny HTTP internet check -> public IP/ISP lookup.
// Green = working, yellow = degraded (or Naboo failed over to T-Mobile), red = failed.
// Polls every POLL_MS; touch the screen to run a cycle now.
#include <M5Unified.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiUdp.h>
#include "secrets.h"

static const uint32_t POLL_MS            = 2UL * 60 * 1000;
static const uint32_t CONNECT_TIMEOUT_MS = 15000;
static const char*    CHECK_URL          = "http://connectivitycheck.gstatic.com/generate_204";  // empty 204 reply
static const char*    ISP_URL            = "http://ip-api.com/line/?fields=query,isp";           // 2 lines: public IP, ISP
static const int      GOOD_RSSI          = -75;   // dBm; weaker than this is yellow
static const uint32_t GOOD_LATENCY_MS    = 1000;  // slower than this is yellow

// Indexes into WIFI_NETS for the failover logic (Naboo sits behind the pfSense dual-WAN router:
// Ghorman/Quantum Fiber is primary, T-Mobile is the backup).
static const int IDX_PRIMARY = 0;  // Ghorman
static const int IDX_PFSENSE = 1;  // Naboo
static const int IDX_BACKUP  = 2;  // T-Mobile

enum Verdict { V_UNKNOWN, V_GREEN, V_YELLOW, V_RED };

struct Result {
  Verdict  verdict   = V_UNKNOWN;
  String   stage;
  bool     found     = false;
  int      channel   = 0;
  int      rssi      = 0;
  String   bssid;
  String   ip;          // local IP
  String   publicIp;
  String   isp;
  int      httpCode  = 0;
  uint32_t latencyMs = 0;
};

static Result   results[WIFI_NET_COUNT];
static int      testing   = -1;   // index currently under test, -1 = none
static uint32_t lastCycle = 0;
static bool     everRan   = false;
static int      hbState   = -1;   // heartbeat: -1 not yet sent, 0 last send failed, 1 last send handed to the network

// ---------- drawing ----------

static uint16_t verdictColor(Verdict v) {
  switch (v) {
    case V_GREEN:  return TFT_GREEN;
    case V_YELLOW: return TFT_YELLOW;
    case V_RED:    return TFT_RED;
    default:       return TFT_DARKGREY;
  }
}

static const int ROW_H = 68, ROW_GAP = 4, ROW_Y0 = 4;

static void drawRow(int i) {
  auto& d = M5.Display;
  const Result& r = results[i];
  int y = ROW_Y0 + i * (ROW_H + ROW_GAP);
  uint16_t bg = (i == testing) ? TFT_CYAN : verdictColor(r.verdict);

  d.fillRoundRect(4, y, d.width() - 8, ROW_H, 8, bg);
  d.setTextColor(TFT_BLACK, bg);

  d.setTextSize(3);
  d.setCursor(12, y + 5);
  d.print(WIFI_NETS[i].label);

  const char* word = (i == testing) ? "..." : r.verdict == V_GREEN ? "OK" : r.verdict == V_YELLOW ? "WARN"
                     : r.verdict == V_RED ? "FAIL" : "";
  d.setTextSize(2);
  d.setCursor(d.width() - 12 - 12 * strlen(word), y + 9);
  d.print(word);

  String stage = (i == testing) ? String("Testing...") : r.stage;
  if (stage.length() > 25) stage = stage.substring(0, 25);
  d.setTextSize(2);
  d.setCursor(12, y + 31);
  d.print(stage);

  if (i != testing && r.found) {
    d.setTextSize(1);
    d.setCursor(12, y + 53);
    d.printf("ch%d %ddBm", r.channel, r.rssi);
    if (r.latencyMs) d.printf(" %lums", (unsigned long)r.latencyMs);
    if (r.publicIp.length()) d.printf("  %s", r.publicIp.c_str());
  }
}

static void drawFooter() {
  auto& d = M5.Display;
  int y = ROW_Y0 + WIFI_NET_COUNT * (ROW_H + ROW_GAP);
  d.fillRect(0, y, d.width(), d.height() - y, TFT_BLACK);
  d.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  d.setTextSize(1);
  d.setCursor(8, y + 4);
  if (testing >= 0) {
    d.print("Testing...");
  } else if (everRan) {
    uint32_t el = millis() - lastCycle;
    uint32_t left = el >= POLL_MS ? 0 : (POLL_MS - el) / 1000;
    d.printf("Next %lu:%02lu (touch=now) HB:%s", (unsigned long)(left / 60), (unsigned long)(left % 60),
             hbState < 0 ? "--" : hbState ? "ok" : "fail");
  }

  // Battery, right-aligned. Level is negative if the PMIC can't report it.
  int lvl = M5.Power.getBatteryLevel();
  bool chg = M5.Power.isCharging() == m5::Power_Class::is_charging;
  char buf[24];
  if (lvl < 0) snprintf(buf, sizeof(buf), "Batt ?");
  else         snprintf(buf, sizeof(buf), "Batt %d%%%s", lvl, chg ? " chg" : "");
  uint16_t col = lvl < 0 ? TFT_LIGHTGREY : chg ? TFT_CYAN : lvl <= 20 ? TFT_RED : lvl <= 40 ? TFT_YELLOW : TFT_GREEN;
  d.setTextColor(col, TFT_BLACK);
  d.setCursor(d.width() - 8 - 6 * strlen(buf), y + 4);
  d.print(buf);
}

static void drawAll() {
  M5.Display.fillScreen(TFT_BLACK);
  for (int i = 0; i < WIFI_NET_COUNT; i++) drawRow(i);
  drawFooter();
}

// ---------- test ----------

static void wifiOff() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
}

static void lookupIsp(Result& r) {
  HTTPClient http;
  http.setTimeout(5000);
  http.begin(ISP_URL);
  if (http.GET() == 200) {
    String body = http.getString();
    int nl = body.indexOf('\n');
    if (nl > 0) {
      // ip-api returns lines in its own fixed field order, not the order requested, so
      // tell them apart by content: the IP line has only digits, dots and colons.
      String a = body.substring(0, nl), b = body.substring(nl + 1);
      a.trim();
      b.trim();
      auto looksLikeIp = [](const String& s) {
        for (size_t i = 0; i < s.length(); i++) {
          char c = s[i];
          if (!(isxdigit(c) || c == '.' || c == ':')) return false;
        }
        return s.length() > 0;
      };
      if (looksLikeIp(a)) { r.publicIp = a; r.isp = b; }
      else                { r.publicIp = b; r.isp = a; }
    }
  }
  http.end();
}

// keepConnected: leave WiFi up on success so the caller can use the link (the heartbeat goes out on Ghorman).
static Result runTest(const WifiNet& net, bool keepConnected) {
  Result r;

  // 1. Scan: is the SSID visible to the Core2 at all?
  WiFi.mode(WIFI_STA);
  // A single ESP32 scan can miss an AP, so retry a few times before declaring it gone.
  int bestRssi = -1000;
  for (int attempt = 0; attempt < 3 && !r.found; attempt++) {
    int n = WiFi.scanNetworks();
    for (int i = 0; i < n; i++) {
      if (WiFi.SSID(i) == net.ssid && WiFi.RSSI(i) > bestRssi) {
        bestRssi  = WiFi.RSSI(i);
        r.found   = true;
        r.channel = WiFi.channel(i);
        r.rssi    = WiFi.RSSI(i);
      }
    }
    WiFi.scanDelete();
  }
  if (!r.found) {
    wifiOff();
    r.verdict = V_RED;
    r.stage   = "SSID not seen";
    return r;
  }

  // 2. Connect (strongest AP if the SSID has several)
  WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);
  WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);
  WiFi.begin(net.ssid, net.password);
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < CONNECT_TIMEOUT_MS) delay(250);
  if (WiFi.status() != WL_CONNECTED) {
    r.verdict = V_RED;
    r.stage   = WiFi.status() == WL_CONNECT_FAILED ? "Auth failed" : "Connect timed out";
    wifiOff();
    return r;
  }
  r.bssid = WiFi.BSSIDstr();
  r.rssi  = WiFi.RSSI();
  r.ip    = WiFi.localIP().toString();
  if (WiFi.localIP() == IPAddress(0, 0, 0, 0)) {
    r.verdict = V_RED;
    r.stage   = "Connected, no IP";
    wifiOff();
    return r;
  }

  // 3. Internet check: one tiny HTTP GET
  HTTPClient http;
  http.setTimeout(5000);
  http.begin(CHECK_URL);
  uint32_t t0 = millis();
  r.httpCode  = http.GET();
  r.latencyMs = millis() - t0;
  http.end();

  if (r.httpCode != 204) {
    if (r.httpCode < 0) r.httpCode = 0;
    r.verdict = V_YELLOW;
    r.stage   = r.httpCode ? "Link up, bad HTTP reply" : "Link up, no internet";
    if (!keepConnected) wifiOff();
    return r;
  }

  // 4. Which ISP is carrying this traffic?
  lookupIsp(r);
  if (!keepConnected) wifiOff();

  if (r.rssi < GOOD_RSSI || r.latencyMs > GOOD_LATENCY_MS) {
    r.verdict = V_YELLOW;
    r.stage   = r.rssi < GOOD_RSSI ? "Working, weak signal" : "Working, slow";
  } else {
    r.verdict = V_GREEN;
    r.stage   = r.isp.length() ? "Working: " + r.isp : String("Working");
  }
  return r;
}

// Naboo is behind the pfSense dual-WAN router, so compare its ISP with what the other two bars see.
static void applyFailoverCheck() {
  if (WIFI_NET_COUNT <= IDX_BACKUP) return;
  Result& n = results[IDX_PFSENSE];
  if (n.verdict != V_GREEN || n.isp.length() == 0) return;
  const Result& primary = results[IDX_PRIMARY];
  const Result& backup  = results[IDX_BACKUP];
  bool likePrimary = primary.isp.length() && n.isp == primary.isp;
  bool likeBackup  = backup.isp.length() && n.isp == backup.isp;
  if (likeBackup && !likePrimary) {
    n.verdict = V_YELLOW;
    n.stage   = "FAILOVER: " + n.isp;
  }
}

// Verdict as sent on the wire: 0 green, 1 yellow, 2 red, 3 unknown / not configured.
static int wireVerdict(int idx) {
  if (idx >= WIFI_NET_COUNT) return 3;
  switch (results[idx].verdict) {
    case V_GREEN:  return 0;
    case V_YELLOW: return 1;
    case V_RED:    return 2;
    default:       return 3;
  }
}

// One UDP datagram to the listener on the Debian TinyPC (osprey/listener.py). Only call while on Ghorman.
// UDP has no acknowledgement, so "true" means the packet was handed to the network, not that it arrived.
static bool sendHeartbeat() {
  char msg[96];
  snprintf(msg, sizeof(msg), "tok=%s up=%lu g=%d n=%d t=%d bat=%d", HEARTBEAT_TOKEN,
           (unsigned long)(millis() / 1000), wireVerdict(IDX_PRIMARY), wireVerdict(IDX_PFSENSE),
           wireVerdict(IDX_BACKUP), (int)M5.Power.getBatteryLevel());
  WiFiUDP udp;
  bool sent = false;
  for (int copy = 0; copy < 2; copy++) {  // two copies 250 ms apart; the listener just overwrites its state
    if (udp.beginPacket(HEARTBEAT_HOST, HEARTBEAT_PORT)) {
      udp.write(reinterpret_cast<const uint8_t*>(msg), strlen(msg));
      if (udp.endPacket() == 1) sent = true;
    }
    delay(250);
  }
  udp.stop();
  Serial.printf("[heartbeat] %s to %s:%d\n", sent ? "sent" : "FAILED", HEARTBEAT_HOST, HEARTBEAT_PORT);
  return sent;
}

static void runCycle() {
  // Everything else first, Ghorman last, so the Core2 is still on the Ghorman LAN when the heartbeat goes out.
  int order[WIFI_NET_COUNT], count = 0;
  for (int i = 0; i < WIFI_NET_COUNT; i++) if (i != IDX_PRIMARY) order[count++] = i;
  if (IDX_PRIMARY < WIFI_NET_COUNT) order[count++] = IDX_PRIMARY;

  for (int k = 0; k < count; k++) {
    int i = order[k];
    testing = i;
    drawRow(i);
    drawFooter();
    results[i] = runTest(WIFI_NETS[i], i == IDX_PRIMARY);
    Serial.printf("[%s] %s: %s  ch%d rssi%d bssid=%s ip=%s http=%d %lums public=%s isp=%s\n",
                  WIFI_NETS[i].label,
                  results[i].verdict == V_GREEN ? "GREEN" : results[i].verdict == V_YELLOW ? "YELLOW" : "RED",
                  results[i].stage.c_str(), results[i].channel, results[i].rssi, results[i].bssid.c_str(),
                  results[i].ip.c_str(), results[i].httpCode, (unsigned long)results[i].latencyMs,
                  results[i].publicIp.c_str(), results[i].isp.c_str());
    testing = -1;
    drawRow(i);
  }
  applyFailoverCheck();
  // If Ghorman is down there is no link to send on, and the silence is what the Zabbix alert sees.
  hbState = (WiFi.status() == WL_CONNECTED && sendHeartbeat()) ? 1 : 0;
  wifiOff();
  lastCycle = millis();
  everRan   = true;
  drawAll();
  Serial.printf("CYCLE DONE (naboo: %s) battery=%d%% %dmV charging=%d hb=%d\n", results[IDX_PFSENSE].stage.c_str(),
                (int)M5.Power.getBatteryLevel(), (int)M5.Power.getBatteryVoltage(),
                (int)(M5.Power.isCharging() == m5::Power_Class::is_charging), hbState);
}

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  Serial.begin(115200);
  M5.Display.setBrightness(160);
  for (auto& r : results) r.stage = "Waiting...";
  drawAll();
  runCycle();
}

void loop() {
  M5.update();
  static uint32_t lastFooter = 0;
  bool touched = M5.Touch.getCount() > 0 && M5.Touch.getDetail(0).wasPressed();
  if (touched || millis() - lastCycle >= POLL_MS) {
    runCycle();
  } else if (millis() - lastFooter >= 1000) {
    lastFooter = millis();
    drawFooter();
  }
  delay(20);
}
