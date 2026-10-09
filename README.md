# node-red-contrib-mcu-dualshock4

Node-RED MCU用のDualShock4(PS4コントローラ)ノードです。  
ESP32上でBluetooth Classicを使用してDualShock4と通信します。

## 概要

このノードは、[Node-RED MCU Edition](https://github.com/phoddie/node-red-mcu)で動作するDualShock4コントローラノードです。  
ESP32の開発ボード上で、Bluetooth Classic経由でPS4コントローラの入力データ(ボタン、スティック、トリガー)を取得し、LEDや振動を制御できます。

## 対応ESP32ボード

**Bluetooth Classicに対応したESP32が必要です。**

DualShock4はBluetooth Classic(BR/EDR)のHIDプロトコルで通信するため、BLE専用のESP32チップでは使用できません。

| チップ | Bluetooth Classic | 対応状況 |
|--------|:-----------------:|:--------:|
| ESP32 (無印) | 対応 | 利用可能 |
| ESP32-WROVER | 対応 | 利用可能 |
| ESP32-S2 | 非搭載 | 利用不可 |
| ESP32-S3 | BLEのみ | 利用不可 |
| ESP32-C3 | BLEのみ | 利用不可 |
| ESP32-C6 | BLEのみ | 利用不可 |
| ESP32-H2 | BLEのみ | 利用不可 |

## ノード

### dualshock4 in

DualShock4コントローラの入力を読み取るノードです。

**出力データ:**

| カテゴリ | プロパティ | データ | 型 |
|---------|-----------|--------|-----|
| buttons | cross, circle, square, triangle | 面ボタン | bool |
| buttons | l1, r1, l2, r2 | ショルダー | bool |
| buttons | share, options, l3, r3 | その他 | bool |
| buttons | ps, touchpad | 特殊 | bool |
| buttons | up, down, left, right | D-Pad | bool |
| analog | lx, ly, rx, ry | スティック | int8 (-128~127) |
| analog | l2_value, r2_value | トリガー | uint8 (0~255) |
| status | battery | バッテリー | uint8 (0~15) |
| status | charging | 充電中 | bool |

**設定:**
- **DS4 MAC** - 接続を許可するDualShock4のBluetoothMACアドレス（空欄で全て許可）
- **ESP32 MAC** - ESP32のBluetoothMACアドレスを変更（DS4が記憶しているペアリング先MAC、空欄でデフォルト）
- **Interval** - データ取得間隔（ms、デフォルト100）
- **Inputs** - 出力に含めるボタン/スティックの選択

フロー開始時に自動で待ち受けを開始します。`msg.kill = true` で出力を停止し、その他のメッセージで再開します。

### dualshock4 out

DualShock4コントローラのLED・振動を制御するノードです。

**入力メッセージ:**

```javascript
// LEDカラー設定
msg.payload = { led: { r: 255, g: 0, b: 0 } };

// 振動設定
msg.payload = { rumble: { small: 100, large: 200 } };

// LED点滅設定 (ミリ秒)
msg.payload = { flash: { on: 500, off: 500 } };

// 複数を同時に設定
msg.payload = {
    led: { r: 0, g: 255, b: 0 },
    rumble: { small: 50, large: 0 }
};
```

**設定:**
- **DS4 MAC** - 接続を許可するDualShock4のBluetoothMACアドレス（空欄で全て許可）
- **ESP32 MAC** - ESP32のBluetoothMACアドレスを変更（空欄でデフォルト）

## アーキテクチャ

```
DualShock4 Controller
    |
    | Bluetooth Classic (L2CAP/HID)
    v
ESP32 (Moddable SDK + XS)
    |
    +-- ds4_native.c (XS in C)
    |     PS4-esp32ライブラリのCラッパー
    |
    +-- ds4_controller.js (共通クラス)
    |     シングルトン / リスナーパターン
    |     Bluetooth接続管理・ネイティブブリッジ
    |
    +-- dualshock4_in_mcu.js     dualshock4_out_mcu.js
    |     入力読み取り              LED/振動制御
    v
Node-RED MCU Runtime
```

### 通信プロトコル

PS4-esp32ライブラリのプロトコル解析に基づいています。

**接続フロー:**
1. `sppInit()` - Bluetooth Classic初期化、SPPサーバー起動
2. `ps4_l2cap_init_services()` - L2CAPサービス登録
   - HID Control (PSM 0x0011)、HID Interrupt (PSM 0x0013)
3. DualShock4がL2CAP接続を開始
4. Interrupt channelのConfig確認後 → `ps4Enable()` で有効化コマンド送信
   - HID Feature Report: `{0x43, 0x02}` (identifier: `0xF4`)
5. 最初のデータパケット受信時に接続イベント発火 (`is_active = true`)

**入力パケット (DualShock4 → ESP32):**
```
HID Input Report (ps4_parser.c で解析):
  Byte 13-16: スティック LX/LY/RX/RY (uint8, center=128 → int8変換)
  Byte 17:    D-pad方向値(下位4bit) + 面ボタン(上位4bit)
  Byte 18:    L1/R1/L2/R2(下位) + Share/Options/L3/R3(上位)
  Byte 19:    PS(bit0) + Touchpad(bit1)
  Byte 20-21: L2/R2 アナログ値 (0-255)
  Byte 42:    バッテリー/充電/オーディオ/マイク
```

**出力コマンド (ESP32 → DualShock4):**
```
HID Output Report (identifier: 0x11) via L2CAP control channel:
  Header:     {0x80, 0x00, 0xFF}
  Byte 5-6:   小/大振動モーター (0-255)
  Byte 7-9:   LED R/G/B (0-255)
  Byte 10-11: フラッシュ ON/OFF時間 (x10ms, 255 = 2.5秒)
```

### ファイル構成

```
node-red-contrib-mcu-dualshock4/
├── package.json                  # Node-REDノード登録 (2ノード)
├── manifest.json                 # Node-RED MCU用マニフェスト (MCUノード + native)
├── node/
│   ├── dualshock4_in.html        # 入力ノード エディタUI
│   ├── dualshock4_in.js          # 入力ノード PC側ロジック
│   ├── dualshock4_in_mcu.js      # 入力ノード MCUランタイム
│   ├── dualshock4_out.html       # 出力ノード エディタUI
│   ├── dualshock4_out.js         # 出力ノード PC側ロジック
│   └── dualshock4_out_mcu.js     # 出力ノード MCUランタイム
├── examples/moddable/            # Moddable SDK単体での動作確認用
└── native/
    ├── manifest.json             # ds4_controller + Cソース + sdkconfig
    ├── ds4_controller.js         # 共通クラス (シングルトン/リスナー管理)
    └── esp32/
        ├── ds4_native.c          # XS in C (PS4-esp32ラッパー)
        ├── sdkconfig/            # Bluetooth Classic有効化 (sdkconfig.defaults)
        └── ps4/                  # PS4-esp32 Cライブラリ (リポジトリに同梱)
            ├── ps4.c             # コア (初期化/コマンド送信/コールバック)
            ├── ps4.h             # 公開API・データ型定義
            ├── ps4_int.h         # 内部定数・HIDコマンド定義
            ├── ps4_l2cap.c       # L2CAPプロトコル層
            ├── ps4_parser.c      # HID Input Reportパーサー
            ├── ps4_spp.c         # SPP初期化・Bluetooth Classic設定
            ├── stack/            # ESP-IDF Bluetooth Classicスタックヘッダ
            └── osi/              # ESP-IDF OSIヘッダ
```

### 技術スタック

| レイヤー | 技術 | 説明 |
|---------|------|------|
| Bluetooth通信 | [PS4-esp32](https://github.com/aed3/PS4-esp32) | Bluetooth Classic経由でDualShock4と通信するCライブラリ |
| ネイティブブリッジ | [XS in C](https://www.moddable.com/documentation/xs/XS%20in%20C) | CコードをModdable SDK(XS JavaScript)から呼び出すAPIレイヤー |
| ランタイム | [Moddable SDK](https://github.com/Moddable-OpenSource/moddable) | ESP32上で動作するJavaScriptランタイム(XSエンジン) |
| フロー実行 | Node-RED MCU | Node-REDフローをマイクロコントローラ上で実行 |

### なぜXS in Cが必要か

Moddable SDKのBLEモジュールはBluetooth Low Energy(BLE)のみ対応しています。  
DualShock4はBluetooth Classic(BR/EDR)のHIDプロトコルを使用するため、PS4-esp32のCライブラリをXS in Cでラップし、JavaScriptから呼び出せるようにする必要があります。

## セットアップ

### 必要な環境

- [Moddable SDK](https://github.com/Moddable-OpenSource/moddable)
- [Node-RED MCU Edition](https://github.com/phoddie/node-red-mcu)
- ESP32(無印)またはESP32-WROVER搭載の開発ボード

PS4-esp32のCソースはこのリポジトリに同梱されています（`native/esp32/ps4/`）。

### ESP-IDF設定

Bluetooth Classic(Bluedroid + SPP)の設定は `native/esp32/sdkconfig/sdkconfig.defaults` に含まれており、`native/manifest.json` の `SDKCONFIGPATH` によりビルド時に自動で適用されます。

> **注意:** `SDKCONFIGPATH` はビルドターゲットが独自に指定しているもの（例: `esp32/m5stack_core2`, `esp32/m5stack_fire`, `esp32/m5paper` などのPSRAM設定）を上書きします。
> これらのボードを使う場合は、ターゲット側の `sdkconfig.defaults` の内容を `native/esp32/sdkconfig/sdkconfig.defaults` に追記してください。
> 素の `esp32` ターゲット（`-p esp32`）では追加作業は不要です。

### ペアリング

DualShock4はペアリング先のMACアドレスを記憶しています。接続するには2つの方法があります:

**方法A: ESP32のMACを変更する（推奨）**
1. DualShock4が記憶しているペアリング先MACアドレスを確認する（通常はPS4コンソールのMAC）
   - [sixaxispairer](https://github.com/user-none/sixaxispairer)（CLI）または [SixaxisPairTool](https://sixaxispairtool.en.lo4d.com/windows)（Windows GUI）でDualShock4をUSB接続して読み出せます
2. ノードの「ESP32 MAC」にそのMACアドレスを設定する
3. ESP32は起動時にそのMACアドレスに変更し、DualShock4からの接続を待つ

**方法B: DualShock4のMACを変更する**
1. ESP32のBluetoothMACアドレスを確認する
2. [sixaxispairer](https://github.com/user-none/sixaxispairer)（CLI）または [SixaxisPairTool](https://sixaxispairtool.en.lo4d.com/windows)（Windows GUI）でDualShock4にESP32のMACアドレスを書き込む
3. ノードの「ESP32 MAC」は空欄のままにする

> **MACアドレス確認・書き換えツール**
> DualShock4が記憶しているペアリング先MACの読み出し／書き換えには、DualShock4をUSBケーブルでPCに接続した上で次のツールを利用してください:
> - [sixaxispairer](https://github.com/user-none/sixaxispairer) — クロスプラットフォームCLI
> - [SixaxisPairTool](https://sixaxispairtool.en.lo4d.com/windows) — Windows向けGUI

### DS4 MACアドレスフィルタ

特定のDualShock4コントローラのみ接続を許可したい場合、ノードの「DS4 MAC」にDualShock4のBluetoothMACアドレスを指定します。
指定した場合、そのMACアドレスからのL2CAP接続のみを受け付け、他のコントローラからの接続は拒否します。
空欄の場合は全てのDualShock4からの接続を受け付けます。

`ps4SetBluetoothMacAddress()`は内部的に`esp_base_mac_addr_set()`を呼び出し、ESP32のベースMACアドレスを変更します（Bluetooth MACはベースMAC + 2）。

## Moddable SDKでの単体動作確認

Node-REDを経由せず、Moddable SDK単体でC拡張（Bluetooth実装）が正しく動作するかテストするためのサンプルプロジェクトが同梱されています。

1. **MACアドレスの設定**: 
   `examples/moddable/main.js` は、ESP32自身のWi-Fi MACアドレスからBluetooth Classic用のMACアドレスを自動計算し、実行時にコンソールへ出力（`trace`）します。
2. **ビルドと書き込み**:
   ターミナルを開き、コマンドラインから `examples/moddable` ディレクトリに移動してから、Moddableの `mcconfig` コマンドを実行します。
   このとき、特定のDS4からの接続のみを許可したい場合は引数（`ds4mac`設定）を渡すことができます。
   ```sh
   cd examples/moddable
   
   # すべてのDS4からの接続を許可する場合
   mcconfig -d -m -p esp32
   
   # 特定のフルMACアドレスのDS4のみを許可する場合
   mcconfig -d -m -p esp32 ds4mac="aa:bb:cc:dd:ee:ff"
   ```
3. **動作確認**:
   ビルドが完了してESP32が再起動するとxsbugデバッガが立ち上がり、以下のように出力されます。
   ```
   ====================================================
   DS4 debug example starting
   ESP32 Bluetooth MAC Address: 24:0A:C4:12:34:58
   DS4 Filter MAC Address: (Any)
   ====================================================
   ```
   この `ESP32 Bluetooth MAC Address` に記載のMACアドレス（例なら `24:0A:C4:12:34:58`）を、手元のPCから `sixaxispairer` などを使ってDualShock4側にペアリング情報として書き込みます。
   
   書き込み後、DualShock4のPSボタンを押して接続を試みてください。正しく接続されると、ボタンやスティックの入力状態がxsbugのコンソールにリアルタイムで出力されます。

## 参考リポジトリ・サードパーティコード

### PS4-esp32

- リポジトリ: [PS4-esp32](https://github.com/aed3/PS4-esp32) by Albert III
- 用途: ESP32でBluetooth Classic経由でDualShock4と通信するCライブラリ
- 同梱: `native/esp32/ps4/` ディレクトリにPS4-esp32のCソースコードを同梱・改変して使用しています
  - MACアドレスフィルタ機能（`ps4SetFilterAddress()`, `ps4MatchesFilterAddress()`）を追加
- ライセンス: PS4-esp32リポジトリには明示的なライセンスファイルがありません。このプロジェクトでの使用にあたっては原作者への帰属を明記しています。

### Moddable SDK

- リポジトリ: [Moddable SDK](https://github.com/Moddable-OpenSource/moddable) by Moddable Tech, Inc.
- 用途: ESP32上で動作するJavaScriptランタイム（XSエンジン）。ビルド時の依存関係として使用。
- 同梱: このリポジトリには含まれていません。別途インストールが必要です。
- ライセンス: LGPL-3.0 / GPL-3.0

### Node-RED MCU Edition

- リポジトリ: [node-red-mcu](https://github.com/phoddie/node-red-mcu) by Peter Hoddie
- 用途: Node-REDフローをマイクロコントローラ上で実行するランタイム
- 同梱: このリポジトリには含まれていません。

### node-red-contrib-python-dualshock4

- リポジトリ: [node-red-contrib-python-dualshock4](https://github.com/404background/node-red-contrib-python-dualshock4)
- 用途: Node-REDエディタUIデザインの参考元（Python版DualShock4ノード）
- 同梱: このリポジトリには含まれていません。
- ライセンス: MIT

### XS in C

- ドキュメント: [XS in C](https://www.moddable.com/documentation/xs/XS%20in%20C)
- 用途: ネイティブC拡張をModdable SDK（XS JavaScript）から呼び出すためのAPIリファレンス

## ライセンス

MIT License - 詳細は [LICENSE](LICENSE) ファイルを参照してください。

`native/esp32/ps4/` ディレクトリに含まれるPS4-esp32由来のコードについては、[PS4-esp32](https://github.com/aed3/PS4-esp32) の原作者 Albert III に帰属します。
