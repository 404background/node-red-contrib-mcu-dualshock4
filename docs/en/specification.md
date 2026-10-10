# Specification

English | [日本語](../ja/specification.md)

## Architecture

```
DualShock4 Controller
    |
    | Bluetooth Classic (HID over L2CAP, SSP)
    v
ESP32 (Moddable SDK + XS)
    |
    +-- ds4_native.c (XS in C)
    |     ESP-IDF Bluedroid Classic HID Host
    |     Pairing / report parsing / output report
    |
    +-- ds4_controller.js (shared class)
    |     Singleton / listener pattern
    |     Connection state, native bridge
    |
    +-- dualshock4_config_mcu.js (configuration node)
    |     Connection settings (DS4 MAC, interval), status to in/out
    |
    +-- dualshock4_in_mcu.js     dualshock4_out_mcu.js
    |     Input                     LED / rumble / unpair
    v
Node-RED MCU Runtime
```

## Protocol

**Connection flow:**
1. Initialize Bluetooth Classic (Bluedroid) and the HID Host
2. When not paired:
   - DS4 MAC set → connect to that address (retry every 2 seconds)
   - DS4 MAC empty (Moddable SDK test program only; required in Node-RED) → inquiry for "Wireless Controller" (or CoD: Peripheral/Gamepad) and connect
   - Bond with Secure Simple Pairing (Just Works). The link key is stored in NVS
3. When paired: register the bonded devices with the HID Host at start-up and accept the connection from the DualShock4 (PS button)
4. After connecting, read Feature Report 0x02 to switch the DualShock4 to full reports (0x11)

**Input report (DualShock4 → ESP32, interrupt channel):**
```
Report 0x11: [0]=0x11 [1-2]=flags [3-]=below
Report 0x01: [0]=0x01 [1-]=below (reduced report before the feature report is read)
  +0..+3: Sticks LX/LY/RX/RY (uint8, center=128 → int8)
  +4:     D-pad hat (low 4 bits, 8=released) + face buttons (high 4 bits)
  +5:     L1/R1/L2/R2 (low) + Share/Options/L3/R3 (high)
  +6:     PS (bit0) + Touchpad (bit1)
  +7..+8: L2/R2 analog values (0-255)
  +29:    Battery (low 4 bits) / charging (bit4)  *0x11 only
```

**Output report (ESP32 → DualShock4, interrupt channel):**
```
Report 0x11 (78 bytes):
  [1]:       0xC0 (HID + CRC)
  [3]:       0x07 (enable rumble | LED | flash)
  [6]-[7]:   Small / large rumble motor (0-255)
  [8]-[10]:  LED R/G/B (0-255)
  [11]-[12]: Flash on/off time (x10ms, 255 = 2.55 s)
  [74]-[77]: CRC32 (0xA2 + [0]..[73])
```

## File Structure

```
node-red-contrib-mcu-dualshock4/
├── package.json                  # Node-RED node registration (3 nodes)
├── manifest.json                 # Node-RED MCU manifest (MCU nodes + native)
├── node/
│   ├── dualshock4_config.html    # Configuration node editor UI
│   ├── dualshock4_config.js      # Configuration node editor-side logic
│   ├── dualshock4_config_mcu.js  # Configuration node MCU runtime
│   ├── dualshock4_in.html        # Input node editor UI
│   ├── dualshock4_in.js          # Input node editor-side logic
│   ├── dualshock4_in_mcu.js      # Input node MCU runtime
│   ├── dualshock4_out.html       # Output node editor UI
│   ├── dualshock4_out.js         # Output node editor-side logic
│   ├── dualshock4_out_mcu.js     # Output node MCU runtime
│   └── locales/                  # Editor labels and help (en-US / ja)
├── examples/dualshock4.json      # Sample flow
├── test/moddable/                # Test with the Moddable SDK only
├── docs/                         # Documentation (en / ja)
└── native/
    ├── manifest.json             # ds4_controller + C source + sdkconfig
    ├── ds4_controller.js         # Shared class (singleton / listeners)
    └── esp32/
        ├── ds4_native.c          # XS in C (Bluedroid HID Host)
        └── sdkconfig/            # Enables Bluetooth Classic / HID Host (sdkconfig.defaults)
```

## Technology Stack

| Layer | Technology | Description |
|---------|------|------|
| Bluetooth | [ESP-IDF](https://github.com/espressif/esp-idf) Bluedroid | Classic HID Host (`esp_hidh_api.h`) and GAP (pairing) |
| Native bridge | [XS in C](https://www.moddable.com/documentation/xs/XS%20in%20C) | API layer to call C code from the Moddable SDK (XS JavaScript) |
| Runtime | [Moddable SDK](https://github.com/Moddable-OpenSource/moddable) | JavaScript runtime (XS engine) on the ESP32 |
| Flow execution | Node-RED MCU | Runs Node-RED flows on microcontrollers |

## Why XS in C

The Moddable SDK BLE module supports Bluetooth Low Energy (BLE) only.  
The DualShock4 uses the HID protocol over Bluetooth Classic (BR/EDR), so the ESP-IDF Classic HID Host is wrapped with XS in C to make it callable from JavaScript.
