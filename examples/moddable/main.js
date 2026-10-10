/*
 * DualShock4 debug example (Moddable SDK only, no Node-RED MCU)
 *
 * Reads DualShock4 controller input via the shared DS4Controller
 * singleton and prints every state change to the trace (xsbug) output.
 *
 * Build (ESP32):
 *   mcconfig -d -m -p esp32
 *   mcconfig -d -m -p esp32 ds4mac="aa:bb:cc:dd:ee:ff"
 *
 * Pairing (first time only):
 *   Hold SHARE + PS on the DualShock4 until the light bar double-flashes.
 *   The ESP32 pairs with the controller given by ds4mac, or with the first
 *   controller found when ds4mac is empty. Afterwards press PS to connect.
 */

import DS4Controller from "ds4_controller";
import config from "mc/config";

const DS4_MAC = config.ds4mac || "";
const POLL_MS = 100;

trace(`====================================================\n`);
trace(`DS4 debug example starting\n`);
trace(`DS4 MAC Address: ${DS4_MAC ? DS4_MAC : "(Any)"}\n`);
trace(`Hold SHARE + PS on the DualShock4 to pair\n`);
trace(`====================================================\n\n`);

const controller = DS4Controller.getInstance();

controller.addListener({
    connect() {
        trace("[DS4] connected\n");
    },
    disconnect() {
        trace("[DS4] disconnected\n");
    },
    state(state) {
        trace(`[DS4] ${state}\n`);
    },
    data(payload) {
        trace(`[DS4] ${JSON.stringify(payload)}\n`);
    }
});

controller.start(DS4_MAC, POLL_MS);
