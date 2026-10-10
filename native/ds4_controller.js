/*
 * DS4Controller - Shared DualShock4 connection manager (XS in C bridge)
 *
 * Singleton pattern: all dualshock4_in / dualshock4_out nodes share
 * the same physical Bluetooth Classic connection via this class.
 *
 * Listener pattern: each node registers a listener object with
 * { connect(), disconnect(), data(payload), state(name), error(message) }
 * callbacks. state(name) receives "connected" | "starting" | "pairing" | "waiting".
 *
 * Pairing: put the DualShock4 into pairing mode (hold SHARE + PS until the
 * light bar double-flashes). The ESP32 connects to the DS4 MAC passed to
 * start(), or to the first controller found when it is empty. After that,
 * pressing PS reconnects the controller.
 */

import Timer from "timer";

let instance = null;

class DS4Controller {
    #listeners;
    #timer;
    #connected;
    #state;
    #interval;

    constructor() {
        this.#listeners = [];
        this.#timer = undefined;
        this.#connected = false;
        this.#state = undefined;
        this.#interval = 100;
    }

    static getInstance() {
        if (!instance) {
            instance = new DS4Controller();
        }
        return instance;
    }

    /* interval: polling period in ms, omitted to keep the current one */
    start(ds4Mac, interval) {
        /* _nativeInit starts Bluetooth only once */
        const first = this.localAddress === undefined;
        try {
            this._nativeInit(ds4Mac || "");
        } catch (e) {
            if (String(e?.message).includes("(0x103)")) {
                trace("DS4: Moddable frees the Bluetooth Classic memory at startup. Patch it and rebuild:\n" +
                      "  file:   $MODDABLE/build/devices/esp32/lib/main/main.c\n" +
                      "  before: #if CONFIG_BT_ENABLED\n" +
                      "  after:  #if CONFIG_BT_ENABLED && !CONFIG_BT_CLASSIC_ENABLED\n" +
                      "  (see README.md \"Moddable SDK Patch\")\n");
            }
            throw e;
        }
        if (first) {
            trace(`DS4: ESP32 Bluetooth address ${this.localAddress}\n`);
        }

        if ((interval > 0) && (interval !== this.#interval)) {
            this.#interval = interval;
            this.stop();
        }

        if (this.#timer !== undefined) return;

        this.#timer = Timer.repeat(() => {
            const change = this._nativeCheckConnectionChanged();
            if (change === 1) {
                this.#connected = true;
                trace(`DS4: connected ${this._nativeGetPeerAddress()}\n`);
                this._notify("connect");
            } else if (change === -1) {
                this.#connected = false;
                trace("DS4: disconnected\n");
                this._notify("disconnect");
            }

            const state = this._nativeGetState();
            if (state !== this.#state) {
                this.#state = state;
                this._notify("state", state);
            }

            const error = this._nativeGetLastError();
            if (error) {
                trace(`DS4: ${error}\n`);
                this._notify("error", error);
            }

            if (!this.#connected) return;

            const data = this._nativeRead();
            if (data) {
                this._notify("data", data);
            }
        }, this.#interval);
    }

    stop() {
        if (this.#timer !== undefined) {
            Timer.clear(this.#timer);
            this.#timer = undefined;
        }
    }

    unpair() {
        this._nativeUnpair();
    }

    get connected() {
        return this.#connected;
    }

    get state() {
        return this.#state;
    }

    get running() {
        return this.#timer !== undefined;
    }

    get localAddress() {
        return this._nativeGetLocalAddress();
    }

    get peerAddress() {
        return this._nativeGetPeerAddress();
    }

    /* --- Listener management --- */

    addListener(listener) {
        this.#listeners.push(listener);

        /* Notify immediately if already connected */
        if (this.#connected && listener.connect) {
            listener.connect();
        } else if (this.#state && listener.state) {
            listener.state(this.#state);
        }
    }

    removeListener(listener) {
        const idx = this.#listeners.indexOf(listener);
        if (idx >= 0) this.#listeners.splice(idx, 1);

        if (this.#listeners.length === 0) {
            this.stop();
        }
    }

    _notify(event, data) {
        /* Copy array to avoid issues if a callback modifies listeners */
        const snapshot = this.#listeners.slice();
        for (const listener of snapshot) {
            if (listener[event]) {
                listener[event](data);
            }
        }
    }

    /* --- Output control --- */

    setLed(r, g, b) {
        this._nativeSetLed(r, g, b);
    }

    setRumble(small, large) {
        this._nativeSetRumble(small, large);
    }

    setFlashRate(onTime, offTime) {
        this._nativeSetFlashRate(onTime, offTime);
    }

    sendToController() {
        this._nativeSendToController();
    }

    /* --- Native methods (XS in C) --- */

    _nativeInit(ds4Mac) @ "xs_ds4_init"
    _nativeCheckConnectionChanged() @ "xs_ds4_check_connection_changed"
    _nativeGetState() @ "xs_ds4_get_state"
    _nativeGetLastError() @ "xs_ds4_get_last_error"
    _nativeGetLocalAddress() @ "xs_ds4_get_local_address"
    _nativeGetPeerAddress() @ "xs_ds4_get_peer_address"
    _nativeUnpair() @ "xs_ds4_unpair"
    _nativeRead() @ "xs_ds4_read"
    _nativeSetLed(r, g, b) @ "xs_ds4_set_led"
    _nativeSetRumble(small, large) @ "xs_ds4_set_rumble"
    _nativeSetFlashRate(onTime, offTime) @ "xs_ds4_set_flash_rate"
    _nativeSendToController() @ "xs_ds4_send_to_controller"
}

export default DS4Controller;
