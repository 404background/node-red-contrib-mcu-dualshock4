/*
 * dualshock4_out - MCU runtime (ESP32)
 *
 * Sends commands to the DualShock4 of the dualshock4_config node:
 * LED color, rumble, flash, and unpair.
 *
 * Output protocol (ESP32 -> DualShock4):
 *   HID Output Report 0x11 on the interrupt channel (with CRC32).
 *   LED color, rumble intensity, and flash rate are buffered natively
 *   and sent together by sendToController().
 */

import { Node } from "nodered";

class DualShock4OutNode extends Node {
    #controller;

    onStart(config) {
        super.onStart(config);

        const controller = RED.nodes.getNode(config.controller);
        if (!controller?.attach) {
            this.status({ fill: "red", shape: "ring", text: "no controller" });
            return;
        }
        this.#controller = controller.attach(this);
    }

    onMessage(msg, done) {
        const controller = this.#controller;
        const payload = msg.payload;

        if (!controller) {
            done?.();
            return;
        }

        if (msg.unpair === true) {
            controller.unpair();
            done?.();
            return;
        }

        if (!payload || typeof payload !== "object") {
            done?.();
            return;
        }

        if (!controller.connected) {
            this.status({ fill: "red", shape: "ring", text: "not connected" });
            done?.();
            return;
        }

        let changed = false;

        if (payload.led) {
            controller.setLed(
                Number(payload.led.r) || 0,
                Number(payload.led.g) || 0,
                Number(payload.led.b) || 0
            );
            changed = true;
        }

        if (payload.rumble) {
            controller.setRumble(
                Number(payload.rumble.small) || 0,
                Number(payload.rumble.large) || 0
            );
            changed = true;
        }

        if (payload.flash) {
            controller.setFlashRate(
                Number(payload.flash.on) || 0,
                Number(payload.flash.off) || 0
            );
            changed = true;
        }

        if (changed) {
            controller.sendToController();
            this.status({ fill: "green", shape: "dot", text: "connected (sent)" });
        }
        done?.();
    }

    static type = "dualshock4_out";

    static {
        RED.nodes.registerType(this.type, this);
    }
}

export default DualShock4OutNode;
