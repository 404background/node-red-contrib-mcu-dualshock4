# 使い方

[English](../en/usage.md) | 日本語

## 対応ボード

**ESP32（無印）のみ対応しています。**（ESP32-WROOM / ESP32-WROVER 搭載ボードなど）

DualShock4はBluetooth Classic(BR/EDR)のHIDプロトコルで通信するため、Bluetooth Classicを搭載していないESP32-S2/S3/C3/C6/H2では使用できません（ビルド時にエラーになります）。

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

## ノード

| ノード | 役割 |
|--------|------|
| **dualshock4 config** | 設定ノード。DualShock4との接続（DS4 MAC・取得間隔）を設定し、in / out ノードで共有します |
| **dualshock4 in** | コントローラの入力を受信します |
| **dualshock4 out** | LED・振動などをコントローラへ送信します |

接続できるDualShock4は1台です。設定ノードは1つだけ使用してください。

### dualshock4 config

**設定:**
- **DS4 MAC** - ペアリング・接続するDualShock4本体のBluetoothMACアドレス（空欄で最初に見つかったDS4とペアリング）
- **取得間隔** - コントローラの状態を読み取る間隔（ms、デフォルト100）

### dualshock4 in

コントローラの入力を受信します。選択した項目が変化したときだけメッセージを送信します。

**出力データ（`msg.payload`）:**

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
- **DS4** - 使用する dualshock4 config ノード
- **出力項目** - 出力に含めるボタン/スティックの選択（初期状態はすべて選択）。`status` は常に含まれます

### dualshock4 out

LED・振動などをコントローラへ送信します。

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

// ペアリング情報を削除して再ペアリングを待つ
msg.unpair = true;
```

指定しなかった項目は前回の値を保持します。振動・点滅は `0` で停止します。

**設定:**
- **DS4** - 使用する dualshock4 config ノード

### ノードの状態表示

in / out ノードに接続状態が表示されます。

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

dualshock4 config ノードのDS4 MACを空欄にした場合は、ペアリングモードで最初に見つかったDualShock4とペアリングします。  
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
- DS4 MACが空欄の場合: 一度ペアリングすると新しいコントローラは探索しません。dualshock4 outノードに `msg.unpair = true` を送るとペアリング情報を削除し、再びペアリングを待ちます
- DualShock4をPS4本体やPCとペアリングし直した場合も、`msg.unpair = true` の後に再度ペアリングしてください

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

内部仕様は [specification.md](specification.md) を参照してください。
