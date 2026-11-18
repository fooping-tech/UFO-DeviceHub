# ゲームモード連携インターフェース（左右 Dial のカプセル転送 LED・排出同期）

「SUMMON THE UFO」の実物カプセル転送で、**画面左の 1 号機 = Dial1 (0x20)**、**画面右の 2 号機 = Dial2 (0x21)** が行う受取・排出・LED 演出の **通信・LED 契約の正本**。

| 何 | 正本 |
|---|---|
| ゲーム仕様（画面演出、UFO の動き、投下 X 座標の固定タイミングなど） | CoreXY_UFO_Game `docs/game_mode_spec.md`（Game 側 Issue #138） |
| 通信・LED 契約（本書） | このリポジトリ `docs/game-mode-interface.md` |
| パケット・列挙値 | [protocol.md](protocol.md)（`shared/messages.h` と `pc/protocol.py`） |
| ピン・電源・設定値 | [hardware.md](hardware.md)（`config/hardware.example.h`） |
| 試験 | [test-plan.md](test-plan.md) |

対応 Issue: UFO-DeviceHub #8 / CoreXY_UFO_Game #138。

> **状態**: ロジックは native テスト・pytest・Mock で確認済み。**実機（2 台）では未確認**。時間・色・LED の上下の向き、G15 の LED の個数は実機で確定する。

---

## 1. 役割分担

* ゲームの判定は PC（Game）だけが行う。Dial は「検出を報告する」「命令を実行する」「自分の状態を光で示す」だけ。
* 左右はそれぞれ 1 カプセル。2 台は別の機器なので、同時受取・同時排出・逆方向同時は互いに干渉しない（命令は宛先 ID ごと、Hub の送信キューも宛先ごと）。
* LED は演出であり、**サーボ・受取検出・イベント送信を待たせたり止めたりしない**。LED 命令が失敗しても排出シーケンスは変わらない。

## 2. 追加したコマンド（プロトコル version は 1 のまま、追加のみ）

### 2.1 `SET_DIAL_EFFECT` (0x23)

Dial の LED に、色・モード・方向・時間・優先度を指定して演出を出す。セッション必須。

| off | 型 | 項目 | 値 |
|---|---|---|---|
| 0 | u8 | effect | `DialEffect` |
| 1 | u8 | direction | `DialDirection` |
| 2 | u8×3 | r, g, b | `0,0,0` = **号機色**（2.2） |
| 5 | u16 | duration_ms | 0 = 置き換え・終端まで続く。1..65535 = その時間で自動終了 |
| 7 | u8 | priority | `EffectPriority` |
| 8 | u8 | target | `DialLedTarget`（ビットマスク） |

`effect_id`（`DialEffect`）:

| id | 名前 | 内容 | 方向 |
|---|---|---|---|
| 0 | NONE | PC 演出を止める（Dial 自身の状態表示に戻る） | - |
| 1 | SOLID | 単色点灯 | - |
| 2 | BREATHE | 呼吸（2 s 周期） | - |
| 3 | CHASE | 4 画素ごとの光が流れる（70 ms / 画素） | **必須** |
| 4 | COMET | 尾を引く光が 1 つ流れる（0.7 s で 1 往路、繰り返し） | **必須** |
| 5 | WIPE | `duration_ms` かけて端から満ちる（0 のときは 1 s 周期で繰り返し） | **必須** |
| 6 | FLASH | 5 Hz 点滅 | - |
| 7 | SPARKLE | きらめき | - |

`DialDirection`: 0 NONE / 1 **TO_SCREEN**（実機 → 画面。取り込み） / 2 **TO_DIAL**（画面 → 実機。排出）。CHASE・COMET・WIPE で NONE は不正。

`EffectPriority`: 0 BACKGROUND / 1 NORMAL / 2 HIGH。**実行中の演出より低い優先度の命令は `IGNORED_PRIORITY`**（同じか高ければ置き換える。NONE による停止も同じ規則）。

`DialLedTarget`: 1 FRONT（G1 の 26 LED） / 2 TRANSFER（G15 の LED） / 3 BOTH。2 系統は別々の演出を持てる。BOTH は「両方に適用するか、どちらにも適用しないか」。

JSON（PC ⇔ Hub）:

```json
{"type":"COMMAND","destination":32,"session_id":123,"command_id":2001,"name":"SET_DIAL_EFFECT",
 "params":{"effect":"CHASE","direction":"TO_SCREEN","color":[0,255,255],"duration_ms":1500,"priority":"NORMAL","target":"BOTH"}}
```

既定値: `direction` NONE、`color` 省略 = 号機色、`duration_ms` 0、`priority` NORMAL、`target` BOTH。列挙は名前でも数値でもよい。

### 2.2 `SET_DIAL_COLOR` (0x24)

号機色（u8 r, g, b）をセッションの間だけ変更する。`0,0,0`（JSON で `color` 省略）は設定ファイルの既定色に戻す。セッション必須。

* 既定色は `config/hardware.h` の `DIAL_UNIT_COLOR_1` = シアン `(0,255,255)`、`DIAL_UNIT_COLOR_2` = マゼンタ `(255,0,255)`。**初期提案色**で、設定または本コマンドで変えられる。
* 号機色は、Dial 自身の受取・排出アニメーション、ARMED 表示、色を省略した PC 演出に使う。**Game は画面側ポータルに同じ色を使う**ことで、画面と実機の同系色を保証する（現在値は STATUS の `unit_color` で読める）。
* セッションが変わる・再起動すると既定色に戻る。色を変える運用では、セッション確立のたびに送り直す。

### 2.3 ACK / NACK

ACK は「受理」であり、演出の完了ではない（演出に完了イベントはない）。

| result | 発生元 | 条件 |
|---|---|---|
| ACCEPTED | Dial | 適用した |
| INVALID_PARAMS | Dial | 長さ不一致、未知の effect / direction / priority / target、方向が必要な効果で NONE |
| IGNORED_PRIORITY | Dial | 対象の出力で、より優先度の高い演出が実行中 |
| NOT_CONFIGURED | Dial | `target` = TRANSFER だが G15 の LED を使わない設定（`DIAL_TRANSFER_LED_PIN -1`）。BOTH なら FRONT だけに適用して ACCEPTED |
| REJECTED_STATE | Dial | 設定モード中、または設定エラー（ERROR）表示中 |
| REJECTED_SESSION | Dial | セッション未確立・旧セッション |
| UNSUPPORTED | 子機 | Dial 以外の機器、または**ファームウェア 0.2 以前の Dial** |
| BAD_REQUEST | Hub | JSON のパラメータ不正、または**ファームウェア 0.2 以前の Hub**（`unknown command`） |
| OFFLINE / TIMEOUT / NO_SESSION / SESSION_MISMATCH | Hub | 既存どおり |

PC ライブラリは不正なパラメータを送信前に `ProtocolError` にする。

### 2.4 重複排除

既存の仕組みをそのまま使う。Hub は ACK がなければ同じ `command_id` で再送し、Dial は `command_id` を覚えていて**再適用しない**（`duplicate: true` で ACK）。再送でアニメーションが最初からやり直しになることはない。イベントは `(source, event_id)` で PC 側が重複を除く。

### 2.5 STATUS（Dial）への追加

Dial の STATUS は 12 → 18 Byte（末尾に追加）。JSON では次の項目が増える。

| 項目 | 内容 |
|---|---|
| `front_effect` / `transfer_effect` | PC 演出の effect 名（`NONE` = Dial 自身の状態表示中） |
| `transfer_led` | G15 の LED が有効か |
| `unit_color` | 使用中の号機色 `[r,g,b]` |

### 2.6 バージョン互換

| 組み合わせ | 動作 |
|---|---|
| 新 PC ライブラリ + 新 Hub + 新 Dial（fw 0.3） | 全機能 |
| 新 Hub + **旧 Dial**（fw 0.2） | 新コマンドは `UNSUPPORTED`。STATUS（12 Byte）は受理し、LED 項目は `NONE` / `false` / `[0,0,0]` |
| **旧 Hub** + 新 Dial | 新コマンドは Hub が `BAD_REQUEST`。**Dial の 18 Byte STATUS を旧 Hub は捨てる**（`rx_bad_payload`）ので GET_STATUS が応答なしになる。**Hub と Dial は同じ版に書き換えること** |
| 旧 PC ライブラリ + 新ファームウェア | 既存機能はそのまま動く。STATUS の追加項目は無視される |

既存コマンド・イベント（`BALL_RECEIVED`, `RELEASE_STARTED`, `RELEASE_DONE`, `RELEASE_FAILED`, `SET_MODE`, `ARM_RECEIVER`, `RELEASE_BALL`, `STOP_ALL`）のバイト列・意味は変えていない。ファームウェアの版は `GET_DEVICES` の `fw` で確認できる（本変更 = `0.3`）。

### 2.7 `DIAL_REQUEST` イベント (0x14)（Dial 本体のメニュー、fw 0.5〜）

Dial のダイヤルを回すと本体にメニューが出る（操作は [hardware.md](hardware.md#メニューダイヤルを回す)）。このうち **GAME RESTART** と **GAME END** は、Dial から Game への依頼としてイベントで届く。

```json
{"type":"EVENT","source":33,"session_id":123,"event_id":7,"name":"DIAL_REQUEST","data":{"request":"GAME_RESTART","code":1}}
```

| `request` | `code` | 意味 |
|---|---|---|
| `GAME_RESTART` | 1 | ゲームを最初からやり直してほしい |
| `GAME_END` | 2 | ゲームを終了してほしい |

* **Dial は依頼を送るだけ**で、自分の状態（ARMED / BALL_DETECTED など）も LED も変えない。リスタート・終了の中身（`STOP_ALL`、`SET_MODE`、画面遷移、確認ダイアログの有無）は Game が決めて、通常のコマンドで Dial を動かす。
* `source` で左右どちらの Dial から来たかが分かる。どちらの Dial からでも同じ意味として扱うことを推奨する。
* ほかのイベントと同じく、Hub が ACK するまで同じ `event_id` で再送され、PC ライブラリは重複を 1 回にまとめる。**セッションがないときは送られない**（Dial の画面に `NO GAME` と出る）。Game が起動していない間に押された依頼が、あとから届くことはない。
* 未知の `request`（将来の追加）は `"UNKNOWN"` + `code` で届く。Game は知らない値を無視すること。
* メニューの残りの項目（DIAL RESET / POWER OFF / RELEASE）は Dial 本体だけで完結し、イベントは出ない。Game から見える変化:

| 項目 | Game から見えること |
|---|---|
| DIAL RESET | `DEVICE_REBOOTED` が届く（再起動が速いと `DEVICE_OFFLINE` は出ない）。Dial は `DISABLED` に戻るので、`SET_MODE GAME` → `ARM_RECEIVER` からやり直す |
| POWER OFF | 通信断の判定時間がたつと `DEVICE_OFFLINE`。電源を入れ直すと `DEVICE_REBOOTED` |
| RELEASE | **イベントなし**（`RELEASE_*` は出ない）。Dial の状態も変わらない。動作中は `RELEASE_BALL` / `ARM_RECEIVER` が `BUSY` になる。カプセルを保持したまま（BALL_DETECTED）人が排出しても Game には伝わらないので、その後の `RELEASE_BALL` は空の排出動作になる |

```python
def on_event(ev):
    if ev.name == "DIAL_REQUEST":
        if ev.data["request"] == "GAME_RESTART": restart_game()
        elif ev.data["request"] == "GAME_END": end_game()
gw.subscribe(on_event)
```

Mock: `hub.dial_request(DeviceId.M5DIAL_2, "GAME_RESTART")`。**旧 Hub（fw 0.4 以前）はこのイベントを捨てる**ので、Hub と Dial を同じ版にすること。

## 3. LED の出力と向き

NeoGacha の筐体をそのまま使う。LED は Dial 1 台につき 2 系統（NeoGacha `M5Dial_Gacha.ino` の `LED_PIN2 = 1` = frontLed、`LED_PIN1 = 15` = topLed）。

| 出力（`target`） | 配線 | 制御 | 状態 |
|---|---|---|---|
| FRONT | G1、26 LED（`DIAL_LED_PIN` / `DIAL_LED_COUNT`） | その Dial | 実装済み |
| TRANSFER | **G15 の LED**（`DIAL_TRANSFER_LED_PIN 15`） | その Dial | 実装済み。**LED 数・並びは未確認**（既定 26 個・FRONT と同じ並び。実機に合わせて `DIAL_TRANSFER_LED_COUNT` を設定する） |

* 左右の割当は機器 ID で決まる: **左 = 0x20、右 = 0x21**。1 つの命令は 1 台の Dial だけに届くので、左右は常に独立。
* LED の並び（NeoGacha `NEOPIXELLED::level2()` のとおり）: **前半の画素は下から上へ、後半の画素は上から下へ**並ぶ 2 列。画素 `i` と画素 `count-1-i` が同じ高さになる。
  * `DIAL_LED_FOLDED 1`（既定）で、2 列が同じ高さで同じように光る。0 にすると 1 本の線として扱う。
* 向きは「経路」で定義する。位置 0 が実機側、末端が画面側。TO_SCREEN は位置 0 から末端へ、TO_DIAL はその逆へ光が動く。
  * 既定では **下 = 実機側、上 = 画面側**（TO_SCREEN は下から上へ流れる）。**未確認**: 逆なら `DIAL_LED_REVERSED 1` / `DIAL_TRANSFER_LED_REVERSED 1`。
* G15 の LED を使わない機体は `DIAL_TRANSFER_LED_PIN -1` にする。その場合、TRANSFER 宛ては `NOT_CONFIGURED`、BOTH は FRONT だけに適用される。

### LED の制御責務・電源

* 決定: **2 系統とも、左右それぞれの Dial が駆動する**。左右独立制御がそのまま成り立ち、受取時にローカルで即発光でき、演出が Dial の状態（受取・排出・STOP）と同じ規則で止まる。
* G15 の LED の輝度は `DIAL_TRANSFER_LED_BRIGHTNESS`（既定 25 = FRONT と同じ）。M5Dial の 5 V から給電している前提（`DIAL_TRANSFER_LED_SEPARATE_POWER 0`）で、サーボ動作中は FRONT と同じく `DIAL_LED_BRIGHTNESS_SERVO` まで減光する。
* **NeoGacha では G15 の LED は点灯させていなかった**（`topLed.setup()` がコメントアウト）。2 系統を同時に光らせたときの電流と、サーボ動作時にブラウンアウトしないことは**実機で未確認**。問題があれば輝度を下げるか別電源にする（別電源にしたら `DIAL_TRANSFER_LED_SEPARATE_POWER 1`）。
* 画面と Dial の間に、さらに別の LED テープを足す場合の配線・制御は未定（現状は上の 2 系統だけで完結する）。

Illumination ATOM の流用可否（確認結果）:

* WS2811 テープ（70 IC、G21）は 1 本のデータ線で、`LedRenderer` が照明モード・演出を全体に描いている。区間を切って左右の転送路に使うことは技術的には可能だが、(1) 環境照明と取り合いになる、(2) 12 V の WS2811 は 1 IC = 3 LED で分解能が粗い、(3) 受取時のローカル即発光ができず PC 経由の遅延が乗る、(4) 新しい区間指定コマンドと `LightingController` の改修が必要。**採用しない**。
* COB テープ（G22、PWM）は単色・単チャンネルで方向表現ができない。**流用不可**。

## 4. 表示の優先順位（ローカル表示と PC 演出の競合）

各出力は、上から順に最初に当てはまるものを表示する。

1. **設定モード / Hub 通信断（赤の呼吸） / 設定エラー（赤点滅）** — PC 演出は破棄する。
2. **受取フラッシュ**: ボール検出から `DIAL_RX_FLASH_MS`（150 ms）は白。PC の命令を待たずに、検出した瞬間に子機ローカルで光る。
3. **PC 演出**（`SET_DIAL_EFFECT`、実行中の間）。
4. **Dial 自身の状態表示**:

| 状態 | FRONT（G1） | TRANSFER（G15） |
|---|---|---|
| DISABLED | pacifica | 消灯 |
| READY | 白のゆっくり呼吸 | 消灯 |
| ARMED | 号機色のチェイス（受取待ち） | 消灯 |
| BALL_DETECTED | 白フラッシュ → 号機色 CHASE **TO_SCREEN**（`DIAL_RX_CHASE_MS` = 1500 ms） → 号機色の呼吸（待機表示） | 同じ |
| RELEASING / RETURNING | 号機色 CHASE **TO_DIAL** | 同じ |
| COMPLETE | フェードアウト | 消灯 |

PC 演出を Dial が自分で終わらせる契機（優先度に関係なく破棄）:

| 契機 | 理由 |
|---|---|
| ボール検出（BALL_DETECTED になった瞬間） | 受取のローカル発光を必ず見せる。前の演出は前の局面のもの |
| 排出シーケンスの終了（`RELEASE_DONE` / `RELEASE_FAILED` を出すとき） | 排出演出の終端処理。PC の後始末命令が届かなくても光り続けない |
| `STOP_ALL` | 安全優先 |
| セッションの変更・終了（PC 無通信 3 秒、`BYE`、再接続） | 号機色も既定に戻る |
| Hub との通信断（3 秒） | 止める手段がなくなるため。復帰後も再開しない |
| 設定モードに入る | 本体操作を優先 |
| 再起動 | 何も復元しない |

受取と PC 後着演出の整合: Dial は検出時に「白 → 号機色 TO_SCREEN」をローカルで始める。Game が `BALL_RECEIVED` を受けて送る取り込み演出（下表 `INTAKE`）は**同じ色・同じ方向**なので、後から届いても見た目がつながる。フラッシュ中に届いた PC 演出はフラッシュの後に表示される。

## 5. アニメーション一覧（提案値）

`pc/game_mode.py` の `ANIMATIONS` が実行可能な形の正本。時間は**提案値**で実機校正で確定する。色はすべて号機色（左 = シアン、右 = マゼンタ）。

| 名前 | 誰が | effect_id | direction | duration_ms | priority | target | いつ |
|---|---|---|---|---|---|---|---|
| （受取フラッシュ） | Dial ローカル | SOLID 白 | - | 150 | - | 両方 | スイッチ ON を 30 ms デバウンスした瞬間 |
| （受取チェイス） | Dial ローカル | CHASE | TO_SCREEN | 1500 | - | 両方 | フラッシュの直後 |
| `ATTRACT` | Game（任意） | BREATHE | - | 0 | BACKGROUND | BOTH | ARMED 中の呼び込み |
| `INTAKE` | Game | CHASE | TO_SCREEN | 1500 | NORMAL | BOTH | `BALL_RECEIVED` 受信時。画面ポータルの吸引と同時 |
| `RETURN_FLOW` | Game | COMET | TO_DIAL | 0 | NORMAL | BOTH | Game が `RETURNING` を始めたとき |
| `RELEASING` | Game（任意） | CHASE | TO_DIAL | 0 | NORMAL | BOTH | `RELEASE_STARTED` 受信時。送らなければ `RETURN_FLOW` が続く |
| （排出チェイス） | Dial ローカル | CHASE | TO_DIAL | 排出中 | - | 両方 | PC 演出がないときの RELEASING / RETURNING |
| `FAULT` | Game | FLASH 赤 | - | 1500 | HIGH | BOTH | `RELEASE_FAILED`、排出タイムアウト |

## 6. タイムラインと同期点

### 6.1 受取（Dial 側スイッチ ON）

```
Dial                          Hub / PC (Game)                     画面
スイッチ ON
 └ 30 ms デバウンス
BALL_DETECTED ─ 白フラッシュ(150ms) → 号機色 TO_SCREEN
 └ EVENT BALL_RECEIVED{cycle} ─────────▶ source と cycle を照合
                                         （この時点で UFO の投下 X を固定: Game 仕様）
                                         SET_DIAL_EFFECT INTAKE ◀──── ポータルが吸引 → UFO へ転送
 ◀──────────────────────────────────────
 PC 演出(1500ms) → 終了後は号機色の呼吸（待機表示）
```

* 同期点 **S1 = `BALL_RECEIVED` の受信**。Game は `source`（0x20 = 左 / 0x21 = 右）と `cycle` を照合し、左下 / 右下のポータル演出を始める。
* `BALL_RECEIVED` は 1 サイクル（`ARM_RECEIVER` 1 回）につき 1 回だけ。スイッチのチャタリング・押しっぱなしでは増えない。
* UFO の投下 X 座標を受取イベント検出時点で固定することは Game 側仕様（#138）で照合する。

### 6.2 排出（仮想ボールが落下完了）

```
PC (Game)                                 Dial
RETURNING 開始
 SET_DIAL_EFFECT RETURN_FLOW ───────────▶ COMET TO_DIAL
 （ポータルにカプセルが吸い込まれる）
同期点 S2: RELEASE_BALL ────────────────▶ ACK ACCEPTED（受理だけ）
                                          サーボ開始
 ◀──────────────── EVENT RELEASE_STARTED{command_id}      同期点 S3
 （任意）SET_DIAL_EFFECT RELEASING
                                          排出角度へ → 保持 → 待機位置へ
 ◀──────────────── EVENT RELEASE_DONE{command_id, elapsed_ms} / RELEASE_FAILED{reason}   同期点 S4
 終端処理                                  PC 演出を破棄 → COMPLETE（フェードアウト）
GET_STATUS で照合（dial_state, switch_on）
ARM_RECEIVER（次サイクル）
```

* **S2**: `RELEASE_BALL` はポータル演出の指定同期点で送る。ACK は受理であり、動作完了ではない。
* **S3**: `RELEASE_STARTED` を「実機の排出中演出」の基準にする。サーボ開始から実際にボールが離れるまでの時間は機構依存なので、**実機で校正する**（未確認）。
* **S4**: `RELEASE_DONE` は制御シーケンスの完了で、**ボールが実際に出たことは保証しない**（サーボ完了 ≠ 排出成功）。`RELEASE_FAILED` の reason は 1 ABORTED（STOP_ALL）、2 LINK_LOST、3 SERVO_NOT_CONFIGURED。
* 次サイクルは、Game が STATUS（`dial_state` = COMPLETE、`switch_on` = false）を確認してから `ARM_RECEIVER` を送る。スイッチが ON のまま ARM した場合、Dial は一度 OFF になるまで検出を保留する。**自動で無条件に再排出しない**（`RELEASE_BALL` は Dial がボールを持っているときだけ受理、同じ `command_id` の再送ではサーボは動かない）。
* **再排出（fw 0.4〜）**: 排出シーケンスが終わって COMPLETE になっても**スイッチが ON のまま**（ボールが出ていない）なら、Game は新しい `command_id` の `RELEASE_BALL` をもう一度送れる。Dial は COMPLETE かつ `switch_on` = true のときだけ受理し、同じシーケンス（`RELEASE_STARTED` → `RELEASE_DONE`）をやり直す。`BALL_RECEIVED` は増えない（同じサイクルの同じボール）。スイッチが OFF なら今までどおり `REJECTED_STATE`。回数の上限は Game が決める（Dial は数えない）。fw 0.3 以前は COMPLETE での `RELEASE_BALL` を常に `REJECTED_STATE` にする。

## 7. フェイルセーフ

| 事象 | Dial の動作 |
|---|---|
| `STOP_ALL` | PC 演出を即破棄。排出中ならサーボを待機位置へ戻し `RELEASE_FAILED`（ABORTED）。DISABLED へ |
| セッション失効（PC 無通信 3 秒 / USB 切断 / 再接続） | PC 演出を破棄、号機色を既定へ、DISABLED へ。実行中の排出は待機位置まで進めて終わる |
| Hub 通信断（3 秒） | PC 演出を破棄、赤の呼吸。受取待ちを解除。排出中は既定で最後まで実行（`DIAL_LINK_LOSS_ABORT_RELEASE 1` で即中断） |
| Dial 再起動 | 待機位置・DISABLED・演出なし。古い命令は Hub が破棄し、届いても `REJECTED_SESSION` |
| LED の不具合・LED 命令の失敗（`IGNORED_PRIORITY`, `NOT_CONFIGURED`, タイムアウト） | **排出・受取には影響しない**。Game は LED 命令の結果を待って `RELEASE_BALL` を遅らせない（`wait=False` で送るか、失敗しても先へ進む） |
| `duration_ms` = 0 の演出が残る | 上表の契機と次の演出で必ず終わる。念のため Game は局面の終わりに `NONE` を送ってよい |

Game 側の必須事項: `RELEASE_STARTED` / `RELEASE_DONE` が来ない場合のタイムアウトを持ち、タイムアウトしたら `GET_STATUS` で照合する（無期限に待たない）。

### 電源保護

* SG90 の動作中は FRONT を `DIAL_LED_BRIGHTNESS_SERVO`（6）まで減光する既存の制限を**そのまま維持**している。PC 演出もこの上限に従う（排出中の演出は暗くなる）。
* G15 の LED も、M5Dial 給電（`DIAL_TRANSFER_LED_SEPARATE_POWER 0`）ではサーボ動作中に同じ上限まで減光する。
* 排出中に高輝度が必要なら、LED を別電源にする・電流を見積もる・実機でブラウンアウト（シリアルの `reset reason: BROWNOUT`）が出ないことを確認する、の 3 つを満たしてから設定を変える。

## 8. PC ライブラリ・Mock・ツール

```python
from pc import DeviceGateway, TraceLog
from pc import game_mode as gm

trace = TraceLog("logs/devicehub-trace.jsonl")
gw = DeviceGateway(port="auto", system_id=1, trace=trace.record)
gw.connect()

gw.set_dial_effect(gm.LEFT, "CHASE", "TO_SCREEN", duration_ms=1500)   # 色省略 = 号機色
gw.set_dial_effect(gm.RIGHT, "COMET", "TO_DIAL", color="#FF00FF", priority="HIGH", target="FRONT")
gw.clear_dial_effect(gm.LEFT)                                          # effect NONE
gw.set_dial_color(gm.RIGHT, (255, 0, 255))

gm.play(gw, gm.LEFT, "INTAKE")        # 5 章の名前付きアニメーション
gm.play(gw, gm.RIGHT, "RETURN_FLOW")
gm.stop(gw, gm.RIGHT)
```

* `pc/mock_hub.py`: `MockDial` が同じ規則（優先度、終端契機、ローカル受取表示）を持つ。`hub.devices[id].led("FRONT" | "TRANSFER")` で「今なにを表示しているか」（`source` = local / pc、effect、direction、color）を取得できる。既定で G15 の LED あり（`MockConfig(dial_transfer_led=False)` で LED なしの機体を模擬）。左右同時受取・同時排出・逆方向同時は `tests/integration/test_gateway_mock.py` にある。
* CLI: `ufo_cli.py dialfx dial1 CHASE TO_SCREEN - 1500`、`dialanim dial2 RETURN_FLOW`、`dialcolor dial2 '#FF00FF'`、`--trace FILE`。
* ブラウザ試験画面（`python3 -m pc.web_console`）: 各 Dial に LED 演出・号機色の操作と表示を追加。

## 9. トレースログ

`DeviceGateway(trace=TraceLog(path).record)` で、送信した命令・ACK・イベントを 1 行 1 JSON で記録する（`pc/trace.py`）。全レコードが同じキーを持つ。

`t_mono`（PC の monotonic 秒）, `wall`, `kind`（command / ack / event / game）, `session_id`, `source_dial`, `cycle`, `command_id`, `event_id`, `event`, `command`, `effect_id`, `rgb`, `direction`, `ack`, `release_state`（STARTED / DONE / FAILED）

* 画面側ログの `capsule_id` との突合: Game が `trace.record("game", source_dial=..., cycle=..., capsule_id=...)` を同じファイルに書く。受取は `(session_id, source_dial, cycle)`、排出は `RELEASE_BALL` の `command_id`（`RELEASE_*` イベントが同じ値を持つ）で結合する。
* Dial のシリアルにも `led front: effect=.. dir=.. rgb=.. cmd=.. cycle=..` が出る（`command_id` で突合）。
* **鍵・実設定（`config/secrets.h`, `config/devices.h`）はログにもコミットにも入れない。**

## 10. Game 側へのハンドオフと記録

プロトコルを変えたら、双方の Issue と PR に次を残す。

```
日付 (JST) / 対象 repo / branch / commit SHA / 対応 Issue・PR
upstream・downstream commit SHA / protocol version
コマンド・イベントの差分 / 互換性 / 必要な PC ライブラリ同期（pc/ 以下のファイル）
テストコマンド・結果 / Mock・実機の区分 / 未検証 / 次作業
```

本変更で Game 側が同期するもの: `pc/protocol.py`, `pc/device_gateway.py`, `pc/mock_hub.py`, `pc/trace.py`, `pc/game_mode.py`, `pc/__init__.py`（`pc/` 一式）。protocol version = 1（追加のみ）、ファームウェア 0.3。
