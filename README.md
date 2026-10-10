# node-red-contrib-mcu-dualshock4

DualShock4 (PS4 controller) nodes for [Node-RED MCU Edition](https://github.com/phoddie/node-red-mcu).  
Reads buttons, sticks and triggers, and controls the LED and rumble over Bluetooth Classic on an ESP32.

## Requirements

- **Original ESP32 only** (ESP32-S2/S3/C3/C6/H2 have no Bluetooth Classic)
- [Moddable SDK](https://github.com/Moddable-OpenSource/moddable) (ESP-IDF v5.x), with the patch below
- [Node-RED MCU Edition](https://github.com/phoddie/node-red-mcu)

## Moddable SDK Patch (Required)

Moddable supports only BLE, so its ESP32 startup code frees the memory reserved for Bluetooth Classic whenever Bluetooth is enabled.
The DualShock4 needs Bluetooth Classic, so without this patch the ESP32 stops with
`bluetooth controller init failed (0x103): Moddable main.c released the Bluetooth Classic memory.`

Change two lines in `$MODDABLE/build/devices/esp32/lib/main/main.c` (BLE-only apps behave the same as before):

```c
// before
#if CONFIG_BT_ENABLED
// after
#if CONFIG_BT_ENABLED && !CONFIG_BT_CLASSIC_ENABLED
```

```sh
sed -i 's/^#if CONFIG_BT_ENABLED$/#if CONFIG_BT_ENABLED \&\& !CONFIG_BT_CLASSIC_ENABLED/' $MODDABLE/build/devices/esp32/lib/main/main.c
```

The same code is in Moddable 5.12.0, 7.2.1 and 9.5.0 (latest). See [docs](docs/en/usage.md#moddable-sdk-patch-required) for details.

## Install

```sh
cd ~/.node-red
npm install @background404/node-red-contrib-mcu-dualshock4
```

## Nodes

- **dualshock4 config** - Connection to the DualShock4 (DS4 MAC, interval), shared by the nodes below
- **dualshock4 in** - Outputs `msg.payload` with `buttons`, `analog` and `status` when the input changes
- **dualshock4 out** - Sets the LED / rumble / flash from `msg.payload`, e.g. `{ led: { r: 255, g: 0, b: 0 } }`

## Pairing

1. Set **DS4 MAC** (the DualShock4's Bluetooth address, required) in the dualshock4 config node
2. Write the flow to the ESP32 and start it
3. Hold **SHARE + PS** on the DualShock4 until the light bar double-flashes
4. The ESP32 pairs automatically. After that, just press **PS** to connect

## Example

Import "dualshock4" from Import → Examples in the Node-RED menu.

## Documentation

- Usage: [English](docs/en/usage.md) | [日本語](docs/ja/usage.md)
- Specification: [English](docs/en/specification.md) | [日本語](docs/ja/specification.md)

## License

MIT - see [LICENSE](LICENSE).
