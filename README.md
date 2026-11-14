# UFO-DeviceHub

UFO Game の ESP マイコン系デバイス（Hub、Illumination、M5Dial ×2）の通信システム。
共通プロトコル、4 種類のファームウェア、PC 用 Python ライブラリ、Mock、テストを 1 つのプロジェクトで管理する。

```
PC (pc/DeviceGateway) --USB JSON Lines--> M5ATOM Hub --ESP-NOW 暗号化ユニキャスト--> Illumination ATOM / M5Dial #1 / M5Dial #2
```

* ゲームロジックは PC のみ。Hub は中継・再送・セッション・死活監視、子機は I/O とフェイルセーフだけ。
* 詳細: [docs/protocol.md](docs/protocol.md)（プロトコル・API）, [docs/architecture.md](docs/architecture.md), [docs/hardware.md](docs/hardware.md)（ピン・角度・安全仕様）, [docs/test-plan.md](docs/test-plan.md)（自動テストと実機確認手順）

## セットアップ

```sh
cp config/devices.example.h  config/devices.h     # 4 台の MAC と Wi-Fi チャネル
cp config/hardware.example.h config/hardware.h    # ピン・リレー極性・サーボ角度
python3 tests/tools/gen_secrets.py                # config/secrets.h（ESP-NOW 鍵を乱数生成）
```

`config/devices.h`, `hardware.h`, `secrets.h` は git 管理外。ない場合はサンプル値でビルドされ、機器が `example_keys` / `example_devices` を報告する。

## ビルド・書込み

```sh
pio run                                  # hub, illumination, m5dial_1, m5dial_2
pio run -e hub          -t upload        # M5ATOM Hub
pio run -e illumination -t upload        # Illumination ATOM (ID 0x10)
pio run -e m5dial_1     -t upload        # M5Dial #1 (ID 0x20)
pio run -e m5dial_2     -t upload        # M5Dial #2 (ID 0x21)
pio run -e noise        -t upload        # 試験用: 他展示を模擬するノイズ送信機
pio run -e illum_mist    -t upload       # 単体 Illumination 演出（ATOM Lite、通信なし）
pio run -e illum_mist_s3 -t upload       # 同上（AtomS3 Lite）
```

### 単体 Illumination 演出（atom_led_mist）

`firmware/atom_led_mist/atom_led_mist.ino` は Hub / ESP-NOW を使わずに単体で動く照明・噴霧の演出スケッチ。
COB テープ（Atomic PWM Base）、WS2811 テープ、噴霧モジュール（Unit 2Relay）をボタンでシーン切替する。
Arduino IDE でもこのファイルをそのまま開いて書き込める（ライブラリ: FastLED）。
ピン・操作・安全設定は [docs/hardware.md](docs/hardware.md#単体-illumination-演出atom_led_mist) を参照。

（`pio` が PATH にない場合は `~/.platformio/penv/bin/pio`）

## テスト

```sh
pio test -e native                                              # ファームウェアのロジック 63 件
uv run --no-project --with pyserial --with pytest python -m pytest    # Python 84 件（Mock 使用）
```

## PC から使う

```python
from pc import DeviceGateway, DeviceId

gw = DeviceGateway(port="auto")
gw.subscribe(lambda ev: print(ev.name, ev.source, ev.data))
gw.connect()

gw.set_light_mode("GAME_PLAY", brightness=180)
gw.set_mode(DeviceId.M5DIAL_1, "GAME")
gw.arm_receiver(DeviceId.M5DIAL_1)       # → BALL_RECEIVED イベント
gw.release_ball(DeviceId.M5DIAL_1)       # → RELEASE_STARTED / RELEASE_DONE
gw.relay_pulse(1500)                     # 蒸気（最大 3000 ms）
```

手動操作・現地診断: `python3 tests/tools/ufo_cli.py --port auto devices|diag|probe|light|effect|steam|game|release|stop|monitor`
（`--port mock` でハードウェアなし）

## ディレクトリ

```
shared/        共通プロトコル（パケット、payload、信頼性、子機コア、ESP-NOW、LED 演出）
firmware/      hub/, illumination/, m5dial/, atom_led_mist/（単体 Illumination 演出）
config/        *.example.h（実設定はコピーして作成）
pc/            protocol.py, device_gateway.py, mock_hub.py
tests/         firmware/（Unity, native）, protocol/, integration/（pytest）, tools/
docs/
```
