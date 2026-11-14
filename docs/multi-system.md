# 同一会場で 2 システムを運用する（ESP-NOW の混信対策）

UFO-DeviceHub を使った展示（Hub + Illumination + M5Dial ×2 + PC）が、同じ会場で **2 セット**動く前提で設計している。
それぞれを **システム 1 / システム 2** と呼ぶ。片方の命令がもう片方の機器を動かしたり、通信が混ざったりしないように、次の 5 層で分離する。

```
             システム 1（CH 1）                         システム 2（CH 11）
PC1 --USB-- Hub S1 (AtomS3R, 画面 "SYSTEM 1")    PC2 --USB-- Hub S2 (AtomS3R, 画面 "SYSTEM 2")
              |  magic 0x5546, 鍵セット S1                    |  magic 0x5547, 鍵セット S2
   Illum S1 / Dial1 S1 / Dial2 S1                  Illum S2 / Dial1 S2 / Dial2 S2
```

## 1. 分離の仕組み

| 層 | 内容 | どこで決まるか | 防げること |
|---|---|---|---|
| 1. チャネル | システムごとに別の 2.4GHz チャネル（既定: S1 = **1**、S2 = **11**、5 ch 以上離す） | `config/devices.h` の `UFO_S1_WIFI_CHANNEL` / `UFO_S2_WIFI_CHANNEL` | 電波の取り合い（遅延・再送の増加）。相手のフレームがそもそも受信されない |
| 2. MAC 許可リスト | Hub は自システムの子機 MAC、子機は自システムの Hub MAC だけをピア登録して受け付ける | `UFO_S1_MAC_*` / `UFO_S2_MAC_*` | 他システムの機器からのフレーム（受信コールバックで破棄） |
| 3. 暗号鍵 | システムごとに別の PMK / LMK（ESP-NOW 暗号化ユニキャスト） | `config/secrets.h` の `UFO_S1_*` / `UFO_S2_*`（`gen_secrets.py` が生成） | MAC を取り違えても、鍵が違うので復号できず破棄される |
| 4. パケット magic | ヘッダ先頭の magic をシステムごとに変える（S1 = `0x5546`、S2 = `0x5547`） | `UFO_SYSTEM_ID`（PlatformIO env） | 設定ミスで MAC と鍵の両方が一致してしまっても、デコード段階で `bad_magic` として捨てる。リンク維持にも使われない |
| 5. PC ↔ Hub のシステム ID | `HELLO` に `system_id` を付けると、違うシステムの Hub は `ERROR SYSTEM_MISMATCH` を返してセッションを張らない | `DeviceGateway(system_id=N)` | 1 台の PC に 2 台の Hub を挿したとき・USB を挿し間違えたときに、別のシステムを操作すること |

加えて、起動時の自己診断を行う。

* 各ファームは **両システムの設定表を両方** 持ち、起動時に照合する（`systemSeparationIssues()`）。
  * **エラー（無線を起動しない）**: 同じ MAC が両システムにある / 鍵が共通 / チャネルが 1..13 の範囲外
  * **警告（動作はする）**: 同じチャネル / チャネルの間隔が 5 未満（帯域が重なる）
* Hub は結果を `BOOT` / `HELLO_ACK` / `DIAG` の `system_id`, `system_check` と AtomS3R の画面（赤帯）に出す。子機はシリアルに出す。

## 2. ビルドの選び方

システムは **ビルド時に固定**する（実行時の切替はしない。現場で取り違えないため）。

| 機器 | システム 1 | システム 2 |
|---|---|---|
| Hub（M5AtomS3R） | `pio run -e hub_s1 -t upload` | `pio run -e hub_s2 -t upload` |
| Illumination ATOM | `illumination_s1` | `illumination_s2` |
| M5Dial #1 | `m5dial_1_s1` | `m5dial_1_s2` |
| M5Dial #2 | `m5dial_2_s1` | `m5dial_2_s2` |

ATOM Lite を Hub にする場合は `hub_atomlite_s1` / `hub_atomlite_s2`（画面の代わりに状態 LED を使う）。

## 3. セットアップ手順

1. **鍵を作る**: `python3 tests/tools/gen_secrets.py`
   * 両システム分の鍵を `config/secrets.h` に作る。既存ファイルがあれば、足りないシステムの鍵だけを追記する（既存の鍵は変えない）。
   * 旧形式（`UFO_PMK` など、システム番号なし）の鍵はシステム 1 として扱う。
2. **MAC を登録する**: `cp config/devices.example.h config/devices.h` を実行し、**1 つのファイルに両システムの MAC を書く**。
   * 各機器はシリアルに `UFO system=N device id=.. MAC=..` を出す。Hub の MAC は `BOOT` 行の `hub_mac` で確認する。
   * 両システムを同じファイルに書くことで、上記の起動時チェックが「同じ MAC を両方に書いた」ミスを見つけられる。
   * 作った `devices.h` と `secrets.h` は、書き込みに使うすべての PC に**手渡しでコピー**する（別の PC で `gen_secrets.py` を実行し直さない）。ファイルがない PC ではファームウェアがビルドエラーになる。手順は [README](../README.md#別の-pc-でビルド書込みするとき必ずやること)。
3. **チャネルを決める**: 会場の Wi-Fi を事前に確認し、混んでいないチャネルを選ぶ。既定は 1 と 11。
   * 会場の AP が 1 / 6 / 11 を使っている場合でも ESP-NOW は動く（CSMA で譲り合う）が、遅延と再送が増える。`ufo_cli.py probe` で計測して判断する。
4. **書き込む**: 機器ごとに `_s1` / `_s2` の env を選ぶ。機器にはシステム番号のラベルを貼っておく。
5. **PC を設定する**: ゲーム側で `DeviceGateway(port="auto", system_id=1)`（システム 2 の PC なら `system_id=2`）とする。
   * `port="auto"` の場合、違うシステムの Hub から `SYSTEM_MISMATCH` が返ると、そのポートを除外して別のシリアルポートを試す。
   * CLI では `python3 tests/tools/ufo_cli.py --system 1 devices`。

## 4. 現地での確認

| 確認項目 | 方法 |
|---|---|
| Hub がどちらのシステムか | AtomS3R の画面上部の帯（S1 = シアン、S2 = オレンジ）と `SYSTEM n`、`CH n` |
| 設定ミスがないか | 画面下部に赤帯が出ていないか。`ufo_cli.py --system n diag` の `system_check` が `ok` か |
| もう片方の電波が来ているか | `diag` の `transport.rx_dropped_unknown_mac`（同じチャネルに他の送信者がいると増える）。別チャネルならほぼ増えない |
| 混信していないか | 両システムを同時に動かしながら `probe 600` を実行し、欠落率と p95 を単独稼働時と比べる（[test-plan.md](test-plan.md) の C8〜C11） |

## 5. 設計上の判断

* **システム番号をパケットヘッダの magic に入れた理由**: ヘッダ長（17 Byte）と payload を変えずに、すべてのフレーム（命令・ACK・イベント・Heartbeat）を区別できるため。システム 1 は従来の `0x5546` のままなので、既存のテストベクタと互換性がある。
* **起動時チェックでエラーなら無線を起動しない理由**: MAC や鍵が共通という設定は、相手のシステムを動かしうる状態。黙って動くより、止まって画面とログで知らせる方が安全。
* **同じチャネルを警告にとどめる理由**: 層 2〜4 で論理的には分離されている。ただし電波は共有されるので、遅延と再送が増える。
* **システム数の上限**: 現在は 2（`UFO_SYSTEM_COUNT`）。magic は 8 システムまで定義してある。3 つ目を足す場合は `config/*.example.h` にブロックを追加し、`shared/ufo_config.h` と `shared/device_registry.cpp` の表を増やす。
