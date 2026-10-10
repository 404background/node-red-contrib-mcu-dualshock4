# Usage

English | [日本語](../ja/usage.md)

## Supported Boards

**Only the original ESP32 is supported** (boards with ESP32-WROOM / ESP32-WROVER, etc.).

The DualShock4 communicates with the HID protocol over Bluetooth Classic (BR/EDR), so it cannot be used with ESP32-S2/S3/C3/C6/H2, which have no Bluetooth Classic (the build fails with an error).

## Setup

### Requirements

- [Moddable SDK](https://github.com/Moddable-OpenSource/moddable) (ESP-IDF v5.x)
- [Node-RED MCU Edition](https://github.com/phoddie/node-red-mcu)
- A development board with the original ESP32

### ESP-IDF Configuration

The Bluetooth Classic (Bluedroid + HID Host) settings are included in `native/esp32/sdkconfig/sdkconfig.defaults` and are applied automatically at build time through `SDKCONFIGPATH` in `native/manifest.json`.

> **Note:** `SDKCONFIGPATH` overrides the one set by build targets that have their own (e.g. the PSRAM settings of `esp32/m5stack_core2`, `esp32/m5stack_fire`, `esp32/m5paper`).
> When using those boards, append the contents of the target's `sdkconfig.defaults` to `native/esp32/sdkconfig/sdkconfig.defaults`.
> No extra work is needed for the plain `esp32` target (`-p esp32`).

## Nodes

### dualshock4 in

Reads input from the DualShock4 controller. A message is sent only when a selected input changes.

**Output data:**

| Category | Property | Data | Type |
|---------|-----------|--------|-----|
| buttons | cross, circle, square, triangle | Face buttons | bool |
| buttons | l1, r1, l2, r2 | Shoulder buttons | bool |
| buttons | share, options, l3, r3 | Others | bool |
| buttons | ps, touchpad | Special | bool |
| buttons | up, down, left, right | D-Pad (diagonals set two directions) | bool |
| analog | lx, ly, rx, ry | Sticks (up / right are positive) | int8 (-128~127) |
| analog | l2_value, r2_value | Triggers | uint8 (0~255) |
| status | battery | Battery | uint8 (0~15) |
| status | charging | Charging | bool |

**Settings:**
- **DS4 MAC** - Bluetooth MAC address of the DualShock4 to pair / connect (empty: pair with the first DualShock4 found)
- **Interval** - Polling interval (ms, default 100)
- **Inputs** - Buttons / sticks to include in the output (all selected by default). `status` is always included

**Input messages:**
- Starts listening automatically when the flow starts
- `msg.unpair = true` - Delete all pairing information stored on the ESP32 and wait for pairing again

### dualshock4 out

Controls the LED and rumble of the DualShock4 controller.

**Input messages:**

```javascript
// LED color
msg.payload = { led: { r: 255, g: 0, b: 0 } };

// Rumble
msg.payload = { rumble: { small: 100, large: 200 } };

// LED flash (milliseconds, max 2550)
msg.payload = { flash: { on: 500, off: 500 } };

// Several at once
msg.payload = {
    led: { r: 0, g: 255, b: 0 },
    rumble: { small: 50, large: 0 }
};
```

Unspecified items keep their previous values. Rumble and flash stop with `0`.  
The controller is the one selected by **DS4 MAC** on the dualshock4 in node (the out node has no settings).

### Node Status

| Status | Meaning |
|------|------|
| pairing | Searching for / connecting to a DualShock4 in pairing mode |
| waiting (press PS) | Paired. Waiting for the PS button on the DualShock4 |
| connected | Connected |

## Pairing

### First Time

1. Write the flow to the ESP32 and start it (the node status becomes `pairing`)
2. Hold **SHARE + PS** on the DualShock4 until the light bar double-flashes (pairing mode)
3. The ESP32 connects to the DualShock4 and pairs automatically (the node status becomes `connected`)

If DS4 MAC is empty, the ESP32 pairs with the first DualShock4 found in pairing mode.  
To use a specific DualShock4 only, or when several controllers are nearby, set DS4 MAC.

### Checking the DualShock4 MAC Address

- **[SixaxisPairTool](https://sixaxispairtool.en.lo4d.com/windows) (Windows GUI, recommended)**  
  Connect the DualShock4 to a PC with a USB cable to see its own MAC address (Device Address) and the currently stored pairing target (Current Master)
- **Bluetooth settings on a PC / smartphone**  
  Shows the address of the DualShock4 ("Wireless Controller") in pairing mode
- **xsbug log**  
  After connecting once with DS4 MAC empty, the address of the connected DualShock4 is printed, e.g. `DS4: connected aa:bb:cc:dd:ee:ff`

> [sixaxispairer](https://github.com/user-none/sixaxispairer) (CLI) is a tool for PS3 controllers (SixAxis / Move) and does not support the DualShock4.

### After That

Pairing information is stored on both the ESP32 (NVS) and the DualShock4.  
Even after the ESP32 restarts, **just press the PS button** to connect.

### Re-pairing

- With DS4 MAC set: if that DualShock4 is not paired yet, the ESP32 waits for pairing automatically. To switch to another DualShock4, change DS4 MAC
- With DS4 MAC empty: once paired, no new controller is searched for. Send `msg.unpair = true` to the dualshock4 in node to delete the pairing information and wait for pairing again
- If the DualShock4 was paired with a PS4 console or PC in the meantime, also send `msg.unpair = true` and pair again

## Testing with the Moddable SDK Only

A sample project is included to test that the C extension (Bluetooth implementation) works with the Moddable SDK alone, without Node-RED.

1. **Build and flash**:
   ```sh
   cd examples/moddable

   # Pair with the first DS4 found
   mcconfig -d -m -p esp32

   # Pair with a specific DS4 only
   mcconfig -d -m -p esp32 ds4mac="aa:bb:cc:dd:ee:ff"
   ```
2. **Check**:
   xsbug prints the following.
   ```
   DS4: ESP32 Bluetooth address 24:0a:c4:12:34:58
   [DS4] pairing
   ```
   Hold **SHARE + PS** on the DualShock4 to enter pairing mode. Once connected, the input state is printed in real time.
   ```
   DS4: connected aa:bb:cc:dd:ee:ff
   [DS4] connected
   [DS4] {"buttons":{...},"analog":{...},"status":{...}}
   ```

See [specification.md](specification.md) for the internals.
