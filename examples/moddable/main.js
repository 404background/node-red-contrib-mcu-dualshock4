/*
 * DualShock4 debug example (Moddable SDK only, no Node-RED MCU)
 *
 * Reads DualShock4 controller input via the shared DS4Controller
 * singleton and prints every state change to the trace (xsbug) output.
 *
 * Build (ESP32):
 *   mcconfig -d -m -p esp32/<subplatform>
 *
 * If you need to hardcode the host/controller Bluetooth MAC addresses,
 * edit PAIRING_MAC / DS4_MAC below. Empty strings let the native side
 * use its defaults.
 *
 * Checking / changing the MAC address stored in the DualShock4:
 *   Connect the DualShock4 to a PC via USB and use one of:
 *     - sixaxispairer (CLI):      https://github.com/user-none/sixaxispairer
 *     - SixaxisPairTool (Win GUI): https://sixaxispairtool.en.lo4d.com/windows
 */

import DS4Controller from "ds4_controller";
import Net from "net";
import config from "mc/config";

// ESP32のMACアドレス（Wi-Fi Station MAC）を取得し、Bluetooth用(+2)に計算
// 厳密なMACブロック繰り上がりは考慮していませんが、ほとんどのESP32で機能します
let macParts = Net.get("MAC").split(":");
let lastByte = (parseInt(macParts[5], 16) + 2) & 0xFF;
macParts[5] = lastByte.toString(16).padStart(2, "0");
const PAIRING_MAC = macParts.join(":");

// mcconfigのオプションでDS4のMACアドレスを渡せるようにする
// 例: mcconfig -d -m -p esp32 ds4mac="aa:bb:cc:dd:ee:ff"
const DS4_MAC = config.ds4mac || ""; 
const POLL_MS = 100;

trace(`====================================================\n`);
trace(`DS4 debug example starting\n`);
trace(`ESP32 Bluetooth MAC Address: ${PAIRING_MAC}\n`);
trace(`DS4 Filter MAC Address: ${DS4_MAC ? DS4_MAC : "(Any)"}\n`);
trace(`====================================================\n`);
trace(`Please configure your DualShock4 to connect to: ${PAIRING_MAC}\n`);
trace(`Using sixaxispairer: sixaxispairer ${PAIRING_MAC}\n`);
trace(`====================================================\n\n`);

const controller = DS4Controller.getInstance();

controller.addListener({
    connect() {
        trace("[DS4] connected\n");
    },
    disconnect() {
        trace("[DS4] disconnected\n");
    },
    data(payload) {
        trace(`[DS4] ${JSON.stringify(payload)}\n`);
    }
});

controller.start(PAIRING_MAC, DS4_MAC, POLL_MS);
