# ハードウェアと設定

設定は `config/` に置く。`*.example.h` をコピーして編集する（実ファイルは git 管理外）。

```
cp config/devices.example.h  config/devices.h    # 両システムの MAC とチャネル
cp config/hardware.example.h config/hardware.h   # ピン・角度・時間
python3 tests/tools/gen_secrets.py               # config/secrets.h（システムごとの PMK/LMK を乱数生成）
```

実ファイルがない場合はサンプルでビルドされ、機器は `example_keys` / `example_devices` を報告する（Hub の画面下部に赤帯（ATOM Lite の Hub では状態 LED が 5 秒ごとに赤）、`HELLO_ACK` / `DIAG` / ゲートウェイのログ警告）。
機器のファームウェアは `devices.h` / `secrets.h` がないとビルドエラーになる（サンプルの MAC・鍵では Hub と通信できないため）。コンパイル確認だけなら `PLATFORMIO_BUILD_FLAGS=-DUFO_ALLOW_EXAMPLE_CONFIG pio run`。別の PC で書き込む手順は [README](../README.md#別の-pc-でビルド書込みするとき必ずやること)。

## 共通

| 項目 | 設定 | 備考 |
|---|---|---|
| システム | `UFO_SYSTEM_ID`（PlatformIO env の `_s1` / `_s2`） | 同じ会場の 2 システムを区別する。[multi-system.md](multi-system.md) |
| Wi-Fi チャネル | `UFO_S1_WIFI_CHANNEL` / `UFO_S2_WIFI_CHANNEL`（`devices.h`） | **同じシステムの全機器で同じ値**。システム間は 5 ch 以上離す（既定 1 / 11）。AP には接続しない |
| MAC | `UFO_S1_MAC_*` / `UFO_S2_MAC_*`（`devices.h`） | 各ファームは起動時にシリアルへ `UFO system=N ... MAC=..` を出す（Hub は `BOOT` 行の `hub_mac`） |
| 鍵 | `UFO_S1_PMK`, `UFO_S1_LMK_*` / `UFO_S2_*`（`secrets.h`） | システムごとに別の鍵にする。鍵を変えたら、そのシステムの 4 台をすべて書き直す |

`devices.example.h` のシステム 1 の M5Dial の MAC は NeoGacha の `setGachaId()` にあった値（`C0:4E:30:12:6B:90` / `C0:4E:30:12:67:D4`）。同じ実機を使う場合にのみ有効で、それ以外はプレースホルダ。
旧形式（`UFO_WIFI_CHANNEL`, `UFO_MAC_*`, `UFO_PMK`, `UFO_LMK_*`）の設定はシステム 1 として読む。

## Hub（M5AtomS3R）

| 項目 | 値 |
|---|---|
| ボード | `m5stack-atoms3`（ESP32-S3、env `hub_s1` / `hub_s2`） |
| USB | ネイティブ USB CDC（`ARDUINO_USB_CDC_ON_BOOT=1`）。macOS では `/dev/cu.usbmodem*` |
| 画面 | 上部の帯にシステム番号（S1 = シアン、S2 = オレンジ）、チャネル、無線状態、PC セッション、子機ごとのオンライン状態。設定エラー・サンプル鍵は下部の赤帯 |

ATOM Lite を Hub にする場合は env `hub_atomlite_s1` / `hub_atomlite_s2`（USB-UART、状態 LED G27: 青 = PC 待ち、緑 = 全台オンライン、黄点滅 = 子機オフライン、赤点滅 = 無線停止）。

## Illumination ATOM（Device ID 0x10）

ATOM Lite + Atomic PWM Base + Unit 2Relay。旧 `atom_led_mist` スケッチの配線と演出をそのまま引き継ぐ。

| 項目 | 設定 | 初期値 | 備考 |
|---|---|---|---|
| WS2811 データ | `ILLUM_LED_PIN` | 21（ベースの「21」の穴） | |
| LED 数 / 種類 / 色順 | `ILLUM_LED_COUNT` / `ILLUM_LED_CHIPSET` / `ILLUM_LED_COLOR_ORDER` | 70 / WS2811 / GRB | 起動時に赤 → 緑 → 青と光れば、色順は正しい |
| LED 輝度上限 | `ILLUM_LED_MAX_BRIGHTNESS` | 200 | 電源容量に合わせて調整する |
| COB テープ PWM | `ILLUM_COB_PIN` | 22（Atomic PWM Base） | 10 kHz / 10 bit、2 乗カーブ。-1 で COB テープなし |
| COB 輝度上限 | `ILLUM_COB_MAX_BRIGHTNESS` | 255 | まぶしければ下げる |
| ミストリレー | `ILLUM_MIST_PIN` | 32（Unit 2Relay RELAY-2、Grove 黄） | -1 で無効（`SET_MIST` は `NOT_CONFIGURED`） |
| ミスト極性 | `ILLUM_MIST_ACTIVE_HIGH` | 1（HIGH = ON） | 逆に動く場合は 0 |
| ミスト 1 回の最大時間 | `ILLUM_MIST_MAX_S` | 600 s | 空焚き防止。超える要求は `clamped` |
| ミスト用ウォッチドッグ | `ILLUM_MIST_WATCHDOG_MS` | 1000 ms | `loop()` が止まったら強制 OFF |
| ボタン | `ILLUM_BUTTON_PIN` | 39 | -1 でローカル操作なし |
| ローカル操作 | `ILLUM_LOCAL_CONTROL` / `ILLUM_LOCAL_MIST` / `ILLUM_LOCAL_BRIGHTNESS` | 1 / 1 / 160 | |
| 蒸気リレー（短パルス用） | `ILLUM_RELAY_PINS` | `{-1}` = **未設定** | Unit 2Relay のもう一方（G26）を使える。設定するまで `RELAY_PULSE` は `NOT_CONFIGURED` |
| リレー極性 | `ILLUM_RELAY_ACTIVE_HIGH` | `{1}` | **未確認** |
| 最大 ON 時間 / 再噴射禁止 | `ILLUM_RELAY_MAX_ON_MS` / `ILLUM_RELAY_COOLDOWN_MS` | 3000 / 2000 ms | |
| セッション終了時の照明 | `ILLUM_SESSION_END_LIGHT_MODE` | 1 (IDLE) | |

### ミストの安全仕様

* 起動時: 出力レベルを「OFF」に書いてからピンを出力に切り替える（ON グリッチ防止）。
* 1 回の実行は最大 `ILLUM_MIST_MAX_S`（10 分）。時間が来たら自動で止まり、`MIST_DONE`（COMPLETED）を送る。
* PC 制御中は、Hub との通信断（3 秒）、セッション終了、`STOP_ALL`、`SET_MIST OFF` で即 OFF。通信断中は開始要求を受けない（`REJECTED_STATE`）。
* 本体ボタンの長押しで、いつでも止められる（PC 制御中も有効。`MIST_DONE` reason = LOCAL_BUTTON）。
* `esp_timer` によるウォッチドッグ: ミストが ON の間は `loop()` が 100 ms ごとにタイマを更新する。1 秒途絶えたら強制 OFF にし、`ERROR` イベント（RELAY_WATCHDOG, detail = 1）を送る。
* 蒸気リレー（`RELAY_PULSE`）とは別の出力で、別の規則で制御する。蒸気リレーは 1 回最大 3000 ms、クールダウンあり、`esp_timer` で最大 ON + 100 ms に強制 OFF。

### ローカル操作（PC のセッションがないとき）

| 操作 | 動作 |
|---|---|
| 短押し | 次のシーン（下表）。状態 LED が青く光る |
| 長押し（0.8 秒） | ミストの ON / OFF（緑 = ON、赤 = OFF）。OFF にすると、以後のシーンでも光だけになる |

| # | シーン | 照明モード | ミスト |
|---|---|---|---|
| 0 | off | OFF | なし |
| 1 | aurora mist | AURORA | 連続 |
| 2 | storm cloud | STORM | 連続 |
| 3 | campfire smoke | CAMPFIRE | 連続 |
| 4 | breathing mist | MIST_GLOW | BREATH |
| 5 | rainbow mist | GAME_PLAY | 連続 |
| 6 | magic puff | MAGIC_PUFF | PUFF |
| 7 | night | NIGHT | BREATH |

* ローカル操作は Hub がなくても動く（展示の単体運用・実機確認用）。ローカルのミストも最大 10 分で止まる。
* PC のセッションが始まるとローカル操作は終了する（ミスト OFF、照明は `ILLUM_SESSION_END_LIGHT_MODE`）。セッション中の短押しは無視する（状態 LED が紫に光る）。

## M5Dial ×2（Device ID 0x20 / 0x21）

NeoGacha `src/M5Dial_Gacha` から確認した値をそのまま初期値にしている。

| 項目 | 設定 | 初期値 | 根拠 |
|---|---|---|---|
| SG90 信号 | `DIAL_SERVO_PIN` | 13 | `#define SERVO_PIN 13` |
| PWM | `DIAL_SERVO_PERIOD_HZ` | 50 | `setPeriodHertz(50)` |
| パルス幅 | `DIAL_SERVO_MIN_US/MAX_US` | 500 / 2400 | `minUs/maxUs` |
| 待機角度 | `DIAL_SERVO_STANDBY_DEG` | 180 | MODE_STOP の `pos[0]=180`、MODE_B の復帰 `write(180)` |
| 排出角度 | `DIAL_SERVO_RELEASE_DEG` | 0 | MODE_B `if (spentTime <= 1500) servo1.write(0)` |
| 保持時間 | `DIAL_SERVO_HOLD_MS` | 1500 | 同上 |
| 復帰整定 | `DIAL_SERVO_RETURN_SETTLE_MS` | 600 | **未確認**（新規。復帰完了までの待ち） |
| 通信断時の排出中動作 | `DIAL_LINK_LOSS_ABORT_RELEASE` | 0（シーケンスを最後まで実行して待機位置へ） | 1 で即待機位置へ |
| サーボの速度 | `DIAL_SERVO_SLEW_DEG_PER_S` | 240（180° を 0.75 秒） | 電源対策。0 = 一気に動かす（NeoGacha と同じ） |
| 停止後のパルス停止 | `DIAL_SERVO_DETACH_MS` | 500 | 電源対策。待機位置に戻ってこの時間後にパルスを止める。0 = 保持し続ける |
| 起動時のサーボ待ち | `DIAL_SERVO_BOOT_DELAY_MS` | 1000 | 電源対策。無線の起動が済んでから待機位置へ動かす |
| 受取スイッチ GPIO | `DIAL_RECEIVER_PIN` | 2（PORT.B、LED の隣のピン） | スイッチ ON = ボール投入 |
| 受取スイッチ極性 | `DIAL_RECEIVER_ACTIVE_LOW`, `DIAL_RECEIVER_PULLUP` | 1, 1（スイッチで GND に落とす、内部プルアップ） | 3.3V 側に繋いだ場合は 0, 0 |
| デバウンス | `DIAL_RECEIVER_DEBOUNCE_MS` | 30 | |
| 模擬ボタン | `DIAL_RECEIVER_SIM_BUTTON` | 0 | 1 にすると BtnA = 受取スイッチ ON（開発用。設定モードは使えなくなる） |
| 前面 LED | `DIAL_LED_PIN`, `DIAL_LED_COUNT`, `DIAL_LED_BRIGHTNESS` | 1, 26, 25 | NeoGacha `LED_PIN2=1`, `NUM_LEDS 26`, `setbrightness(25)` |
| サーボ動作中の LED | `DIAL_LED_BRIGHTNESS_SERVO` | 6 | 電源対策。サーボが動いている間は LED を暗くする |
| 設定モード | `DIAL_SETTINGS_HOLD_MS`, `DIAL_SETTINGS_TIMEOUT_S` | 2000, 300 | ボタン長押し時間 / 無操作で自動終了 |
| エンコーダ | `DIAL_ENCODER_COUNTS_PER_STEP` | 4 | 1 クリックで項目が 1 つ進まない場合に調整 |

NeoGacha との違い:

* NeoGacha の MODE_STOP / MODE_A はダイヤル回転のたびに 180°/160° を往復していた（攪拌動作）。本システムでは PC の `RELEASE_BALL` 以外でサーボを動かさないため移植していない。
* 起動時は無線の起動から `DIAL_SERVO_BOOT_DELAY_MS` 後に待機角度 180° を出力する。電源投入時にホーンが待機位置へ動く。
* サーボはゆっくり動かし、待機位置で止まったらパルスを止める（下記の電源対策）。

### サーボの電源対策

SG90 は動き始めと引っかかったときに最大 0.7 A ほど流れ、M5Dial の電源が落ちる（再起動する）ことがある。ファームウェアでは次の対策をしている。

1. **ゆっくり動かす**: 180° を一気に動かさず、`DIAL_SERVO_SLEW_DEG_PER_S`（既定 240°/秒）で少しずつ角度を変える。保持時間・整定時間は移動が終わってから数える。
2. **止まったらパルスを止める**: 待機位置に戻って `DIAL_SERVO_DETACH_MS` 後にパルスを止める。保持電流と細かい振動がなくなる。次の動作で自動的に再開する。
3. **ほかの負荷と重ねない**: サーボが動いている間は LED を `DIAL_LED_BRIGHTNESS_SERVO` まで暗くし、スピーカーを鳴らさない。
4. **起動時に重ねない**: 無線の起動が終わってから `DIAL_SERVO_BOOT_DELAY_MS` 後にサーボを動かす。

それでも落ちる場合は配線側で対策する:

* サーボの電源（赤・茶）の根元に **470〜1000 µF の電解コンデンサ**を入れる。
* サーボを M5Dial の 5V ではなく**別の 5V 電源**から給電し、GND だけ M5Dial と共通にする。
* USB 給電の場合は、2 A 以上の電源アダプタとしっかりしたケーブルを使う。

パルスを止めた状態では、ホーンはギアの摩擦だけで止まっている。待機位置でゲートが押し戻されてボールが漏れる場合は `DIAL_SERVO_DETACH_MS 0` にする（保持し続ける）。

### 設定モード（ハードウェアテスト）

ゲームのサイクル中（`WAITING` / `GOT BALL` / 排出中）以外のとき、**本体ボタンを 2 秒長押し**すると設定モードに入る（画面が紺色）。

| 操作 | 動作 |
|---|---|
| ダイヤルを回す | 項目を選ぶ |
| 押す | 選んだ項目を実行 |
| 2 秒長押し / `EXIT` | 設定モードを終わる（サーボは待機位置へ） |

| 項目 | 内容 |
|---|---|
| `SERVO TEST` | 排出シーケンス（排出角度 → 保持 → 待機位置）を 1 回動かす |
| `SERVO ANGLE` | 押すと調整開始。ダイヤル 1 クリックで 5° 動く。もう一度押すと待機位置へ戻る |
| `LED TEST` | 押すたびに 赤 → 緑 → 青 → 白 → 虹 → 流れる → 消灯 |
| `SWITCH TEST` | スイッチの状態と ON 回数を表示し、ON のたびに音が鳴る。押すと回数を 0 に戻す |

* 画面下には、どの項目でもスイッチの状態（`switch G2 ON/off xN`）とサーボ角度を表示する。
* 設定モード中は PC への `BALL_RECEIVED` などのイベントを送らず、PC からの命令は `STOP_ALL` 以外すべて `REJECTED_STATE` になる。PC には状態 `SETTINGS` が見える。
* PC の `STOP_ALL` で設定モードは終わる。操作がないまま `DIAL_SETTINGS_TIMEOUT_S`（5 分）たっても終わる。

**サーボの回転方向・角度・保持時間は実機の排出機構で確認するまで変更しないこと。** 確認手順は `docs/test-plan.md` 3.2。

### 表示

`DIAL #n ID 0xNN` / Hub 接続（`HUB OK`, `NO HUB LINK`, `HUB / NO SESSION`）/ 状態（`STANDBY`, `READY`, `WAITING`=受取待ち, `GOT BALL`=受取済み, `RELEASE`=排出中, `RETURN`, `DONE`, `ERROR`, `SETTINGS`）/ モード・サイクル / スイッチ状態・`[SIM BTN]` / MAC 不一致・エラー・サンプル鍵警告 / 自 MAC。

### 前面 LED（NeoGacha の演出を流用）

| 状態 | 演出 |
|---|---|
| DISABLED | pacifica（NeoGacha MODE_STOP） |
| READY | 白のゆっくり呼吸 |
| ARMED | シアンのチェイス |
| BALL_DETECTED | cylon（NeoGacha `clyon()` を非ブロッキング化） |
| RELEASING / RETURNING | 高速レインボー |
| COMPLETE | フェードアウト（NeoGacha `fade()`） |
| Hub 通信断 | 赤の呼吸 |

## 照明モード・演出（Illumination）

| モード | WS2811 | COB |
|---|---|---|
| OFF | 消灯 | 消灯 |
| IDLE | pacifica | ゆっくり明暗 |
| STUDY | 暖色の呼吸 | 明るめで一定（デスクライト） |
| GAME_READY | シアンのチェイス | 暗め |
| GAME_PLAY | レインボー（speed で速度） | 暖色の下地 |
| RESULT | ゴールドのきらめき | やや明るい |
| ERROR | 赤点滅 1 Hz | 消灯 |
| CUSTOM | `color`。speed=0 で点灯、>0 で呼吸 | `brightness` |
| AURORA | 青緑〜紫が流れる | ゆったり明暗 |
| STORM | 雷雲 + 稲妻 | 普段はほのか、稲妻で明るく |
| CAMPFIRE | 炎（Fire2012） | ロウソクのゆらぎ |
| MIST_GLOW | ミストが出ている間は明るく、止まると暗く | ミストと連動 |
| MAGIC_PUFF | ミストが出ている間はキラキラ | ミストと連動 |
| NIGHT | 消灯 | ゆっくり呼吸 |

MIST_GLOW / MAGIC_PUFF はミストの出力に連動するので、`SET_MIST` と組み合わせて使う。
`transition_ms` を指定すると、モード切替時に WS2811 と COB の両方がクロスフェードする。演出（既定時間）: GAME_START 1500, ARM_MOVE 600, ARM_DESCEND 1200, CATCH 800, BALL_RECEIVED 1000, BALL_RELEASED 1000, SCORE 1500, GAME_CLEAR 4000, GAME_OVER 2500 ms（最大 10000 ms）。
演出中、COB は GAME_START で明るくなっていき、CATCH で点滅、SCORE / GAME_CLEAR で全灯、GAME_OVER でフェードアウトする。
