# アーキテクチャ

## 構成

```
                  PC / UFO Game  (pc/device_gateway.py)
                       |  USB Serial, JSON Lines, セッション/ACK/EVENT_ACK
                 M5ATOM Hub  (firmware/hub)
                       |  ESP-NOW 暗号化ユニキャスト, 固定チャネル
          +------------+------------+
   Illumination     M5Dial #1    M5Dial #2      (firmware/illumination, firmware/m5dial)
   LED / Relay      SG90 / SW    SG90 / SW
```

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
firmware/hub/             # hub_core.*（純 C++）, json_bridge.*（ArduinoJson）, main.cpp
firmware/illumination/    # lighting.*, relay.*, illumination_controller.*（純 C++）, led_renderer.*, main.cpp
firmware/m5dial/          # ball_receiver.*, dispenser.*, dial_controller.*（純 C++）, display.*, main.cpp
pc/                       # protocol.py, device_gateway.py, mock_hub.py
```

* 通信コードは `shared/` に 1 つだけ。Hub と子機で重複実装しない。
* 「純 C++」のファイルは Arduino に依存せず、`pio test -e native` でホスト上でテストする。
  ハードウェアは `RelayOutput`, `ServoOutput`, `NodeTransport`, `RadioOutput`, `LineOutput` のインターフェース越しに触る。
* M5Dial は 1 つのファームウェアで、`-DUFO_DEVICE_ID=0x20/0x21`（`m5dial_1` / `m5dial_2` env）だけが違う。

## 実行モデル（全機器共通）

* `loop()` は非ブロッキング。`delay()` は 1 ms の譲りのみ。
* ESP-NOW 受信コールバック（Wi-Fi タスク）は「長さチェック + 登録 MAC か」だけを見て FreeRTOS キューへコピーして戻る。
  デコード・検証・アクチュエータ操作はすべて `loop()` 側。
* 1 回の `loop()` で処理するフレーム数・シリアル文字数に上限（予算）を設け、通信が詰まっても他の処理（リレー OFF 判定など）が止まらない。

### Illumination

```
loop: NodeRuntime.loop() → IlluminationController.tick()（リレー時間判定）→ LedRenderer.render()（≤60fps）→ 状態LED
```
* リレー制御と LED 描画は別モジュール。リレーは描画より先に判定。
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
| Hub | 青 | PC セッションなし |
| Hub | 緑 | セッションあり・全子機オンライン |
| Hub | 橙点滅 | オフラインの子機あり |
| Hub / 子機 | 赤パルス・赤点滅 | サンプル鍵使用 / 無線初期化失敗・MAC 不一致 |
| Illumination | 赤点滅 / 青 / 緑 / 橙 | Hub 通信断 / セッションなし / 正常 / リレー ON |

## セキュリティモデル

* 静的プロビジョニングのみ。未登録デバイスの自動ペアリングはしない。子機は Hub だけ、Hub は 3 台だけを Peer 登録する。
* Peer はすべて暗号化（PMK + リンクごとの LMK）。ブロードキャスト Peer は登録しない。
* 受信は「登録 MAC か（コールバック）」→「フレーム検証」→「source_id が MAC の登録 ID と一致」→「destination が自分」の順に検査。
* 物理動作は確立済みセッション + 未実行 command_id + 機器状態の条件がそろったときだけ。
* MAC フィルタと暗号化は電波の混雑を解消しない。遅延・欠落は再送・タイムアウトで扱い、`GET_DIAG` / `ufo_cli.py probe` で測る。
