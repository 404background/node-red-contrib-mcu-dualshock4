# node-red-contrib-mcu-dualshock4

DualShock4 (PS4 controller) nodes for [Node-RED MCU Edition](https://github.com/phoddie/node-red-mcu).  
Reads buttons, sticks and triggers, and controls the LED and rumble over Bluetooth Classic on an ESP32.

## Requirements

- **Original ESP32 only** (ESP32-S2/S3/C3/C6/H2 have no Bluetooth Classic)
- [Moddable SDK](https://github.com/Moddable-OpenSource/moddable) (ESP-IDF v5.x)
- [Node-RED MCU Edition](https://github.com/phoddie/node-red-mcu)

## Install

```sh
cd ~/.node-red
npm install @background404/node-red-contrib-mcu-dualshock4
```

## Nodes

- **dualshock4 in** - Outputs `msg.payload` with `buttons`, `analog` and `status`
- **dualshock4 out** - Sets the LED / rumble / flash from `msg.payload`, e.g. `{ led: { r: 255, g: 0, b: 0 } }`

## Pairing

1. Write the flow to the ESP32 and start it
2. Hold **SHARE + PS** on the DualShock4 until the light bar double-flashes
3. The ESP32 pairs automatically. After that, just press **PS** to connect

Set **DS4 MAC** in the node to pair with a specific controller only.

## Documentation

- Usage: [English](docs/en/usage.md) | [日本語](docs/ja/usage.md)
- Specification: [English](docs/en/specification.md) | [日本語](docs/ja/specification.md)

## License

MIT - see [LICENSE](LICENSE).
