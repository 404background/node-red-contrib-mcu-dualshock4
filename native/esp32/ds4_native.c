/*
 * ds4_native.c - XS in C bridge for PS4-esp32 library
 *
 * DualShock4 communication protocol (Bluetooth Classic HID over L2CAP):
 *
 * [Connection]
 *   1. sppInit() initializes Bluetooth Classic + SPP server
 *   2. ps4_l2cap_init_services() registers L2CAP PSMs:
 *      - HID Control  (PSM 0x0011)
 *      - HID Interrupt (PSM 0x0013)
 *   3. DualShock4 initiates L2CAP connection
 *   4. After config confirmation on interrupt channel -> ps4Enable()
 *      sends HID Feature Report {0x43, 0x02} (identifier 0xF4)
 *   5. First data packet received -> connection event fires
 *
 * [Input - DualShock4 -> ESP32]
 *   HID Input Report (ps4_parser.c):
 *     Byte 13-16: Analog sticks LX/LY/RX/RY (uint8, center=128 -> int8)
 *     Byte 17:    D-pad (lower 4bit direction) + face buttons (upper 4bit)
 *     Byte 18:    L1/R1/L2/R2 (lower) + Share/Options/L3/R3 (upper)
 *     Byte 19:    PS (bit0) + Touchpad (bit1)
 *     Byte 20-21: L2/R2 analog trigger values (0-255)
 *     Byte 42:    Status (battery/charging/audio/mic)
 *
 * [Output - ESP32 -> DualShock4]
 *   HID Output Report (identifier 0x11) via L2CAP control channel:
 *     Header:     {0x80, 0x00, 0xFF}
 *     Byte 5-6:   Small/Large rumble motor (0-255)
 *     Byte 7-9:   LED color R/G/B (0-255)
 *     Byte 10-11: Flash on/off time (x10ms, 255 = 2.5s)
 */

#include "xsmc.h"
#include "xsHost.h"
#include "mc.xs.h"

#include "ps4.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    uint8_t initialized;
    uint8_t prevConnected;
    ps4_t latestData;
    ps4_event_t latestEvent;
    ps4_cmd_t outputCmd;
} ds4_native_t;

static ds4_native_t *gDS4 = NULL;

static uint8_t clampByte(xsIntegerValue value) {
    if (value < 0) return 0;
    if (value > 255) return 255;
    return (uint8_t)value;
}

/********************************************************************************/
/*                        C A L L B A C K S                                     */
/********************************************************************************/

static void ds4_event_callback(void *object, ps4_t data, ps4_event_t event) {
    ds4_native_t *ds4 = (ds4_native_t *)object;
    if (!ds4) return;

    c_memcpy(&ds4->latestData, &data, sizeof(ps4_t));
    c_memcpy(&ds4->latestEvent, &event, sizeof(ps4_event_t));
}


/********************************************************************************/
/*                  X S   I N   C   F U N C T I O N S                           */
/********************************************************************************/

/*
 * xs_ds4_init(pairingMac: string, ds4Mac: string): void
 *
 * Initializes PS4 controller connection.
 * pairingMac: MAC address to set on ESP32 (so DS4 recognizes it as pairing target)
 * ds4Mac: DualShock4's own MAC address (to filter incoming connections)
 * Guards against double initialization - ps4Init() is only called once.
 * Bluetooth Classic init is handled by ps4_spp.c:sppInit().
 */
void xs_ds4_init(xsMachine *the) {
    /* Allocate singleton state if needed */
    if (!gDS4) {
        gDS4 = (ds4_native_t *)c_calloc(1, sizeof(ds4_native_t));
        if (!gDS4) {
            xsUnknownError("out of memory");
            return;
        }
    }

    /* Process pairingMac (xsArg(0)) first, before any other xsmcToString call.
     * xsmcToString returns a pointer into the XS heap that can be invalidated
     * by GC triggered by subsequent XS API calls. */
    {
        const char *pairingMac = xsmcToString(xsArg(0));
        if (pairingMac && c_strlen(pairingMac) > 0) {
            uint8_t addr[6];
            if (sscanf(pairingMac, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
                       &addr[0], &addr[1], &addr[2],
                       &addr[3], &addr[4], &addr[5]) == 6) {
                ps4SetBluetoothMacAddress(addr);
            }
        }
    }

    /* Process ds4Mac (xsArg(1)) separately */
    {
        const char *ds4Mac = xsmcToString(xsArg(1));
        if (ds4Mac && c_strlen(ds4Mac) > 0) {
            uint8_t addr[6];
            if (sscanf(ds4Mac, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
                       &addr[0], &addr[1], &addr[2],
                       &addr[3], &addr[4], &addr[5]) == 6) {
                ps4SetFilterAddress(addr);
            }
        }
    }

    /* Initialize PS4-esp32 only once */
    if (!gDS4->initialized) {
        gDS4->initialized = 1;
        gDS4->prevConnected = 0;
        c_memset(&gDS4->outputCmd, 0, sizeof(ps4_cmd_t));
        /* Same LED color that ps4Enable() sets on connect */
        gDS4->outputCmd.r = 32;
        gDS4->outputCmd.g = 32;
        gDS4->outputCmd.b = 200;

        /* ps4Init() -> sppInit() + ps4_l2cap_init_services()
         * sppInit() initializes Bluetooth Classic stack (non-Arduino path)
         * Must only be called once */
        ps4Init();
    }
}

/*
 * xs_ds4_set_callbacks(): void
 */
void xs_ds4_set_callbacks(xsMachine *the) {
    if (!gDS4) return;

    ps4SetEventObjectCallback(gDS4, ds4_event_callback);
}

/*
 * xs_ds4_is_connected(): boolean
 */
void xs_ds4_is_connected(xsMachine *the) {
    xsmcSetBoolean(xsResult, ps4IsConnected());
}

/*
 * xs_ds4_check_connection_changed(): integer
 * Returns: 1 = newly connected, -1 = newly disconnected, 0 = no change
 *
 * ps4IsConnected() is used as the source of truth: PS4-esp32 only invokes
 * the connection callback on connect, never on disconnect.
 */
void xs_ds4_check_connection_changed(xsMachine *the) {
    if (!gDS4) {
        xsmcSetInteger(xsResult, 0);
        return;
    }

    uint8_t connected = ps4IsConnected() ? 1 : 0;
    int change = 0;
    if (connected && !gDS4->prevConnected) {
        change = 1;
    } else if (!connected && gDS4->prevConnected) {
        change = -1;
    }
    gDS4->prevConnected = connected;
    xsmcSetInteger(xsResult, change);
}

/*
 * xs_ds4_read(): object | null
 *
 * Returns latest controller state:
 * { buttons: {...}, analog: {...}, status: {...} }
 */
void xs_ds4_read(xsMachine *the) {
    if (!gDS4 || !ps4IsConnected()) {
        xsmcSetNull(xsResult);
        return;
    }

    ps4_t *data = &gDS4->latestData;

    /* xsVar(0) = temp value, xsVar(1) = sub-object */
    xsmcVars(2);

    xsmcSetNewObject(xsResult);

    /* --- buttons --- */
    xsmcSetNewObject(xsVar(1));

    xsmcSetBoolean(xsVar(0), data->button.cross);
    xsmcSet(xsVar(1), xsID("cross"), xsVar(0));
    xsmcSetBoolean(xsVar(0), data->button.circle);
    xsmcSet(xsVar(1), xsID("circle"), xsVar(0));
    xsmcSetBoolean(xsVar(0), data->button.square);
    xsmcSet(xsVar(1), xsID("square"), xsVar(0));
    xsmcSetBoolean(xsVar(0), data->button.triangle);
    xsmcSet(xsVar(1), xsID("triangle"), xsVar(0));

    xsmcSetBoolean(xsVar(0), data->button.l1);
    xsmcSet(xsVar(1), xsID("l1"), xsVar(0));
    xsmcSetBoolean(xsVar(0), data->button.r1);
    xsmcSet(xsVar(1), xsID("r1"), xsVar(0));
    xsmcSetBoolean(xsVar(0), data->button.l2);
    xsmcSet(xsVar(1), xsID("l2"), xsVar(0));
    xsmcSetBoolean(xsVar(0), data->button.r2);
    xsmcSet(xsVar(1), xsID("r2"), xsVar(0));

    xsmcSetBoolean(xsVar(0), data->button.share);
    xsmcSet(xsVar(1), xsID("share"), xsVar(0));
    xsmcSetBoolean(xsVar(0), data->button.options);
    xsmcSet(xsVar(1), xsID("options"), xsVar(0));
    xsmcSetBoolean(xsVar(0), data->button.l3);
    xsmcSet(xsVar(1), xsID("l3"), xsVar(0));
    xsmcSetBoolean(xsVar(0), data->button.r3);
    xsmcSet(xsVar(1), xsID("r3"), xsVar(0));

    xsmcSetBoolean(xsVar(0), data->button.ps);
    xsmcSet(xsVar(1), xsID("ps"), xsVar(0));
    xsmcSetBoolean(xsVar(0), data->button.touchpad);
    xsmcSet(xsVar(1), xsID("touchpad"), xsVar(0));

    /* D-pad: PS4-esp32 reports diagonals as separate flags, fold them in */
    xsmcSetBoolean(xsVar(0), data->button.up || data->button.upleft || data->button.upright);
    xsmcSet(xsVar(1), xsID("up"), xsVar(0));
    xsmcSetBoolean(xsVar(0), data->button.down || data->button.downleft || data->button.downright);
    xsmcSet(xsVar(1), xsID("down"), xsVar(0));
    xsmcSetBoolean(xsVar(0), data->button.left || data->button.upleft || data->button.downleft);
    xsmcSet(xsVar(1), xsID("left"), xsVar(0));
    xsmcSetBoolean(xsVar(0), data->button.right || data->button.upright || data->button.downright);
    xsmcSet(xsVar(1), xsID("right"), xsVar(0));

    xsmcSet(xsResult, xsID("buttons"), xsVar(1));

    /* --- analog --- */
    xsmcSetNewObject(xsVar(1));

    xsmcSetInteger(xsVar(0), data->analog.stick.lx);
    xsmcSet(xsVar(1), xsID("lx"), xsVar(0));
    xsmcSetInteger(xsVar(0), data->analog.stick.ly);
    xsmcSet(xsVar(1), xsID("ly"), xsVar(0));
    xsmcSetInteger(xsVar(0), data->analog.stick.rx);
    xsmcSet(xsVar(1), xsID("rx"), xsVar(0));
    xsmcSetInteger(xsVar(0), data->analog.stick.ry);
    xsmcSet(xsVar(1), xsID("ry"), xsVar(0));
    xsmcSetInteger(xsVar(0), data->analog.button.l2);
    xsmcSet(xsVar(1), xsID("l2_value"), xsVar(0));
    xsmcSetInteger(xsVar(0), data->analog.button.r2);
    xsmcSet(xsVar(1), xsID("r2_value"), xsVar(0));

    xsmcSet(xsResult, xsID("analog"), xsVar(1));

    /* --- status --- */
    xsmcSetNewObject(xsVar(1));

    xsmcSetInteger(xsVar(0), data->status.battery);
    xsmcSet(xsVar(1), xsID("battery"), xsVar(0));
    xsmcSetBoolean(xsVar(0), data->status.charging);
    xsmcSet(xsVar(1), xsID("charging"), xsVar(0));

    xsmcSet(xsResult, xsID("status"), xsVar(1));
}

void xs_ds4_set_led(xsMachine *the) {
    if (!gDS4) return;
    gDS4->outputCmd.r = clampByte(xsmcToInteger(xsArg(0)));
    gDS4->outputCmd.g = clampByte(xsmcToInteger(xsArg(1)));
    gDS4->outputCmd.b = clampByte(xsmcToInteger(xsArg(2)));
}

void xs_ds4_set_rumble(xsMachine *the) {
    if (!gDS4) return;
    gDS4->outputCmd.smallRumble = clampByte(xsmcToInteger(xsArg(0)));
    gDS4->outputCmd.largeRumble = clampByte(xsmcToInteger(xsArg(1)));
}

void xs_ds4_set_flash_rate(xsMachine *the) {
    if (!gDS4) return;
    /* milliseconds -> units of 10ms (255 = 2.55s) */
    gDS4->outputCmd.flashOn = clampByte(xsmcToInteger(xsArg(0)) / 10);
    gDS4->outputCmd.flashOff = clampByte(xsmcToInteger(xsArg(1)) / 10);
}

void xs_ds4_send_to_controller(xsMachine *the) {
    if (!gDS4 || !ps4IsConnected()) return;
    ps4SetOutput(gDS4->outputCmd);
}
