// Hardware pin / actuator configuration.
//   cp config/hardware.example.h config/hardware.h   (git-ignored) and edit.
// Values marked UNVERIFIED are not confirmed on the real hardware. Values that
// would move an actuator are disabled (-1) until set explicitly.
#pragma once

// ---------------- Hub (M5ATOM Lite) ----------------
#define HUB_SERIAL_BAUD 115200
#define HUB_STATUS_LED_PIN 27  // ATOM Lite built-in SK6812

// ---------------- Illumination ATOM ----------------
#define ILLUM_STATUS_LED_PIN 27        // ATOM Lite built-in SK6812
#define ILLUM_LED_PIN 26               // UNVERIFIED: LED strip data pin (ATOM Grove G26)
#define ILLUM_LED_COUNT 30             // UNVERIFIED: number of LEDs on the strip
#define ILLUM_LED_MAX_BRIGHTNESS 200   // global cap (0-255) for power budget
// Steam generator relay(s). -1 = not configured -> RELAY_PULSE answers NOT_CONFIGURED.
#define ILLUM_RELAY_COUNT 1
#define ILLUM_RELAY_PINS {-1}          // UNVERIFIED: e.g. {32}
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
// 1: on hub link loss, a running release returns to standby immediately.
// 0: the running sequence finishes normally (hold, then return).
#define DIAL_LINK_LOSS_ABORT_RELEASE 0

// Ball receiver switch. -1 = not wired yet (use the simulation button).
#define DIAL_RECEIVER_PIN -1           // UNVERIFIED: e.g. 15 (PORT.A) or 2 (PORT.B)
#define DIAL_RECEIVER_ACTIVE_LOW 1     // 1: switch pulls to GND when a ball is present
#define DIAL_RECEIVER_PULLUP 1
#define DIAL_RECEIVER_DEBOUNCE_MS 30
// 1: the M5Dial button (BtnA) simulates the receiver switch (development only).
#define DIAL_RECEIVER_SIM_BUTTON 1

// NeoGacha front LED (NeoPixel ring on PORT.B G1, 26 pixels in NEOPIXELLED.h).
#define DIAL_LED_PIN 1
#define DIAL_LED_COUNT 26
#define DIAL_LED_BRIGHTNESS 25
