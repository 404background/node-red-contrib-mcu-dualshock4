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

### Moddable SDKの修正（必須）

Moddable SDKはBLEのみをサポートしているため、Bluetoothが有効なとき起動時にBluetooth Classic用のメモリを解放します（`build/devices/esp32/lib/main/main.c`。2018年のコミット `7ed07750e`「free bluetooth classic reserve memory at startup - we don't support classic」で追加され、5.12.0・7.2.1・最新の9.5.0でも同じです）。
DualShock4はBluetooth Classicを使うため、そのままでは起動時に `bluetooth controller init failed (0x103)` で停止し、再起動を繰り返します。

`main.c` の次の行を変更してください（BLEだけを使うアプリの動作は変わりません）。

```c
// 変更前
#if CONFIG_BT_ENABLED
// 変更後
#if CONFIG_BT_ENABLED && !CONFIG_BT_CLASSIC_ENABLED
```

```sh
sed -i 's/^#if CONFIG_BT_ENABLED$/#if CONFIG_BT_ENABLED \&\& !CONFIG_BT_CLASSIC_ENABLED/' $MODDABLE/build/devices/esp32/lib/main/main.c
```

### 動作確認済みのバージョン

| ソフトウェア | バージョン |
|-------------|-----------|
| Moddable SDK | 5.12.0 |
| ESP-IDF | v5.5.1 |
| node-red-mcu | e91b670 |

ESP32（ESP32-D0WDQ6）とDualShock4の実機で、ペアリング・再接続・入力・LED・振動を確認しています（Moddable SDKの修正後）。

### ビルド時のトラブルシューティング

このノードはBluetoothを有効にするためsdkconfigを変更するので、初回のビルドで `Reconfiguring ESP-IDF...`（CMakeの再設定）が実行されます。
このとき次のようなエラーで止まる場合は、ESP-IDFやModdableを更新する前のビルドフォルダが残っている可能性があります。

```
The C compiler ... is not able to compile a simple test program.
ninja: error: loading 'build.ninja': No such file or directory
```

エラーに表示されているビルドフォルダを削除してから、再度ビルドしてください（Node-RED MCUの場合はプラグインのキャッシュも削除します）。

```sh
rm -rf $MODDABLE/build/tmp/esp32/debug/<名前>
rm -rf ~/.node-red/mcu-plugin-cache/<名前>
```

## ノード

| ノード | 役割 |
|--------|------|
| **dualshock4 config** | 設定ノード。DualShock4との接続（DS4 MAC・取得間隔）を設定し、in / out ノードで共有します |
| **dualshock4 in** | コントローラの入力を受信します |
| **dualshock4 out** | LED・振動などをコントローラへ送信します |

接続できるDualShock4は1台です。設定ノードは1つだけ使用してください。

### dualshock4 config

**設定:**
- **DS4 MAC** - ペアリング・接続するDualShock4本体のBluetoothMACアドレス（**必須**。未設定の場合はエラーになり、接続しません）
- **取得間隔** - コントローラの状態を読み取る間隔（ms、デフォルト100）

### dualshock4 in

コントローラの入力を受信します。選択した項目が変化したときだけメッセージを送信します（スティック・トリガーは値が2以上変化したとき）。

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
| starting | Bluetoothを起動中 |
| pairing | ペアリングモードのDS4を探索・接続中 |
| waiting (press PS) | ペアリング済み。DS4のPSボタンが押されるのを待機中 |
| connected | 接続中 |

ペアリングや接続に失敗した場合は、原因がデバッグサイドバーに警告として表示されます。

## ペアリング

### 初回

1. フローをESP32に書き込み、起動します（ノードの状態が `pairing` になります）
2. DualShock4の **SHARE + PS** を、ライトバーが素早く2回ずつ点滅するまで長押しします（ペアリングモード）
3. ESP32が自動でDualShock4に接続し、ペアリングします（ノードの状態が `connected` になります）

ペアリングするDualShock4のMACアドレスを、dualshock4 config ノードのDS4 MACに設定しておく必要があります。  
特定のDualShock4だけを使いたい場合や、近くに複数のコントローラがある場合は、DS4 MACを指定してください。

### DualShock4のMACアドレスの確認方法

- **[SixaxisPairTool](https://sixaxispairtool.en.lo4d.com/windows)（Windows GUI、推奨）**  
  DualShock4をUSBケーブルでPCに接続すると、本体のMACアドレス（Device Address）と、現在記憶しているペアリング先（Current Master）を確認できます
- **PC・スマートフォンのBluetooth設定画面**  
  ペアリングモード中のDualShock4（"Wireless Controller"）のアドレスを確認できます
- **Moddable SDK単体の動作確認プログラム**（[後述](#moddable-sdkでの単体動作確認)）  
  `ds4mac` を指定せずに実行すると最初に見つかったDualShock4と接続し、`DS4: connected aa:bb:cc:dd:ee:ff` のようにアドレスが出力されます

> [sixaxispairer](https://github.com/user-none/sixaxispairer)（CLI）はPS3用コントローラ（SixAxis / Move）向けのツールで、DualShock4には対応していません。

### 2回目以降

ペアリング情報はESP32（NVS）とDualShock4の両方に保存されます。  
ESP32を再起動しても、**PSボタンを押すだけ**で接続されます。

### 再ペアリング

- DS4 MACを指定している場合: 指定したDualShock4が未ペアリングなら、自動でペアリングを待ちます。別のDualShock4に変えるときはDS4 MACを変更してください
- DualShock4をPS4本体やPCとペアリングし直した場合は、dualshock4 outノードに `msg.unpair = true` を送ってペアリング情報を削除し、再度ペアリングしてください

## サンプルフロー

Node-REDのメニュー「読み込み → サンプル → @background404/node-red-contrib-mcu-dualshock4 → dualshock4」から読み込めます（`examples/dualshock4.json`）。

| 内容 | ノード |
|------|--------|
| 全入力をデバッグ表示 | dualshock4 in → debug |
| 面ボタンでLEDの色、L2/R2トリガーで振動を変える | dualshock4 in → function → dualshock4 out |
| LED赤・点滅・停止・ペアリング解除 | inject → dualshock4 out |

読み込み後、MCU用のフローとしてESP32向けにビルドしてください。

## Moddable SDKでの単体動作確認

Node-REDを経由せず、Moddable SDK単体でC拡張（Bluetooth実装）が正しく動作するかテストするためのサンプルプロジェクトが同梱されています。

1. **ビルドと書き込み**:
   ```sh
   cd test/moddable

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
