/*
 * dualshock4_in - MCU runtime (ESP32)
 *
 * Receives DualShock4 input through the dualshock4_config node.
 * Sends a message only when the selected inputs change
 * (sticks / triggers by ANALOG_THRESHOLD or more).
 */

import { Node } from "nodered";

/* Sticks jitter by about ±1 at rest; smaller changes are not sent */
const ANALOG_THRESHOLD = 2;

class DualShock4InNode extends Node {
    #selectedInputs;
    #last;

    onStart(config) {
        super.onStart(config);

        this.#selectedInputs = config.selectedInputs || {};

        const controller = RED.nodes.getNode(config.controller);
        if (!controller?.attach) {
            this.status({ fill: "red", shape: "ring", text: "no controller" });
            return;
        }

        controller.attach(this, {
            disconnect: () => {
                this.#last = undefined;
            },
            data: (data) => {
                const payload = this.#filterInputs(data);
                if (this.#changed(this.#last, payload)) {
                    this.#last = payload;
                    this.send({ payload });
                }
            }
        });
    }

    /* Buttons and status must match exactly; analog values ignore small jitter */
    #changed(prev, cur) {
        if (!prev) return true;
        for (const group of ["buttons", "status"]) {
            if (JSON.stringify(prev[group]) !== JSON.stringify(cur[group])) return true;
        }
        const a = prev.analog ?? {}, b = cur.analog ?? {};
        for (const key in b) {
            if (!(key in a) || Math.abs(b[key] - a[key]) >= ANALOG_THRESHOLD) return true;
        }
        return false;
    }

    /* No selection saved (new node): everything. Otherwise only checked items. */
    #filterInputs(data) {
        const sel = this.#selectedInputs;
        if (Object.keys(sel).length === 0) return data;

        const result = {};
        for (const group of ["buttons", "analog"]) {
            const values = {};
            for (const key in data[group]) {
                if (sel[key]) values[key] = data[group][key];
            }
            if (Object.keys(values).length > 0) result[group] = values;
        }
        result.status = data.status;
        return result;
    }

    static type = "dualshock4_in";

    static {
        RED.nodes.registerType(this.type, this);
    }
}

export default DualShock4InNode;
