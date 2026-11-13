// Illumination ATOM (Device ID 0x10): LED illumination + steam generator relay.
#include <Arduino.h>
#include <FastLED.h>
#include <esp_timer.h>

#include "device_ids.h"
#include "illumination_controller.h"
#include "led_renderer.h"
#include "node_runtime.h"
#include "ufo_config.h"

using namespace ufo;

namespace {

constexpr uint8_t kRelayCount = ILLUM_RELAY_COUNT;
const int kRelayPins[kRelayCount] = ILLUM_RELAY_PINS;
const uint8_t kRelayActiveHigh[kRelayCount] = ILLUM_RELAY_ACTIVE_HIGH;
bool g_relay_configured[kRelayCount];

inline uint8_t levelFor(uint8_t id, bool on) { return (on == (kRelayActiveHigh[id] != 0)) ? HIGH : LOW; }

// Hardware watchdog independent of loop(): an esp_timer one-shot forces every
// relay OFF if a pulse is still ON at max_on + margin (e.g. loop() stalled).
esp_timer_handle_t g_wd_timer = nullptr;
volatile bool g_wd_fired = false;

void forceAllRelaysOff() {
  for (uint8_t i = 0; i < kRelayCount; ++i)
    if (g_relay_configured[i]) digitalWrite(kRelayPins[i], levelFor(i, false));
}

void watchdogCb(void*) {
  forceAllRelaysOff();
  g_wd_fired = true;
}

class GpioRelays : public RelayOutput {
 public:
  void setRelay(uint8_t id, bool on) override {
    if (id >= kRelayCount || !g_relay_configured[id]) return;
    digitalWrite(kRelayPins[id], levelFor(id, on));
    if (on)
      mask_ |= static_cast<uint8_t>(1u << id);
    else
      mask_ &= static_cast<uint8_t>(~(1u << id));
    if (!g_wd_timer) return;
    if (on) {
      esp_timer_stop(g_wd_timer);  // (re)arm for the newest pulse
      esp_timer_start_once(g_wd_timer, (ILLUM_RELAY_MAX_ON_MS + 100) * 1000ULL);
    } else if (mask_ == 0) {
      esp_timer_stop(g_wd_timer);
    }
  }

 private:
  uint8_t mask_ = 0;
} g_gpio;

RelayController g_relay(g_gpio, kRelayCount, g_relay_configured, ILLUM_RELAY_MAX_ON_MS, ILLUM_RELAY_COOLDOWN_MS);
IlluminationController g_app(g_relay, static_cast<LightMode>(ILLUM_SESSION_END_LIGHT_MODE));
NodeRuntime g_runtime(static_cast<uint8_t>(DeviceId::Illumination), g_app);
LedRenderer g_renderer;

CRGB g_status[1];
CLEDController* g_status_ctl = nullptr;

void initRelaysSafe() {
  for (uint8_t i = 0; i < kRelayCount; ++i) {
    g_relay_configured[i] = kRelayPins[i] >= 0;
    if (!g_relay_configured[i]) continue;
    // Write the inactive level first, then enable the output: no ON glitch.
    digitalWrite(kRelayPins[i], levelFor(i, false));
    pinMode(kRelayPins[i], OUTPUT);
    digitalWrite(kRelayPins[i], levelFor(i, false));
  }
  esp_timer_create_args_t args = {};
  args.callback = watchdogCb;
  args.name = "relay_wd";
  esp_timer_create(&args, &g_wd_timer);
}

void updateStatusLed(uint32_t now) {
  static uint32_t last = 0;
  if (now - last < 100 || !g_status_ctl) return;
  last = now;
  CRGB c;
  if (g_runtime.macMismatch() || !g_runtime.radioOk())
    c = ((now / 200) % 2) ? CRGB::Red : CRGB::Black;
  else if (g_relay.anyOn())
    c = CRGB(255, 80, 0);
  else if (!g_runtime.core().hubLinkUp())
    c = ((now / 500) % 2) ? CRGB(80, 0, 0) : CRGB::Black;
  else if (!g_runtime.core().sessionValid())
    c = CRGB(0, 0, 80);
  else
    c = CRGB(0, 80, 0);
  g_status[0] = c;
  g_status_ctl->showLeds(60);
}

}  // namespace

void setup() {
  initRelaysSafe();  // first thing: relays OFF
  Serial.begin(115200);
  g_status_ctl = &FastLED.addLeds<SK6812, ILLUM_STATUS_LED_PIN, GRB>(g_status, 1);

  uint32_t now = millis();
  g_app.setEmitter(&g_runtime.core());
  g_app.begin(now);
  g_renderer.begin(ILLUM_LED_MAX_BRIGHTNESS);
  g_runtime.begin();

  for (uint8_t i = 0; i < kRelayCount; ++i)
    Serial.printf("relay %u: pin=%d active_%s %s\n", i, kRelayPins[i], kRelayActiveHigh[i] ? "high" : "low",
                  g_relay_configured[i] ? "" : "(NOT CONFIGURED -> RELAY_PULSE answers NOT_CONFIGURED)");
  Serial.printf("LED strip: pin=%d count=%d max_on=%dms cooldown=%dms\n", ILLUM_LED_PIN, ILLUM_LED_COUNT,
                ILLUM_RELAY_MAX_ON_MS, ILLUM_RELAY_COOLDOWN_MS);
}

void loop() {
  uint32_t now = millis();
  g_runtime.loop();           // radio -> NodeCore -> IlluminationController
  g_app.tick(now);            // relay timing first (independent of LED drawing)
  if (g_wd_fired) {
    g_wd_fired = false;
    g_relay.allOff(now, RelayEndReason::Watchdog);
    EventData e;
    e.code = static_cast<uint8_t>(EventCode::DeviceError);
    e.reason = static_cast<uint8_t>(ErrorCode::RelayWatchdog);
    g_runtime.core().emitEvent(e, now);
    Serial.println("ERROR: relay hardware watchdog fired");
  }
  g_renderer.render(g_app.lighting(), now);
  updateStatusLed(now);
  delay(1);
}
