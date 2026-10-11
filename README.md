# node-red-contrib-mcu-dualshock4

DualShock4 (PS4 controller) nodes for [Node-RED MCU Edition](https://github.com/phoddie/node-red-mcu).  
Reads buttons, sticks and triggers, and controls the LED and rumble over Bluetooth Classic on an ESP32.

## Requirements

- **Original ESP32 only** (ESP32-S2/S3/C3/C6/H2 have no Bluetooth Classic)
- [Moddable SDK](https://github.com/Moddable-OpenSource/moddable) (ESP-IDF v5.x), with the patch below
- [Node-RED MCU Edition](https://github.com/phoddie/node-red-mcu)

## Moddable SDK Patch (Required)

Moddable supports only BLE, so its ESP32 startup code frees the memory reserved for Bluetooth Classic whenever Bluetooth is enabled.
The DualShock4 needs Bluetooth Classic, so without this patch Bluetooth does not start and the dualshock4 config node reports
`BT init failed (0x103): patch Moddable main.c`

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

- **dualshock4 config** - Connection to the DualShock4 (DS4 MAC (optional), interval), shared by the nodes below
- **dualshock4 in** - Outputs `msg.payload` with `buttons`, `analog` and `status` when the input changes
- **dualshock4 out** - Sets the LED / rumble / flash from `msg.payload`, e.g. `{ led: { r: 255, g: 0, b: 0 } }`

## Connecting a DualShock4

No PC tool is needed. The ESP32 finds the DualShock4 in pairing mode by itself.

1. Add a **dualshock4 in** / **dualshock4 out** node and a **dualshock4 config** node. **DS4 MAC can be left empty**
2. Write the flow to the ESP32 and start it (the node status becomes `pairing`)
3. Hold **SHARE + PS** on the DualShock4 until the light bar double-flashes (pairing mode)
4. The ESP32 pairs automatically. The status becomes `connected`, and the debug sidebar (and the serial log) shows the DualShock4's address:
   ```
   DS4: connected aa:bb:cc:dd:ee:ff. Set it as DS4 MAC to use only this controller
   ```
5. From then on, just press **PS** to connect (also after the ESP32 restarts)
6. Optional: enter that address as **DS4 MAC** and write the flow again. Then only this DualShock4 is paired and accepted

| DS4 MAC | Behavior |
|---|---|
| Empty | Pairs with the **first** DualShock4 in pairing mode it finds, then keeps using that controller. If other DualShock4s may be in pairing mode nearby during the first pairing, set DS4 MAC |
| Set | Pairs with and accepts only that DualShock4. Others are rejected (`rejected xx:xx:...: not the DS4 MAC`) |

- The DualShock4 remembers only one host. After it was paired with a PS4, PC or another ESP32, hold **SHARE + PS** again and this ESP32 pairs with it again
- To switch to another DualShock4 with DS4 MAC empty, send `msg.unpair = true` to the dualshock4 out node, then put the new controller in pairing mode
- To check the address beforehand, you can also use "Device Address" in [SixaxisPairTool](https://sixaxispairtool.en.lo4d.com/windows) (only to read it; nothing has to be written to the controller) or the Bluetooth settings of a PC / smartphone

## Example

Import "dualshock4" from Import → Examples in the Node-RED menu.

## Documentation

- Usage: [English](docs/en/usage.md) | [日本語](docs/ja/usage.md)
- Specification: [English](docs/en/specification.md) | [日本語](docs/ja/specification.md)

## License

MIT - see [LICENSE](LICENSE).
