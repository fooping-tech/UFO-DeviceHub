# テスト計画

## 1. 自動テスト（ハードウェア不要）

```
~/.platformio/penv/bin/pio test -e native                       # ファームウェアのロジック（C++, Unity）
uv run --no-project --with pyserial --with pytest python -m pytest   # PC ライブラリ + Mock（または pip install -r pc/requirements.txt; pytest）
~/.platformio/penv/bin/pio run                                   # 2 システム × 4 台分のビルド（hub_s1 ... m5dial_2_s2）
```

| スイート | 内容 |
|---|---|
| `tests/firmware/test_protocol` | 直列化、リトルエンディアン、境界（最大 payload / 長さ不一致 / magic / version / type）、ゴールデンベクタ |
| `tests/firmware/test_reliability` | ACK タイムアウト・再送 3 回・打ち切り、宛先ごとの FIFO、イベント無期限再送、重複排除、Link 監視 |
| `tests/firmware/test_node_core` | 他システム（magic 違い）のフレームを拒否してリンク維持にも使わない、不正 MAC・ブロードキャスト・ID 偽装・他機宛ての拒否、不正フレーム、セッション必須、旧セッション拒否、重複 command_id、Heartbeat 応答とセッション追従、Hub 通信断、イベント再送 |
| `tests/firmware/test_illumination` | ミスト（起動時 OFF、BREATH / PUFF の周期、置き換え、最大時間と `clamped`、`MIST_DONE`、通信断・セッション終了・STOP_ALL・OFF・ボタン長押しで停止、未設定ピン）、ローカル操作（シーン送り、長押しでミスト ON/OFF、PC セッション開始で終了、設定で無効化）、新しい照明モード、モード切替、演出の割込みと優先度、演出終了後の復帰、リレーの指定時間 ON、最大 ON 制限、再噴射禁止、BUSY、通信断・セッション終了・STOP_ALL で OFF、未設定ピン、同一 command_id の二重噴射防止 |
| `tests/firmware/test_dial` | 起動時の安全状態、受取検出、チャタリング、ON 保持、ゲーム外の無視、ARM 時に ON だった場合、排出シーケンス（角度・時間）、排出中の重複命令、同一 command_id の二重排出防止、通信断（受取解除・排出中は待機位置へ）、STOP_ALL 中断、セッション変更、電源再投入後の再送で排出しない |
| `tests/firmware/test_hub` | HELLO の system_id 不一致で SYSTEM_MISMATCH、SET_MIST の検証と STATUS / MIST_DONE、JSON 不正・行長超過・バージョン違い・未知 type、HELLO、パラメータ検証、NO_SESSION / SESSION_MISMATCH / UNKNOWN_DEVICE、未登録 MAC / ID 偽装、GET_DEVICES / GET_DIAG、PC 無通信でセッション終了 |
| `tests/firmware/test_system` | Hub + 3 子機（実コード）をメモリ上の無線で接続: 個別制御、リレー、ボール 1 サイクル、PC 未 ACK のイベント再送、ACK 消失からの回復（排出 1 回）、再送上限で TIMEOUT、子機電源断・復帰（再起動検出・セッション再同期）、PC USB 切断で安全化、再接続で旧命令を再送しない、他展示フレームの無視、STOP_ALL |
| `tests/protocol/test_protocol_py.py` | Python 側の直列化とゴールデンベクタ（システム 2 のフレーム、他システム magic の拒否を含む）、SET_MIST、パラメータ検証、行フレーミング |
| `tests/protocol/test_consistency.py` | `shared/*.h` と `pc/protocol.py` の列挙値・名前・パラメータ長・セッション不要命令の一致 |
| `tests/integration/test_gateway_mock.py` | `DeviceGateway`（実コード）× `MockHub`: 別システムの Hub に接続しない、ミストの状態と MIST_DONE、正常 ACK、ACK 消失、EVENT 重複、イベント再送、旧セッションのイベント、子機通信断・復帰、ボール受取、排出開始・完了、リレー、タイムアウト、USB 切断・再接続とセッション再同期、Hub 無応答、Hub 再起動、asyncio |
| `tests/integration/test_web_console.py` | ブラウザ試験画面の HTTP API（実 `DeviceGateway` × `MockHub`）: ページ・meta、命令・STATUS・機器一覧・STOP_ALL・診断、不正パラメータは送信前に 400、他サイトからの POST・不正 Host・CORS プリフライトを拒否、イベントのストリーム配信 |

## 2. 実機で確認すること（**未確認**）

自動テストは「ロジックが仕様どおり」であることを示すだけで、以下は実機でしか確認できない。結果を記録するまで成功と見なさない。

準備: `config/devices.h` に両システムの MAC、`config/hardware.h` にピンを設定し、`gen_secrets.py` の鍵で 4 台とも書き込む。
操作は `python3 tests/tools/ufo_cli.py --system <n> --port <Hub のポート> <command>`、またはブラウザ画面 `python3 -m pc.web_console --system <n>`。

### 2.1 基本疎通

| # | 手順 | 合格条件 |
|---|---|---|
| B1 | 4 台を起動し `ufo_cli.py devices` | 3 台 `online: true`, `session_ok: true`。Hub LED 緑 |
| B2 | `ufo_cli.py diag` | `example_keys: false`, `invalid_packets` が増え続けない |
| B3 | 各子機のシリアル出力 | `MAC=` が `devices.h` と一致。`WARNING` なし |
| B4 | `ufo_cli.py probe 60` | 失敗 0、rtt median / p95 を記録（目安: median < 20 ms） |

### 2.2 Illumination

| # | 手順 | 合格条件 |
|---|---|---|
| I1 | `light GAME_PLAY 180` → `light IDLE` | 表示が切り替わる。`transition_ms` 指定でクロスフェード |
| I2 | `effect SCORE` → 直後 `effect ARM_MOVE` | 2 つ目は `IGNORED_PRIORITY`。演出終了後に元のモードへ戻る |
| I3 | リレー出力に LED / テスタを接続し `steam 1000` | 1000 ms ± 20 ms ON。`RELAY_DONE on_ms≈1000` |
| I4 | `steam 9000` | `clamped: true`、3000 ms で OFF |
| I5 | 起動・リセット直後のリレー出力をオシロで観測 | ON パルスが出ない（**ブート時の GPIO グリッチ・ストラップピンに注意**） |
| I6 | `steam 3000` 中に Hub の電源を切る | 3 秒以内に OFF（通信断判定 3000 ms） |
| I7 | `steam 3000` 中に PC の USB を抜く | Hub が 3 秒後に SESSION_END → OFF |
| I8 | 極性を `ILLUM_RELAY_ACTIVE_HIGH {0}` にして I3 / I5 | LOW アクティブのモジュールで正しく動作 |
| I9 | 蒸気発生器を接続して I3 | 実際の噴射時間・再噴射禁止時間が機器仕様内 |
| I10 | 起動直後 | WS2811 が赤 → 緑 → 青の順に光る（色順）。ミストリレーは ON にならない |
| I11 | `light AURORA` / `STORM` / `CAMPFIRE` / `NIGHT` | WS2811 と COB の両方が表 [hardware.md](hardware.md) どおりに光る |
| I12 | `mist BREATH 60` | 8 秒 ON / 6 秒 OFF。60 秒で止まり `MIST_DONE`（COMPLETED） |
| I13 | `mist CONTINUOUS` 中に Hub の電源を切る / PC の USB を抜く | 3 秒以内に OFF |
| I14 | `mist CONTINUOUS` 中に本体ボタンを長押し | 即 OFF、`MIST_DONE`（LOCAL_BUTTON） |
| I15 | PC を接続せずに本体ボタンを短押し・長押し | シーンが順に切り替わり、ミストが連動する。長押しで光だけになる |
| I16 | `ILLUM_MIST_MAX_S` を 30 にしてローカル操作でミストを出す | 30 秒で止まる |

### 2.3 M5Dial（**サーボ角度・方向は機構で確認するまで変更しない**）

| # | 手順 | 合格条件 |
|---|---|---|
| D1 | 機構を外した状態で起動 | ホーンが待機位置（180°）へ動き、以後静止 |
| D2 | `game dial1` → BtnA（模擬）を押す | `BALL_RECEIVED` が 1 回だけ。押し続けても増えない |
| D3 | `release dial1` | 0° へ動き約 1.5 s 保持、180° へ戻る。`RELEASE_STARTED` → `RELEASE_DONE`。NeoGacha と同じ動き |
| D4 | 機構を付けて D3 | ボールが排出され、ホーンが干渉・過負荷にならない。`DIAL_SERVO_RETURN_SETTLE_MS` が戻り時間より長い |
| D5 | 受取スイッチ配線後（`DIAL_RECEIVER_PIN` 設定、`DIAL_RECEIVER_SIM_BUTTON 0`）、ボールを落とす | 1 球 1 イベント。チャタリングで重複しない |
| D6 | 排出中に `release dial1` をもう一度 | `BUSY` |
| D7 | 排出中に Hub の電源を切る | 既定（`DIAL_LINK_LOSS_ABORT_RELEASE 0`）でシーケンスが最後まで進み 180° で停止 |
| D8 | 排出中に M5Dial の電源を切り、再投入 | 起動後 180°。PC 側の再送で再排出しない（`DISABLED`） |
| D9 | `dial2` で D2–D3 | #1 は動かない（個別制御） |

### 2.4 通信・他展示との共存

| # | 手順 | 合格条件 |
|---|---|---|
| C1 | 予備の ATOM に `pio run -e noise -t upload`（同チャネルでブロードキャスト・偽造フレーム・非暗号ユニキャストを 5 ms 間隔で送信） | 子機が動かない。`diag` の `transport.rx_dropped_unknown_mac` と子機の `device_rx_rejected` が増える |
| C2 | C1 中に B4 / I3 / D3 | 正常動作。rtt・再送数の悪化を記録 |
| C3 | `noise` を `-DNOISE_SPOOF_HUB_MAC=1` でビルド（Hub の MAC を名乗るが鍵なし） | 子機が動かない（**暗号化 Peer が非暗号フレームを捨てることの確認**） |
| C4 | 1 台だけ別の鍵（`gen_secrets.py --force` 後にその 1 台だけ書込み） | その機器は online にならない・制御できない |
| C5 | 1 台だけ `UFO_WIFI_CHANNEL` を変えて書込み | その機器は offline。他の機器は正常 |
| C6 | 展示会場で `probe 600` | 欠落率・p95 を記録し、`UFO_ACK_TIMEOUT_MS` / `UFO_MAX_RETRIES` の妥当性を判断 |
| C7 | NeoGacha 実機を同じ会場で稼働 | 相互に誤動作しない（NeoGacha のフレームは 5 Byte で本プロトコルの最短長 17 Byte 未満） |
| C8 | システム 1・2 を同時に稼働し、各 PC で `--system n probe 600` | 単独稼働時と比べて欠落率・p95 が大きく悪化しない。Hub の画面に赤帯が出ない |
| C9 | 1 台の PC に両方の Hub を挿し、`--system 2 --port auto devices` | システム 2 の Hub に接続する（システム 1 の Hub は `SYSTEM_MISMATCH` で除外） |
| C10 | システム 1 の Illumination を `illumination_s2` で書き込む（MAC は S1 表のまま） | システム 2 の Hub には接続できない（MAC 不一致）。システム 1 からも制御できない（magic・鍵が違う） |
| C11 | `devices.h` で同じ MAC を両システムに書いて書き込む | 無線を起動しない。Hub の画面とログに `a MAC is in BOTH systems` が出る |
| C12 | 両システムを同じチャネルにして C8 | 警告（画面の赤帯）は出るが誤動作しない。遅延と再送の増え方を記録する |

### 2.5 PC 連携

| # | 手順 | 合格条件 |
|---|---|---|
| P1 | ゲーム実行中に USB を抜き差し | `GATEWAY_DISCONNECTED` → 自動再接続 → 新セッション。抜いている間の命令は `GatewayNotConnected` |
| P2 | Hub をリセット | `BOOT` 受信 → 新セッション |
| P3 | macOS / Windows でポート自動検出 | `port="auto"` で Hub を選ぶ。複数の USB シリアルがある場合は `system_id` を指定するか、ポートを明示する |
