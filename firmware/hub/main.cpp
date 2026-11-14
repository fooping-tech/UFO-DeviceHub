// UFO Device Hub (M5AtomS3R; an ATOM Lite works too): USB Serial JSON Lines <-> ESP-NOW.
// No game logic here: routing, reliability, session and link supervision only.
#include <Arduino.h>
#include <esp_random.h>
#ifndef UFO_HUB_DISPLAY
#define UFO_HUB_DISPLAY 0
#endif
#if !UFO_HUB_DISPLAY
#include <FastLED.h>
#endif

#include "device_ids.h"
#include "espnow_transport.h"
#include "hub_core.h"
#include "hub_display.h"
#include "json_bridge.h"
#include "ufo_config.h"
#include "version.h"

using namespace ufo;

namespace {

EspNowTransport g_radio;

class SerialOut : public LineOutput {
 public:
  void writeLine(const char* line, size_t len) override {
    Serial.write(reinterpret_cast<const uint8_t*>(line), len);
    Serial.write('\n');
  }
} g_out;

class Radio : public RadioOutput {
 public:
  bool sendRadio(const uint8_t mac[6], const uint8_t* data, size_t len) override {
    return g_radio.send(mac, data, len);
  }
} g_radio_out;

HubInfo makeInfo() {
  HubInfo info;
  g_radio.selfMac(info.mac);
  info.fw = UFO_FW_VERSION_STR;
  info.channel = wifiChannel();
  info.system_id = systemId();
  info.system_check = systemSeparationText(systemSeparationIssues());
  info.example_keys = usingExampleKeys();
  info.example_devices = usingExampleDevices();
  info.example_hardware = UFO_USING_EXAMPLE_HARDWARE != 0;
  return info;
}

HubConfig makeConfig() {
  HubConfig c;
  c.system_id = systemId();
  c.command_policy.ack_timeout_ms = UFO_ACK_TIMEOUT_MS;
  c.command_policy.max_retries = UFO_MAX_RETRIES;
  c.heartbeat_interval_ms = UFO_HEARTBEAT_INTERVAL_MS;
  c.link_timeout_ms = UFO_LINK_TIMEOUT_MS;
  c.pc_timeout_ms = UFO_PC_TIMEOUT_MS;
  c.pc_event_retry_ms = UFO_PC_EVENT_RETRY_MS;
  return c;
}

HubJsonBridge* g_bridge = nullptr;
HubCore* g_core = nullptr;

char g_line[UFO_SERIAL_MAX_LINE + 1];
size_t g_line_len = 0;
bool g_discarding = false;

bool g_radio_ok = false;
uint8_t g_sep_issues = 0;
#if !UFO_HUB_DISPLAY
CRGB g_led[1];
#endif

void diagTransport(JsonObject t, void*) {
  const TransportStats& s = g_radio.stats();
  t["rx_callbacks"] = s.rx_callbacks;
  t["rx_dropped_unknown_mac"] = s.rx_dropped_unknown_mac;
  t["rx_dropped_length"] = s.rx_dropped_length;
  t["rx_dropped_queue_full"] = s.rx_dropped_queue_full;
  t["tx_mac_ok"] = s.tx_ok;
  t["tx_mac_fail"] = s.tx_fail;
  t["tx_errors"] = s.tx_errors;
  t["peers"] = g_radio.peerCount();
}

void readSerial(uint32_t now) {
  int budget = 512;  // bounded work per loop iteration
  while (Serial.available() > 0 && budget-- > 0) {
    int c = Serial.read();
    if (c < 0) break;
    if (c == '\r') continue;
    if (c == '\n') {
      if (g_discarding) {
        g_discarding = false;
      } else if (g_line_len > 0) {
        g_line[g_line_len] = '\0';
        g_bridge->handleLine(g_line, g_line_len, now);
      }
      g_line_len = 0;
      continue;
    }
    if (g_discarding) continue;
    if (g_line_len >= UFO_SERIAL_MAX_LINE) {
      g_bridge->reportLineTooLong(UFO_SERIAL_MAX_LINE);
      g_discarding = true;
      g_line_len = 0;
      continue;
    }
    g_line[g_line_len++] = static_cast<char>(c);
  }
}

void updateLed(uint32_t now) {
  static uint32_t last = 0;
  if (now - last < 100) return;
  last = now;
  size_t online = 0;
  uint8_t mask = 0;
  for (size_t i = 0; i < g_core->childCount(); ++i)
    if (g_core->childAt(i).link.up()) {
      ++online;
      mask |= static_cast<uint8_t>(1u << i);
    }
#if UFO_HUB_DISPLAY
  (void)online;
  HubScreen sc;
  sc.system_id = systemId();
  sc.channel = wifiChannel();
  sc.radio_ok = g_radio_ok;
  sc.pc_session = g_core->session() != 0;
  sc.online_mask = mask;
  sc.child_count = static_cast<uint8_t>(g_core->childCount());
  sc.example_keys = usingExampleKeys();
  sc.problem = g_sep_issues ? systemSeparationText(g_sep_issues) : nullptr;
  hubDisplayShow(sc);
#else
  (void)mask;
  CRGB c;
  if (g_core->session() == 0)
    c = CRGB(0, 0, 40);  // blue: waiting for the PC
  else if (online == g_core->childCount())
    c = CRGB(0, 40, 0);  // green: session + all devices online
  else
    c = ((now / 250) % 2) ? CRGB(40, 25, 0) : CRGB::Black;  // blinking amber: device(s) offline
  if ((usingExampleKeys() || g_sep_issues) && (now / 1000) % 5 == 0) c = CRGB(40, 0, 0);  // red pulse
  if (!g_radio_ok) c = ((now / 200) % 2) ? CRGB(60, 0, 0) : CRGB::Black;
  g_led[0] = c;
  FastLED.show();
#endif
}

}  // namespace

void setup() {
  Serial.setRxBufferSize(2048);
  Serial.setTxBufferSize(4096);
  Serial.begin(HUB_SERIAL_BAUD);
#if UFO_HUB_DISPLAY
  hubDisplayBegin();
#else
  FastLED.addLeds<SK6812, HUB_STATUS_LED_PIN, GRB>(g_led, 1);
  FastLED.setBrightness(80);
  g_led[0] = CRGB(40, 0, 40);
  FastLED.show();
#endif

  // Two systems in one venue: refuse to go on the air with a provisioning that
  // would let them interfere (shared MAC / keys, invalid channel).
  g_sep_issues = systemSeparationIssues();
  bool radio_ok = false;
  if (!(g_sep_issues & kSepErrors)) radio_ok = g_radio.begin(wifiChannel(), primaryMasterKey());
  g_radio_ok = radio_ok;
  static HubInfo info = makeInfo();
  static HubJsonBridge bridge(g_out, g_radio_out, info);
  static HubCore core(bridge, makeConfig());
  bridge.attach(&core);
  bridge.setDiagProvider(diagTransport, nullptr);
  g_bridge = &bridge;
  g_core = &core;

  bridge.emitBoot();
  if (g_sep_issues & kSepErrors) {
    char msg[96];
    snprintf(msg, sizeof(msg), "system config error: %s - radio NOT started", systemSeparationText(g_sep_issues));
    bridge.log("error", msg);
  } else if (g_sep_issues) {
    char msg[96];
    snprintf(msg, sizeof(msg), "system config warning: %s", systemSeparationText(g_sep_issues));
    bridge.log("warn", msg);
  }
  if (!radio_ok) bridge.log("error", "ESP-NOW not running");
  if (usingExampleKeys()) bridge.log("warn", "EXAMPLE ESP-NOW KEYS IN USE - run tests/tools/gen_secrets.py");
  if (usingExampleDevices()) bridge.log("warn", "config/devices.h missing - example MAC table in use");

  const DeviceEntry* self = findDevice(kHubId);
  if (self && !macEqual(self->mac, info.mac)) bridge.log("warn", "hub MAC differs from config/devices.h UFO_MAC_HUB");

  for (size_t i = 0; i < deviceCount(); ++i) {
    const DeviceEntry& d = deviceAt(i);
    if (d.id == kHubId) continue;
    if (radio_ok && !g_radio.addPeer(d.mac, d.lmk)) {
      char msg[64];
      snprintf(msg, sizeof(msg), "peer registration failed: %s", d.name);
      bridge.log("error", msg);
    }
  }
  core.begin(millis(), esp_random());
}

void loop() {
  uint32_t now = millis();
  g_bridge->setNow(now);
  readSerial(now);
  RxFrame f;
  int budget = 16;
  while (budget-- > 0 && g_radio.poll(f)) g_core->onFrame(f.mac, f.data, f.len, f.rssi, now);
  g_core->tick(now);
  updateLed(now);
  delay(1);
}
