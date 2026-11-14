// ATOM Lite / AtomS3 Lite + Atomic PWM Base + Unit 2Relay
// LEDテープ2本と噴霧モジュールを組み合わせた演出
//
//   COBテープ（電球色・12V）… PWMベース経由で明るさ調整
//                             配線: テープの+ → 12V端子、テープの− → OUT端子
//   WS2811テープ（12V）…… データ線で色・パターン制御
//   噴霧モジュール（5V）…… Unit 2Relay 経由でON/OFF
//   ※ピン番号は、ボードに応じて下の「ピン設定」で自動的に切り替わります
//
// 操作:
//   ボタン短押し ………… 次のシーンへ
//   ボタン長押し（約0.8秒）… 噴霧のON/OFFを切り替え
//                           （全体が緑に光れば噴霧ON、赤なら噴霧停止＝光だけ）
//
// 必要ライブラリ: FastLED
// ボード: ATOM Lite なら「M5Atom」、AtomS3 Lite なら「M5AtomS3」を選ぶ
//
// UFO-DeviceHub での位置づけ:
//   ESP-NOW / Hub を使わずに単体で動く Illumination 用スケッチ（展示演出・実機確認用）。
//   Arduino IDE ではこのファイルをそのまま開いて書き込める。
//   PlatformIO では  pio run -e illum_mist -t upload（ATOM Lite）
//                     pio run -e illum_mist_s3 -t upload（AtomS3 Lite）

#include <Arduino.h>
#include <FastLED.h>

// Arduino-ESP32 コア 3.x はピン指定の ledcAttach()/ledcWrite()、
// 2.x（PlatformIO の espressif32@6.x）はチャネル指定の API。どちらでもビルドできるようにする。
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  #define COB_LEDC_V3 1
#else
  #define COB_LEDC_V3 0
  constexpr uint8_t COB_PWM_CH = 0;
#endif

// ===== ピン設定 =====
#if CONFIG_IDF_TARGET_ESP32S3
  // ---- AtomS3 Lite ----
  constexpr uint8_t COB_PWM_PIN = 5;    // Atomic PWM Base のPWM入力（ベースの「22」の穴）
  constexpr uint8_t WS_DATA_PIN = 39;   // WS2811のデータ線（ベースの「21」の穴）
  constexpr uint8_t RELAY_PIN   = 2;    // 2Relay の信号（Groveの黄色線 = RELAY-2）
  constexpr uint8_t BUTTON_PIN  = 41;   // 本体のボタン
#else
  // ---- ATOM Lite（ESP32）----
  constexpr uint8_t COB_PWM_PIN = 22;   // Atomic PWM Base のPWM入力（ベースの「22」の穴）
  constexpr uint8_t WS_DATA_PIN = 21;   // WS2811のデータ線（ベースの「21」の穴）
  constexpr uint8_t RELAY_PIN   = 32;   // 2Relay の信号（Groveの黄色線 = RELAY-2）
  constexpr uint8_t BUTTON_PIN  = 39;   // 本体のボタン
#endif

// リレーのON/OFFが逆に動く場合は、この2行を入れ替えてください
constexpr uint8_t RELAY_ON  = HIGH;
constexpr uint8_t RELAY_OFF = LOW;

// ===== WS2811テープ設定 =====
constexpr uint16_t NUM_LEDS = 70;     // 14 IC/m x 5m = 70個
constexpr uint8_t WS_BRIGHTNESS = 100; // 0-255。霧の中は光が拡散するので少し高め
#define COLOR_ORDER GRB

// ===== COBテープ（PWM）設定 =====
constexpr uint32_t PWM_FREQ = 10000;
constexpr uint8_t  PWM_BITS = 10;
constexpr uint8_t  COB_BRIGHTNESS = 255;  // COB全体の明るさの上限 0-255（まぶしければ下げる）

// ===== 噴霧の安全設定 =====
// 1つのシーンで噴霧できる合計時間。超えると自動で止まります（空焚き防止）。
// 次のシーンに切り替えるか、長押しで噴霧を入れ直すと、またカウントし直します。
constexpr unsigned long MIST_MAX_ON_MS = 10UL * 60UL * 1000UL;  // 10分
constexpr unsigned long LONG_PRESS_MS  = 800;
constexpr unsigned long DEBOUNCE_MS    = 30;
constexpr unsigned long FRAME_MS       = 15;

CRGB leds[NUM_LEDS];

// ===== シーン定義 =====
enum CobMode  : uint8_t { COB_OFF, COB_DIM, COB_FLICKER, COB_SYNC, COB_NIGHT, COB_STORM, COB_SWELL };
enum WsMode   : uint8_t {
  WS_OFF, WS_RAINBOW, WS_AURORA, WS_FIRE, WS_GLOW, WS_BURST, WS_STORM
};
enum MistMode : uint8_t {
  MIST_NONE,    // 出さない
  MIST_CONT,    // ずっと出す
  MIST_BREATH,  // 8秒出して6秒休む（呼吸のように）
  MIST_PUFF     // 3秒だけ出して9秒休む（ぽわっと）
};

struct Scene {
  CobMode cob;
  WsMode ws;
  MistMode mist;
  const char* name;
};

const Scene SCENES[] = {
  {COB_OFF,     WS_OFF,     MIST_NONE,   "off"},
  {COB_SWELL,   WS_AURORA,  MIST_CONT,   "aurora mist"},       // 霧の中を流れるオーロラ（COBもゆったり明暗）
  {COB_STORM,   WS_STORM,   MIST_CONT,   "storm cloud"},       // 雷雲：霧の中でピカッ
  {COB_FLICKER, WS_FIRE,    MIST_CONT,   "campfire smoke"},    // 焚き火と煙
  {COB_SYNC,    WS_GLOW,    MIST_BREATH, "breathing mist"},    // 霧が出ると明るく、止まると暗く
  {COB_DIM,     WS_RAINBOW, MIST_CONT,   "rainbow mist"},      // 霧が虹色に染まる（COBは一定の暖色の下地）
  {COB_SYNC,    WS_BURST,   MIST_PUFF,   "magic puff"},        // ぽわっと出るたびにキラキラ（COBも一緒にふわっと明るく）
  {COB_NIGHT,   WS_OFF,     MIST_BREATH, "night"},             // 寝る前用：やさしい光と霧
};
constexpr uint8_t SCENE_COUNT = sizeof(SCENES) / sizeof(SCENES[0]);

// ===== 状態 =====
uint8_t sceneIndex = 0;
unsigned long sceneStartMs = 0;
unsigned long lastFrameMs = 0;

bool mistEnabled = true;      // 長押しで切り替え。falseなら光だけ
bool mistLocked = false;      // 最大時間を超えて強制停止中
bool relayOn = false;
unsigned long mistOnMs = 0;   // このシーンで噴霧した合計時間

uint8_t mistGlow = 0;         // 0〜255。霧が「出ている演出」中に上がり、止まると下がる（光との連動用）
uint8_t g_bolt = 0;           // 稲妻の強さ 0〜255（COBとWS2811で共有）

// ===== 噴霧 =====

// シーンの経過時間 t(ms) のときに「霧が出ているはず」かどうか
bool mistPattern(MistMode m, unsigned long t) {
  switch (m) {
    case MIST_CONT:   return true;
    case MIST_BREATH: return (t % 14000) < 8000;
    case MIST_PUFF:   return (t % 12000) < 3000;
    default:          return false;
  }
}

// 実際のリレーを制御（長押しで停止中・最大時間超過のときは出さない）
void updateMist(bool wanted, unsigned long dt) {
  bool on = wanted && mistEnabled && !mistLocked;
  if (on) {
    mistOnMs += dt;
    if (mistOnMs >= MIST_MAX_ON_MS) {
      mistLocked = true;
      on = false;
      Serial.println("mist: max time reached, stopped (switch scene to reset)");
    }
  }
  if (on != relayOn) {
    relayOn = on;
    digitalWrite(RELAY_PIN, on ? RELAY_ON : RELAY_OFF);
    Serial.printf("mist: %s\n", on ? "ON" : "OFF");
  }
}

// ===== 稲妻 =====
// ときどき、ピカピカッと数回光る。戻り値は 0〜255（光っていないときは0）
uint8_t lightning(unsigned long now) {
  static unsigned long burstStart = 0;
  static unsigned long burstEnd = 0;
  static unsigned long nextMs = 2500;
  if (now >= nextMs) {
    burstStart = now;
    burstEnd = now + random16(180, 520);
    nextMs = burstEnd + random16(2500, 8000);
  }
  if (now >= burstStart && now < burstEnd) {
    uint8_t phase = ((now - burstStart) / 45) % 3;  // 45msごとに強弱
    return phase == 1 ? 70 : 255;
  }
  return 0;
}

// ===== COBテープ =====
// 明るさ(0-255)を設定。見た目がなめらかになるよう2乗カーブにしている
void setCob(uint8_t v) {
  v = scale8(v, COB_BRIGHTNESS);
  uint32_t duty = (uint32_t)v * v * ((1 << PWM_BITS) - 1) / 65025;
#if COB_LEDC_V3
  ledcWrite(COB_PWM_PIN, duty);
#else
  ledcWrite(COB_PWM_CH, duty);
#endif
}

// ロウソクのようなゆらぎ
void cobFlicker(unsigned long now) {
  static unsigned long nextMs = 0;
  static uint8_t target = 200;
  static int cur = 200;
  if (now >= nextMs) {
    target = random8(90, 255);
    nextMs = now + random8(40, 120);
  }
  cur += ((int)target - cur) / 4;
  setCob((uint8_t)cur);
}

void applyCob(CobMode mode, unsigned long now) {
  switch (mode) {
    case COB_DIM:     setCob(80); break;
    case COB_FLICKER: cobFlicker(now); break;
    case COB_SYNC:    setCob(20 + scale8(mistGlow, 150)); break;  // 霧と一緒に明るくなる
    case COB_NIGHT:   setCob(beatsin8(6, 10, 110)); break;        // ゆっくり呼吸
    case COB_SWELL:   setCob(beatsin8(7, 25, 150)); break;        // ゆったり明暗を繰り返す
    case COB_STORM: {
      uint8_t bolt = scale8(g_bolt, 220);
      setCob(bolt > 25 ? bolt : 25);                              // 普段はほのかに、稲妻でパッと明るく
      break;
    }
    default:          setCob(0); break;
  }
}

// ===== WS2811テープ：光り方 =====

// 虹が流れる
void wsRainbow() {
  static uint8_t startHue = 0;
  startHue++;
  fill_rainbow(leds, NUM_LEDS, startHue, 3);
}

// オーロラ：青緑〜紫の光がゆらゆら流れる
void wsAurora(unsigned long now) {
  for (int i = 0; i < NUM_LEDS; i++) {
    uint8_t wave1 = sin8((uint8_t)(i * 7 + now / 10));
    uint8_t wave2 = sin8((uint8_t)(i * 3 - now / 17));
    uint8_t hue = 130 + (wave1 >> 2);          // 130〜193（青緑〜青紫）
    uint8_t val = 50 + scale8(wave2, 205);     // 50〜255
    leds[i] = CHSV(hue, 255, val);
  }
}

// 炎のゆらめき（FastLED の Fire2012 方式）
void wsFire() {
  static uint8_t heat[NUM_LEDS];
  constexpr uint8_t COOLING = 55;
  constexpr uint8_t SPARKING = 120;

  for (int i = 0; i < NUM_LEDS; i++) {
    heat[i] = qsub8(heat[i], random8(0, ((COOLING * 10) / NUM_LEDS) + 2));
  }
  for (int k = NUM_LEDS - 1; k >= 2; k--) {
    heat[k] = (heat[k - 1] + heat[k - 2] + heat[k - 2]) / 3;
  }
  if (random8() < SPARKING) {
    int y = random8(7);
    heat[y] = qadd8(heat[y], random8(160, 255));
  }
  for (int j = 0; j < NUM_LEDS; j++) {
    leds[j] = HeatColor(heat[j]);
  }
}

// 霧と連動：霧が出ているあいだは明るく、止まるとゆっくり暗くなる。色はゆっくり移り変わる
void wsGlow() {
  uint8_t v = 40 + scale8(mistGlow, 215);
  uint8_t hue = beatsin8(5, 130, 230);   // 青緑 → 青 → 紫 → ピンクをゆっくり往復
  for (int i = 0; i < NUM_LEDS; i++) {
    leds[i] = CHSV((uint8_t)(hue + i / 2), 255, v);
  }
}

// 霧が出ているあいだ、キラキラが弾ける（止まると静かに消える）
void wsBurst() {
  fadeToBlackBy(leds, NUM_LEDS, 25);
  for (uint8_t k = 0; k < 2; k++) {
    if (random8() < scale8(mistGlow, 160)) {
      leds[random16(NUM_LEDS)] = CHSV(random8(), 200, 255);
    }
  }
}

// 雷雲：暗い青灰色の雲がゆっくり流れ、ときどき稲妻が走る
void wsStorm(unsigned long now) {
  static int center = NUM_LEDS / 2;
  static uint8_t lastBolt = 0;
  if (g_bolt > 0 && lastBolt == 0) {
    center = random8(NUM_LEDS);            // 稲妻が走り始めたら場所を決める
  }
  lastBolt = g_bolt;

  for (int i = 0; i < NUM_LEDS; i++) {
    uint8_t cloud = 12 + scale8(sin8((uint8_t)(i * 5 + now / 25)), 38);
    leds[i] = CHSV(165, 180, cloud);
    if (g_bolt > 0) {
      int d = abs(i - center);
      int fall = 255 - (d * 4 < 235 ? d * 4 : 235);   // 中心ほど強く光る
      uint8_t amount = (uint16_t)g_bolt * fall / 255;
      nblend(leds[i], CRGB(230, 230, 255), amount);
    }
  }
}

void applyWs(WsMode mode, unsigned long now) {
  switch (mode) {
    case WS_RAINBOW: wsRainbow();     break;
    case WS_AURORA:  wsAurora(now);   break;
    case WS_FIRE:    wsFire();        break;
    case WS_GLOW:    wsGlow();        break;
    case WS_BURST:   wsBurst();       break;
    case WS_STORM:   wsStorm(now);    break;
    default:         fill_solid(leds, NUM_LEDS, CRGB::Black); break;
  }
}

// 起動時の色順テスト：赤→緑→青の順に全体が光ればOK
void colorOrderTest() {
  const CRGB testColors[] = {CRGB::Red, CRGB::Green, CRGB::Blue};
  for (CRGB c : testColors) {
    fill_solid(leds, NUM_LEDS, c);
    FastLED.show();
    delay(500);
  }
  fill_solid(leds, NUM_LEDS, CRGB::Black);
  FastLED.show();
}

// ===== シーン切り替え・噴霧ON/OFF =====
void nextScene(unsigned long now) {
  sceneIndex = (sceneIndex + 1) % SCENE_COUNT;
  sceneStartMs = now;
  mistOnMs = 0;
  mistLocked = false;
  fill_solid(leds, NUM_LEDS, CRGB::Black);  // 前のシーンの残りを消す
  Serial.printf("scene: %s\n", SCENES[sceneIndex].name);
}

void toggleMist() {
  mistEnabled = !mistEnabled;
  mistOnMs = 0;
  mistLocked = false;
  Serial.printf("mist: %s\n", mistEnabled ? "enabled" : "muted");

  // 確認用に全体を一瞬光らせる（緑＝噴霧ON、赤＝噴霧停止）
  CRGB c = mistEnabled ? CRGB(CRGB::Green) : CRGB(CRGB::Red);
  fill_solid(leds, NUM_LEDS, c);
  FastLED.show();
  delay(300);
}

void setup() {
  Serial.begin(115200);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, RELAY_OFF);   // 起動時は必ず噴霧停止

#if COB_LEDC_V3
  ledcAttach(COB_PWM_PIN, PWM_FREQ, PWM_BITS);
#else
  ledcSetup(COB_PWM_CH, PWM_FREQ, PWM_BITS);
  ledcAttachPin(COB_PWM_PIN, COB_PWM_CH);
#endif
  setCob(0);

  FastLED.addLeds<WS2811, WS_DATA_PIN, COLOR_ORDER>(leds, NUM_LEDS);
  FastLED.setBrightness(WS_BRIGHTNESS);
  FastLED.setDither(DISABLE_DITHER);
  colorOrderTest();

  Serial.println("Ready. 短押し: 次のシーン / 長押し: 噴霧ON-OFF");
  Serial.printf("scene: %s\n", SCENES[sceneIndex].name);

  lastFrameMs = millis();
  sceneStartMs = millis();
}

void loop() {
  unsigned long now = millis();

  // ===== ボタン（短押し＝次のシーン、長押し＝噴霧ON/OFF）=====
  static bool btnDown = false;
  static bool longHandled = false;
  static unsigned long btnEdgeMs = 0;

  bool pressed = (digitalRead(BUTTON_PIN) == LOW);
  if (pressed && !btnDown && now - btnEdgeMs > DEBOUNCE_MS) {
    btnDown = true;
    longHandled = false;
    btnEdgeMs = now;
  } else if (pressed && btnDown && !longHandled && now - btnEdgeMs >= LONG_PRESS_MS) {
    longHandled = true;
    toggleMist();
  } else if (!pressed && btnDown && now - btnEdgeMs > DEBOUNCE_MS) {
    btnDown = false;
    btnEdgeMs = now;
    if (!longHandled) {
      nextScene(now);
    }
  }

  // ===== 一定間隔で、噴霧・COB・WS2811を更新 =====
  if (now - lastFrameMs >= FRAME_MS) {
    unsigned long dt = now - lastFrameMs;
    lastFrameMs = now;

    const Scene& sc = SCENES[sceneIndex];

    bool wanted = mistPattern(sc.mist, now - sceneStartMs);  // 演出上「霧が出ているはず」の状態
    updateMist(wanted, dt);

    // 光との連動用：霧が出ている間は上がり（約1秒）、止まるとゆっくり下がる（約2秒）
    mistGlow = wanted ? qadd8(mistGlow, 4) : qsub8(mistGlow, 2);

    g_bolt = lightning(now);

    applyCob(sc.cob, now);
    applyWs(sc.ws, now);
    FastLED.show();
  }
}
