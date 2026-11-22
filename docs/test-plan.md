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
| `tests/firmware/test_dial`（メニュー） | `CLOSE` の位置で開く・一周・逆回し、無操作で閉じる、結果表示中は回しても押しても動かない、メニューの排出（イベントなし・ゲームの状態を変えない・PC の `RELEASE_BALL` は BUSY・STOP_ALL で中断・設定モードでは不可）、`DIAL_REQUEST`（GAME_RESTART / GAME_END、セッションなし・通信断では送らない）。`test_system`: Dial の依頼が Hub の JSON（`request` 名 + `code`）として PC に届く |
| `tests/firmware/test_dial`（画面） | ドット絵画面: 受取（白フラッシュ → 吸い込み → 文字に戻る）、排出（RELEASING から RETURNING に変わってもアニメーションが続く、カプセルが下へ出る）、起動直後は入場アニメーションなし、全状態 × 通信あり/なしで丸い画面の外に描かない。見た目は `tests/tools/scene_preview/run.sh` で画像にして確認 |
| `tests/firmware/test_dial`（ゲームモード LED） | LED の並び（下から上・上から下の 2 列）と TO_SCREEN / TO_DIAL の流れる向き、`SET_DIAL_EFFECT` の受理・不正パラメータ・優先度・時間切れ、G15 の LED なし（`NOT_CONFIGURED` / BOTH は前面のみ）とあり、受取のローカル発光（白 → 号機色 TO_SCREEN → 待機）と PC 後着演出、フラッシュ中に届いた演出、排出演出と終端、**LED の有無で排出シーケンスが変わらない**、STOP_ALL・セッション変更・通信断・設定モード・設定エラーで停止、`SET_DIAL_COLOR`、再送で演出がやり直しにならない |
| `tests/firmware/test_hub` | `SET_DIAL_EFFECT` / `SET_DIAL_COLOR` の JSON 検証と STATUS、HELLO の system_id 不一致で SYSTEM_MISMATCH、SET_MIST の検証と STATUS / MIST_DONE、JSON 不正・行長超過・バージョン違い・未知 type、HELLO、パラメータ検証、NO_SESSION / SESSION_MISMATCH / UNKNOWN_DEVICE、未登録 MAC / ID 偽装、GET_DEVICES / GET_DIAG、PC 無通信でセッション終了 |
| `tests/firmware/test_system` | **左右同時受取・同時排出、左右逆方向同時（ACK 消失つき）、PC 消失・Dial 再起動で演出停止**、Hub + 3 子機（実コード）をメモリ上の無線で接続: 個別制御、リレー、ボール 1 サイクル、PC 未 ACK のイベント再送、ACK 消失からの回復（排出 1 回）、再送上限で TIMEOUT、子機電源断・復帰（再起動検出・セッション再同期）、PC USB 切断で安全化、再接続で旧命令を再送しない、他展示フレームの無視、STOP_ALL |
| `tests/protocol/test_protocol_py.py` | Python 側の直列化とゴールデンベクタ（システム 2 のフレーム、他システム magic の拒否を含む）、SET_MIST、パラメータ検証、行フレーミング |
| `tests/protocol/test_consistency.py` | `shared/*.h` と `pc/protocol.py` の列挙値・名前・パラメータ長・セッション不要命令の一致 |
| `tests/integration/test_gateway_mock.py` | **ゲームモード: 演出と STATUS、送信前の検証、優先度と時間切れ、G15 の LED なし / あり、ローカル受取表示と PC 後着演出、左右同時受取・同時排出・逆方向同時、再送で再適用しない、STOP_ALL・通信断・セッション終了で停止、排出中の STOP_ALL、LED 失敗でも排出できる、名前付き演出とトレースログ**、`DeviceGateway`（実コード）× `MockHub`: 別システムの Hub に接続しない、ミストの状態と MIST_DONE、正常 ACK、ACK 消失、EVENT 重複、イベント再送、旧セッションのイベント、子機通信断・復帰、ボール受取、排出開始・完了、リレー、タイムアウト、USB 切断・再接続とセッション再同期、Hub 無応答、Hub 再起動、asyncio |
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

### 2.3b ゲームモードの LED・排出同期（[game-mode-interface.md](game-mode-interface.md)、**すべて未確認**）

2 台とも同じ版（fw 0.3）に書き込み、Hub も書き換えてから行う。記録には `--trace FILE` のログと Dial のシリアル出力を添える。

| # | 手順 | 合格条件 |
|---|---|---|
| G1 | `devices` | Hub・Dial 2 台の `fw` が `0.3` |
| G2 | `dialfx dial1 CHASE TO_SCREEN` → `dialfx dial1 CHASE TO_DIAL` | G1・G15 の両方で、2 列がそろって画面側へ / 実機側へ流れて見える。逆なら `DIAL_LED_REVERSED` / `DIAL_TRANSFER_LED_REVERSED`、2 列がずれるなら `_FOLDED` と LED 数を見直して記録 |
| G2b | `dialfx dial1 WIPE TO_SCREEN - 3000 NORMAL TRANSFER` | G15 の LED が端から端まで満ちる（途中で止まる・余るなら `DIAL_TRANSFER_LED_COUNT` を実数に直す） |
| G3 | `status dial1` / `status dial2` | `unit_color` が 1 号機シアン・2 号機マゼンタ。`dialcolor dial2 '#FF8000'` で変わり、PC を再接続すると既定に戻る |
| G4 | `game dial1` → 受取スイッチ（G2）にカプセルを落とす | 30 ms デバウンス後すぐ白く光り、号機色が画面方向へ流れる。`BALL_RECEIVED` は 1 回。極性（`DIAL_RECEIVER_ACTIVE_LOW`）が正しい |
| G5 | G4 の直後に `dialanim dial1 INTAKE` | 見た目が途切れない。1.5 秒後に待機表示 |
| G6 | `dialanim dial1 RETURN_FLOW` → `release dial1` | 実機方向の光のまま SG90 が動き、`RELEASE_STARTED` → `RELEASE_DONE` の後にフェードアウト。**ボールが実際に出たかを目視で記録**（`RELEASE_DONE` は排出成功を意味しない） |
| G7 | G6 で `RELEASE_STARTED` からボールが離れるまでの時間を測る | 値を記録し、Game 側の演出時間（同期点 S3）に反映 |
| G8 | 2 台同時に G4 → 同時に `release` | 互いに干渉しない。排出は各 1 回 |
| G9 | dial1 に `INTAKE`、dial2 に `RETURN_FLOW` を同時に出す | 左は画面方向、右は実機方向 |
| G10 | 演出中に `stop` / PC の USB を抜く / Hub の電源を切る | 演出が止まる（通信断は 3 秒以内に赤の呼吸）。サーボは待機位置 |
| G11 | 排出中の LED（2 系統） | `DIAL_LED_BRIGHTNESS_SERVO` まで暗くなる。シリアルに `BROWNOUT` が出ない。2 系統同時点灯時の電流を実測して記録 |
| G12 | `DIAL_TRANSFER_LED_PIN -1` で書き込み `dialfx dial1 CHASE TO_DIAL - 0 NORMAL TRANSFER` | `NOT_CONFIGURED`。`BOTH` なら前面 LED だけ光る |
| G13 | 確定した LED 数・向き・給電方法 | `config/hardware.h` と `hardware.md` に記録 |
| G14 | Game（CoreXY_UFO_Game）と結合 | 画面ログの `capsule_id` と DeviceHub のトレース（`session_id, source_dial, cycle, command_id`）が突合できる |

### 2.3c Dial 本体のメニュー（fw 0.5、**すべて未確認**）

Hub と Dial を同じ版（fw 0.5）に書き込んでから行う。

| # | 手順 | 合格条件 |
|---|---|---|
| M1 | 通常画面でダイヤルを 1 クリック回す | `MENU` 画面が出て `DIAL RESET`（逆回しなら `GAME END`）を指す。1 クリックで 1 項目進む（進まなければ `DIAL_ENCODER_COUNTS_PER_STEP`） |
| M2 | 何もせず 10 秒待つ / `CLOSE` で押す | 通常画面に戻る |
| M3 | `RELEASE` で押す | 排出シーケンスが 1 回動く。PC の `events` に `RELEASE_*` が出ない。`BROWNOUT` で再起動しない |
| M4 | PC を接続して `GAME RESTART` / `GAME END` で押す | 画面に `SENT`。PC に `DIAL_REQUEST`（`request` = GAME_RESTART / GAME_END、`source` が押した Dial）が 1 回ずつ届く |
| M5 | PC を切断して M4 | 画面に `NO GAME`。再接続後に遅れて届かない |
| M6 | `DIAL RESET` で押す | 再起動して `STANDBY`。PC に `DEVICE_REBOOTED`。サーボは待機位置 |
| M7 | `POWER OFF` で押す（USB 給電 / バッテリー） | 画面・LED が消える。PC に `DEVICE_OFFLINE`。USB 給電ではリセットボタンで復帰、バッテリーでは電源が切れる。**どちらで確認したかを記録** |
| M8 | 排出中に `DIAL RESET` / `POWER OFF` | `SERVO RUNNING` と出て実行されない |
| M9 | ゲーム中（WAITING / GOT BALL）にメニューを開いて閉じる | 受取検出・排出・LED 演出に影響しない |
| M11 | 通常画面を各状態で見る（`game` → カプセル投入 → `release`） | 色が号機色（シアン / マゼンタ。赤と青が入れ替わっていない）、吸い込み・排出のアニメーションがなめらか、文字が読める。**アニメーション中も受取検出・サーボの動き・LED が遅れない**（シリアルの `switch ON` とサーボのランプを確認） |
| M10 | Game（CoreXY_UFO_Game）と結合 | `DIAL_REQUEST` でリスタート・終了が動く（Game 側の実装が必要） |

### 2.3d スタッフ用リモコン（M5StopWatch、fw 0.6）

Hub とリモコンを同じ版（fw 0.6）に書き込んでから行う。PC 側は `ufo_cli.py --system 2 --port auto monitor` などでイベントを見る。

| # | 手順 | 合格条件 |
|---|---|---|
| R1 | 電源を入れる | 画面上部が `S2 つながっている`（緑）。`devices` で `staff_remote` が online、`fw 0.6` |
| R2 | スタッフ画面の各区画・中央を長押し、黄ボタン | `STAFF_ACTION`（`action` = NEXT / SKIP / MENU / SWITCH / STOP）が 1 回ずつ。区画が満ちてから短く振動 |
| R3 | 各区画を短くタップ / 途中で指をずらす / ふっき を 1.2 秒長押し | タップ・ずらしでは送らない（「ながおし で おくる」）。ふっき の長押しで RECOVER が 1 回 |
| R4 | そうさ画面で黄ボタン + 右・左・奥・手前に傾ける | `REMOTE_INPUT` が約 100 ms ごと。`x` が右で +、`y` が奥で +（違えば `REMOTE_TILT_*`）。離すと `move=false` が 1 回来て止まる |
| R5 | つかむ / はなす / ポータル | `pose` が FIST / PEACE / POINT で約 1.2 秒、その後 OPEN |
| R6 | 黄ボタンを押したまま PC を切断 | 送信が止まり、再接続後に古い傾きがまとめて届かない |
| R7 | ゲームと結合（ゲームの `docs/staff_operations.md`） | 下のゲーム側の確認表 |

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
