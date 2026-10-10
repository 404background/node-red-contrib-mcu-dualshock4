/*
 * dualshock4_config - MCU runtime (ESP32)
 *
 * Configuration node: owns the Bluetooth connection to one DualShock4
 * (DS4 MAC, polling interval). dualshock4 in / out nodes attach to it.
 * DS4 MAC is required; without it Bluetooth is not started.
 */

import { Node } from "nodered";
import DS4Controller from "ds4_controller";

const MAC_PATTERN = /^([0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}$/;

class DualShock4ConfigNode extends Node {
    #controller;
    #error;

    onStart(config) {
        super.onStart(config);

        const interval = Number(config.interval) > 0 ? Number(config.interval) : 100;
        const ds4mac = config.ds4mac || "";
        if (!MAC_PATTERN.test(ds4mac)) {
            this.#error = ds4mac ? "invalid DS4 MAC" : "DS4 MAC not set";
            this.error(`${this.#error}: set the DualShock4 Bluetooth address in the dualshock4 config node`);
            return;
        }

        this.#controller = DS4Controller.getInstance();
        try {
            this.#controller.start(ds4mac, interval);
            this.#controller.addListener({
                error: (message) => this.warn(`DS4: ${message}`)
            });
        } catch (e) {
            this.#error = e?.message ?? String(e);
            this.error(this.#error);
        }
    }

    /*
     * Registers node for connection status updates plus optional handlers
     * { connect, disconnect, data }. Returns the controller, or undefined
     * when Bluetooth failed to start.
     */
    attach(node, handlers = {}) {
        if (this.#error) {
            node.status({ fill: "red", shape: "ring", text: this.#error });
            return;
        }

        this.#controller.addListener({
            connect: () => {
                node.status({ fill: "green", shape: "dot", text: "connected" });
                handlers.connect?.();
            },
            disconnect: () => {
                node.status({ fill: "red", shape: "ring", text: "disconnected" });
                handlers.disconnect?.();
            },
            state: (state) => {
                if (state === "starting") {
                    node.status({ fill: "grey", shape: "ring", text: "starting" });
                } else if (state === "pairing") {
                    node.status({ fill: "yellow", shape: "dot", text: "pairing" });
                } else if (state === "waiting") {
                    node.status({ fill: "yellow", shape: "ring", text: "waiting (press PS)" });
                }
            },
            data: handlers.data
        });
        return this.#controller;
    }

    static type = "dualshock4_config";

    static {
        RED.nodes.registerType(this.type, this);
    }
}

export default DualShock4ConfigNode;
