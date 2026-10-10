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
    +-- dualshock4_in_mcu.js     dualshock4_out_mcu.js
    |     Input                     LED / rumble
    v
Node-RED MCU Runtime
```

## Protocol

**Connection flow:**
1. Initialize Bluetooth Classic (Bluedroid) and the HID Host
2. When not paired:
   - DS4 MAC set → connect to that address (retry every 2 seconds)
   - DS4 MAC empty → inquiry for "Wireless Controller" (or CoD: Peripheral/Gamepad) and connect
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
├── package.json                  # Node-RED node registration (2 nodes)
├── manifest.json                 # Node-RED MCU manifest (MCU nodes + native)
├── node/
│   ├── dualshock4_in.html        # Input node editor UI
│   ├── dualshock4_in.js          # Input node editor-side logic
│   ├── dualshock4_in_mcu.js      # Input node MCU runtime
│   ├── dualshock4_out.html       # Output node editor UI
│   ├── dualshock4_out.js         # Output node editor-side logic
│   ├── dualshock4_out_mcu.js     # Output node MCU runtime
│   └── locales/                  # Editor labels and help (en-US / ja)
├── examples/moddable/            # Test with the Moddable SDK only
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

## References and Third-Party Code

### SixaxisPairTool

- Download: [SixaxisPairTool](https://sixaxispairtool.en.lo4d.com/windows)
- Use: Checking the DualShock4's own MAC address and pairing target (optional)

### PS4-esp32

- Repository: [PS4-esp32](https://github.com/aed3/PS4-esp32) by Albert III
- Use: Reference for the DualShock4 input / output report structure
- Included: Not included in this repository (no source code is used)

### Linux hid-playstation

- Source: [drivers/hid/hid-playstation.c](https://github.com/torvalds/linux/blob/master/drivers/hid/hid-playstation.c)
- Use: Reference for the Bluetooth output report (0x11) format and CRC32 calculation

### ESP-IDF

- Repository: [ESP-IDF](https://github.com/espressif/esp-idf) by Espressif Systems
- Use: Bluedroid Classic HID Host / GAP API. Included in the Moddable SDK build
- License: Apache-2.0

### Moddable SDK

- Repository: [Moddable SDK](https://github.com/Moddable-OpenSource/moddable) by Moddable Tech, Inc.
- Use: JavaScript runtime (XS engine) on the ESP32. Used as a build dependency
- Included: Not included in this repository. Install separately
- License: LGPL-3.0 / GPL-3.0

### Node-RED MCU Edition

- Repository: [node-red-mcu](https://github.com/phoddie/node-red-mcu) by Peter Hoddie
- Use: Runtime that runs Node-RED flows on microcontrollers
- Included: Not included in this repository

### node-red-contrib-python-dualshock4

- Repository: [node-red-contrib-python-dualshock4](https://github.com/404background/node-red-contrib-python-dualshock4)
- Use: Reference for the Node-RED editor UI design (Python DualShock4 node)
- Included: Not included in this repository
- License: MIT

### XS in C

- Documentation: [XS in C](https://www.moddable.com/documentation/xs/XS%20in%20C)
- Use: API reference for calling native C extensions from the Moddable SDK (XS JavaScript)
