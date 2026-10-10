/*
 * DualShock4 debug example (Moddable SDK only, no Node-RED MCU)
 *
 * Reads DualShock4 controller input via the shared DS4Controller
 * singleton and prints every state change to the trace output.
 * Face buttons change the light bar color, L2 / R2 drive the rumble.
 *
 * Build (ESP32):
 *   mcconfig -d -m -p esp32
 *   mcconfig -d -m -p esp32 ds4mac="aa:bb:cc:dd:ee:ff"
 *   mcconfig -m -p esp32        (release: trace goes to the serial port)
 *
 * Pairing (first time only):
 *   Hold SHARE + PS on the DualShock4 until the light bar double-flashes.
 *   The ESP32 pairs with the controller given by ds4mac, or with the first
 *   controller found when ds4mac is empty. Afterwards press PS to connect.
 *
 * Checking the DualShock4's own MAC address (for ds4mac):
 *   Connect the DualShock4 to a PC via USB and read "Device Address" in
 *   SixaxisPairTool (Windows): https://sixaxispairtool.en.lo4d.com/windows
 *   (sixaxispairer by user-none supports PS3 controllers only.)
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
let last;
let lastOutput;

/* Sticks jitter by about ±1 at rest; ignore smaller analog changes */
function changed(prev, cur) {
    if (!prev) return true;
    if (JSON.stringify(prev.buttons) !== JSON.stringify(cur.buttons)) return true;
    if (JSON.stringify(prev.status) !== JSON.stringify(cur.status)) return true;
    for (const key in cur.analog) {
        if (Math.abs(cur.analog[key] - prev.analog[key]) >= 2) return true;
    }
    return false;
}

controller.addListener({
    connect() {
        trace("[DS4] connected\n");
    },
    disconnect() {
        trace("[DS4] disconnected\n");
        last = lastOutput = undefined;
    },
    state(state) {
        trace(`[DS4] ${state}\n`);
    },
    error(message) {
        trace(`[DS4] error: ${message}\n`);
    },
    data(payload) {
        if (!changed(last, payload)) return;
        last = payload;
        trace(`[DS4] ${JSON.stringify(payload)}\n`);

        const b = payload.buttons;
        let led = [0, 0, 64];
        if (b.cross) led = [0, 0, 255];
        else if (b.circle) led = [255, 0, 0];
        else if (b.triangle) led = [0, 255, 0];
        else if (b.square) led = [255, 0, 255];
        const rumble = [payload.analog.l2_value, payload.analog.r2_value];

        const output = JSON.stringify([led, rumble]);
        if (output === lastOutput) return;
        lastOutput = output;
        controller.setLed(...led);
        controller.setRumble(...rumble);
        controller.sendToController();
    }
});

controller.start(DS4_MAC, POLL_MS);
