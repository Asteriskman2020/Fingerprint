/*
 * Demo_Fingerprint_V1 - ESP32-C6 Fingerprint Reader Web Dashboard
 * Sensor on UART: RX=GPIO20, TX=GPIO19, WAKE/IRQ=GPIO15
 * Adafruit Fingerprint Sensor Library
 * Colorful web dashboard, WiFi/MQTT config portal, OTA, RGB LED
 */

#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoOTA.h>
#include <PubSubClient.h>
#include <LittleFS.h>
#include <Adafruit_Fingerprint.h>

// ============== HARDWARE PINS ==============
#define FP_RX 20
#define FP_TX 19
#define FP_IRQ 15
#define LED_PIN 8   // ESP32-C6 Super-mini built-in LED (active LOW)
#define CONFIG_BTN 9 // BOOT button on ESP32-C6

// ============== AP CONFIG ==============
#define AP_SSID "FP-Setup"
#define AP_PASSWORD "12345678"

// ============== TIMING ==============
#define MQTT_INTERVAL 60000
#define REBOOT_INTERVAL 7200000UL
#define FP_SCAN_INTERVAL 500
#define LED_BLINK_OK 100
#define LED_BLINK_FAIL 50

// ============== CONFIG STORAGE ==============
char cfg_wifi_ssid[33] = "";
char cfg_wifi_pass[65] = "";
char cfg_mqtt_server[65] = "";
uint16_t cfg_mqtt_port = 1883;
char cfg_mqtt_user[33] = "";
char cfg_mqtt_pass[65] = "";
char cfg_mqtt_topic[65] = "home/fingerprint";

// ============== GLOBALS ==============
HardwareSerial fpSerial(1);  // UART1
Adafruit_Fingerprint finger = Adafruit_Fingerprint(&fpSerial);

WebServer server(80);
WiFiClient espClient;
PubSubClient mqtt(espClient);

bool configMode = false;
bool wifiConnected = false;
bool mqttConnected = false;
bool sensorFound = false;
unsigned long bootTime = 0;
unsigned long lastMqtt = 0;
unsigned long lastScan = 0;

// Fingerprint state
int lastMatchId = -1;
int lastConfidence = 0;
int lastStatus = -1;            // 0=no finger, 1=matched, 2=not found, 3=error
int templateCount = 0;
int totalScans = 0;
int totalMatches = 0;
int totalRejects = 0;
unsigned long lastMatchTime = 0;
unsigned long lastEventTime = 0;
char lastEventStr[32] = "Waiting...";

// Shared format buffer
char buf[256];

// ============== PROGMEM HTML: DASHBOARD ==============
static const char DASHBOARD_HTML[] PROGMEM = R"rawliteral(<!DOCTYPE html><html><head>
<meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'>
<meta http-equiv='refresh' content='3'>
<title>Fingerprint Dashboard</title><style>
*{margin:0;padding:0;box-sizing:border-box}
body{background:linear-gradient(135deg,#0c0118 0%%,#1a0533 40%%,#0d1b2a 100%%);color:#e2e8f0;font-family:'Segoe UI',system-ui,sans-serif;min-height:100vh}
@keyframes pulse{0%%,100%%{opacity:1}50%%{opacity:.5}}
@keyframes glow{0%%,100%%{box-shadow:0 0 5px var(--gc)}50%%{box-shadow:0 0 20px var(--gc),0 0 40px var(--gc)}}
@keyframes spin{0%%{transform:rotate(0deg)}100%%{transform:rotate(360deg)}}
.hdr{background:linear-gradient(135deg,#2d1b69,#1e3a5f,#0d7377);padding:18px 24px;display:flex;align-items:center;justify-content:space-between;border-bottom:2px solid #7c3aed}
.hdr h1{font-size:1.4rem;background:linear-gradient(90deg,#c084fc,#38bdf8,#22d3ee);-webkit-background-clip:text;-webkit-text-fill-color:transparent;font-weight:800}
.dots{display:flex;gap:12px;align-items:center}
.dot{width:12px;height:12px;border-radius:50%%}
.sg{background:#22c55e;box-shadow:0 0 8px #22c55e,0 0 16px #22c55e50;animation:pulse 2s infinite}
.sr{background:#ef4444;box-shadow:0 0 8px #ef4444,0 0 16px #ef444450;animation:pulse 1s infinite}
.dl{font-size:.6rem;color:#94a3b8;text-align:center;margin-top:2px}
.main{max-width:840px;margin:0 auto;padding:20px}
.status{background:linear-gradient(135deg,#1e0a3c,#0f172a,#0a1628);border:2px solid;border-image:linear-gradient(135deg,#7c3aed,#06b6d4,#22c55e) 1;border-radius:0;padding:28px;text-align:center;margin-bottom:20px;position:relative;overflow:hidden}
.status::before{content:'';position:absolute;top:-50%%;left:-50%%;width:200%%;height:200%%;background:conic-gradient(from 0deg,transparent,rgba(124,58,237,.1),transparent,rgba(6,182,212,.1),transparent);animation:spin 8s linear infinite}
.status>*{position:relative;z-index:1}
.status .lbl{font-size:.8rem;color:#c084fc;text-transform:uppercase;letter-spacing:3px;margin-bottom:10px}
.status .ico{font-size:4rem;margin:10px 0;filter:drop-shadow(0 0 12px rgba(124,58,237,.6))}
.status .val{font-size:2rem;font-weight:800;letter-spacing:2px}
.status .sub{font-size:.8rem;color:#94a3b8;margin-top:12px}
.matched{color:#22c55e;text-shadow:0 0 20px #22c55e80}
.rejected{color:#ef4444;text-shadow:0 0 20px #ef444480}
.waiting{color:#c084fc;text-shadow:0 0 20px #c084fc60}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(170px,1fr));gap:14px;margin-bottom:20px}
.card{background:linear-gradient(145deg,#1a1035,#0f172a);border:1px solid #334155;border-radius:14px;padding:18px;text-align:center;transition:all .3s;position:relative;overflow:hidden}
.card:hover{transform:translateY(-2px);border-color:#7c3aed}
.card::after{content:'';position:absolute;top:0;left:0;right:0;height:3px}
.card:nth-child(1)::after{background:linear-gradient(90deg,#8b5cf6,#a78bfa)}
.card:nth-child(2)::after{background:linear-gradient(90deg,#06b6d4,#22d3ee)}
.card:nth-child(3)::after{background:linear-gradient(90deg,#f59e0b,#fbbf24)}
.card:nth-child(4)::after{background:linear-gradient(90deg,#3b82f6,#60a5fa)}
.card:nth-child(5)::after{background:linear-gradient(90deg,#22c55e,#4ade80)}
.card:nth-child(6)::after{background:linear-gradient(90deg,#ef4444,#f87171)}
.card .cl{font-size:.7rem;color:#94a3b8;text-transform:uppercase;letter-spacing:1px;margin-bottom:8px}
.card .cv{font-size:1.5rem;font-weight:700;color:#f8fafc}
.card .cu{font-size:.65rem;color:#64748b;margin-top:5px}
.hi{color:#4ade80;text-shadow:0 0 10px #22c55e60}
.lo{color:#f87171;text-shadow:0 0 10px #ef444460}
.med{color:#fbbf24;text-shadow:0 0 10px #f59e0b60}
.sys .card::after{background:linear-gradient(90deg,#6366f1,#818cf8)}
.acts{text-align:center;margin:20px 0}
.enroll-btn{display:inline-block;padding:12px 32px;background:linear-gradient(135deg,#7c3aed,#6d28d9);color:#fff;border-radius:10px;text-decoration:none;font-weight:700;font-size:.95rem;box-shadow:0 4px 15px #7c3aed50;transition:all .3s}
.enroll-btn:hover{transform:translateY(-2px);box-shadow:0 6px 25px #7c3aed70}
.del-btn{display:inline-block;padding:12px 32px;background:linear-gradient(135deg,#dc2626,#b91c1c);color:#fff;border-radius:10px;text-decoration:none;font-weight:700;font-size:.95rem;margin-left:10px;box-shadow:0 4px 15px #dc262650;transition:all .3s}
.del-btn:hover{transform:translateY(-2px);box-shadow:0 6px 25px #dc262670}
.ftr{text-align:center;padding:20px;font-size:.75rem}
.ftr a{color:#c084fc;text-decoration:none;margin:0 8px;transition:color .3s}
.ftr a:hover{color:#e9d5ff}
.ftr span{color:#475569}
</style></head><body>
<div class='hdr'><h1>Fingerprint Dashboard</h1>
<div class='dots'>
<div><div class='dot %WDOT%'></div><div class='dl'>WiFi</div></div>
<div><div class='dot %MDOT%'></div><div class='dl'>MQTT</div></div>
<div><div class='dot %SDOT%'></div><div class='dl'>Sensor</div></div>
</div></div>
<div class='main'>
<div class='status'>
<div class='lbl'>Scanner Status</div>
<div class='ico'>%ICON%</div>
<div class='val %SCLS%'>%STXT%</div>
<div class='sub'>%EVENT% &middot; %AGO%</div>
</div>
<div class='grid'>
<div class='card'><div class='cl'>Match ID</div><div class='cv %MIDCLS%'>%MID%</div><div class='cu'>fingerprint</div></div>
<div class='card'><div class='cl'>Confidence</div><div class='cv %CCLS%'>%CONF%</div><div class='cu'>score</div></div>
<div class='card'><div class='cl'>Templates</div><div class='cv'>%TCNT%</div><div class='cu'>stored</div></div>
<div class='card'><div class='cl'>Total Scans</div><div class='cv'>%SCANS%</div><div class='cu'>attempts</div></div>
<div class='card'><div class='cl'>Matches</div><div class='cv hi'>%MATCHES%</div><div class='cu'>accepted</div></div>
<div class='card'><div class='cl'>Rejects</div><div class='cv lo'>%REJECTS%</div><div class='cu'>denied</div></div>
</div>
<div class='grid sys'>
<div class='card'><div class='cl'>RSSI</div><div class='cv'>%RSSI%</div><div class='cu'>dBm</div></div>
<div class='card'><div class='cl'>Free Heap</div><div class='cv'>%HEAP%</div><div class='cu'>bytes</div></div>
<div class='card'><div class='cl'>Uptime</div><div class='cv'>%UPTIME%</div><div class='cu'>&nbsp;</div></div>
<div class='card'><div class='cl'>Reboot In</div><div class='cv'>%REBOOT%</div><div class='cu'>&nbsp;</div></div>
</div>
<div class='acts'>
<a class='enroll-btn' href='/enroll'>Enroll New Finger</a>
<a class='del-btn' href='/delete'>Delete All</a>
</div>
</div>
<div class='ftr'><span>Fingerprint Dashboard V1</span> <a href='/json'>JSON</a> <a href='/config'>Config</a></div>
</body></html>)rawliteral";

// ============== PROGMEM HTML: CONFIG ==============
static const char CONFIG_HTML[] PROGMEM = R"rawliteral(<!DOCTYPE html><html><head>
<meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'>
<title>Fingerprint Config</title><style>
*{margin:0;padding:0;box-sizing:border-box}
body{background:linear-gradient(135deg,#0c0118 0%%,#1a0533 40%%,#0d1b2a 100%%);color:#e2e8f0;font-family:'Segoe UI',system-ui,sans-serif;min-height:100vh;display:flex;justify-content:center;align-items:center}
.box{background:linear-gradient(145deg,#1a1035,#0f172a);border:2px solid #7c3aed40;border-radius:20px;padding:36px;width:90%%;max-width:440px;box-shadow:0 8px 32px rgba(124,58,237,.2)}
h2{background:linear-gradient(90deg,#c084fc,#38bdf8);-webkit-background-clip:text;-webkit-text-fill-color:transparent;margin-bottom:20px;text-align:center;font-size:1.5rem;font-weight:800}
.sep{height:2px;background:linear-gradient(90deg,transparent,#7c3aed,#06b6d4,transparent);margin:6px 0 16px;border:none}
label{display:block;color:#c084fc;font-size:.75rem;margin:12px 0 4px;text-transform:uppercase;letter-spacing:1px;font-weight:600}
input{width:100%%;padding:11px 14px;border:1px solid #334155;border-radius:10px;background:#0c0118;color:#f8fafc;font-size:.95rem;transition:border-color .3s}
input:focus{outline:none;border-color:#7c3aed;box-shadow:0 0 12px #7c3aed30}
.btn{width:100%%;padding:14px;margin-top:24px;background:linear-gradient(135deg,#7c3aed,#6d28d9);color:#fff;border:none;border-radius:10px;font-size:1rem;cursor:pointer;font-weight:700;box-shadow:0 4px 15px #7c3aed40;transition:all .3s;letter-spacing:.5px}
.btn:hover{transform:translateY(-2px);box-shadow:0 6px 25px #7c3aed60}
.ok{background:linear-gradient(135deg,#166534,#15803d);color:#4ade80;padding:12px;border-radius:10px;text-align:center;margin-bottom:14px;font-weight:600}
.info{text-align:center;color:#64748b;font-size:.75rem;margin-top:20px}
</style></head><body>
<div class='box'>
<h2>Fingerprint Config</h2>
<hr class='sep'>
%MSG%
<form method='POST' action='/save'>
<label>WiFi SSID</label><input name='ssid' value='%SSID%'>
<label>WiFi Password</label><input name='wifipass' type='password' value='%WIFIPASS%'>
<label>MQTT Server</label><input name='mqttserver' value='%MQTTSERVER%'>
<label>MQTT Port</label><input name='mqttport' type='number' value='%MQTTPORT%'>
<label>MQTT User</label><input name='mqttuser' value='%MQTTUSER%'>
<label>MQTT Password</label><input name='mqttpass' type='password' value='%MQTTPASS%'>
<label>MQTT Topic Prefix</label><input name='mqtttopic' value='%MQTTTOPIC%'>
<button class='btn' type='submit'>Save &amp; Reboot</button>
</form>
<div class='info'>MAC: %MAC% | IP: %IP%</div>
</div></body></html>)rawliteral";

// ============== PROGMEM HTML: ENROLL ==============
static const char ENROLL_HTML[] PROGMEM = R"rawliteral(<!DOCTYPE html><html><head>
<meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'>
<title>Enroll Finger</title><style>
*{margin:0;padding:0;box-sizing:border-box}
body{background:linear-gradient(135deg,#0c0118 0%%,#1a0533 40%%,#0d1b2a 100%%);color:#e2e8f0;font-family:'Segoe UI',system-ui,sans-serif;min-height:100vh;display:flex;justify-content:center;align-items:center}
.box{background:linear-gradient(145deg,#1a1035,#0f172a);border:2px solid #7c3aed40;border-radius:20px;padding:36px;width:90%%;max-width:440px;text-align:center;box-shadow:0 8px 32px rgba(124,58,237,.2)}
h2{background:linear-gradient(90deg,#c084fc,#38bdf8);-webkit-background-clip:text;-webkit-text-fill-color:transparent;margin-bottom:20px;font-size:1.5rem;font-weight:800}
.msg{padding:18px;border-radius:12px;margin:16px 0;font-size:1rem;font-weight:600}
.ok{background:linear-gradient(135deg,#166534,#15803d);color:#4ade80;box-shadow:0 4px 15px #22c55e30}
.err{background:linear-gradient(135deg,#7f1d1d,#991b1b);color:#fca5a5;box-shadow:0 4px 15px #ef444430}
.info{background:linear-gradient(135deg,#1e3a5f,#1e40af);color:#93c5fd;box-shadow:0 4px 15px #3b82f630}
a{color:#c084fc;text-decoration:none;display:inline-block;margin-top:20px;font-weight:600;transition:color .3s}
a:hover{color:#e9d5ff}
</style></head><body>
<div class='box'>
<h2>Enroll Finger</h2>
<div class='msg %ECLS%'>%EMSG%</div>
<a href='/'>Back to Dashboard</a>
</div></body></html>)rawliteral";

// ============== PROGMEM CHUNKED STREAMING ==============
void sendProgmemChunked(const char* pgm, const char* (*resolve)(const char*)) {
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/html", "");

  const char* p = pgm;
  char chunk[128];
  int ci = 0;

  while (true) {
    char c = pgm_read_byte(p++);
    if (c == 0) break;

    if (c == '%') {
      char next = pgm_read_byte(p);
      if (next == '%') {
        p++;
        chunk[ci++] = '%';
        if (ci >= (int)sizeof(chunk)) { server.sendContent(chunk, ci); ci = 0; }
      } else {
        if (ci > 0) { server.sendContent(chunk, ci); ci = 0; }
        char key[24];
        int ki = 0;
        while (true) {
          char k = pgm_read_byte(p++);
          if (k == 0 || k == '%') break;
          if (ki < (int)sizeof(key) - 1) key[ki++] = k;
        }
        key[ki] = 0;
        if (ki > 0) {
          const char* val = resolve(key);
          if (val && val[0]) server.sendContent(val);
        }
      }
    } else {
      chunk[ci++] = c;
      if (ci >= (int)sizeof(chunk)) {
        server.sendContent(chunk, ci);
        ci = 0;
      }
    }
  }
  if (ci > 0) server.sendContent(chunk, ci);
  server.sendContent("");
}

// ============== UTILITY ==============
void uptimeStr(char* dst, size_t sz) {
  unsigned long s = (millis() - bootTime) / 1000;
  snprintf(dst, sz, "%luh%02lum", s / 3600, (s % 3600) / 60);
}

void timeAgo(unsigned long ts, char* dst, size_t sz) {
  if (ts == 0) { snprintf(dst, sz, "never"); return; }
  unsigned long ago = (millis() - ts) / 1000;
  if (ago < 60) snprintf(dst, sz, "%lus ago", ago);
  else if (ago < 3600) snprintf(dst, sz, "%lum ago", ago / 60);
  else snprintf(dst, sz, "%luh ago", ago / 3600);
}

void ledOn()  { digitalWrite(LED_PIN, LOW); }   // Active LOW
void ledOff() { digitalWrite(LED_PIN, HIGH); }

void ledBlink(int times, int ms) {
  for (int i = 0; i < times; i++) {
    ledOn(); delay(ms); ledOff(); delay(ms);
  }
}

// ============== DASHBOARD TOKEN RESOLVER ==============
static char _uptime[12], _reboot[12], _ago[16];

const char* dashResolve(const char* key) {
  if (strcmp(key, "WDOT") == 0) return wifiConnected ? "sg" : "sr";
  if (strcmp(key, "MDOT") == 0) return mqttConnected ? "sg" : "sr";
  if (strcmp(key, "SDOT") == 0) return sensorFound ? "sg" : "sr";

  if (strcmp(key, "ICON") == 0) {
    switch (lastStatus) {
      case 1: return "&#9989;";   // check mark - matched
      case 2: return "&#10060;";  // cross - rejected
      case 3: return "&#9888;";   // warning - error
      default: return "&#9995;";  // hand - waiting
    }
  }
  if (strcmp(key, "SCLS") == 0) {
    switch (lastStatus) {
      case 1: return "matched";
      case 2: return "rejected";
      case 3: return "rejected";
      default: return "waiting";
    }
  }
  if (strcmp(key, "STXT") == 0) {
    switch (lastStatus) {
      case 0: return "Ready - Place Finger";
      case 1: return "MATCH FOUND";
      case 2: return "NOT RECOGNIZED";
      case 3: return "SENSOR ERROR";
      default: return "Initializing...";
    }
  }
  if (strcmp(key, "EVENT") == 0) return lastEventStr;
  if (strcmp(key, "AGO") == 0) { timeAgo(lastEventTime, _ago, sizeof(_ago)); return _ago; }

  if (strcmp(key, "MID") == 0) {
    if (lastMatchId < 0) return "--";
    snprintf(buf, 8, "#%d", lastMatchId);
    return buf;
  }
  if (strcmp(key, "MIDCLS") == 0) return lastMatchId >= 0 ? "hi" : "";
  if (strcmp(key, "CONF") == 0) {
    if (lastConfidence == 0) return "--";
    snprintf(buf, 8, "%d", lastConfidence);
    return buf;
  }
  if (strcmp(key, "CCLS") == 0) {
    if (lastConfidence == 0) return "";
    if (lastConfidence >= 100) return "hi";
    if (lastConfidence >= 50) return "med";
    return "lo";
  }
  if (strcmp(key, "TCNT") == 0) { snprintf(buf, 8, "%d", templateCount); return buf; }
  if (strcmp(key, "SCANS") == 0) { snprintf(buf, 8, "%d", totalScans); return buf; }
  if (strcmp(key, "MATCHES") == 0) { snprintf(buf, 8, "%d", totalMatches); return buf; }
  if (strcmp(key, "REJECTS") == 0) { snprintf(buf, 8, "%d", totalRejects); return buf; }

  if (strcmp(key, "RSSI") == 0) { snprintf(buf, 8, "%d", WiFi.RSSI()); return buf; }
  if (strcmp(key, "HEAP") == 0) { snprintf(buf, 12, "%u", ESP.getFreeHeap()); return buf; }
  if (strcmp(key, "UPTIME") == 0) { uptimeStr(_uptime, sizeof(_uptime)); return _uptime; }
  if (strcmp(key, "REBOOT") == 0) {
    unsigned long rem = 0;
    if (millis() - bootTime < REBOOT_INTERVAL) rem = (REBOOT_INTERVAL - (millis() - bootTime)) / 1000;
    snprintf(_reboot, sizeof(_reboot), "%luh%02lum", rem / 3600, (rem % 3600) / 60);
    return _reboot;
  }
  return "";
}

// ============== CONFIG TOKEN RESOLVER ==============
bool _showSaveMsg = false;

const char* cfgResolve(const char* key) {
  if (strcmp(key, "MAC") == 0) { WiFi.macAddress().toCharArray(buf, 24); return buf; }
  if (strcmp(key, "IP") == 0) {
    (configMode ? WiFi.softAPIP() : WiFi.localIP()).toString().toCharArray(buf, 20);
    return buf;
  }
  if (strcmp(key, "SSID") == 0) return cfg_wifi_ssid;
  if (strcmp(key, "WIFIPASS") == 0) return cfg_wifi_pass;
  if (strcmp(key, "MQTTSERVER") == 0) return cfg_mqtt_server;
  if (strcmp(key, "MQTTPORT") == 0) { snprintf(buf, 8, "%u", cfg_mqtt_port); return buf; }
  if (strcmp(key, "MQTTUSER") == 0) return cfg_mqtt_user;
  if (strcmp(key, "MQTTPASS") == 0) return cfg_mqtt_pass;
  if (strcmp(key, "MQTTTOPIC") == 0) return cfg_mqtt_topic;
  if (strcmp(key, "MSG") == 0) return _showSaveMsg ? "<div class='ok'>Saved! Rebooting...</div>" : "";
  return "";
}

// ============== ENROLL TOKEN RESOLVER ==============
static char _enrollMsg[80];
static char _enrollCls[8];

const char* enrollResolve(const char* key) {
  if (strcmp(key, "EMSG") == 0) return _enrollMsg;
  if (strcmp(key, "ECLS") == 0) return _enrollCls;
  return "";
}

// ============== WEB HANDLERS ==============
void handleDashboard() {
  sendProgmemChunked(DASHBOARD_HTML, dashResolve);
}

void handleJson() {
  snprintf(buf, sizeof(buf),
    "{\"matchId\":%d,\"confidence\":%d,\"status\":%d,"
    "\"templates\":%d,\"scans\":%d,\"matches\":%d,\"rejects\":%d,"
    "\"sensor\":%s,\"rssi\":%d,\"heap\":%u,\"up\":%lu}",
    lastMatchId, lastConfidence, lastStatus,
    templateCount, totalScans, totalMatches, totalRejects,
    sensorFound ? "true" : "false",
    WiFi.RSSI(), ESP.getFreeHeap(), (millis() - bootTime) / 1000);
  server.send(200, "application/json", buf);
}

void handleConfigPage() {
  _showSaveMsg = false;
  sendProgmemChunked(CONFIG_HTML, cfgResolve);
}

void handleSave() {
  if (server.hasArg("ssid")) server.arg("ssid").toCharArray(cfg_wifi_ssid, sizeof(cfg_wifi_ssid));
  if (server.hasArg("wifipass")) server.arg("wifipass").toCharArray(cfg_wifi_pass, sizeof(cfg_wifi_pass));
  if (server.hasArg("mqttserver")) server.arg("mqttserver").toCharArray(cfg_mqtt_server, sizeof(cfg_mqtt_server));
  if (server.hasArg("mqttport")) cfg_mqtt_port = server.arg("mqttport").toInt();
  if (server.hasArg("mqttuser")) server.arg("mqttuser").toCharArray(cfg_mqtt_user, sizeof(cfg_mqtt_user));
  if (server.hasArg("mqttpass")) server.arg("mqttpass").toCharArray(cfg_mqtt_pass, sizeof(cfg_mqtt_pass));
  if (server.hasArg("mqtttopic")) server.arg("mqtttopic").toCharArray(cfg_mqtt_topic, sizeof(cfg_mqtt_topic));
  saveConfig();
  _showSaveMsg = true;
  sendProgmemChunked(CONFIG_HTML, cfgResolve);
  delay(3000);
  ESP.restart();
}

void handleEnroll() {
  if (!sensorFound) {
    snprintf(_enrollMsg, sizeof(_enrollMsg), "Sensor not found!");
    snprintf(_enrollCls, sizeof(_enrollCls), "err");
    sendProgmemChunked(ENROLL_HTML, enrollResolve);
    return;
  }

  // Find next free ID
  int id = -1;
  for (int i = 1; i <= 127; i++) {
    if (finger.loadModel(i) != FINGERPRINT_OK) { id = i; break; }
  }
  if (id < 0) {
    snprintf(_enrollMsg, sizeof(_enrollMsg), "Database full (127 templates)!");
    snprintf(_enrollCls, sizeof(_enrollCls), "err");
    sendProgmemChunked(ENROLL_HTML, enrollResolve);
    return;
  }

  Serial.printf("Enrolling ID #%d - Place finger...\n", id);
  snprintf(_enrollMsg, sizeof(_enrollMsg), "Enrolling ID #%d - Place finger on sensor now...", id);
  snprintf(_enrollCls, sizeof(_enrollCls), "info");
  sendProgmemChunked(ENROLL_HTML, enrollResolve);

  // Wait for finger with timeout
  ledOn();
  unsigned long start = millis();
  int p = -1;
  while (millis() - start < 10000) {
    p = finger.getImage();
    if (p == FINGERPRINT_OK) break;
    delay(100);
  }

  if (p != FINGERPRINT_OK) {
    Serial.println(F("Enroll: timeout waiting for finger"));
    return;
  }

  p = finger.image2Tz(1);
  if (p != FINGERPRINT_OK) { Serial.println(F("Enroll: image2Tz(1) failed")); return; }

  Serial.println(F("Remove finger..."));
  delay(2000);

  // Wait for finger removal
  while (finger.getImage() != FINGERPRINT_NOFINGER) delay(100);

  Serial.println(F("Place same finger again..."));

  // Wait for second placement
  start = millis();
  p = -1;
  while (millis() - start < 10000) {
    p = finger.getImage();
    if (p == FINGERPRINT_OK) break;
    delay(100);
  }

  if (p != FINGERPRINT_OK) { Serial.println(F("Enroll: timeout second read")); return; }

  p = finger.image2Tz(2);
  if (p != FINGERPRINT_OK) { Serial.println(F("Enroll: image2Tz(2) failed")); return; }

  p = finger.createModel();
  if (p != FINGERPRINT_OK) { Serial.println(F("Enroll: createModel failed")); return; }

  p = finger.storeModel(id);
  if (p == FINGERPRINT_OK) {
    Serial.printf("Enrolled successfully as ID #%d\n", id);
    ledBlink(3, LED_BLINK_OK);
    finger.getTemplateCount();
    templateCount = finger.templateCount;
  } else {
    Serial.println(F("Enroll: storeModel failed"));
    ledBlink(5, LED_BLINK_FAIL);
  }
  ledOff();
}

void handleDelete() {
  if (sensorFound) {
    finger.emptyDatabase();
    finger.getTemplateCount();
    templateCount = finger.templateCount;
    totalScans = 0;
    totalMatches = 0;
    totalRejects = 0;
    lastMatchId = -1;
    lastConfidence = 0;
    lastStatus = 0;
    snprintf(lastEventStr, sizeof(lastEventStr), "DB cleared");
    lastEventTime = millis();
    Serial.println(F("Fingerprint database cleared"));
  }
  server.sendHeader("Location", "/");
  server.send(302);
}

// ============== CONFIG LOAD/SAVE ==============
void loadConfig() {
  if (!LittleFS.begin(true)) { LittleFS.format(); LittleFS.begin(true); }

  File f = LittleFS.open("/config.json", "r");
  if (f) {
    String c = f.readString();
    f.close();

    auto extract = [&](const char* key, char* dst, size_t sz) {
      String k = String("\"") + key + "\":\"";
      int idx = c.indexOf(k);
      if (idx >= 0) {
        int start = idx + k.length();
        int end = c.indexOf("\"", start);
        if (end > start) c.substring(start, end).toCharArray(dst, sz);
      }
    };

    extract("wifi_ssid", cfg_wifi_ssid, sizeof(cfg_wifi_ssid));
    extract("wifi_pass", cfg_wifi_pass, sizeof(cfg_wifi_pass));
    extract("mqtt_server", cfg_mqtt_server, sizeof(cfg_mqtt_server));
    extract("mqtt_user", cfg_mqtt_user, sizeof(cfg_mqtt_user));
    extract("mqtt_pass", cfg_mqtt_pass, sizeof(cfg_mqtt_pass));
    extract("mqtt_topic", cfg_mqtt_topic, sizeof(cfg_mqtt_topic));

    int idx = c.indexOf("\"mqtt_port\":");
    if (idx >= 0) {
      int start = idx + 12;
      int end = c.indexOf(",", start);
      if (end < 0) end = c.indexOf("}", start);
      if (end > start) cfg_mqtt_port = c.substring(start, end).toInt();
    }
  }
  Serial.printf("Config: SSID=%s, MQTT=%s:%d\n", cfg_wifi_ssid, cfg_mqtt_server, cfg_mqtt_port);
}

void saveConfig() {
  File f = LittleFS.open("/config.json", "w");
  if (f) {
    f.printf("{\"wifi_ssid\":\"%s\",\"wifi_pass\":\"%s\","
             "\"mqtt_server\":\"%s\",\"mqtt_port\":%d,"
             "\"mqtt_user\":\"%s\",\"mqtt_pass\":\"%s\","
             "\"mqtt_topic\":\"%s\"}",
             cfg_wifi_ssid, cfg_wifi_pass,
             cfg_mqtt_server, cfg_mqtt_port,
             cfg_mqtt_user, cfg_mqtt_pass, cfg_mqtt_topic);
    f.close();
    Serial.println(F("Config saved"));
  }
}

// ============== WIFI ==============
bool connectWiFi() {
  if (strlen(cfg_wifi_ssid) == 0) return false;

  Serial.printf("Connecting to WiFi: %s\n", cfg_wifi_ssid);
  WiFi.mode(WIFI_STA);
  WiFi.begin(cfg_wifi_ssid, cfg_wifi_pass);

  int tries = 0;
  while (WiFi.status() != WL_CONNECTED && tries < 40) {
    delay(500);
    Serial.print(".");
    tries++;
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("WiFi connected: %s\n", WiFi.localIP().toString().c_str());
    return true;
  }
  Serial.println(F("WiFi connection failed"));
  return false;
}

// ============== MQTT ==============
void connectMQTT() {
  if (strlen(cfg_mqtt_server) == 0) return;

  mqtt.setServer(cfg_mqtt_server, cfg_mqtt_port);
  Serial.printf("Connecting MQTT: %s:%d\n", cfg_mqtt_server, cfg_mqtt_port);

  String clientId = "FP-" + String((uint32_t)ESP.getEfuseMac(), HEX);
  if (strlen(cfg_mqtt_user) > 0) {
    mqttConnected = mqtt.connect(clientId.c_str(), cfg_mqtt_user, cfg_mqtt_pass);
  } else {
    mqttConnected = mqtt.connect(clientId.c_str());
  }

  if (mqttConnected) Serial.println(F("MQTT connected"));
  else Serial.printf("MQTT failed, rc=%d\n", mqtt.state());
}

void publishMQTT() {
  if (!mqttConnected) return;

  char topic[96];

  snprintf(topic, sizeof(topic), "%s/match_id", cfg_mqtt_topic);
  snprintf(buf, 8, "%d", lastMatchId);
  mqtt.publish(topic, buf);

  snprintf(topic, sizeof(topic), "%s/confidence", cfg_mqtt_topic);
  snprintf(buf, 8, "%d", lastConfidence);
  mqtt.publish(topic, buf);

  snprintf(topic, sizeof(topic), "%s/templates", cfg_mqtt_topic);
  snprintf(buf, 8, "%d", templateCount);
  mqtt.publish(topic, buf);

  // JSON status
  snprintf(topic, sizeof(topic), "%s/status", cfg_mqtt_topic);
  snprintf(buf, sizeof(buf),
    "{\"id\":%d,\"conf\":%d,\"status\":%d,\"templates\":%d,"
    "\"scans\":%d,\"matches\":%d,\"rejects\":%d}",
    lastMatchId, lastConfidence, lastStatus,
    templateCount, totalScans, totalMatches, totalRejects);
  mqtt.publish(topic, buf);
}

// ============== OTA ==============
void setupOTA() {
  ArduinoOTA.setHostname("FP-ESP32C6");
  ArduinoOTA.onStart([]() { Serial.println(F("OTA Start")); });
  ArduinoOTA.onEnd([]() { Serial.println(F("\nOTA End")); ledOff(); });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    digitalWrite(LED_PIN, !((progress / (total / 10)) % 2));  // Active LOW
    Serial.printf("OTA: %u%%\r", progress / (total / 100));
  });
  ArduinoOTA.onError([](ota_error_t error) { Serial.printf("OTA Error[%u]\n", error); });
  ArduinoOTA.begin();
  Serial.println(F("OTA ready"));
}

// ============== CONFIG PORTAL ==============
void startConfigPortal() {
  configMode = true;
  Serial.println(F("Config portal started"));
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASSWORD);
  Serial.printf("AP IP: %s\n", WiFi.softAPIP().toString().c_str());

  server.on("/", handleConfigPage);
  server.on("/config", handleConfigPage);
  server.on("/save", HTTP_POST, handleSave);
  server.begin();
}

// ============== FINGERPRINT SCANNING ==============
void scanFingerprint() {
  int p = finger.getImage();
  if (p != FINGERPRINT_OK) return;  // No finger present

  totalScans++;
  lastEventTime = millis();

  p = finger.image2Tz();
  if (p != FINGERPRINT_OK) {
    lastStatus = 3;
    snprintf(lastEventStr, sizeof(lastEventStr), "Image convert error");
    ledBlink(2, LED_BLINK_FAIL);
    return;
  }

  p = finger.fingerSearch();
  if (p == FINGERPRINT_OK) {
    // Match found
    lastMatchId = finger.fingerID;
    lastConfidence = finger.confidence;
    lastStatus = 1;
    lastMatchTime = millis();
    totalMatches++;
    snprintf(lastEventStr, sizeof(lastEventStr), "Match ID#%d (%d%%)", lastMatchId, lastConfidence);
    Serial.printf("Match found! ID#%d confidence=%d\n", lastMatchId, lastConfidence);

    ledBlink(2, LED_BLINK_OK);

    // Publish immediately on match
    if (mqttConnected) publishMQTT();
  } else if (p == FINGERPRINT_NOTFOUND) {
    lastStatus = 2;
    lastConfidence = 0;
    totalRejects++;
    snprintf(lastEventStr, sizeof(lastEventStr), "Not recognized");
    Serial.println(F("Fingerprint not found"));

    ledBlink(4, LED_BLINK_FAIL);

    if (mqttConnected) publishMQTT();
  } else {
    lastStatus = 3;
    snprintf(lastEventStr, sizeof(lastEventStr), "Search error %d", p);
    Serial.printf("Search error: %d\n", p);
  }

  // Wait for finger removal before next scan
  while (finger.getImage() != FINGERPRINT_NOFINGER) delay(50);
}

// ============== SETUP ==============
void setup() {
  Serial.begin(115200);
  Serial.println(F("\n=== Fingerprint Dashboard V1 ==="));

  pinMode(LED_PIN, OUTPUT);
  ledOff();
  pinMode(FP_IRQ, INPUT);
  pinMode(CONFIG_BTN, INPUT_PULLUP);

  bootTime = millis();

  // Init fingerprint sensor
  fpSerial.begin(57600, SERIAL_8N1, FP_RX, FP_TX);
  if (finger.verifyPassword()) {
    sensorFound = true;
    Serial.println(F("Fingerprint sensor found"));
    finger.getTemplateCount();
    templateCount = finger.templateCount;
    Serial.printf("Templates stored: %d\n", templateCount);
    // Set sensor LED (if supported)
    finger.LEDcontrol(FINGERPRINT_LED_BREATHING, 100, FINGERPRINT_LED_BLUE);
  } else {
    Serial.println(F("Fingerprint sensor NOT found!"));
  }

  // Load config
  loadConfig();

  // Check for config mode
  if (digitalRead(CONFIG_BTN) == LOW) {
    Serial.println(F("BOOT button held - waiting 3s for config mode..."));
    unsigned long pressStart = millis();
    while (digitalRead(CONFIG_BTN) == LOW && millis() - pressStart < 3000) {
      ledOn(); delay(50); ledOff(); delay(50);
    }
    if (millis() - pressStart >= 3000) {
      startConfigPortal();
      return;
    }
  }

  // Try WiFi
  wifiConnected = connectWiFi();

  if (!wifiConnected) {
    startConfigPortal();
    return;
  }

  // Normal mode
  setupOTA();
  connectMQTT();

  server.on("/", handleDashboard);
  server.on("/json", handleJson);
  server.on("/config", handleConfigPage);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/enroll", handleEnroll);
  server.on("/delete", handleDelete);
  server.begin();
  Serial.println(F("Web server started"));

  lastStatus = 0;
  snprintf(lastEventStr, sizeof(lastEventStr), "Ready");
  ledBlink(2, LED_BLINK_OK);
}

// ============== LOOP ==============
void loop() {
  if (configMode) {
    server.handleClient();
    // Blink LED in config mode
    digitalWrite(LED_PIN, !((millis() / 200) % 2));
    return;
  }

  // Normal mode
  if (wifiConnected) {
    ArduinoOTA.handle();
    server.handleClient();

    // MQTT reconnect
    if (strlen(cfg_mqtt_server) > 0) {
      if (!mqtt.connected()) {
        mqttConnected = false;
        static unsigned long lastReconnect = 0;
        if (millis() - lastReconnect > 30000) {
          lastReconnect = millis();
          connectMQTT();
        }
      }
      mqtt.loop();

      // Periodic MQTT publish
      if (mqttConnected && millis() - lastMqtt >= MQTT_INTERVAL) {
        lastMqtt = millis();
        publishMQTT();
      }
    }
  }

  // Fingerprint scanning
  if (sensorFound && millis() - lastScan >= FP_SCAN_INTERVAL) {
    lastScan = millis();
    scanFingerprint();
  }

  // Auto reboot
  if (millis() - bootTime >= REBOOT_INTERVAL) {
    Serial.println(F("Auto reboot"));
    ESP.restart();
  }

  // Serial debug every 10s
  static unsigned long lastDebug = 0;
  if (millis() - lastDebug >= 10000) {
    lastDebug = millis();
    Serial.printf("FP: sensor=%d templates=%d scans=%d matches=%d rejects=%d heap=%u\n",
      sensorFound ? 1 : 0, templateCount, totalScans, totalMatches, totalRejects, ESP.getFreeHeap());
  }
}
