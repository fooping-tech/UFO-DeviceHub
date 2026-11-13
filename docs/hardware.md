# ハードウェアと設定

設定は `config/` に置く。`*.example.h` をコピーして編集する（実ファイルは git 管理外）。

```
cp config/devices.example.h  config/devices.h    # MAC アドレス
cp config/hardware.example.h config/hardware.h   # ピン・角度・時間
python3 tests/tools/gen_secrets.py               # config/secrets.h（PMK/LMK 乱数生成）
```

実ファイルがない場合はサンプルでビルドされ、機器は `example_keys` / `example_devices` を報告する（Hub の状態 LED が 5 秒ごとに赤、`HELLO_ACK` / `DIAG` / ゲートウェイのログ警告）。
`-DUFO_REQUIRE_SECRETS` を付けると `secrets.h` がない場合にビルドエラーにできる。

## 共通

| 項目 | 設定 | 備考 |
|---|---|---|
| Wi-Fi チャネル | `UFO_WIFI_CHANNEL`（`devices.h`） | **全機器で同じ値**。AP には接続しない |
| MAC | `UFO_MAC_*`（`devices.h`） | 各ファームは起動時にシリアルへ `MAC=..` を出す（Hub は `BOOT` 行の `hub_mac`） |
| 鍵 | `UFO_PMK`, `UFO_LMK_*`（`secrets.h`） | 鍵を変えたら 4 台とも書き直す |

`devices.example.h` の M5Dial の MAC は NeoGacha の `setGachaId()` にあった値（`C0:4E:30:12:6B:90` / `C0:4E:30:12:67:D4`）。同じ実機を使う場合のみ有効。Hub と Illumination はプレースホルダ。

## Hub（M5ATOM Lite）

| 項目 | 値 |
|---|---|
| ボード | `m5stack-atom` |
| USB Serial | 115200 bps（`HUB_SERIAL_BAUD`） |
| 状態 LED | G27 内蔵 SK6812 |

## Illumination ATOM（Device ID 0x10）

| 項目 | 設定 | 初期値 | 状態 |
|---|---|---|---|
| LED テープ データ | `ILLUM_LED_PIN` | 26（Grove） | **未確認** |
| LED 数 | `ILLUM_LED_COUNT` | 30 | **未確認** |
| LED 輝度上限 | `ILLUM_LED_MAX_BRIGHTNESS` | 200 | 電源容量に合わせて調整 |
| リレー数 | `ILLUM_RELAY_COUNT` | 1 | |
| リレー GPIO | `ILLUM_RELAY_PINS` | `{-1}` = **未設定（リレー無効）** | **未確認**。設定するまで `RELAY_PULSE` は `NOT_CONFIGURED` |
| リレー極性 | `ILLUM_RELAY_ACTIVE_HIGH` | `{1}` | **未確認**（モジュールによって LOW アクティブ） |
| 最大 ON 時間 | `ILLUM_RELAY_MAX_ON_MS` | 3000 ms | 超える要求は 3000 に制限し `clamped` |
| 再噴射禁止 | `ILLUM_RELAY_COOLDOWN_MS` | 2000 ms | 中の要求は `COOLDOWN` |
| セッション終了時の照明 | `ILLUM_SESSION_END_LIGHT_MODE` | 1 (IDLE) | |

リレーの安全仕様:

* 起動時: ピンの出力レベルを「OFF」に書いてから出力に切り替える（ON グリッチ防止）。`setup()` の最初に実行。
* 通信断（Hub フレームなし 3 秒）、セッション終了、`STOP_ALL`、`RELAY_OFF` で即 OFF。
* 通信断中は `RELAY_PULSE` を受けない（`REJECTED_STATE`）。
* 同じ command_id は再実行しない。ON 中の追加要求は `BUSY`（延長しない）。
* `esp_timer` による独立ウォッチドッグ（最大 ON + 100 ms）で強制 OFF。発火時は `ERROR` イベント（RELAY_WATCHDOG）。

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
| 受取スイッチ GPIO | `DIAL_RECEIVER_PIN` | -1（未配線） | **未確定** |
| 受取スイッチ極性 | `DIAL_RECEIVER_ACTIVE_LOW`, `DIAL_RECEIVER_PULLUP` | 1, 1 | **未確定** |
| デバウンス | `DIAL_RECEIVER_DEBOUNCE_MS` | 30 | |
| 模擬ボタン | `DIAL_RECEIVER_SIM_BUTTON` | 1（BtnA = 受取スイッチ ON） | **本番では 0 にする** |
| 前面 LED | `DIAL_LED_PIN`, `DIAL_LED_COUNT`, `DIAL_LED_BRIGHTNESS` | 1, 26, 25 | NeoGacha `LED_PIN2=1`, `NUM_LEDS 26`, `setbrightness(25)` |

NeoGacha との違い:

* NeoGacha の MODE_STOP / MODE_A はダイヤル回転のたびに 180°/160° を往復していた（攪拌動作）。本システムでは PC の `RELEASE_BALL` 以外でサーボを動かさないため移植していない。
* 起動時は NeoGacha と同じく attach 後すぐ待機角度 180° を出力する。電源投入時にホーンが待機位置へ動く。
* サーボはアタッチしたまま（NeoGacha と同じ通電条件）。

**サーボの回転方向・角度・保持時間は実機の排出機構で確認するまで変更しないこと。** 確認手順は `docs/test-plan.md` 3.2。

### 表示

`DIAL #n ID 0xNN` / Hub 接続（`HUB OK`, `NO HUB LINK`, `HUB / NO SESSION`）/ 状態（`STANDBY`, `READY`, `WAITING`=受取待ち, `GOT BALL`=受取済み, `RELEASE`=排出中, `RETURN`, `DONE`, `ERROR`）/ モード・サイクル / スイッチ状態・`[SIM BTN]` / MAC 不一致・エラー・サンプル鍵警告 / 自 MAC。

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

| モード | 描画 |
|---|---|
| OFF | 消灯 |
| IDLE | pacifica |
| STUDY | 暖色の呼吸 |
| GAME_READY | シアンのチェイス |
| GAME_PLAY | レインボー（speed で速度） |
| RESULT | ゴールドのきらめき |
| ERROR | 赤点滅 1 Hz |
| CUSTOM | `color`。speed=0 で点灯、>0 で呼吸 |

`transition_ms` でモード切替をクロスフェード。演出（既定時間）: GAME_START 1500, ARM_MOVE 600, ARM_DESCEND 1200, CATCH 800, BALL_RECEIVED 1000, BALL_RELEASED 1000, SCORE 1500, GAME_CLEAR 4000, GAME_OVER 2500 ms（最大 10000 ms）。
