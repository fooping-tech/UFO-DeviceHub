# UFO デバイス通信プロトコル v1

正本（単一管理）は次の 2 ファイル。本書はその説明。

| 何 | 正本 | ミラー（自動検査） |
|---|---|---|
| パケット構造・定数 | `shared/protocol.h` | `pc/protocol.py` |
| Message / Command / Event / ACK / 状態の値 | `shared/messages.h`, `shared/messages.cpp` | `pc/protocol.py` |
| Device ID | `shared/device_ids.h` | `pc/protocol.py` |

ゲームモード（左右 Dial のカプセル転送 LED 演出・排出同期）の契約は [game-mode-interface.md](game-mode-interface.md)。

`tests/protocol/test_consistency.py` がヘッダを解析し、Python 側と値・名前・パラメータ長が一致しなければ失敗する。
`tests/protocol/vectors.json`（`tests/tools/gen_vectors.py` で生成）を C++ と Python の両方のテストが検証する（バイト単位で同一）。

---

## 1. 全体

```
PC (DeviceGateway) --USB Serial / JSON Lines--> Hub (M5ATOM) --ESP-NOW (暗号化ユニキャスト)--> 子機
```

* ゲームロジックは PC のみ。Hub は中継・信頼性・セッション・死活監視だけ。子機は I/O とフェイルセーフだけ。
* ACK は「命令の受理」。物理動作の完了はイベント（`RELEASE_DONE`, `RELAY_DONE`）で通知する。
* `RELEASE_DONE` は制御シーケンス完了であり、ボールが実際に出たことは保証しない。

## 2. ESP-NOW パケット

マルチバイト整数はすべてリトルエンディアン。構造体をそのまま送らず `encodePacket()/decodePacket()` で明示的に変換する。

| off | size | field | 説明 |
|---|---|---|---|
| 0 | 2 | magic | システム 1: `0x5546`、システム n: `0x5546 + n - 1`（システム 2 = `0x5547`） |
| 2 | 1 | version | `1` |
| 3 | 1 | source_id | 送信元 Device ID |
| 4 | 1 | destination_id | 宛先 Device ID（`0xFF` は常に拒否） |
| 5 | 1 | message_type | 下表 |
| 6 | 2 | sequence | 送信元ごとの通番（再送では同じ値） |
| 8 | 4 | session_id | PC 起点のセッション |
| 12 | 4 | command_id | COMMAND: 命令 ID / EVENT: イベント ID / ACK: 応答対象の ID |
| 16 | 1 | payload_length | 0..47 |
| 17 | n | payload | |

> **仕様との差異**: 元仕様は「ヘッダ 16 Byte、最大 64 Byte」だが、列挙されたフィールドの合計は 17 Byte。
> フィールド定義を優先し、**ヘッダ 17 Byte + payload 最大 47 Byte = 最大 64 Byte** とした。

受信時の検証（`decodePacket` は厳密）: 長さ 17..64、magic（**自システムの値のみ**）、version、message_type、`17 + payload_length == 受信長`。
同じ会場のもう一方のシステムのフレームは magic が違うので `bad_magic` として捨てる（[multi-system.md](multi-system.md)）。
さらに Hub/子機は **送信元 MAC と source_id の両方** を静的プロビジョニング表（`config/devices.h`）と照合し、宛先が自分であることを確認する。

### Message Type

| 値 | 名前 | 方向 | ACK |
|---|---|---|---|
| 0x01 | COMMAND | Hub → 子機 | 子機が ACK |
| 0x02 | EVENT | 子機 → Hub | Hub が ACK（受理まで同じ ID で再送） |
| 0x03 | ACK | 双方向 | なし |
| 0x04 | STATUS | 子機 → Hub | なし（GET_STATUS の応答） |
| 0x05 | HEARTBEAT | 双方向 | なし（Hub 1 秒周期、子機は即応答） |
| 0x06 | ERROR | 子機 → Hub | なし（予約、プロトコル異常の通知） |

### Payload

COMMAND / EVENT の payload は先頭 1 Byte がコード。

#### COMMAND（`CommandCode`）

| code | 名前 | params (code の後) | セッション必須 |
|---|---|---|---|
| 0x01 | SESSION_BEGIN | なし（session は header） | Hub 専用 |
| 0x02 | SESSION_END | なし | Hub 専用 |
| 0x03 | GET_STATUS | なし → ACK + STATUS | 不要 |
| 0x04 | STOP_ALL | なし | **不要（常に受理）** |
| 0x05 | SET_MODE | u8 OperationMode (0 STANDBY / 1 GAME) | 必須 |
| 0x10 | SET_LIGHT_MODE | u8 mode, u8 brightness, u16 transition_ms, u8 speed, u8 r, u8 g, u8 b | 必須 |
| 0x11 | TRIGGER_EFFECT | u8 effect, u16 duration_ms (0=既定), u8 intensity | 必須 |
| 0x12 | RELAY_PULSE | u8 relay_id, u16 duration_ms (1..) | 必須 |
| 0x13 | RELAY_OFF | u8 relay_id (0xFF=全部) | 不要 |
| 0x14 | SET_MIST | u8 MistPattern, u16 duration_s（0 = 機器の最大。最大は既定 600 s、超えると `clamped`）。OFF で停止 | 必須 |
| 0x20 | ARM_RECEIVER | なし | 必須 |
| 0x21 | DISARM_RECEIVER | なし | 不要 |
| 0x22 | RELEASE_BALL | なし（角度・保持時間は機器設定でありワイヤに載せない） | 必須 |
| 0x23 | SET_DIAL_EFFECT | u8 DialEffect, u8 DialDirection, u8 r, u8 g, u8 b（0,0,0 = 号機色）, u16 duration_ms（0 = 置き換えまで）, u8 EffectPriority, u8 DialLedTarget | 必須 |
| 0x24 | SET_DIAL_COLOR | u8 r, u8 g, u8 b（号機色。0,0,0 = 設定の既定色） | 必須 |

params 長が表と 1 Byte でも違えば `INVALID_PARAMS`。

#### EVENT（`EventCode`）

| code | JSON 名 | data |
|---|---|---|
| 0x10 | BALL_RECEIVED | u16 cycle |
| 0x11 | RELEASE_STARTED | u32 command_id |
| 0x12 | RELEASE_DONE | u32 command_id, u16 elapsed_ms |
| 0x13 | RELEASE_FAILED | u32 command_id, u8 reason (1 ABORTED, 2 LINK_LOST, 3 SERVO_NOT_CONFIGURED) |
| 0x14 | DIAL_REQUEST | u8 request (1 GAME_RESTART, 2 GAME_END)。Dial 本体のメニューで人が選んだ依頼（fw 0.5〜）。JSON の data は `request`（名前）と `code`（数値）。Dial は依頼を送るだけで、どうするかは Game が決める。[game-mode-interface.md](game-mode-interface.md) 2.7 |
| 0x20 | RELAY_DONE | u8 relay_id, u16 on_ms, u8 reason (0 COMPLETED, 1 OFF_COMMAND, 2 SAFETY, 3 WATCHDOG, 4 LOCAL_BUTTON) |
| 0x21 | MIST_DONE | u16 on_s（実際に ON だった秒数）, u8 reason（RELAY_DONE と同じ）。ローカル操作で動いたミストでは送らない |
| 0x30 | ERROR | u8 ErrorCode, u8 detail |
| 0x40 | STAFF_ACTION | u8 StaffAction (1 NEXT, 2 SKIP, 3 MENU, 4 RECOVER, 5 SWITCH, 6 STOP)。スタッフ用リモコン（M5StopWatch、0x30、fw 0.6〜）で押された、ゲームのスタッフ操作（CoreXY_UFO_Game `docs/staff_operations.md` の F6〜F10・Space）。JSON の data は `action`（名前）と `code`。リモコンは送るだけで、どうするかは Game が決める |
| 0x41 | REMOTE_INPUT | u8 buttons (0x01 MOVE: 移動ボタンを押している, 0x02 AXIS_Z: 前後の傾きで上下), i8 x, i8 y（-100..100、右・奥が +）, u8 RemotePose (0 OPEN, 1 FIST, 2 PEACE, 3 POINT), u16 frame。JSON の data は `move, z, x, y, pose`（名前）, `frame`。押している間 100 ms ごと、離したら 1 回（何も押していない）、その後は送らない。最新だけが有効: 新しいフレームを入れる前に、Hub の ACK を待っている古い REMOTE_INPUT を送信待ちから消す。停止（STOP・STOP_ALL・セッションの変化・通信断）でも消す（古い傾きが遅れて届くことはない）。ほかのイベントが 4 件以上たまっているときはその回を飛ばす。Game は来なくなったら止める |

#### ACK（6 Byte）

`u8 acked_type, u8 result, u8 flags, u16 acked_seq, u8 detail`。header の command_id = 応答対象の ID。

flags: `0x01 DUPLICATE`（同じ command_id を処理済み。結果は保存値）、`0x02 CLAMPED`（受理したがパラメータを制限）。

| result | 名前 | 発生源 |
|---|---|---|
| 0x00 | ACCEPTED | 子機 |
| 0x01 | BUSY | 子機（動作中） |
| 0x02 | REJECTED_STATE | 子機（状態が条件を満たさない） |
| 0x03 | REJECTED_SESSION | 子機（未確立 / 旧セッション） |
| 0x04 | INVALID_PARAMS | 子機 |
| 0x05 | UNSUPPORTED | 子機（その機器にない命令） |
| 0x06 | COOLDOWN | 子機（リレー再噴射禁止時間中） |
| 0x07 | IGNORED_PRIORITY | 子機（より優先度の高い演出中） |
| 0x08 | NOT_CONFIGURED | 子機（ピン未設定） |
| 0x80 | TIMEOUT | Hub（再送上限） |
| 0x81 | OFFLINE | Hub（子機通信断） |
| 0x82 | QUEUE_FULL | Hub |
| 0x83 | UNKNOWN_DEVICE | Hub |
| 0x84 | NO_SESSION | Hub |
| 0x85 | BAD_REQUEST | Hub（JSON / パラメータ不正） |
| 0x86 | SESSION_MISMATCH | Hub |

#### HEARTBEAT

* Hub → 子機（5 Byte）: `u32 hub_uptime_ms, u8 flags(bit0 = PC セッションあり)`。header.session = Hub の現在セッション。
* 子機 → Hub（18 Byte）: `u16 echo_seq, u32 boot_id, u32 uptime_ms, u8 device_type, u8 state, u8 flags, i8 rssi, u16 rx_rejected, u8 fw_major, u8 fw_minor`
  * `echo_seq` で Hub が RTT を測る。`boot_id` の変化で再起動を検出する。`rssi` は Arduino-ESP32 2.x では 0（不明）。

#### STATUS

共通 15 Byte: `u8 device_type, u8 state, u8 flags, u8 error, u8 op_mode, u32 uptime_ms, u32 boot_id, u8 fw_major, u8 fw_minor`
+ Illumination 18 Byte: `u8 light_mode, u8 brightness, u8 active_effect, u16 effect_remaining_ms, u8 relay_on_mask, u16 relay_remaining_ms, u16 relay_cooldown_ms, u16 relay_pulses, u8 mist_pattern, u8 mist_flags (0x01 OUTPUT_ON, 0x02 LOCAL_MODE), u16 mist_remaining_s, u16 mist_on_s`
+ スタッフ用リモコン: 共通部分だけ（state 0 IDLE / 1 ACTIVE = REMOTE_INPUT を送っている）
+ Dial 18 Byte: `u8 dial_state, u16 cycle, u8 switch_on, u8 servo_deg, u16 releases, u32 last_release_command, u8 sim_button, u8 front_effect, u8 transfer_effect, u8 led_flags (0x01 TRANSFER_FITTED), u8 color_r, u8 color_g, u8 color_b`
  * 末尾 6 Byte はファームウェア 0.3 で追加。0.2 以前の Dial の 12 Byte も受理する（追加項目は 0）

NodeFlag: `0x01 SESSION_VALID, 0x02 HUB_LINK_UP, 0x04 BUSY, 0x08 EXAMPLE_KEYS, 0x10 CONFIG_ERROR, 0x20 RELAY_ON（蒸気リレーまたはミスト）, 0x40 SWITCH_ON, 0x80 GAME_MODE`

#### 列挙値

* LightMode: 0 OFF, 1 IDLE, 2 STUDY, 3 GAME_READY, 4 GAME_PLAY, 5 RESULT, 6 ERROR, 7 CUSTOM, 8 AURORA, 9 STORM, 10 CAMPFIRE, 11 MIST_GLOW, 12 MAGIC_PUFF, 13 NIGHT
* MistPattern: 0 OFF, 1 CONTINUOUS, 2 BREATH（8 秒出す / 6 秒休む）, 3 PUFF（3 秒出す / 9 秒休む）
* EffectId: 1 GAME_START, 2 ARM_MOVE, 3 ARM_DESCEND, 4 CATCH, 5 BALL_RECEIVED, 6 BALL_RELEASED, 7 SCORE, 8 GAME_CLEAR, 9 GAME_OVER
* DialState: 0 DISABLED, 1 READY, 2 ARMED, 3 BALL_DETECTED, 4 RELEASING, 5 RETURNING, 6 COMPLETE, 7 ERROR, 8 SETTINGS（本体の設定モード。STOP_ALL 以外の命令は REJECTED_STATE）
* DialEffect: 0 NONE, 1 SOLID, 2 BREATHE, 3 CHASE, 4 COMET, 5 WIPE, 6 FLASH, 7 SPARKLE（CHASE / COMET / WIPE は方向が必須）
* DialDirection: 0 NONE, 1 TO_SCREEN（実機 → 画面）, 2 TO_DIAL（画面 → 実機）
* EffectPriority: 0 BACKGROUND, 1 NORMAL, 2 HIGH（実行中より低い優先度は `IGNORED_PRIORITY`）
* DialLedTarget: 1 FRONT（G1 の LED）, 2 TRANSFER（G15 の LED。使わない設定なら `NOT_CONFIGURED`）, 3 BOTH
* IlluminationState: 0 NORMAL, 1 EFFECT, 2 RELAY_ACTIVE, 3 MIST_ACTIVE, 7 ERROR

---

## 3. 信頼性・セッション

### 初期値（`shared/link_config.h`、`config/devices.h` か `-D` で変更可）

| 項目 | 値 |
|---|---|
| Heartbeat 周期 | 1000 ms |
| 通信断判定 | 3000 ms（子機: Hub フレームなし / Hub: 子機フレームなし） |
| ACK タイムアウト | 100 ms |
| 最大再送 | 3 回（計 4 送信） |
| イベント再送 | 3 回の高速再送後、500 ms 間隔で ACK まで継続 |
| PC 無通信判定 | 3000 ms → Hub がセッションを閉じる |
| Hub → PC イベント再送 | 500 ms 間隔で EVENT_ACK まで |

### 命令（PC → 子機）

1. PC が `command_id`（セッション内一意、1..0x7FFFFFFF）を振る。`0x80000000` 以上は Hub 専用。
2. Hub は宛先ごとに **1 件ずつ** 送信し（同一機器の順序保証）、100 ms で ACK がなければ同じバイト列を再送、3 回で `TIMEOUT` を PC に返す。
3. 子機は `command_id` と結果を直近 16 件保存する（`DedupCache`）。同じ ID の再送は **再実行せず**、保存結果に `DUPLICATE` を付けて ACK する。
4. ESP-NOW の送信完了コールバックは ACK とは見なさない（診断カウンタ `tx_mac_ok/fail` のみ）。

### イベント（子機 → PC）

1. 子機はイベント ID（起動ごとに乱数開始、単調増加）を付け、Hub の ACK まで **同じ ID で** 再送する。
2. Hub は PC 向け送信箱（16 件）に入れてから子機へ ACK。送信箱が満杯なら ACK しない（子機が後で再送 = 背圧）。
3. Hub は `EVENT_ACK` を受けるまで 500 ms ごとに PC へ再送する。
4. PC（`DeviceGateway`）は受信したすべての EVENT に `EVENT_ACK` を返し、`(source, event_id)` で重複を除去する。セッション違いのイベントは捨てる。

### セッション

* PC が接続（再接続）するたびに乱数の `session_id` を作り `HELLO` を送る。
* Hub は新セッションで **キュー中の命令と未配送イベントを破棄**し、全子機に `SESSION_BEGIN` を送る。
* 子機は `SESSION_BEGIN` でのみセッションを確立する。Hub の Heartbeat のセッションが自分と違えば即座に自セッションを終了（安全状態）し、Hub が再度 `SESSION_BEGIN` を送る（再同期）。
* セッションが変わると子機は重複排除キャッシュと未送信イベントを捨て、ゲーム状態（GAME モード・受取待ち）を解除する。
* 物理動作命令は「確立済みかつ一致するセッション」でのみ実行する。
* PC 無通信 3 秒 / `BYE` → Hub が `SESSION_END` を送る → リレー OFF、ダイヤル DISABLED。
* 子機が再起動したこと（`boot_id` の変化）を Hub が検出すると、その子機宛ての未送信命令を破棄する（古い動作命令を再送しない）。

---

## 4. PC ⇔ Hub JSON Lines

* USB Serial 115200 bps、UTF-8、1 行 1 JSON オブジェクト、改行 `\n`。
* Hub の受信行は最大 512 Byte。超えた行は改行まで捨てて `ERROR LINE_TOO_LONG`。
* `protocol_version` は省略可。指定する場合は `1`。

### PC → Hub

| type | フィールド | 応答 |
|---|---|---|
| `HELLO` | `session_id` (≠0), `system_id`（省略可。指定した場合、違うシステムの Hub は `ERROR SYSTEM_MISMATCH` を返してセッションを張らない） | `HELLO_ACK` |
| `HEARTBEAT` | | `HEARTBEAT {session_id, uptime_ms}` |
| `COMMAND` | `destination, session_id, command_id, name, params` | `ACK`（後で） |
| `EVENT_ACK` | `source, event_id` | なし |
| `GET_DEVICES` | | `DEVICES` |
| `GET_DIAG` | | `DIAG` |
| `BYE` | | `SESSION_CLOSED`（セッション終了 → 子機安全化） |

```json
{"protocol_version":1,"type":"COMMAND","destination":16,"session_id":123,"command_id":1001,"name":"SET_LIGHT_MODE","params":{"mode":"GAME_PLAY","brightness":180}}
```

COMMAND の `name` と `params`:

| name | params（既定値） |
|---|---|
| `SET_LIGHT_MODE` | `mode`(必須), `brightness`(128), `transition_ms`(0), `speed`(128), `color` (`[r,g,b]` / `"#RRGGBB"`) |
| `TRIGGER_EFFECT` | `effect` または `effect_id`(必須), `duration_ms`(0=既定), `intensity`(255) |
| `RELAY_PULSE` | `relay_id`(0), `duration_ms`(必須, 1..65535。3000 超は機器側で 3000 に制限) |
| `RELAY_OFF` | `relay_id`(255=全部) |
| `SET_MIST` | `pattern`(必須: `OFF` / `CONTINUOUS` / `BREATH` / `PUFF`), `duration_s`(0=機器の最大) |
| `SET_MODE` | `mode`: `STANDBY` / `GAME` |
| `SET_DIAL_EFFECT` | `effect`(必須), `direction`(NONE), `color`(省略 = 号機色), `duration_ms`(0), `priority`(NORMAL), `target`(BOTH) |
| `SET_DIAL_COLOR` | `color`（省略 / `[0,0,0]` = 設定の既定色） |
| `ARM_RECEIVER`, `DISARM_RECEIVER`, `RELEASE_BALL`, `STOP_ALL`, `GET_STATUS` | なし |

列挙は名前（`"GAME_PLAY"`）でも数値でもよい。

### Hub → PC

| type | 内容 |
|---|---|
| `BOOT` | Hub 起動（`fw, hub_mac, channel, system_id, system_check, example_keys`）。PC はセッションを張り直す |
| `HELLO_ACK` | `session_id, fw, hub_mac, channel, system_id, system_check, example_keys, example_devices` |
| `ACK` | `destination, command_id, result, origin ("device"/"hub"), duplicate, clamped, attempts, reason?` |
| `EVENT` | `source, session_id, event_id, name, attempt, data` |
| `STATUS` | `source, device_type, state, flags{...}, op_mode, uptime_ms, boot_id, fw, error` + 機器別項目（Illumination: `light_mode, brightness, active_effect, effect_remaining_ms, relay_*, mist_pattern, mist_on, local_mode, mist_remaining_s, mist_on_s` / Dial: `dial_state, cycle, switch_on, servo_deg, releases, last_release_command, sim_button, front_effect, transfer_effect, transfer_led, unit_color`） |
| `LINK` | `source, online` |
| `DEVICE_REBOOTED` | `source, boot_id` |
| `SESSION_CLOSED` | `session_id, reason (pc_timeout / pc_request)` |
| `DEVICES` | `devices[]: id, name, type, mac, online, last_seen_ms, state, flags, session_ok, boot_id, uptime_ms, fw, rssi, rtt_ms` |
| `DIAG` | Hub MAC、fw、カウンタ（不正パケット種類別、ACK タイムアウト、再送、送受信数、RTT 等）、ESP-NOW 送受信統計、子機ごとのカウンタ |
| `ERROR` | `code (BAD_JSON / LINE_TOO_LONG / UNSUPPORTED_VERSION / UNKNOWN_TYPE / BAD_REQUEST / OUTPUT_TOO_LONG / SYSTEM_MISMATCH), detail` |
| `LOG` | `level, msg` |

---

## 5. Python API（`pc/`）

```python
from pc import DeviceGateway, DeviceId

gw = DeviceGateway(port="auto", system_id=1)  # 自動検出・自動再接続。system_id の Hub にだけ接続する
gw.subscribe(on_event, names={"BALL_RECEIVED", "RELEASE_DONE", "DEVICE_OFFLINE"})
gw.connect()                               # セッション確立まで待つ

gw.set_light_mode("GAME_PLAY", brightness=180, transition_ms=300)
gw.trigger_effect("SCORE")
gw.set_mist("BREATH", duration_s=120)     # 終了時に MIST_DONE。gw.mist_off() で停止
gw.relay_pulse(1500)
gw.set_mode(DeviceId.M5DIAL_1, "GAME")
gw.arm_receiver(DeviceId.M5DIAL_1)
res = gw.release_ball(DeviceId.M5DIAL_1)   # CommandResult(result="ACCEPTED", ...)
gw.set_dial_effect(DeviceId.M5DIAL_1, "CHASE", "TO_SCREEN", duration_ms=1500)  # LED 演出（game-mode-interface.md）
gw.set_dial_color(DeviceId.M5DIAL_2, (255, 0, 255)); gw.clear_dial_effect(DeviceId.M5DIAL_1)
gw.get_status(DeviceId.M5DIAL_1); gw.get_devices(); gw.get_diagnostics()
gw.stop_all()
```

* すべての命令は `CommandResult` を返す（`.ok`, `.result`, `.duplicate`, `.clamped`, `.attempts`）。`wait=False` で `Future`。
* 未接続時は `GatewayNotConnected` を送出し **何も送らない**（動作命令を後から送ることはない）。
* パラメータ不正は `ProtocolError`（送信前）。
* イベント: `BALL_RECEIVED, RELEASE_STARTED, RELEASE_DONE, RELEASE_FAILED, DIAL_REQUEST, RELAY_DONE, MIST_DONE, STAFF_ACTION, REMOTE_INPUT, ERROR`（機器）と `DEVICE_ONLINE, DEVICE_OFFLINE, DEVICE_REBOOTED, SESSION_CLOSED, GATEWAY_CONNECTED, GATEWAY_DISCONNECTED, HUB_ERROR`（通信層）。
* `event_queue()` / `asyncio_queue(loop)` でも受け取れる。
* `DeviceGateway(trace=TraceLog(path).record)` で命令・ACK・イベントを JSON Lines に記録する（`pc/trace.py`）。名前付きのゲームモード演出は `pc/game_mode.py`。
