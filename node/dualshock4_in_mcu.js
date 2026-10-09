/*
 * dualshock4_in - MCU runtime (ESP32)
 *
 * Reads DualShock4 controller input via shared DS4Controller instance.
 * Filters output based on selectedInputs configuration.
 *
 * Starts listening when the flow starts. msg.kill === true stops it,
 * any other message starts it again.
 */

import { Node } from "nodered";
import DS4Controller from "ds4_controller";

class DualShock4InNode extends Node {
    #controller;
    #listener;
    #mac;
    #ds4mac;
    #interval;
    #selectedInputs;

    onStart(config) {
        super.onStart(config);

        this.#mac = config.mac || "";
        this.#ds4mac = config.ds4mac || "";
        this.#interval = Number(config.interval) > 0 ? Number(config.interval) : 100;
        this.#selectedInputs = config.selectedInputs || {};

        this.#start();
    }

    onMessage(msg, done) {
        if (msg.kill === true) {
            this.#stop();
        } else if (!this.#listener) {
            this.#start();
        }
        done?.();
    }

    #start() {
        this.#controller = DS4Controller.getInstance();

        this.#listener = {
            connect: () => {
                this.status({ fill: "green", shape: "dot", text: "connected" });
            },
            disconnect: () => {
                this.status({ fill: "red", shape: "ring", text: "disconnected" });
            },
            data: (data) => {
                const filtered = this.#filterInputs(data);
                if (Object.keys(filtered).length > 0) {
                    this.send({ payload: filtered });
                }
            }
        };

        if (!this.#controller.connected) {
            this.status({ fill: "yellow", shape: "ring", text: "waiting" });
        }

        this.#controller.addListener(this.#listener);

        try {
            this.#controller.start(this.#mac, this.#ds4mac, this.#interval);
        } catch (e) {
            this.status({ fill: "red", shape: "ring", text: "error" });
            this.error(e?.message ?? e);
        }
    }

    #stop() {
        if (this.#listener && this.#controller) {
            this.#controller.removeListener(this.#listener);
            this.#listener = null;
        }
        this.status({ fill: "red", shape: "ring", text: "stopped" });
    }

    #filterInputs(data) {
        const sel = this.#selectedInputs;
        const hasSelection = Object.values(sel).some(v => v);

        if (!hasSelection) return data;

        const result = {};

        if (data.buttons) {
            const buttons = {};
            for (const key in data.buttons) {
                if (sel[key]) buttons[key] = data.buttons[key];
            }
            if (Object.keys(buttons).length > 0) result.buttons = buttons;
        }

        if (data.analog) {
            const analog = {};
            for (const key in data.analog) {
                if (sel[key]) analog[key] = data.analog[key];
            }
            if (Object.keys(analog).length > 0) result.analog = analog;
        }

        if (data.status) {
            result.status = data.status;
        }

        return result;
    }

    static type = "dualshock4_in";

    static {
        RED.nodes.registerType(this.type, this);
    }
}

export default DualShock4InNode;
