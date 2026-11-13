// M5Dial ball receiver / dispenser (Device ID 0x20 or 0x21 via -DUFO_DEVICE_ID).
// Same firmware for both units; behaviour is driven only by commands from the PC.
#include <Arduino.h>
#include <ESP32Servo.h>
#include <FastLED.h>
#include <M5Dial.h>

#include "device_ids.h"
#include "dial_controller.h"
#include "display.h"
#include "led_effects.h"
#include "node_runtime.h"
#include "ufo_config.h"

#ifndef UFO_DEVICE_ID
#error "UFO_DEVICE_ID must be set (0x20 or 0x21) - see platformio.ini"
#endif

using namespace ufo;

namespace {

Servo g_servo;

class SG90 : public ServoOutput {
 public:
  void writeAngle(uint8_t deg) override { g_servo.write(deg); }
} g_sg90;

DispenserConfig makeDispenserConfig() {
  DispenserConfig c;
  c.configured = DIAL_SERVO_PIN >= 0;
  c.standby_deg = DIAL_SERVO_STANDBY_DEG;
  c.release_deg = DIAL_SERVO_RELEASE_DEG;
  c.hold_ms = DIAL_SERVO_HOLD_MS;
  c.return_settle_ms = DIAL_SERVO_RETURN_SETTLE_MS;
  return c;
}

DialConfig makeDialConfig() {
  DialConfig c;
  c.abort_release_on_link_loss = DIAL_LINK_LOSS_ABORT_RELEASE != 0;
  c.sim_button = DIAL_RECEIVER_SIM_BUTTON != 0;
  return c;
}

Dispenser g_dispenser(g_sg90, makeDispenserConfig());
BallReceiver g_receiver(DIAL_RECEIVER_DEBOUNCE_MS);
DialController g_app(g_dispenser, g_receiver, makeDialConfig());
NodeRuntime g_runtime(UFO_DEVICE_ID, g_app);
DialDisplay g_display;

CRGB g_leds[DIAL_LED_COUNT];
CLEDController* g_led_ctl = nullptr;
fx::PacificaState g_pacifica;

bool readSwitch() {
  bool on = false;
#if DIAL_RECEIVER_PIN >= 0
  int level = digitalRead(DIAL_RECEIVER_PIN);
  on = DIAL_RECEIVER_ACTIVE_LOW ? (level == LOW) : (level == HIGH);
#endif
#if DIAL_RECEIVER_SIM_BUTTON
  on = on || M5Dial.BtnA.isPressed();
#endif
  return on;
}

void renderLed(uint32_t now) {
  static uint32_t last = 0;
  if (!g_led_ctl || now - last < 20) return;
  last = now;
  const uint16_t n = DIAL_LED_COUNT;
  if (!g_runtime.core().hubLinkUp()) {
    fx::breathe(g_leds, n, now, CRGB(120, 0, 0), 2000);
  } else {
    switch (g_app.dialState()) {
      case DialState::Disabled: fx::pacifica(g_leds, n, g_pacifica, now); break;  // NeoGacha MODE_STOP
      case DialState::Ready: fx::breathe(g_leds, n, now, CRGB(80, 80, 80), 3000); break;
      case DialState::Armed: fx::chase(g_leds, n, now, CRGB(0, 180, 255), 200); break;
      case DialState::BallDetected: fx::cylon(g_leds, n, now, 230); break;  // NeoGacha clyon()
      case DialState::Releasing:
      case DialState::Returning: fx::rainbow(g_leds, n, now * 3, 255); break;
      case DialState::Complete: fx::fadeAll(g_leds, n, 245); break;  // NeoGacha fade()
      case DialState::Error: fx::blink(g_leds, n, now, CRGB::Red, 500); break;
    }
  }
  g_led_ctl->showLeds(DIAL_LED_BRIGHTNESS);
}

void beepOnStateChange() {
  static DialState last = DialState::Disabled;
  DialState s = g_app.dialState();
  if (s == last) return;
  switch (s) {
    case DialState::Armed: M5Dial.Speaker.tone(4000, 60); break;
    case DialState::BallDetected: M5Dial.Speaker.tone(6000, 120); break;
    case DialState::Releasing: M5Dial.Speaker.tone(3000, 200); break;
    case DialState::Complete: M5Dial.Speaker.tone(7000, 80); break;
    case DialState::Error: M5Dial.Speaker.tone(1000, 400); break;
    default: break;
  }
  last = s;
}

}  // namespace

void setup() {
  auto cfg = M5.config();
  M5Dial.begin(cfg, true, false);  // encoder on, RFID off
  Serial.begin(115200);

  // SG90 exactly as NeoGacha: 50 Hz, 500-2400 us, then straight to standby.
#if DIAL_SERVO_PIN >= 0
  g_servo.setPeriodHertz(DIAL_SERVO_PERIOD_HZ);
  g_servo.attach(DIAL_SERVO_PIN, DIAL_SERVO_MIN_US, DIAL_SERVO_MAX_US);
#endif

#if DIAL_RECEIVER_PIN >= 0
  pinMode(DIAL_RECEIVER_PIN, DIAL_RECEIVER_PULLUP ? INPUT_PULLUP : INPUT);
#endif

#if DIAL_LED_PIN >= 0
  g_led_ctl = &FastLED.addLeds<NEOPIXEL, DIAL_LED_PIN>(g_leds, DIAL_LED_COUNT);
#endif

  uint32_t now = millis();
  M5Dial.update();
  g_app.setEmitter(&g_runtime.core());
  g_app.begin(readSwitch(), now);  // DISABLED, servo at standby
  g_runtime.begin();
  g_display.begin(g_runtime.selfMacStr());
  if (g_runtime.macMismatch()) Serial.println("WARNING: this unit's MAC is not the one configured for this ID");
#if DIAL_RECEIVER_PIN < 0
  Serial.println("receiver switch pin not configured: only the simulation button can trigger BALL_RECEIVED");
#endif
  M5Dial.Speaker.tone(6000, 100);
}

void loop() {
  M5Dial.update();
  uint32_t now = millis();
  g_runtime.loop();               // radio -> NodeCore -> DialController commands
  g_app.tick(readSwitch(), now);  // receiver edge + servo sequence (non-blocking)

  DisplayModel m;
  m.device_id = UFO_DEVICE_ID;
  m.dial_number = UFO_DEVICE_ID == static_cast<uint8_t>(DeviceId::M5Dial1) ? 1 : 2;
  m.link = g_runtime.core().hubLinkUp();
  m.session = g_runtime.core().sessionValid();
  m.state = g_app.dialState();
  m.op = g_app.operationMode();
  m.cycle = g_app.cycle();
  m.switch_on = g_receiver.stableOn();
  m.sim_button = DIAL_RECEIVER_SIM_BUTTON != 0;
  m.example_keys = usingExampleKeys();
  m.mac_mismatch = g_runtime.macMismatch();
  m.radio_ok = g_runtime.radioOk();
  m.error = static_cast<ErrorCode>(g_app.errorCode());
  g_display.update(m);
  beepOnStateChange();
  renderLed(now);
  delay(1);
}
