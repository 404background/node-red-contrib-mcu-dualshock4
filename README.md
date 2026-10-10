# node-red-contrib-mcu-dualshock4

Node-RED MCU用のDualShock4(PS4コントローラ)ノードです。  
ESP32上でBluetooth Classicを使用してDualShock4と通信します。

## 概要

このノードは、[Node-RED MCU Edition](https://github.com/phoddie/node-red-mcu)で動作するDualShock4コントローラノードです。  
ESP32の開発ボード上で、Bluetooth Classic経由でPS4コントローラの入力データ(ボタン、スティック、トリガー)を取得し、LEDや振動を制御できます。

ペアリングはDualShock4のペアリングモード（SHARE + PS 長押し）で行うため、ツールでDualShock4側のMACアドレスを書き換える必要はありません。

## 対応ボード

**ESP32（無印）のみ対応しています。**（ESP32-WROOM / ESP32-WROVER 搭載ボードなど）

DualShock4はBluetooth Classic(BR/EDR)のHIDプロトコルで通信するため、Bluetooth Classicを搭載していないESP32-S2/S3/C3/C6/H2では使用できません（ビルド時にエラーになります）。

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
| buttons | up, down, left, right | D-Pad（斜めは2方向がtrue） | bool |
| analog | lx, ly, rx, ry | スティック（上・右が正） | int8 (-128~127) |
| analog | l2_value, r2_value | トリガー | uint8 (0~255) |
| status | battery | バッテリー | uint8 (0~15) |
| status | charging | 充電中 | bool |

**設定:**
- **DS4 MAC** - ペアリング・接続するDualShock4本体のBluetoothMACアドレス（空欄で最初に見つかったDS4とペアリング）
- **Interval** - データ取得間隔（ms、デフォルト100）
- **Inputs** - 出力に含めるボタン/スティックの選択

**入力メッセージ:**
- フロー開始時に自動で待ち受けを開始します
- `msg.kill = true` - 出力を停止
- `msg.unpair = true` - ESP32に記録されたペアリング情報を全て削除し、再ペアリングを待つ
- その他のメッセージ - 停止中なら再開

### dualshock4 out

DualShock4コントローラのLED・振動を制御するノードです。

**入力メッセージ:**

```javascript
// LEDカラー設定
msg.payload = { led: { r: 255, g: 0, b: 0 } };

// 振動設定
msg.payload = { rumble: { small: 100, large: 200 } };

// LED点滅設定 (ミリ秒, 最大2550)
msg.payload = { flash: { on: 500, off: 500 } };

// 複数を同時に設定
msg.payload = {
    led: { r: 0, g: 255, b: 0 },
    rumble: { small: 50, large: 0 }
};
```

**設定:**
- **DS4 MAC** - dualshock4 inノードと同じ値を設定してください

### ノードの状態表示

| 表示 | 意味 |
|------|------|
| pairing | ペアリングモードのDS4を探索・接続中 |
| waiting (press PS) | ペアリング済み。DS4のPSボタンが押されるのを待機中 |
| connected | 接続中 |

## ペアリング

### 初回

1. フローをESP32に書き込み、起動します（ノードの状態が `pairing` になります）
2. DualShock4の **SHARE + PS** を、ライトバーが素早く2回ずつ点滅するまで長押しします（ペアリングモード）
3. ESP32が自動でDualShock4に接続し、ペアリングします（ノードの状態が `connected` になります）

DS4 MACを空欄にした場合は、ペアリングモードで最初に見つかったDualShock4とペアリングします。  
特定のDualShock4だけを使いたい場合や、近くに複数のコントローラがある場合は、DS4 MACを指定してください。

### DualShock4のMACアドレスの確認方法

- **[SixaxisPairTool](https://sixaxispairtool.en.lo4d.com/windows)（Windows GUI、推奨）**  
  DualShock4をUSBケーブルでPCに接続すると、本体のMACアドレス（Device Address）と、現在記憶しているペアリング先（Current Master）を確認できます
- **PC・スマートフォンのBluetooth設定画面**  
  ペアリングモード中のDualShock4（"Wireless Controller"）のアドレスを確認できます
- **xsbugのログ**  
  DS4 MACを空欄にして一度接続すると、`DS4: connected aa:bb:cc:dd:ee:ff` のように接続したDualShock4のアドレスが出力されます

> [sixaxispairer](https://github.com/user-none/sixaxispairer)（CLI）はPS3用コントローラ（SixAxis / Move）向けのツールで、DualShock4には対応していません。

### 2回目以降

ペアリング情報はESP32（NVS）とDualShock4の両方に保存されます。  
ESP32を再起動しても、**PSボタンを押すだけ**で接続されます。

### 再ペアリング

- DS4 MACを指定している場合: 指定したDualShock4が未ペアリングなら、自動でペアリングを待ちます。別のDualShock4に変えるときはDS4 MACを変更してください
- DS4 MACが空欄の場合: 一度ペアリングすると新しいコントローラは探索しません。dualshock4 inノードに `msg.unpair = true` を送るとペアリング情報を削除し、再びペアリングを待ちます
- DualShock4をPS4本体やPCとペアリングし直した場合も、`msg.unpair = true` の後に再度ペアリングしてください

## アーキテクチャ

```
DualShock4 Controller
    |
    | Bluetooth Classic (HID over L2CAP, SSP)
    v
ESP32 (Moddable SDK + XS)
    |
    +-- ds4_native.c (XS in C)
    |     ESP-IDF Bluedroid Classic HID Host
    |     ペアリング / レポート解析 / 出力レポート送信
    |
    +-- ds4_controller.js (共通クラス)
    |     シングルトン / リスナーパターン
    |     接続状態管理・ネイティブブリッジ
    |
    +-- dualshock4_in_mcu.js     dualshock4_out_mcu.js
    |     入力読み取り              LED/振動制御
    v
Node-RED MCU Runtime
```

### 通信プロトコル

**接続フロー:**
1. Bluetooth Classic(Bluedroid)とHID Hostを初期化
2. 未ペアリングの場合:
   - DS4 MAC指定あり → そのアドレスへ接続を試行（2秒間隔でリトライ）
   - DS4 MAC空欄 → Inquiryで "Wireless Controller"（またはCoD: Peripheral/Gamepad）を探索して接続
   - Secure Simple Pairing（Just Works）でボンディング。リンクキーはNVSに保存
3. ペアリング済みの場合: 起動時にボンディング済みデバイスをHID Hostに登録し、DualShock4からの接続（PSボタン）を受け付け
4. 接続後、Feature Report 0x02を読み出してDualShock4をフルレポート(0x11)モードに切り替え

**入力レポート (DualShock4 → ESP32, Interrupt channel):**
```
Report 0x11: [0]=0x11 [1-2]=フラグ [3-]=以下
Report 0x01: [0]=0x01 [1-]=以下（Feature Report読み出し前の簡易レポート）
  +0..+3: スティック LX/LY/RX/RY (uint8, center=128 → int8変換)
  +4:     D-padハット値(下位4bit, 8=未入力) + 面ボタン(上位4bit)
  +5:     L1/R1/L2/R2(下位) + Share/Options/L3/R3(上位)
  +6:     PS(bit0) + Touchpad(bit1)
  +7..+8: L2/R2 アナログ値 (0-255)
  +29:    バッテリー(下位4bit)/充電(bit4)  ※0x11のみ
```

**出力レポート (ESP32 → DualShock4, Interrupt channel):**
```
Report 0x11 (78 bytes):
  [1]:      0xC0 (HID + CRC)
  [3]:      0x07 (振動 | LED | 点滅 を有効化)
  [6]-[7]:  小/大振動モーター (0-255)
  [8]-[10]: LED R/G/B (0-255)
  [11]-[12]: 点滅 ON/OFF時間 (x10ms, 255 = 2.55秒)
  [74]-[77]: CRC32 (0xA2 + [0]..[73])
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
        ├── ds4_native.c          # XS in C (Bluedroid HID Host)
        └── sdkconfig/            # Bluetooth Classic / HID Host 有効化 (sdkconfig.defaults)
```

### 技術スタック

| レイヤー | 技術 | 説明 |
|---------|------|------|
| Bluetooth通信 | [ESP-IDF](https://github.com/espressif/esp-idf) Bluedroid | Classic HID Host (`esp_hidh_api.h`) とGAP（ペアリング） |
| ネイティブブリッジ | [XS in C](https://www.moddable.com/documentation/xs/XS%20in%20C) | CコードをModdable SDK(XS JavaScript)から呼び出すAPIレイヤー |
| ランタイム | [Moddable SDK](https://github.com/Moddable-OpenSource/moddable) | ESP32上で動作するJavaScriptランタイム(XSエンジン) |
| フロー実行 | Node-RED MCU | Node-REDフローをマイクロコントローラ上で実行 |

### なぜXS in Cが必要か

Moddable SDKのBLEモジュールはBluetooth Low Energy(BLE)のみ対応しています。  
DualShock4はBluetooth Classic(BR/EDR)のHIDプロトコルを使用するため、ESP-IDFのClassic HID HostをXS in Cでラップし、JavaScriptから呼び出せるようにしています。

## セットアップ

### 必要な環境

- [Moddable SDK](https://github.com/Moddable-OpenSource/moddable)（ESP-IDF v5.x）
- [Node-RED MCU Edition](https://github.com/phoddie/node-red-mcu)
- ESP32（無印）搭載の開発ボード

### ESP-IDF設定

Bluetooth Classic(Bluedroid + HID Host)の設定は `native/esp32/sdkconfig/sdkconfig.defaults` に含まれており、`native/manifest.json` の `SDKCONFIGPATH` によりビルド時に自動で適用されます。

> **注意:** `SDKCONFIGPATH` はビルドターゲットが独自に指定しているもの（例: `esp32/m5stack_core2`, `esp32/m5stack_fire`, `esp32/m5paper` などのPSRAM設定）を上書きします。
> これらのボードを使う場合は、ターゲット側の `sdkconfig.defaults` の内容を `native/esp32/sdkconfig/sdkconfig.defaults` に追記してください。
> 素の `esp32` ターゲット（`-p esp32`）では追加作業は不要です。

## Moddable SDKでの単体動作確認

Node-REDを経由せず、Moddable SDK単体でC拡張（Bluetooth実装）が正しく動作するかテストするためのサンプルプロジェクトが同梱されています。

1. **ビルドと書き込み**:
   ```sh
   cd examples/moddable

   # 最初に見つかったDS4とペアリングする場合
   mcconfig -d -m -p esp32

   # 特定のDS4のみとペアリングする場合
   mcconfig -d -m -p esp32 ds4mac="aa:bb:cc:dd:ee:ff"
   ```
2. **動作確認**:
   xsbugに以下のように出力されます。
   ```
   DS4: ESP32 Bluetooth address 24:0a:c4:12:34:58
   [DS4] pairing
   ```
   DualShock4の **SHARE + PS** を長押ししてペアリングモードにすると、接続されて入力状態がリアルタイムで出力されます。
   ```
   DS4: connected aa:bb:cc:dd:ee:ff
   [DS4] connected
   [DS4] {"buttons":{...},"analog":{...},"status":{...}}
   ```

## 参考リポジトリ・サードパーティコード

### SixaxisPairTool

- 配布元: [SixaxisPairTool](https://sixaxispairtool.en.lo4d.com/windows)
- 用途: DualShock4本体のMACアドレス・ペアリング先の確認（任意）

### PS4-esp32

- リポジトリ: [PS4-esp32](https://github.com/aed3/PS4-esp32) by Albert III
- 用途: DualShock4の入力レポート構造・出力レポート構造の参考
- 同梱: このリポジトリには含まれていません（ソースコードは使用していません）

### Linux hid-playstation

- ソース: [drivers/hid/hid-playstation.c](https://github.com/torvalds/linux/blob/master/drivers/hid/hid-playstation.c)
- 用途: Bluetooth出力レポート(0x11)のフォーマットとCRC32計算の参考

### ESP-IDF

- リポジトリ: [ESP-IDF](https://github.com/espressif/esp-idf) by Espressif Systems
- 用途: Bluedroid Classic HID Host / GAP API。Moddable SDKのビルドに含まれます
- ライセンス: Apache-2.0

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
