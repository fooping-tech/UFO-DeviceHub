// M5Dial ball receiver / dispenser (Device ID 0x20 or 0x21 via -DUFO_DEVICE_ID).
// Same firmware for both units; behaviour is driven only by commands from the PC.
//
// Hardware (NeoGacha): SG90 on G13 (PORT.A), NeoPixel ring on G1 (PORT.B),
// ball switch on G2 (PORT.B, next to the LED). Switch ON = ball received.
//
// Device button:
//   hold DIAL_SETTINGS_HOLD_MS   enter / leave the settings mode (hardware test)
//   in settings: turn = choose, press = run
//     SERVO TEST   release sequence (release -> hold -> standby)
//     SERVO ANGLE  press, turn to move the horn (5 deg per step), press to return
//     LED TEST     press: next colour / pattern
//     SWITCH TEST  live state + ON count (beep on each ON), press: reset count
//     EXIT
//
// LEDs (game mode, docs/game-mode-interface.md): the front LED (G1) shows the
// device state; the PC can run SET_DIAL_EFFECT on it and on the second LED on
// G15 (NeoGacha LED_PIN1 / topLed, DIAL_TRANSFER_LED_PIN). Both are laid out
// "up from the bottom, then down from the top" (two columns). A detected ball
// flashes white at once and then runs the unit colour towards the screen.
//
// Power: the servo ramps (DIAL_SERVO_SLEW_DEG_PER_S), is released at rest
// (DIAL_SERVO_DETACH_MS), waits DIAL_SERVO_BOOT_DELAY_MS after power up, and
// while it runs the LED ring is dimmed and the speaker stays quiet.
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

// Older config/hardware.h files: defaults for the newer settings.
#ifndef DIAL_SERVO_SLEW_DEG_PER_S
#define DIAL_SERVO_SLEW_DEG_PER_S 240
#endif
#ifndef DIAL_SERVO_DETACH_MS
#define DIAL_SERVO_DETACH_MS 500
#endif
#ifndef DIAL_SERVO_BOOT_DELAY_MS
#define DIAL_SERVO_BOOT_DELAY_MS 1000
#endif
#ifndef DIAL_LED_BRIGHTNESS_SERVO
#define DIAL_LED_BRIGHTNESS_SERVO 6
#endif
#ifndef DIAL_SETTINGS_HOLD_MS
#define DIAL_SETTINGS_HOLD_MS 2000
#endif
#ifndef DIAL_SETTINGS_TIMEOUT_S
#define DIAL_SETTINGS_TIMEOUT_S 300
#endif
#ifndef DIAL_RECEIVER_PULSE_STRETCH_MS
#define DIAL_RECEIVER_PULSE_STRETCH_MS 80
#endif
#ifndef DIAL_ENCODER_COUNTS_PER_STEP
#define DIAL_ENCODER_COUNTS_PER_STEP 4
#endif
// Game mode LEDs
#ifndef DIAL_UNIT_COLOR_1
#define DIAL_UNIT_COLOR_1 {0, 255, 255}  // dial 1 (0x20, left of the screen): cyan
#endif
#ifndef DIAL_UNIT_COLOR_2
#define DIAL_UNIT_COLOR_2 {255, 0, 255}  // dial 2 (0x21, right of the screen): magenta
#endif
#ifndef DIAL_RX_FLASH_MS
#define DIAL_RX_FLASH_MS 150
#endif
#ifndef DIAL_RX_CHASE_MS
#define DIAL_RX_CHASE_MS 1500
#endif
#ifndef DIAL_LED_FOLDED
#define DIAL_LED_FOLDED 1
#endif
#ifndef DIAL_LED_REVERSED
#define DIAL_LED_REVERSED 0
#endif
#ifndef DIAL_TRANSFER_LED_PIN
#define DIAL_TRANSFER_LED_PIN -1
#endif
#ifndef DIAL_TRANSFER_LED_COUNT
#define DIAL_TRANSFER_LED_COUNT 26
#endif
#ifndef DIAL_TRANSFER_LED_FOLDED
#define DIAL_TRANSFER_LED_FOLDED 1
#endif
#ifndef DIAL_TRANSFER_LED_CHIPSET
#define DIAL_TRANSFER_LED_CHIPSET WS2812B
#endif
#ifndef DIAL_TRANSFER_LED_COLOR_ORDER
#define DIAL_TRANSFER_LED_COLOR_ORDER GRB
#endif
#ifndef DIAL_TRANSFER_LED_REVERSED
#define DIAL_TRANSFER_LED_REVERSED 0
#endif
#ifndef DIAL_TRANSFER_LED_BRIGHTNESS
#define DIAL_TRANSFER_LED_BRIGHTNESS 25
#endif
#ifndef DIAL_TRANSFER_LED_SEPARATE_POWER
#define DIAL_TRANSFER_LED_SEPARATE_POWER 0
#endif

using namespace ufo;

namespace {

// SG90 with deferred attach (boot) and release (pulses off at rest).
class SG90 : public ServoOutput {
 public:
  void writeAngle(uint8_t deg) override {
#if DIAL_SERVO_PIN >= 0
    pending_ = deg;
    has_pending_ = true;
    if (ready_) apply();
#endif
  }
  void release() override {
#if DIAL_SERVO_PIN >= 0
    if (!attached_) return;
    servo_.detach();
    pinMode(DIAL_SERVO_PIN, OUTPUT);
    digitalWrite(DIAL_SERVO_PIN, LOW);  // no floating pin -> no random twitching
    attached_ = false;
#endif
  }
  // Called every loop: starts the servo once the boot delay is over.
  void service(uint32_t now, uint32_t ready_at) {
    if (!ready_ && now >= ready_at) {
      ready_ = true;
      if (has_pending_) apply();
    }
  }

 private:
  void apply() {
#if DIAL_SERVO_PIN >= 0
    if (!attached_) {
      servo_.setPeriodHertz(DIAL_SERVO_PERIOD_HZ);
      servo_.attach(DIAL_SERVO_PIN, DIAL_SERVO_MIN_US, DIAL_SERVO_MAX_US);
      attached_ = true;
    }
    servo_.write(pending_);
    has_pending_ = false;
#endif
  }
  Servo servo_;
  uint8_t pending_ = 0;
  bool has_pending_ = false;
  bool ready_ = false;
  bool attached_ = false;
} g_sg90;

DispenserConfig makeDispenserConfig() {
  DispenserConfig c;
  c.configured = DIAL_SERVO_PIN >= 0;
  c.standby_deg = DIAL_SERVO_STANDBY_DEG;
  c.release_deg = DIAL_SERVO_RELEASE_DEG;
  c.hold_ms = DIAL_SERVO_HOLD_MS;
  c.return_settle_ms = DIAL_SERVO_RETURN_SETTLE_MS;
  c.slew_deg_per_s = DIAL_SERVO_SLEW_DEG_PER_S;
  c.detach_after_ms = DIAL_SERVO_DETACH_MS;
  return c;
}

DialConfig makeDialConfig() {
  DialConfig c;
  c.abort_release_on_link_loss = DIAL_LINK_LOSS_ABORT_RELEASE != 0;
  c.sim_button = DIAL_RECEIVER_SIM_BUTTON != 0;
  const uint8_t color1[3] = DIAL_UNIT_COLOR_1;
  const uint8_t color2[3] = DIAL_UNIT_COLOR_2;
  const uint8_t* color = UFO_DEVICE_ID == static_cast<uint8_t>(DeviceId::M5Dial2) ? color2 : color1;
  c.color_r = color[0];
  c.color_g = color[1];
  c.color_b = color[2];
  c.rx_flash_ms = DIAL_RX_FLASH_MS;
  c.rx_chase_ms = DIAL_RX_CHASE_MS;
  c.transfer_led = DIAL_TRANSFER_LED_PIN >= 0;
  return c;
}

Dispenser g_dispenser(g_sg90, makeDispenserConfig());
BallReceiver g_receiver(DIAL_RECEIVER_DEBOUNCE_MS);
DialController g_app(g_dispenser, g_receiver, makeDialConfig());
NodeRuntime g_runtime(UFO_DEVICE_ID, g_app);
DialDisplay g_display;
uint32_t g_servo_ready_at = 0;

CRGB g_leds[DIAL_LED_COUNT];
CLEDController* g_led_ctl = nullptr;
fx::PacificaState g_pacifica;
// Second LED on G15 (NeoGacha topLed), game mode target TRANSFER.
#if DIAL_TRANSFER_LED_PIN >= 0
CRGB g_strip[DIAL_TRANSFER_LED_COUNT];
CLEDController* g_strip_ctl = nullptr;
#endif

// ---- settings mode (UI state; the safety rules live in DialController) ----
const char* const kLedPatterns[] = {"RED", "GREEN", "BLUE", "WHITE", "RAINBOW", "CHASE", "OFF"};
constexpr uint8_t kLedPatternCount = sizeof(kLedPatterns) / sizeof(kLedPatterns[0]);

struct SettingsUi {
  SettingsItem item = SettingsItem::Release;
  bool adjusting = false;
  uint8_t angle = DIAL_SERVO_STANDBY_DEG;
  uint8_t led = 0;
  uint16_t last_count = 0;
  uint32_t last_input = 0;
} g_set;

int32_t g_enc_last = 0;
bool g_long_done = false;

// The capsule only taps the switch: the ON pulse can be a few ms, shorter than
// the debounce time and shorter than one loop() while the screen redraws. A GPIO
// interrupt latches the ON edge; the loop then sees the switch ON for at least
// DIAL_RECEIVER_PULSE_STRETCH_MS (counted from when the loop noticed it), so a
// tap of any length becomes exactly one debounced OFF->ON edge.
volatile bool g_sw_pulse = false;
volatile uint32_t g_sw_pulses = 0;
uint32_t g_sw_stretch_until = 0;

void IRAM_ATTR onSwitchEdge() {
  g_sw_pulse = true;
  ++g_sw_pulses;
}

const char* resetReasonText(esp_reset_reason_t r) {
  switch (r) {
    case ESP_RST_POWERON: return "power on";
    case ESP_RST_BROWNOUT: return "BROWNOUT (supply voltage dropped - servo current?)";
    case ESP_RST_PANIC: return "PANIC (firmware crash)";
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT: return "WATCHDOG (firmware hang)";
    case ESP_RST_SW: return "software restart";
    case ESP_RST_EXT: return "reset pin";
    default: return "other";
  }
}

bool readSwitch() {
  bool on = false;
#if DIAL_RECEIVER_PIN >= 0
  int level = digitalRead(DIAL_RECEIVER_PIN);
  on = DIAL_RECEIVER_ACTIVE_LOW ? (level == LOW) : (level == HIGH);
  uint32_t now = millis();
  if (g_sw_pulse) {
    g_sw_pulse = false;
    g_sw_stretch_until = now + DIAL_RECEIVER_PULSE_STRETCH_MS;
  }
  if (static_cast<int32_t>(g_sw_stretch_until - now) > 0) on = true;
#endif
#if DIAL_RECEIVER_SIM_BUTTON
  on = on || M5Dial.BtnA.isPressed();
#endif
  return on;
}

void beep(uint16_t hz, uint32_t ms) {
  if (g_dispenser.moving()) return;  // speaker + servo together = current peak
  M5Dial.Speaker.tone(hz, ms);
}

void enterSettings(uint32_t now) {
  if (!g_app.enterSettings(now)) {
    beep(1000, 300);  // a game cycle or a release is running
    return;
  }
  g_set = SettingsUi();
  g_set.last_input = now;
  g_set.angle = g_dispenser.angle();
  g_enc_last = M5Dial.Encoder.read();
  beep(5000, 80);
  Serial.println("settings mode: on");
}

void exitSettings(uint32_t now) {
  g_app.exitSettings(now);
  g_set.adjusting = false;
  beep(3000, 80);
  Serial.println("settings mode: off");
}

void settingsTurn(int steps, uint32_t now) {
  if (g_set.adjusting) {
    int a = static_cast<int>(g_set.angle) + steps * 5;
    a = a < 0 ? 0 : (a > 180 ? 180 : a);
    g_set.angle = static_cast<uint8_t>(a);
    g_app.settingsServoAngle(g_set.angle, now);
    return;
  }
  const int n = static_cast<int>(SettingsItem::Count);
  int i = (static_cast<int>(g_set.item) + steps) % n;
  if (i < 0) i += n;
  g_set.item = static_cast<SettingsItem>(i);
  beep(6000, 15);
}

void settingsPress(uint32_t now) {
  switch (g_set.item) {
    case SettingsItem::Release:
      if (!g_app.settingsReleaseTest(now)) beep(1000, 200);
      break;
    case SettingsItem::Angle:
      if (g_set.adjusting) {
        g_set.adjusting = false;
        g_app.settingsServoHome(now);
      } else if (g_app.settingsServoAngle(g_dispenser.angle(), now)) {
        g_set.adjusting = true;
        g_set.angle = g_dispenser.angle();
      } else {
        beep(1000, 200);  // the release test is still running
      }
      break;
    case SettingsItem::Led: g_set.led = (g_set.led + 1) % kLedPatternCount; break;
    case SettingsItem::Switch: g_app.resetSettingsSwitchCount(); break;
    case SettingsItem::Exit: exitSettings(now); break;
    default: break;
  }
}

void pollInput(uint32_t now) {
  auto& b = M5Dial.BtnA;
#if !DIAL_RECEIVER_SIM_BUTTON
  if (b.wasPressed()) g_long_done = false;
  if (b.isPressed() && !g_long_done && b.pressedFor(DIAL_SETTINGS_HOLD_MS)) {
    g_long_done = true;
    if (g_app.inSettings())
      exitSettings(now);
    else
      enterSettings(now);
  }
  if (b.wasReleased() && !g_long_done && g_app.inSettings()) {
    g_set.last_input = now;
    settingsPress(now);
  }
#endif
  int32_t pos = M5Dial.Encoder.read();
  int32_t diff = pos - g_enc_last;
  int steps = diff / DIAL_ENCODER_COUNTS_PER_STEP;
  if (steps != 0) {
    g_enc_last += steps * DIAL_ENCODER_COUNTS_PER_STEP;
    if (g_app.inSettings()) {
      g_set.last_input = now;
      settingsTurn(steps, now);
    }
  }
  if (g_app.inSettings()) {
    uint16_t c = g_app.settingsSwitchCount();
    if (c != g_set.last_count) {
      g_set.last_count = c;
      g_set.last_input = now;
      beep(6000, 120);  // switch ON
      Serial.printf("settings: switch ON (#%u)\n", c);
    }
    if (DIAL_SETTINGS_TIMEOUT_S > 0 && now - g_set.last_input > DIAL_SETTINGS_TIMEOUT_S * 1000UL) exitSettings(now);
  }
}

void renderSettingsLed(uint32_t now) {
  const uint16_t n = DIAL_LED_COUNT;
  switch (g_set.led) {
    case 0: fill_solid(g_leds, n, CRGB::Red); break;
    case 1: fill_solid(g_leds, n, CRGB::Green); break;
    case 2: fill_solid(g_leds, n, CRGB::Blue); break;
    case 3: fill_solid(g_leds, n, CRGB::White); break;
    case 4: fx::rainbow(g_leds, n, now * 3, 255); break;
    case 5: fx::chase(g_leds, n, now, CRGB(0, 180, 255), 200); break;
    default: fill_solid(g_leds, n, CRGB::Black); break;
  }
}

// Draws one effect frame: every pixel = colour x level at its path position.
void drawEffect(const LedPlan& p, CRGB* leds, const LedGeometry& g) {
  const uint16_t len = ledPathLength(g);
  for (uint16_t i = 0; i < g.count; i++) {
    uint8_t lv = dialEffectLevel(p.effect, p.direction, ledPathPos(g, i), len, p.elapsed_ms, p.duration_ms);
    leds[i] = lv ? CRGB(p.r, p.g, p.b).nscale8_video(lv) : CRGB::Black;
  }
}

void renderLed(uint32_t now) {
  static uint32_t last = 0;
  if (now - last < 20) return;
  last = now;
  const bool servo = g_dispenser.busy();
  if (g_led_ctl) {
    const uint16_t n = DIAL_LED_COUNT;
    LedGeometry front;
    front.count = n;
    front.layout = DIAL_LED_FOLDED ? LedLayout::Folded : LedLayout::Linear;
    front.reversed = DIAL_LED_REVERSED != 0;
    LedPlan p = g_app.ledPlan(DialLedTarget::Front, now);
    const CRGB unit(p.r, p.g, p.b);
    switch (p.scene) {
      case LedScene::Settings: renderSettingsLed(now); break;
      case LedScene::LinkLost: fx::breathe(g_leds, n, now, CRGB(120, 0, 0), 2000); break;
      case LedScene::Error: fx::blink(g_leds, n, now, CRGB::Red, 500); break;
      case LedScene::Idle: fx::pacifica(g_leds, n, g_pacifica, now); break;  // NeoGacha MODE_STOP
      case LedScene::Ready: fx::breathe(g_leds, n, now, CRGB(80, 80, 80), 3000); break;
      case LedScene::Armed: fx::chase(g_leds, n, now, unit, 200); break;
      case LedScene::Complete: fx::fadeAll(g_leds, n, 245); break;  // NeoGacha fade()
      case LedScene::Effect: drawEffect(p, g_leds, front); break;
      case LedScene::Off: fill_solid(g_leds, n, CRGB::Black); break;
    }
    uint8_t bri = DIAL_LED_BRIGHTNESS;
    if (servo && bri > DIAL_LED_BRIGHTNESS_SERVO) bri = DIAL_LED_BRIGHTNESS_SERVO;
    g_led_ctl->showLeds(bri);
  }
#if DIAL_TRANSFER_LED_PIN >= 0
  if (g_strip_ctl) {
    LedGeometry strip;
    strip.count = DIAL_TRANSFER_LED_COUNT;
    strip.layout = DIAL_TRANSFER_LED_FOLDED ? LedLayout::Folded : LedLayout::Linear;
    strip.reversed = DIAL_TRANSFER_LED_REVERSED != 0;
    LedPlan p = g_app.ledPlan(DialLedTarget::Transfer, now);
    if (p.scene == LedScene::Effect)
      drawEffect(p, g_strip, strip);
    else
      fill_solid(g_strip, DIAL_TRANSFER_LED_COUNT, CRGB::Black);
    uint8_t bri = DIAL_TRANSFER_LED_BRIGHTNESS;
#if !DIAL_TRANSFER_LED_SEPARATE_POWER
    // fed from the M5Dial 5 V: same power budget as the front LED while the servo runs
    if (servo && bri > DIAL_LED_BRIGHTNESS_SERVO) bri = DIAL_LED_BRIGHTNESS_SERVO;
#endif
    g_strip_ctl->showLeds(bri);
  }
#endif
}

// Serial trace of the PC effects (matches the PC side trace by command_id).
void logLedEffects() {
  static uint32_t last_cmd[2] = {0, 0};
  static DialEffect last_fx[2] = {DialEffect::None, DialEffect::None};
  const DialLedTarget targets[2] = {DialLedTarget::Front, DialLedTarget::Transfer};
  for (int i = 0; i < 2; ++i) {
    const DialLedSlot& s = g_app.ledSlot(targets[i]);
    if (s.effect == last_fx[i] && s.command_id == last_cmd[i]) continue;
    last_fx[i] = s.effect;
    last_cmd[i] = s.command_id;
    Serial.printf("led %s: effect=%u dir=%u rgb=%u,%u,%u ms=%u prio=%u cmd=%lu cycle=%u\n", i ? "transfer" : "front",
                  static_cast<unsigned>(s.effect), static_cast<unsigned>(s.direction), s.r, s.g, s.b, s.duration_ms,
                  s.priority, static_cast<unsigned long>(s.command_id), g_app.cycle());
  }
}

void beepOnStateChange() {
  static DialState last = DialState::Disabled;
  DialState s = g_app.dialState();
  if (s == last) return;
  switch (s) {
    case DialState::Armed: beep(4000, 60); break;
    case DialState::BallDetected: beep(6000, 120); break;
    // RELEASING: no beep - the servo starts at the same moment
    case DialState::Complete: beep(7000, 80); break;
    case DialState::Error: beep(1000, 400); break;
    default: break;
  }
  last = s;
}

}  // namespace

void setup() {
  auto cfg = M5.config();
  M5Dial.begin(cfg, true, false);  // encoder on, RFID off
  Serial.begin(115200);

#if DIAL_RECEIVER_PIN >= 0
  pinMode(DIAL_RECEIVER_PIN, DIAL_RECEIVER_PULLUP ? INPUT_PULLUP : INPUT);
  attachInterrupt(digitalPinToInterrupt(DIAL_RECEIVER_PIN), onSwitchEdge, DIAL_RECEIVER_ACTIVE_LOW ? FALLING : RISING);
#endif
#if DIAL_SERVO_PIN >= 0
  pinMode(DIAL_SERVO_PIN, OUTPUT);
  digitalWrite(DIAL_SERVO_PIN, LOW);  // quiet until the servo is attached
#endif

#if DIAL_LED_PIN >= 0
  g_led_ctl = &FastLED.addLeds<NEOPIXEL, DIAL_LED_PIN>(g_leds, DIAL_LED_COUNT);
#endif
#if DIAL_TRANSFER_LED_PIN >= 0
  g_strip_ctl = &FastLED.addLeds<DIAL_TRANSFER_LED_CHIPSET, DIAL_TRANSFER_LED_PIN, DIAL_TRANSFER_LED_COLOR_ORDER>(
      g_strip, DIAL_TRANSFER_LED_COUNT);
#endif
  FastLED.clear(true);

  uint32_t now = millis();
  M5Dial.update();
  g_app.setEmitter(&g_runtime.core());
  g_app.begin(readSwitch(), now);  // DISABLED, servo -> standby (applied after the boot delay)
  g_runtime.begin();               // radio start-up current first ...
  g_servo_ready_at = millis() + DIAL_SERVO_BOOT_DELAY_MS;  // ... then the servo
  g_display.begin(g_runtime.selfMacStr());
  g_enc_last = M5Dial.Encoder.read();
  if (g_runtime.macMismatch()) Serial.println("WARNING: this unit's MAC is not the one configured for this ID");
#if DIAL_RECEIVER_PIN < 0
  Serial.println("receiver switch pin not configured: only the simulation button can trigger BALL_RECEIVED");
#else
  Serial.printf("receiver switch: G%d %s, servo slew %d deg/s, release at rest after %d ms\n", DIAL_RECEIVER_PIN,
                DIAL_RECEIVER_ACTIVE_LOW ? "active LOW (to GND)" : "active HIGH", DIAL_SERVO_SLEW_DEG_PER_S,
                DIAL_SERVO_DETACH_MS);
#endif
  {
    uint8_t r, g, b;
    g_app.unitColor(r, g, b);
    Serial.printf("unit colour %u,%u,%u; transfer strip: ", r, g, b);
    if (DIAL_TRANSFER_LED_PIN >= 0)
      Serial.printf("G%d x%d\n", DIAL_TRANSFER_LED_PIN, DIAL_TRANSFER_LED_COUNT);
    else
      Serial.println("not configured (front LED only)");
  }
  Serial.printf("settings mode: hold the button %d ms\n", DIAL_SETTINGS_HOLD_MS);
  Serial.printf("reset reason: %s\n", resetReasonText(esp_reset_reason()));
  M5Dial.Speaker.tone(6000, 100);
}

void loop() {
  M5Dial.update();
  uint32_t now = millis();
  g_sg90.service(now, g_servo_ready_at);
  g_runtime.loop();               // radio -> NodeCore -> DialController commands
  pollInput(now);                 // button / encoder (settings mode)
  g_app.tick(readSwitch(), now);  // receiver edge + servo sequence (non-blocking)
  {
    static DialState prev = DialState::Disabled;
    static bool prev_on = false;
    bool on = g_receiver.stableOn();
    if (on && !prev_on)
      Serial.printf("switch ON (state %s%s)\n", dialStateLabel(g_app.dialState()),
                    prev == DialState::Armed && g_app.dialState() == DialState::BallDetected ? " -> BALL_RECEIVED sent"
                    : g_app.dialState() == DialState::Armed || g_app.dialState() == DialState::BallDetected ? ""
                                                                    : ", not armed: not reported");
    prev_on = on;
    prev = g_app.dialState();
  }

  if (g_app.inSettings()) {
    SettingsModel s;
    s.item = g_set.item;
    s.adjusting = g_set.adjusting;
    s.servo_deg = g_dispenser.angle();
    s.servo_busy = g_dispenser.busy() && !g_set.adjusting;
    s.led_pattern = kLedPatterns[g_set.led];
    s.switch_on = g_receiver.stableOn();
    s.switch_count = g_app.settingsSwitchCount();
    s.receiver_pin = DIAL_RECEIVER_PIN;
    g_display.updateSettings(s);
  } else {
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
  }
  beepOnStateChange();
  logLedEffects();
  renderLed(now);
  delay(1);
}
