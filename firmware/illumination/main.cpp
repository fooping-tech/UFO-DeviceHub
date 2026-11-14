// Illumination ATOM (Device ID 0x10): WS2811 + COB tape illumination, mist
// module, optional steam relay, device button for local scenes.
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

// ---- mist relay -------------------------------------------------------------
// Independent of loop(): an esp_timer one-shot that loop() keeps re-arming while
// the mist is ON. If loop() stalls for ILLUM_MIST_WATCHDOG_MS the mist goes OFF.
constexpr int kMistPin = ILLUM_MIST_PIN;
inline uint8_t mistLevel(bool on) { return (on == (ILLUM_MIST_ACTIVE_HIGH != 0)) ? HIGH : LOW; }
esp_timer_handle_t g_mist_wd = nullptr;
volatile bool g_mist_wd_fired = false;

void mistWatchdogCb(void*) {
  if (kMistPin >= 0) digitalWrite(kMistPin, mistLevel(false));
  g_mist_wd_fired = true;
}

class GpioMist : public MistOutput {
 public:
  void setMist(bool on) override {
    if (kMistPin < 0) return;
    digitalWrite(kMistPin, mistLevel(on));
    on_ = on;
    if (!g_mist_wd) return;
    esp_timer_stop(g_mist_wd);
    if (on) esp_timer_start_once(g_mist_wd, ILLUM_MIST_WATCHDOG_MS * 1000ULL);
  }
  void kick(uint32_t now) {  // from loop(): proves loop() is alive
    if (!on_ || !g_mist_wd || now - last_kick_ < 100) return;
    last_kick_ = now;
    esp_timer_stop(g_mist_wd);
    esp_timer_start_once(g_mist_wd, ILLUM_MIST_WATCHDOG_MS * 1000ULL);
  }

 private:
  bool on_ = false;
  uint32_t last_kick_ = 0;
} g_mist_out;

MistController g_mist(g_mist_out, kMistPin >= 0, ILLUM_MIST_MAX_S * 1000UL);
IlluminationController g_app(g_relay, g_mist, static_cast<LightMode>(ILLUM_SESSION_END_LIGHT_MODE));
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

  if (kMistPin >= 0) {
    digitalWrite(kMistPin, mistLevel(false));
    pinMode(kMistPin, OUTPUT);
    digitalWrite(kMistPin, mistLevel(false));
  }
  esp_timer_create_args_t margs = {};
  margs.callback = mistWatchdogCb;
  margs.name = "mist_wd";
  esp_timer_create(&margs, &g_mist_wd);
}

// ---- button: short press = next scene, long press = mist ON/OFF ------------
uint32_t g_feedback_until = 0;
CRGB g_feedback;

void pollButton(uint32_t now) {
  if (ILLUM_BUTTON_PIN < 0) return;
  constexpr uint32_t kDebounceMs = 30, kLongMs = 800;
  static bool down = false, long_done = false;
  static uint32_t edge = 0;
  bool pressed = digitalRead(ILLUM_BUTTON_PIN) == LOW;
  ButtonResult r = ButtonResult::Ignored;
  bool acted = false;
  if (pressed && !down && now - edge > kDebounceMs) {
    down = true;
    long_done = false;
    edge = now;
  } else if (pressed && down && !long_done && now - edge >= kLongMs) {
    long_done = true;
    r = g_app.onButtonLong(now);
    acted = true;
  } else if (!pressed && down && now - edge > kDebounceMs) {
    down = false;
    edge = now;
    if (!long_done) {
      r = g_app.onButtonShort(now);
      acted = true;
    }
  }
  if (!acted) return;
  switch (r) {
    case ButtonResult::SceneChanged:
      Serial.printf("scene: %s\n", IlluminationController::kLocalScenes[g_app.localScene()].name);
      g_feedback = CRGB(0, 0, 120);
      break;
    case ButtonResult::MistOn:
      Serial.println("mist: ON (button)");
      g_feedback = CRGB(0, 160, 0);
      break;
    case ButtonResult::MistOff:
      Serial.println("mist: OFF (button)");
      g_feedback = CRGB(160, 0, 0);
      break;
    case ButtonResult::Ignored:
      Serial.println("button ignored: the PC session is in control");
      g_feedback = CRGB(80, 0, 80);
      break;
  }
  g_feedback_until = now + 300;
}

void updateStatusLed(uint32_t now) {
  static uint32_t last = 0;
  if (now - last < 100 || !g_status_ctl) return;
  last = now;
  CRGB c;
  if (static_cast<int32_t>(now - g_feedback_until) < 0)
    c = g_feedback;  // button feedback
  else if (g_runtime.macMismatch() || !g_runtime.radioOk())
    c = ((now / 200) % 2) ? CRGB::Red : CRGB::Black;
  else if (g_relay.anyOn())
    c = CRGB(255, 80, 0);
  else if (g_app.localMode())
    c = g_mist.outputOn() ? CRGB(0, 60, 60) : CRGB(40, 40, 40);  // local scenes (no PC)
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
  if (ILLUM_BUTTON_PIN >= 0) pinMode(ILLUM_BUTTON_PIN, INPUT_PULLUP);
  g_app.setEmitter(&g_runtime.core());
  LocalConfig lc;
  lc.enabled = ILLUM_LOCAL_CONTROL != 0 && ILLUM_BUTTON_PIN >= 0;
  lc.mist = ILLUM_LOCAL_MIST != 0;
  lc.brightness = ILLUM_LOCAL_BRIGHTNESS;
  g_app.setLocalConfig(lc);
  g_app.begin(now);
  g_renderer.begin(ILLUM_LED_MAX_BRIGHTNESS, ILLUM_COB_PIN, ILLUM_COB_MAX_BRIGHTNESS);
  g_renderer.colorOrderTest();
  g_runtime.begin();

  for (uint8_t i = 0; i < kRelayCount; ++i)
    Serial.printf("relay %u: pin=%d active_%s %s\n", i, kRelayPins[i], kRelayActiveHigh[i] ? "high" : "low",
                  g_relay_configured[i] ? "" : "(NOT CONFIGURED -> RELAY_PULSE answers NOT_CONFIGURED)");
  Serial.printf("LED strip: pin=%d count=%d  COB: pin=%d  max_on=%dms cooldown=%dms\n", ILLUM_LED_PIN,
                ILLUM_LED_COUNT, ILLUM_COB_PIN, ILLUM_RELAY_MAX_ON_MS, ILLUM_RELAY_COOLDOWN_MS);
  Serial.printf("mist: pin=%d active_%s max=%ds %s\n", kMistPin, ILLUM_MIST_ACTIVE_HIGH ? "high" : "low",
                ILLUM_MIST_MAX_S, kMistPin >= 0 ? "" : "(NOT CONFIGURED -> SET_MIST answers NOT_CONFIGURED)");
  Serial.println("button: short = next scene, long = mist ON/OFF (only without a PC session)");
}

void loop() {
  uint32_t now = millis();
  g_runtime.loop();           // radio -> NodeCore -> IlluminationController
  pollButton(now);
  g_app.tick(now);            // relay / mist timing first (independent of LED drawing)
  g_mist_out.kick(now);
  if (g_mist_wd_fired) {
    g_mist_wd_fired = false;
    g_mist.stop(now, RelayEndReason::Watchdog);
    EventData e;
    e.code = static_cast<uint8_t>(EventCode::DeviceError);
    e.reason = static_cast<uint8_t>(ErrorCode::RelayWatchdog);
    e.detail = 1;  // 1 = mist relay
    g_runtime.core().emitEvent(e, now);
    Serial.println("ERROR: mist hardware watchdog fired");
  }
  if (g_wd_fired) {
    g_wd_fired = false;
    g_relay.allOff(now, RelayEndReason::Watchdog);
    EventData e;
    e.code = static_cast<uint8_t>(EventCode::DeviceError);
    e.reason = static_cast<uint8_t>(ErrorCode::RelayWatchdog);
    g_runtime.core().emitEvent(e, now);
    Serial.println("ERROR: relay hardware watchdog fired");
  }
  g_renderer.render(g_app.lighting(), g_mist.outputOn(), now);
  updateStatusLed(now);
  delay(1);
}
