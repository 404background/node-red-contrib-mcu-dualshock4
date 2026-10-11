/*
 * dualshock4_config - MCU runtime (ESP32)
 *
 * Configuration node: owns the Bluetooth connection to one DualShock4
 * (DS4 MAC, polling interval). dualshock4 in / out nodes attach to it.
 * DS4 MAC is optional: when empty, the first DualShock4 in pairing mode is
 * paired and its address is reported, so it can be entered afterwards.
 */

import { Node } from "nodered";
import DS4Controller from "ds4_controller";

const MAC_PATTERN = /^([0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}$/;

/* Only one DualShock4 connection exists: the first config node owns it */
let owner;

class DualShock4ConfigNode extends Node {
    #controller;
    #error;

    onStart(config) {
        super.onStart(config);

        const interval = Number(config.interval) > 0 ? Number(config.interval) : 100;
        const ds4mac = (config.ds4mac || "").toLowerCase();

        if (owner) {
            /* Another config node already started the connection: share it */
            if ((owner.ds4mac !== ds4mac) || (owner.interval !== interval)) {
                this.#error = "use only one dualshock4 config";
                this.error(this.#error);
            } else {
                this.#controller = owner.controller;
                this.#error = owner.error;
            }
            return;
        }

        this.#error = this.#start(ds4mac, interval);
        if (this.#error)
            this.error(this.#error);
        owner = { ds4mac, interval, controller: this.#controller, error: this.#error };
    }

    /* Returns an error message, or undefined when started */
    #start(ds4mac, interval) {
        if (ds4mac && !MAC_PATTERN.test(ds4mac))
            return "invalid DS4 MAC";

        const controller = DS4Controller.getInstance();
        const error = controller.start(ds4mac, interval);
        if (error)
            return error;

        /* SixaxisPairTool shows the ESP32 as "Current Master" after pairing */
        if (ds4mac && (ds4mac === controller.localAddress)) {
            controller.stop();
            return "DS4 MAC is the ESP32 (use Device Address)";
        }

        this.#controller = controller;
        let reported;
        controller.addListener({
            error: (message) => this.warn(`DS4: ${message}`),
            connect: () => {
                /* Without DS4 MAC, tell which controller was paired (once per address) */
                const address = controller.peerAddress;
                if (ds4mac || !address || (address === reported))
                    return;
                reported = address;
                this.warn(`DS4: connected ${address}. Set it as DS4 MAC to use only this controller`);
            }
        });
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
