# アーキテクチャ

## 構成

```
                  PC / UFO Game  (pc/device_gateway.py)
                       |  USB Serial, JSON Lines, セッション/ACK/EVENT_ACK
                 Hub (M5AtomS3R)  (firmware/hub)
                       |  ESP-NOW 暗号化ユニキャスト, 固定チャネル, システム別 magic
          +------------+------------+
   Illumination     M5Dial (0x20)  Staff remote   (firmware/illumination, firmware/m5dial,
   WS2811/COB/Mist  SG90 / SW      M5StopWatch     firmware/staff_remote)
```

同じ会場に、この構成がもう 1 セット（システム 2）ある。**Dial はシステムごとに 1 台**（Dial1 = ID 0x20）。2 台目の ID 0x21（`m5dial_2_*` env）はプロトコル・ファーム・テストに残しているが、展示では使わない（ゲームは CoreXY_UFO_Game #200 から Dial1 だけを使う）。分離の仕組みは [multi-system.md](multi-system.md) を参照。

責務の分離:

| 層 | 責務 | 持たないもの |
|---|---|---|
| PC（ゲーム） | ゲーム状態、照明モード・演出の決定、加点、排出タイミング | 再送・重複排除（ゲートウェイが行う） |
| `DeviceGateway` | 接続・再接続、セッション、命令 ID、ACK 待ち、EVENT_ACK、重複除去、イベント配信 | ゲームロジック |
| Hub | JSON ⇔ バイナリ変換、ルーティング、再送、タイムアウト、Heartbeat、死活監視、セッション同期、診断 | ゲームロジック、子機の動作判断 |
| 子機共通 (`NodeCore`) | パケット検証、MAC/ID 照合、セッション、重複排除、ACK、Heartbeat 応答、イベント再送、Hub 通信断検知 | ハードウェア |
| 子機アプリ | I/O 制御とフェイルセーフ（リレー上限、サーボシーケンス、受取スイッチ） | 勝敗・加点の判断 |

## コードの配置

```
shared/                   # 全ファームウェア共通（PlatformIO ライブラリ ufo_shared）
  protocol.*              # パケット直列化（純 C++）
  messages.*              # payload 定義（純 C++）
  device_ids.h, device_registry.cpp   # Device ID ⇔ MAC ⇔ LMK（config/ から）
  reliability.*           # ReliableQueue / DedupCache / RecentIds / LinkMonitor（純 C++）
  node_core.*             # 子機プロトコルエンジン（純 C++）
  espnow_transport.*      # ESP-NOW（Arduino のみ）
  node_runtime.*          # 子機の Arduino グルー（Arduino のみ）
  led_effects.*           # NeoGacha から移植した非ブロッキング LED 演出（Arduino のみ）
firmware/hub/             # hub_core.*（純 C++）, json_bridge.*（ArduinoJson）, hub_display.*（AtomS3R 画面）, main.cpp
firmware/illumination/    # lighting.*, relay.*, mist.*, illumination_controller.*（純 C++）, led_renderer.*（WS2811 + COB）, main.cpp
firmware/m5dial/          # ball_receiver.*, dispenser.*, dial_controller.*（純 C++）, display.*, main.cpp
pc/                       # protocol.py, device_gateway.py, mock_hub.py
```

* 通信コードは `shared/` に 1 つだけ。Hub と子機で重複実装しない。
* 「純 C++」のファイルは Arduino に依存せず、`pio test -e native` でホスト上でテストする。
  ハードウェアは `RelayOutput`, `ServoOutput`, `NodeTransport`, `RadioOutput`, `LineOutput` のインターフェース越しに触る。
* M5Dial は 1 つのファームウェアで、`-DUFO_DEVICE_ID=0x20/0x21`（`m5dial_1` / `m5dial_2` env）だけが違う。展示で使うのは 0x20 の 1 台だけ。

## 実行モデル（全機器共通）

* `loop()` は非ブロッキング。`delay()` は 1 ms の譲りのみ。
* ESP-NOW 受信コールバック（Wi-Fi タスク）は「長さチェック + 登録 MAC か」だけを見て FreeRTOS キューへコピーして戻る。
  デコード・検証・アクチュエータ操作はすべて `loop()` 側。
* 1 回の `loop()` で処理するフレーム数・シリアル文字数に上限（予算）を設け、通信が詰まっても他の処理（リレー OFF 判定など）が止まらない。

### Illumination

```
loop: NodeRuntime.loop() → ボタン → IlluminationController.tick()（リレー・ミストの時間判定）→ ミスト WDT 更新 → LedRenderer.render()（≤60fps, WS2811 + COB）→ 状態LED
```
* リレー・ミストの制御と LED 描画は別モジュール。出力の ON/OFF は描画より先に判定する。
* ミスト（`MistController`）はパターン（連続 / BREATH / PUFF）と最大実行時間を持つ。蒸気リレー（`RelayController`、短パルス）とは別の出力。
* 本体ボタンは PC セッションがないときだけローカルのシーンを切り替える（`IlluminationController::onButtonShort/Long`）。
* さらに `esp_timer` のワンショット（最大 ON 時間 + 100 ms）で **loop と独立に** 強制 OFF する二重化。

### M5Dial

```
loop: M5Dial.update() → NodeRuntime.loop() → DialController.tick(受取スイッチ) → 表示（変化時のみ）→ ブザー → LED
```
* `Dispenser` が SG90 の「排出位置 → 保持 → 待機位置 → 整定」を時間で進める状態機械。

### Hub

```
loop: シリアル読取（行組立て, 512B 上限）→ JsonBridge → HubCore
      ESP-NOW 受信キュー → HubCore.onFrame
      HubCore.tick（PC 監視, 子機リンク監視, Heartbeat, 再送, PC へのイベント再送）→ 状態 LED
```

## 状態 LED

| 機器 | 色 | 意味 |
|---|---|---|
| Hub（AtomS3R） | 画面 | システム番号（帯の色 S1 = シアン / S2 = オレンジ）、チャネル、PC セッション、子機ごとのオンライン状態、設定エラー（赤帯） |
| Hub（ATOM Lite） | 青 / 緑 / 橙点滅 | PC セッションなし / 全子機オンライン / オフラインの子機あり |
| Hub / 子機 | 赤パルス・赤点滅 | サンプル鍵使用 / 無線初期化失敗・MAC 不一致 |
| Illumination | 赤点滅 / 青 / 緑 / 橙 | Hub 通信断 / セッションなし / 正常 / リレー ON |
| Illumination | 白 / 水色 | ローカル操作中（ミスト OFF / ON） |
| Illumination | 一瞬 青 / 緑 / 赤 / 紫 | ボタン: シーン切替 / ミスト ON / ミスト OFF / 無視（PC 制御中） |

## セキュリティモデル

* 同じ会場の 2 システムは、チャネル・MAC 表・鍵・パケット magic・PC 側の system_id で分離する（[multi-system.md](multi-system.md)）。
* 静的プロビジョニングのみ。未登録デバイスの自動ペアリングはしない。子機は Hub だけ、Hub は 3 台だけを Peer 登録する。
* Peer はすべて暗号化（PMK + リンクごとの LMK）。ブロードキャスト Peer は登録しない。
* 受信は「登録 MAC か（コールバック）」→「フレーム検証」→「source_id が MAC の登録 ID と一致」→「destination が自分」の順に検査。
* 物理動作は確立済みセッション + 未実行 command_id + 機器状態の条件がそろったときだけ。
* MAC フィルタと暗号化は電波の混雑を解消しない。遅延・欠落は再送・タイムアウトで扱い、`GET_DIAG` / `ufo_cli.py probe` で測る。
