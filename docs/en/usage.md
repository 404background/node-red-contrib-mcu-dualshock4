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

### Moddable SDK Patch (Required)

The Moddable SDK supports only BLE, so when Bluetooth is enabled it releases the Bluetooth Classic memory at startup (`build/devices/esp32/lib/main/main.c`; added in 2018 by commit `7ed07750e` "free bluetooth classic reserve memory at startup - we don't support classic", and unchanged in 5.12.0, 7.2.1 and the latest 9.5.0).
The DualShock4 uses Bluetooth Classic, so without a patch the dualshock4 config node reports `BT init failed (0x103): patch Moddable main.c` and Bluetooth does not start.

Change the following lines in `main.c` (apps that only use BLE behave the same as before).

```c
// before
#if CONFIG_BT_ENABLED
// after
#if CONFIG_BT_ENABLED && !CONFIG_BT_CLASSIC_ENABLED
```

```sh
sed -i 's/^#if CONFIG_BT_ENABLED$/#if CONFIG_BT_ENABLED \&\& !CONFIG_BT_CLASSIC_ENABLED/' $MODDABLE/build/devices/esp32/lib/main/main.c
```

### Tested Versions

| Software | Version |
|----------|---------|
| Moddable SDK | 5.12.0 |
| ESP-IDF | v5.5.1 |
| node-red-mcu | e91b670 |

Pairing, reconnection, input, LED and rumble were checked on real hardware (ESP32-D0WDQ6 and a DualShock4) with the Moddable SDK patch applied.

### Build Troubleshooting

This node changes the sdkconfig to enable Bluetooth, so the first build runs `Reconfiguring ESP-IDF...` (CMake reconfiguration).
If it stops with an error like the following, a build folder from before updating ESP-IDF or Moddable is likely left over.

```
The C compiler ... is not able to compile a simple test program.
ninja: error: loading 'build.ninja': No such file or directory
```

Delete the build folder shown in the error and build again (for Node-RED MCU, also delete the plugin cache).

```sh
rm -rf $MODDABLE/build/tmp/esp32/debug/<name>
rm -rf ~/.node-red/mcu-plugin-cache/<name>
```

## Nodes

| Node | Role |
|------|------|
| **dualshock4 config** | Configuration node. Sets the connection to the DualShock4 (DS4 MAC, interval), shared by the in / out nodes |
| **dualshock4 in** | Receives controller input |
| **dualshock4 out** | Sends LED / rumble commands to the controller |

Only one DualShock4 connection is supported. Use a single configuration node (config nodes with a different DS4 MAC or interval report `use only one dualshock4 config`).

### dualshock4 config

**Settings:**
- **DS4 MAC** - Bluetooth MAC address of the DualShock4 to pair / connect (**required**; without it an error is reported and nothing connects)
- **Interval** - Polling interval of the controller state (ms, default 100)

### dualshock4 in

Receives controller input. A message is sent only when a selected input changes (sticks and triggers by 2 or more).

**Output data (`msg.payload`):**

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
- **DS4** - The dualshock4 config node to use
- **Inputs** - Buttons / sticks to include in the output (all selected by default). `status` is always included

### dualshock4 out

Sends LED / rumble commands to the controller.

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

// Delete pairing information and wait for pairing again
msg.unpair = true;
```

Unspecified items keep their previous values. Rumble and flash stop with `0`.

**Settings:**
- **DS4** - The dualshock4 config node to use

### Node Status

The in / out nodes show the connection state.

| Status | Meaning |
|------|------|
| starting | Starting Bluetooth |
| pairing | Searching for / connecting to a DualShock4 in pairing mode |
| waiting (press PS) | Paired. Waiting for the PS button on the DualShock4 |
| connected | Connected |

If pairing or connecting fails, the reason is shown as a warning in the debug sidebar.

## Pairing

### First Time

1. Write the flow to the ESP32 and start it (the node status becomes `pairing`)
2. Hold **SHARE + PS** on the DualShock4 until the light bar double-flashes (pairing mode)
3. The ESP32 connects to the DualShock4 and pairs automatically (the node status becomes `connected`)

Set the MAC address of the DualShock4 to pair in DS4 MAC of the dualshock4 config node beforehand.  
To use a specific DualShock4 only, or when several controllers are nearby, set DS4 MAC.

### Checking the DualShock4 MAC Address

- **[SixaxisPairTool](https://sixaxispairtool.en.lo4d.com/windows) (Windows GUI, recommended)**  
  Connect the DualShock4 to a PC with a USB cable to see its own MAC address (Device Address) and the currently stored pairing target (Current Master).  
  Set **Device Address** as DS4 MAC. After pairing with the ESP32, Current Master shows the ESP32's own address (setting it is reported as an error)
- **Bluetooth settings on a PC / smartphone**  
  Shows the address of the DualShock4 ("Wireless Controller") in pairing mode
- **Moddable SDK test program** ([below](#testing-with-the-moddable-sdk-only))  
  Run without `ds4mac`: it connects to the first DualShock4 found and prints its address, e.g. `DS4: connected aa:bb:cc:dd:ee:ff`

> [sixaxispairer](https://github.com/user-none/sixaxispairer) (CLI) is a tool for PS3 controllers (SixAxis / Move) and does not support the DualShock4.

### After That

Pairing information is stored on both the ESP32 (NVS) and the DualShock4.  
Even after the ESP32 restarts, **just press the PS button** to connect.

### Re-pairing

- With DS4 MAC set: if that DualShock4 is not paired yet, the ESP32 waits for pairing automatically. To switch to another DualShock4, change DS4 MAC
- Even after the DualShock4 was paired with a PS4 console, PC or another ESP32, hold **SHARE + PS** to put it in pairing mode: the ESP32 detects it and pairs again automatically (`msg.unpair` is not needed)

### Using several ESP32s

The DualShock4 remembers only one host. Pairing with a new host overwrites the previous one, so it cannot be registered with several ESP32s at the same time.  
To switch, power on the ESP32 you want to use and hold **SHARE + PS** on the DualShock4. It pairs with that ESP32 again, and afterwards the PS button is enough to connect.

- A DualShock4 in pairing mode pairs with whichever powered-on ESP32 (with the same DS4 MAC) finds it first. Power off the other ESP32s when switching

### "rejected xx:xx:xx:xx:xx:xx: not the DS4 MAC"

A device whose address differs from DS4 MAC tried to connect, so it was rejected.

- If the shown address is the DualShock4 you want to use, set DS4 MAC in the dualshock4 config node to that address
- If the ESP32 still has pairing information of another controller, send `msg.unpair = true` to the dualshock4 out node to delete it, then pair again
- If the DualShock4 keeps connecting to another device such as a PC or PS4, remove the DualShock4 from that device's Bluetooth settings

## Sample Flow

Import it from the Node-RED menu "Import → Examples → @background404/node-red-contrib-mcu-dualshock4 → dualshock4" (`examples/dualshock4.json`).

| Contents | Nodes |
|------|--------|
| Show all input in the debug sidebar | dualshock4 in → debug |
| LED color by face button, rumble by L2/R2 trigger | dualshock4 in → function → dualshock4 out |
| LED red, flash, stop, unpair | inject → dualshock4 out |

After importing, build it for the ESP32 as an MCU flow.

## Testing with the Moddable SDK Only

A sample project is included to test that the C extension (Bluetooth implementation) works with the Moddable SDK alone, without Node-RED.

1. **Build and flash**:
   ```sh
   cd test/moddable

   # Pair with the first DS4 found
   mcconfig -d -m -p esp32

   # Pair with a specific DS4 only
   mcconfig -d -m -p esp32 ds4mac="aa:bb:cc:dd:ee:ff"
   ```
2. **Check**:
   xsbug prints the following.
   ```
   [DS4] pairing
   ```
   Hold **SHARE + PS** on the DualShock4 to enter pairing mode. Once connected, the input state is printed in real time.
   ```
   DS4: connected aa:bb:cc:dd:ee:ff
   [DS4] connected
   [DS4] {"buttons":{...},"analog":{...},"status":{...}}
   ```

See [specification.md](specification.md) for the internals.
