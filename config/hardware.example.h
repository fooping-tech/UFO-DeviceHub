// Hardware pin / actuator configuration.
//   cp config/hardware.example.h config/hardware.h   (git-ignored) and edit.
// Values marked UNVERIFIED are not confirmed on the real hardware. Values that
// would move an actuator are disabled (-1) until set explicitly.
#pragma once

// ---------------- Hub (M5AtomS3R, USB to the PC) ----------------
// The hub shows its status on the AtomS3R screen (system number, channel, PC
// session, devices online). Built with -DUFO_HUB_DISPLAY=1 (env hub_s1 / hub_s2).
#define HUB_SERIAL_BAUD 115200   // USB CDC: the value does not matter, kept for USB-UART boards
#define HUB_STATUS_LED_PIN 27    // only for an ATOM Lite hub (no display): built-in SK6812

// ---------------- Illumination ATOM ----------------
// ATOM Lite + Atomic PWM Base + Unit 2Relay (the former atom_led_mist setup):
//   COB tape (warm white, 12V)  Atomic PWM Base, PWM input = G22
//   WS2811 tape (12V, 70 ICs)   data = G21 (the base's "21" hole)
//   mist module (5V)            Unit 2Relay RELAY-2, Grove yellow = G32
//   button                      ATOM built-in, G39
#define ILLUM_STATUS_LED_PIN 27        // ATOM Lite built-in SK6812
#define ILLUM_LED_PIN 21               // WS2811 data
#define ILLUM_LED_COUNT 70             // 14 IC/m x 5 m
#define ILLUM_LED_CHIPSET WS2811       // FastLED chipset (WS2812B for 5V pixel strips)
#define ILLUM_LED_COLOR_ORDER GRB      // boot test: red -> green -> blue means the order is right
#define ILLUM_LED_MAX_BRIGHTNESS 200   // global cap (0-255) for power budget
// COB tape through the Atomic PWM Base. -1 = no COB tape.
#define ILLUM_COB_PIN 22
#define ILLUM_COB_PWM_FREQ 10000
#define ILLUM_COB_PWM_BITS 10
#define ILLUM_COB_LEDC_CHANNEL 0       // Arduino-ESP32 2.x only
#define ILLUM_COB_MAX_BRIGHTNESS 255   // COB cap (0-255); lower it if too bright
// Mist module relay. -1 = not configured -> SET_MIST answers NOT_CONFIGURED.
#define ILLUM_MIST_PIN 32
#define ILLUM_MIST_ACTIVE_HIGH 1       // 1: HIGH = ON (swap if the relay works inverted)
#define ILLUM_MIST_MAX_S 600           // one mist run lasts at most 10 min (dry-run protection)
#define ILLUM_MIST_WATCHDOG_MS 1000    // mist forced OFF if loop() stalls this long
// Device button: local scenes when no PC session. -1 = no button.
#define ILLUM_BUTTON_PIN 39
#define ILLUM_LOCAL_CONTROL 1          // 1: short press = next scene
#define ILLUM_LOCAL_MIST 1             // 1: local scenes run the mist (long press toggles)
#define ILLUM_LOCAL_BRIGHTNESS 160     // WS2811 brightness of the local scenes
// Steam generator relay(s) for short game pulses (RELAY_PULSE). -1 = not configured.
// The other Unit 2Relay channel (Grove white = G26) can be used here.
#define ILLUM_RELAY_COUNT 1
#define ILLUM_RELAY_PINS {-1}          // UNVERIFIED: e.g. {26}
#define ILLUM_RELAY_ACTIVE_HIGH {1}    // 1: HIGH = ON, 0: LOW = ON (relay module polarity)
#define ILLUM_RELAY_MAX_ON_MS 3000     // hard limit for one pulse
#define ILLUM_RELAY_COOLDOWN_MS 2000   // re-fire prohibited after a pulse ends
#define ILLUM_SESSION_END_LIGHT_MODE 1 // LightMode applied when the PC session ends (1 = IDLE)

// ---------------- M5Dial ----------------
// SG90 (from NeoGacha M5Dial_Gacha: GPIO13, 50Hz, 500-2400us,
// standby 180deg, release 0deg held ~1500ms, then back to 180deg).
#define DIAL_SERVO_PIN 13
#define DIAL_SERVO_PERIOD_HZ 50
#define DIAL_SERVO_MIN_US 500
#define DIAL_SERVO_MAX_US 2400
#define DIAL_SERVO_STANDBY_DEG 180
#define DIAL_SERVO_RELEASE_DEG 0
#define DIAL_SERVO_HOLD_MS 1500
#define DIAL_SERVO_RETURN_SETTLE_MS 600  // UNVERIFIED: time for the horn to get back to standby
// Power: the SG90 inrush / stall current (up to ~0.7 A) can brown out the M5Dial.
// The servo moves in small steps at this speed instead of one 180 deg jump
// (0 = full speed). Hold / settle times are counted after the ramp ends.
#define DIAL_SERVO_SLEW_DEG_PER_S 240    // 180 deg in 0.75 s (SG90 full speed ~600 deg/s)
// Stop the servo pulses this long after it reached standby (0 = keep holding).
// No holding current / jitter at rest; the next motion re-attaches the servo.
#define DIAL_SERVO_DETACH_MS 500
// The first servo move after power up waits this long (radio start-up current).
#define DIAL_SERVO_BOOT_DELAY_MS 1000
// 1: on hub link loss, a running release returns to standby immediately.
// 0: the running sequence finishes normally (hold, then return).
#define DIAL_LINK_LOSS_ABORT_RELEASE 0

// Ball receiver switch: PORT.B, the pin next to the LED (G2). Switch ON = ball in.
// -1 = not wired (use the simulation button).
#define DIAL_RECEIVER_PIN 2
#define DIAL_RECEIVER_ACTIVE_LOW 1     // 1: the switch connects the pin to GND (internal pull-up)
#define DIAL_RECEIVER_PULLUP 1
#define DIAL_RECEIVER_DEBOUNCE_MS 30
// The capsule only taps the switch (ON for a few ms): an interrupt catches the
// ON edge and the switch then counts as ON for this long (> DEBOUNCE_MS).
#define DIAL_RECEIVER_PULSE_STRETCH_MS 80
// 1: the M5Dial button (BtnA) ALSO simulates the receiver switch (development
// only; it then cannot open the settings mode).
#define DIAL_RECEIVER_SIM_BUTTON 0

// Settings mode (hardware test): hold the M5Dial button this long while no game
// cycle runs. Turn the dial to pick SERVO / ANGLE / LED / SWITCH / EXIT, press to run.
#define DIAL_SETTINGS_HOLD_MS 2000
#define DIAL_SETTINGS_TIMEOUT_S 300    // back to normal after this long without input
#define DIAL_ENCODER_COUNTS_PER_STEP 4 // encoder counts per detent
// Menu: turn the dial (outside the settings mode), press to run the shown item:
// CLOSE / DIAL RESET / POWER OFF / RELEASE / GAME RESTART / GAME END.
#define DIAL_MENU_TIMEOUT_S 10         // the menu closes after this long without input (0 = never)

// NeoGacha front LED (NeoPixel ring on PORT.B G1, 26 pixels in NEOPIXELLED.h).
#define DIAL_LED_PIN 1
#define DIAL_LED_COUNT 26
#define DIAL_LED_BRIGHTNESS 25
#define DIAL_LED_BRIGHTNESS_SERVO 6    // cap while the servo runs (power budget)

// ---- Game mode LEDs (docs/game-mode-interface.md) ----
// Unit colour: used by the device's own receive / release animation and by PC
// effects sent without a colour. The game shows the same colour on the screen
// side. Proposal: dial 1 (0x20, left) cyan, dial 2 (0x21, right) magenta.
// The PC can change it per session with SET_DIAL_COLOR.
#define DIAL_UNIT_COLOR_1 {0, 255, 255}
#define DIAL_UNIT_COLOR_2 {255, 0, 255}
// Local receive animation: white flash, then the unit colour runs TO_SCREEN.
#define DIAL_RX_FLASH_MS 150           // UNVERIFIED (proposal, calibrate on the real machine)
#define DIAL_RX_CHASE_MS 1500          // UNVERIFIED
// LED arrangement (NeoGacha NEOPIXELLED::level2): the first half of the pixels
// goes up from the bottom, the second half comes down from the top - two
// columns. With FOLDED 1 both columns show the same flow. TO_SCREEN runs
// bottom -> top; set REVERSED 1 if the screen side is at the bottom.
#define DIAL_LED_FOLDED 1
#define DIAL_LED_REVERSED 0            // UNVERIFIED: which end faces the screen
// Second LED on PORT.A G15 (NeoGacha LED_PIN1 / topLed), game mode target
// TRANSFER. -1 = not used: effects for TRANSFER answer NOT_CONFIGURED, effects
// for BOTH run on the front LED only.
#define DIAL_TRANSFER_LED_PIN 15
#define DIAL_TRANSFER_LED_COUNT 26     // UNVERIFIED: NeoGacha NUM_LEDS; set the real pixel count
#define DIAL_TRANSFER_LED_FOLDED 1     // UNVERIFIED: same arrangement as the front LED
#define DIAL_TRANSFER_LED_CHIPSET WS2812B
#define DIAL_TRANSFER_LED_COLOR_ORDER GRB
#define DIAL_TRANSFER_LED_REVERSED 0   // UNVERIFIED
#define DIAL_TRANSFER_LED_BRIGHTNESS 25  // as the front LED (NeoGacha setbrightness(25))
// 0: the strip is fed from the M5Dial 5 V -> dimmed to DIAL_LED_BRIGHTNESS_SERVO
//    while the servo runs. 1: own 5 V supply (GND shared) -> never dimmed.
#define DIAL_TRANSFER_LED_SEPARATE_POWER 0

// ---- Staff remote (M5StopWatch, docs/hardware.md) ----
// Tilt = a relative stick: the orientation when the yellow key went down is the centre.
#define REMOTE_TILT_DEAD_DEG 4         // dead zone (degrees)
#define REMOTE_TILT_FULL_DEG 25        // tilt for full speed
#define REMOTE_TILT_SWAP_XY 1          // the StopWatch IMU is turned 90 deg to the screen and its
#define REMOTE_TILT_INVERT_X 0         // forward axis reversed (checked on the device 2026-11-22)
#define REMOTE_TILT_INVERT_Y 1
#define REMOTE_HOLD_MS 450             // every touch operation is a press-and-hold this long
#define REMOTE_STOP_HOLD_MS 250        // ストップ
#define REMOTE_RECOVER_HOLD_MS 1200    // ふっき (the arm moves)
#define REMOTE_BRIGHTNESS 160
#define REMOTE_STANDBY_BRIGHTNESS 60   // the standby screen (dark, but clearly not off)
#define REMOTE_DIM_S 60                // the standby screen after this long without input
#define REMOTE_VIBRATION 255           // strength of the clicks, 0 = no vibration
