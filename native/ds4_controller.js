/*
 * DS4Controller - Shared DualShock4 connection manager (XS in C bridge)
 *
 * Singleton pattern: all dualshock4_in / dualshock4_out nodes share
 * the same physical Bluetooth Classic connection via this class.
 *
 * Listener pattern: each node registers a listener object with
 * { connect(), disconnect(), data(payload) } callbacks.
 *
 * pairingMac passed to start() must match the MAC address currently
 * stored inside the DualShock4. Read/write that value by connecting the
 * controller via USB and using:
 *   - sixaxispairer (CLI):       https://github.com/user-none/sixaxispairer
 *   - SixaxisPairTool (Windows): https://sixaxispairtool.en.lo4d.com/windows
 */

import Timer from "timer";

let instance = null;

class DS4Controller {
    #listeners;
    #timer;
    #connected;
    #interval;

    constructor() {
        this.#listeners = [];
        this.#timer = undefined;
        this.#connected = false;
        this.#interval = 100;
    }

    static getInstance() {
        if (!instance) {
            instance = new DS4Controller();
        }
        return instance;
    }

    start(pairingMac, ds4Mac, interval = 100) {
        /* _nativeInit guards against double ps4Init() internally */
        this._nativeInit(pairingMac || "", ds4Mac || "");
        this._nativeSetCallbacks();

        if (this.#timer !== undefined) return;

        this.#interval = interval;

        this.#timer = Timer.repeat(() => {
            const change = this._nativeCheckConnectionChanged();
            if (change === 1) {
                this.#connected = true;
                this._notify("connect");
            } else if (change === -1) {
                this.#connected = false;
                this._notify("disconnect");
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

    get connected() {
        return this.#connected;
    }

    get running() {
        return this.#timer !== undefined;
    }

    /* --- Listener management --- */

    addListener(listener) {
        this.#listeners.push(listener);

        /* Notify immediately if already connected */
        if (this.#connected && listener.connect) {
            listener.connect();
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

    _nativeInit(pairingMac, ds4Mac) @ "xs_ds4_init"
    _nativeSetCallbacks() @ "xs_ds4_set_callbacks"
    _nativeCheckConnectionChanged() @ "xs_ds4_check_connection_changed"
    _nativeRead() @ "xs_ds4_read"
    _nativeSetLed(r, g, b) @ "xs_ds4_set_led"
    _nativeSetRumble(small, large) @ "xs_ds4_set_rumble"
    _nativeSetFlashRate(onTime, offTime) @ "xs_ds4_set_flash_rate"
    _nativeSendToController() @ "xs_ds4_send_to_controller"
}

export default DS4Controller;
