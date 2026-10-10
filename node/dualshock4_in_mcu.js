/*
 * dualshock4_in - MCU runtime (ESP32)
 *
 * Reads DualShock4 controller input via shared DS4Controller instance.
 * Sends a message only when the selected inputs change.
 * msg.unpair === true forgets paired controllers.
 */

import { Node } from "nodered";
import DS4Controller from "ds4_controller";

class DualShock4InNode extends Node {
    #controller;
    #selectedInputs;
    #last;

    onStart(config) {
        super.onStart(config);

        const interval = Number(config.interval) > 0 ? Number(config.interval) : 100;
        this.#selectedInputs = config.selectedInputs || {};
        this.#controller = DS4Controller.getInstance();

        this.#controller.addListener({
            connect: () => {
                this.status({ fill: "green", shape: "dot", text: "connected" });
            },
            disconnect: () => {
                this.#last = undefined;
                this.status({ fill: "red", shape: "ring", text: "disconnected" });
            },
            state: (state) => {
                if (state === "pairing") {
                    this.status({ fill: "yellow", shape: "dot", text: "pairing" });
                } else if (state === "waiting") {
                    this.status({ fill: "yellow", shape: "ring", text: "waiting (press PS)" });
                }
            },
            data: (data) => {
                const payload = this.#filterInputs(data);
                const json = JSON.stringify(payload);
                if (json !== this.#last) {
                    this.#last = json;
                    this.send({ payload });
                }
            }
        });

        try {
            this.#controller.start(config.ds4mac || "", interval);
        } catch (e) {
            this.status({ fill: "red", shape: "ring", text: "error" });
            this.error(e?.message ?? e);
        }
    }

    onMessage(msg, done) {
        if (msg.unpair === true) {
            this.#controller.unpair();
        }
        done?.();
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
