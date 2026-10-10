# 仕様

[English](../en/specification.md) | 日本語

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
    +-- dualshock4_config_mcu.js (設定ノード)
    |     接続設定 (DS4 MAC・取得間隔)、in/outへの状態通知
    |
    +-- dualshock4_in_mcu.js     dualshock4_out_mcu.js
    |     入力受信                  LED/振動・unpair
    v
Node-RED MCU Runtime
```

## 通信プロトコル

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

## ファイル構成

```
node-red-contrib-mcu-dualshock4/
├── package.json                  # Node-REDノード登録 (3ノード)
├── manifest.json                 # Node-RED MCU用マニフェスト (MCUノード + native)
├── node/
│   ├── dualshock4_config.html    # 設定ノード エディタUI
│   ├── dualshock4_config.js      # 設定ノード PC側ロジック
│   ├── dualshock4_config_mcu.js  # 設定ノード MCUランタイム
│   ├── dualshock4_in.html        # 入力ノード エディタUI
│   ├── dualshock4_in.js          # 入力ノード PC側ロジック
│   ├── dualshock4_in_mcu.js      # 入力ノード MCUランタイム
│   ├── dualshock4_out.html       # 出力ノード エディタUI
│   ├── dualshock4_out.js         # 出力ノード PC側ロジック
│   ├── dualshock4_out_mcu.js     # 出力ノード MCUランタイム
│   └── locales/                  # エディタ表示・ヘルプ (en-US / ja)
├── examples/dualshock4.json      # サンプルフロー
├── test/moddable/                # Moddable SDK単体での動作確認用
├── docs/                         # ドキュメント (en / ja)
└── native/
    ├── manifest.json             # ds4_controller + Cソース + sdkconfig
    ├── ds4_controller.js         # 共通クラス (シングルトン/リスナー管理)
    └── esp32/
        ├── ds4_native.c          # XS in C (Bluedroid HID Host)
        └── sdkconfig/            # Bluetooth Classic / HID Host 有効化 (sdkconfig.defaults)
```

## 技術スタック

| レイヤー | 技術 | 説明 |
|---------|------|------|
| Bluetooth通信 | [ESP-IDF](https://github.com/espressif/esp-idf) Bluedroid | Classic HID Host (`esp_hidh_api.h`) とGAP（ペアリング） |
| ネイティブブリッジ | [XS in C](https://www.moddable.com/documentation/xs/XS%20in%20C) | CコードをModdable SDK(XS JavaScript)から呼び出すAPIレイヤー |
| ランタイム | [Moddable SDK](https://github.com/Moddable-OpenSource/moddable) | ESP32上で動作するJavaScriptランタイム(XSエンジン) |
| フロー実行 | Node-RED MCU | Node-REDフローをマイクロコントローラ上で実行 |

## なぜXS in Cが必要か

Moddable SDKのBLEモジュールはBluetooth Low Energy(BLE)のみ対応しています。  
DualShock4はBluetooth Classic(BR/EDR)のHIDプロトコルを使用するため、ESP-IDFのClassic HID HostをXS in Cでラップし、JavaScriptから呼び出せるようにしています。
