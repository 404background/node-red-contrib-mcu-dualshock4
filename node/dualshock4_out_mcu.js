/*
 * dualshock4_out - MCU runtime (ESP32)
 *
 * Controls DualShock4 LED, rumble, and flash via shared DS4Controller instance.
 *
 * Output protocol (ESP32 -> DualShock4):
 *   HID Output Report (identifier 0x11) via L2CAP control channel
 *   LED color, rumble intensity, and flash rate are buffered in outputCmd
 *   and sent together via ps4SetOutput() -> ps4Cmd() -> ps4_l2cap_send_hid()
 */

import { Node } from "nodered";
import DS4Controller from "ds4_controller";

class DualShock4OutNode extends Node {
    #controller;
    #listener;

    onStart(config) {
        super.onStart(config);

        this.#controller = DS4Controller.getInstance();

        this.#listener = {
            connect: () => {
                this.status({ fill: "green", shape: "dot", text: "connected" });
            },
            disconnect: () => {
                this.status({ fill: "red", shape: "ring", text: "disconnected" });
            }
        };

        if (!this.#controller.connected) {
            this.status({ fill: "yellow", shape: "ring", text: "waiting" });
        }

        this.#controller.addListener(this.#listener);

        try {
            this.#controller.start(config.mac || "", config.ds4mac || "");
        } catch (e) {
            this.status({ fill: "red", shape: "ring", text: "error" });
            this.error(e?.message ?? e);
        }
    }

    onMessage(msg, done) {
        const payload = msg.payload;
        if (!payload || typeof payload !== "object") {
            done?.();
            return;
        }

        if (!this.#controller.connected) {
            this.status({ fill: "red", shape: "ring", text: "not connected" });
            done?.();
            return;
        }

        let changed = false;

        if (payload.led) {
            this.#controller.setLed(
                Number(payload.led.r) || 0,
                Number(payload.led.g) || 0,
                Number(payload.led.b) || 0
            );
            changed = true;
        }

        if (payload.rumble) {
            this.#controller.setRumble(
                Number(payload.rumble.small) || 0,
                Number(payload.rumble.large) || 0
            );
            changed = true;
        }

        if (payload.flash) {
            this.#controller.setFlashRate(
                Number(payload.flash.on) || 0,
                Number(payload.flash.off) || 0
            );
            changed = true;
        }

        if (changed) {
            this.#controller.sendToController();
            this.status({ fill: "green", shape: "dot", text: "sent" });
        }
        done?.();
    }

    static type = "dualshock4_out";

    static {
        RED.nodes.registerType(this.type, this);
    }
}

export default DualShock4OutNode;
