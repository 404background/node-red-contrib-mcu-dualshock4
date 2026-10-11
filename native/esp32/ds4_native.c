/*
 * ds4_native.c - XS in C bridge: DualShock4 over Bluetooth Classic (ESP32)
 *
 * Uses the ESP-IDF Bluedroid Classic HID Host (esp_hidh_api.h).
 *
 * [Pairing - first time only]
 *   1. Hold SHARE + PS on the DualShock4 until the light bar double-flashes
 *      (pairing mode: the controller becomes discoverable).
 *   2. The ESP32 connects to it:
 *        - DS4 MAC configured: connects to that address
 *        - DS4 MAC empty:      inquiry, picks the first "Wireless Controller".
 *          After that the bonded controller is used as if it was configured,
 *          so other controllers are not paired until it is unpaired.
 *   3. Secure Simple Pairing (Just Works) bonds both sides. The DualShock4
 *      stores the ESP32 address, the ESP32 stores the link key in NVS.
 *
 * [Reconnection]
 *   Pressing PS makes the DualShock4 connect to the ESP32 by itself.
 *   Bonded devices are registered with the HID host at start-up so that
 *   these incoming connections are accepted.
 *
 * [Input - DualShock4 -> ESP32]  (interrupt channel, DATA | INPUT)
 *   Report 0x01 (reduced, sent until a feature report is read):
 *     [0]=0x01 [1..]=LX LY RX RY BTN0 BTN1 BTN2 L2 R2
 *   Report 0x11 (full):
 *     [0]=0x11 [1..2]=flags [3..]=LX LY RX RY BTN0 BTN1 BTN2 L2 R2 ...
 *     battery/charging at [3 + 29]
 *   BTN0: D-pad hat (low nibble, 8 = released) + Square/Cross/Circle/Triangle
 *   BTN1: L1 R1 L2 R2 Share Options L3 R3
 *   BTN2: PS (bit0) Touchpad (bit1)
 *
 * [Output - ESP32 -> DualShock4]  (interrupt channel, DATA | OUTPUT = 0xA2)
 *   Report 0x11, 78 bytes:
 *     [1]=0xC0 (HID + CRC) [3]=0x07 (rumble | LED | blink)
 *     [6]=small rumble [7]=large rumble [8..10]=R G B
 *     [11]=flash on [12]=flash off (x10ms)  [74..77]=CRC32(0xA2 + [0..73])
 */

#include "xsmc.h"
#include "xsHost.h"
#include "mc.xs.h"

#include "sdkconfig.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_gap_bt_api.h"
#include "esp_hidh_api.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#if !CONFIG_IDF_TARGET_ESP32
#error "DualShock4 requires the original ESP32 (Bluetooth Classic). ESP32-S2/S3/C3/C6/H2 are not supported."
#endif

#if !CONFIG_BT_HID_HOST_ENABLED
#error "CONFIG_BT_HID_HOST_ENABLED is required (see native/esp32/sdkconfig/sdkconfig.defaults)"
#endif

#define DS4_TAG "DS4"

#define DS4_NAME "Wireless Controller"
#define DS4_OUTPUT_REPORT_SIZE 78
#define DS4_RETRY_MS 2000
#define DS4_INQUIRY_LEN 10      /* x 1.28s */
#define DS4_WATCH_MS 6000
#define DS4_WATCH_INQUIRY_LEN 2 /* x 1.28s */

enum {
    kPairIdle = 0,
    kPairDiscovering,
    kPairConnecting,
    kPairWatching       /* bonded: looking for the controller in pairing mode */
};

typedef struct {
    uint8_t cross, circle, square, triangle;
    uint8_t l1, r1, l2, r2;
    uint8_t share, options, l3, r3;
    uint8_t ps, touchpad;
    uint8_t up, down, left, right;
    int8_t lx, ly, rx, ry;
    uint8_t l2_value, r2_value;
    uint8_t battery;
    uint8_t charging;
} ds4_state_t;

typedef struct {
    uint8_t smallRumble, largeRumble;
    uint8_t r, g, b;
    uint8_t flashOn, flashOff;
} ds4_output_t;

static portMUX_TYPE gMux = portMUX_INITIALIZER_UNLOCKED;

static uint8_t gInitialized;
static char gInitError[64];
static volatile uint8_t gHidReady;
static volatile uint8_t gConnected;
static uint8_t gPrevConnected;
static esp_bd_addr_t gPeer;

static uint8_t gHasTarget;
static esp_bd_addr_t gTarget;

static volatile uint8_t gPairState = kPairIdle;
static volatile uint8_t gFound;
static esp_bd_addr_t gFoundAddr;
static esp_bd_addr_t gWatchAddr;    /* controller to look for while kPairWatching */
static volatile int64_t gNextAttempt;

static ds4_state_t gState;
static ds4_output_t gOutput = { 0, 0, 0, 0, 64, 0, 0 };

/* Last error, readable from JS (ESP-IDF logs are not visible under Moddable) */
static char gLastError[64];
static volatile uint8_t gLastErrorChanged;

/* Minimal descriptor, only used to register bonded controllers at start-up */
static const uint8_t gDescriptor[] = {
    0x05, 0x01, 0x09, 0x05, 0xA1, 0x01, 0x85, 0x11,
    0x06, 0x00, 0xFF, 0x09, 0x20, 0x15, 0x00, 0x26,
    0xFF, 0x00, 0x75, 0x08, 0x95, 0x4D, 0x81, 0x02,
    0xC0
};

/********************************************************************************/
/*                              H E L P E R S                                   */
/********************************************************************************/

static int64_t nowMs(void) {
    return esp_timer_get_time() / 1000;
}

/* Reports an error to JS, unless it repeats the previous one.
 * Failures of the pairing retries are expected and not reported. */
static void setError(const char *format, ...) {
    char message[sizeof(gLastError)];
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    if (!strcmp(message, gLastError))
        return;
    strcpy(gLastError, message);
    gLastErrorChanged = 1;
}

static uint8_t clampByte(xsIntegerValue value) {
    if (value < 0) return 0;
    if (value > 255) return 255;
    return (uint8_t)value;
}

static int parseMac(const char *str, esp_bd_addr_t addr) {
    unsigned int b[6];
    if (!str || sscanf(str, "%x:%x:%x:%x:%x:%x", &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]) != 6)
        return 0;
    for (int i = 0; i < 6; i++) {
        if (b[i] > 0xFF) return 0;
        addr[i] = (uint8_t)b[i];
    }
    return 1;
}

static void formatMac(const uint8_t *addr, char *str) {
    snprintf(str, 18, "%02x:%02x:%02x:%02x:%02x:%02x",
             addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]);
}

static int isBonded(const esp_bd_addr_t addr) {
    int count = esp_bt_gap_get_bond_device_num();
    if (count <= 0) return 0;

    esp_bd_addr_t *list = c_malloc(sizeof(esp_bd_addr_t) * count);
    if (!list) return 0;

    int found = 0;
    if (esp_bt_gap_get_bond_device_list(&count, list) == ESP_OK) {
        for (int i = 0; i < count; i++) {
            if (!memcmp(list[i], addr, ESP_BD_ADDR_LEN)) {
                found = 1;
                break;
            }
        }
    }
    c_free(list);
    return found;
}

/* The first bonded controller, used when no DS4 MAC is configured */
static int getBondedController(esp_bd_addr_t addr) {
    int count = esp_bt_gap_get_bond_device_num();
    if (count <= 0) return 0;

    esp_bd_addr_t *list = c_malloc(sizeof(esp_bd_addr_t) * count);
    if (!list) return 0;

    int found = 0;
    if ((esp_bt_gap_get_bond_device_list(&count, list) == ESP_OK) && (count > 0)) {
        memcpy(addr, list[0], ESP_BD_ADDR_LEN);
        found = 1;
    }
    c_free(list);
    return found;
}

/* Pairing is needed when the configured controller (or, without a
 * configured address, any controller) has not been bonded yet. */
static int needsPairing(void) {
    if (gHasTarget)
        return !isBonded(gTarget);
    return esp_bt_gap_get_bond_device_num() <= 0;
}

static uint32_t crc32(uint32_t crc, const uint8_t *data, size_t length) {
    crc = ~crc;
    while (length--) {
        crc ^= *data++;
        for (int i = 0; i < 8; i++)
            crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
    }
    return ~crc;
}

static void sendOutput(void) {
    uint8_t report[DS4_OUTPUT_REPORT_SIZE] = { 0 };
    esp_bd_addr_t peer;

    portENTER_CRITICAL(&gMux);
    memcpy(peer, gPeer, sizeof(peer));
    report[0] = 0x11;
    report[1] = 0xC0;
    report[3] = 0x07;
    report[6] = gOutput.smallRumble;
    report[7] = gOutput.largeRumble;
    report[8] = gOutput.r;
    report[9] = gOutput.g;
    report[10] = gOutput.b;
    report[11] = gOutput.flashOn;
    report[12] = gOutput.flashOff;
    portEXIT_CRITICAL(&gMux);

    const uint8_t header = 0xA2;
    uint32_t crc = crc32(0, &header, 1);
    crc = crc32(crc, report, DS4_OUTPUT_REPORT_SIZE - 4);
    report[74] = (uint8_t)crc;
    report[75] = (uint8_t)(crc >> 8);
    report[76] = (uint8_t)(crc >> 16);
    report[77] = (uint8_t)(crc >> 24);

    esp_err_t err = esp_bt_hid_host_send_data(peer, report, sizeof(report));
    if (err != ESP_OK)
        ESP_LOGW(DS4_TAG, "send output failed: %s", esp_err_to_name(err));
}

static void parseInput(const uint8_t *data, uint16_t length) {
    /* Skip the HID transaction header if the stack left it in */
    if (length && data[0] == 0xA1) {
        data++;
        length--;
    }
    if (length < 10) return;

    int base;
    if (data[0] == 0x11)
        base = 3;
    else if (data[0] == 0x01)
        base = 1;
    else
        return;
    if (length < base + 9) return;

    const uint8_t *p = data + base;
    uint8_t btn0 = p[4], btn1 = p[5], btn2 = p[6];
    uint8_t hat = btn0 & 0x0F;
    ds4_state_t s;

    s.lx = (int8_t)(p[0] - 128);
    s.ly = (int8_t)(127 - p[1]);
    s.rx = (int8_t)(p[2] - 128);
    s.ry = (int8_t)(127 - p[3]);

    s.up = (hat == 0) || (hat == 1) || (hat == 7);
    s.right = (hat == 1) || (hat == 2) || (hat == 3);
    s.down = (hat == 3) || (hat == 4) || (hat == 5);
    s.left = (hat == 5) || (hat == 6) || (hat == 7);

    s.square = (btn0 & 0x10) ? 1 : 0;
    s.cross = (btn0 & 0x20) ? 1 : 0;
    s.circle = (btn0 & 0x40) ? 1 : 0;
    s.triangle = (btn0 & 0x80) ? 1 : 0;

    s.l1 = (btn1 & 0x01) ? 1 : 0;
    s.r1 = (btn1 & 0x02) ? 1 : 0;
    s.l2 = (btn1 & 0x04) ? 1 : 0;
    s.r2 = (btn1 & 0x08) ? 1 : 0;
    s.share = (btn1 & 0x10) ? 1 : 0;
    s.options = (btn1 & 0x20) ? 1 : 0;
    s.l3 = (btn1 & 0x40) ? 1 : 0;
    s.r3 = (btn1 & 0x80) ? 1 : 0;

    s.ps = (btn2 & 0x01) ? 1 : 0;
    s.touchpad = (btn2 & 0x02) ? 1 : 0;

    s.l2_value = p[7];
    s.r2_value = p[8];

    portENTER_CRITICAL(&gMux);
    if (length > base + 29) {
        s.battery = p[29] & 0x0F;
        s.charging = (p[29] & 0x10) ? 1 : 0;
    } else {
        /* reduced report has no status, keep the last known values */
        s.battery = gState.battery;
        s.charging = gState.charging;
    }
    gState = s;
    portEXIT_CRITICAL(&gMux);
}

static int isController(esp_bt_gap_cb_param_t *param) {
    for (int i = 0; i < param->disc_res.num_prop; i++) {
        esp_bt_gap_dev_prop_t *prop = &param->disc_res.prop[i];
        if (prop->type == ESP_BT_GAP_DEV_PROP_BDNAME) {
            if (prop->len >= (int)strlen(DS4_NAME) && !memcmp(prop->val, DS4_NAME, strlen(DS4_NAME)))
                return 1;
        }
        else if (prop->type == ESP_BT_GAP_DEV_PROP_EIR) {
            uint8_t len = 0;
            uint8_t *name = esp_bt_gap_resolve_eir_data(prop->val, ESP_BT_EIR_TYPE_CMPL_LOCAL_NAME, &len);
            if (name && len >= strlen(DS4_NAME) && !memcmp(name, DS4_NAME, strlen(DS4_NAME)))
                return 1;
        }
        else if (prop->type == ESP_BT_GAP_DEV_PROP_COD) {
            uint32_t cod = *(uint32_t *)prop->val;
            /* Peripheral / gamepad */
            if ((esp_bt_gap_get_cod_major_dev(cod) == ESP_BT_COD_MAJOR_DEV_PERIPHERAL) &&
                ((esp_bt_gap_get_cod_minor_dev(cod) & 0x0F) == 0x02))
                return 1;
        }
    }
    return 0;
}

/* Called periodically from the XS timer: (re)starts pairing attempts */
static void service(void) {
    if (!gHidReady || gConnected || (gPairState != kPairIdle))
        return;
    if (nowMs() < gNextAttempt)
        return;
    esp_err_t err;
    if (!needsPairing()) {
        /* Bonded: the controller connects by itself when PS is pressed.
         * The DualShock4 remembers only one host, so it may have been paired
         * with another ESP32 since. It is discoverable only in pairing mode
         * (SHARE + PS): when it shows up, pair again. Without a DS4 MAC the
         * bonded controller is the one to look for. */
        if (gHasTarget)
            memcpy(gWatchAddr, gTarget, ESP_BD_ADDR_LEN);
        else if (!getBondedController(gWatchAddr))
            return;
        gFound = 0;
        gPairState = kPairWatching;
        if (esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY, DS4_WATCH_INQUIRY_LEN, 0) != ESP_OK)
            gPairState = kPairIdle;
        gNextAttempt = nowMs() + DS4_WATCH_MS;
        return;
    }

    if (gHasTarget) {
        char str[18];
        formatMac(gTarget, str);
        ESP_LOGI(DS4_TAG, "pairing: connecting to %s", str);
        gPairState = kPairConnecting;
        if ((err = esp_bt_hid_host_connect(gTarget)) != ESP_OK) {
            gPairState = kPairIdle;
            gNextAttempt = nowMs() + DS4_RETRY_MS;
        }
    }
    else {
        ESP_LOGI(DS4_TAG, "pairing: searching for a DualShock4 in pairing mode");
        gFound = 0;
        gPairState = kPairDiscovering;
        if ((err = esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY, DS4_INQUIRY_LEN, 0)) != ESP_OK) {
            gPairState = kPairIdle;
            gNextAttempt = nowMs() + DS4_RETRY_MS;
        }
    }
}

/********************************************************************************/
/*                        C A L L B A C K S  (BTC task)                         */
/********************************************************************************/

static void gapCallback(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param) {
    switch (event) {
    case ESP_BT_GAP_DISC_RES_EVT:
        if ((gPairState == kPairWatching) && !gFound &&
            !memcmp(param->disc_res.bda, gWatchAddr, ESP_BD_ADDR_LEN) && isController(param)) {
            memcpy(gFoundAddr, param->disc_res.bda, ESP_BD_ADDR_LEN);
            gFound = 1;
            esp_bt_gap_cancel_discovery();
        }
        else if ((gPairState == kPairDiscovering) && !gFound && isController(param)) {
            memcpy(gFoundAddr, param->disc_res.bda, ESP_BD_ADDR_LEN);
            gFound = 1;
            esp_bt_gap_cancel_discovery();
        }
        break;

    case ESP_BT_GAP_DISC_STATE_CHANGED_EVT:
        if ((param->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STOPPED) && (gPairState == kPairWatching)) {
            gPairState = kPairIdle;
            if (gFound && !gConnected) {
                /* The old link key is no longer valid in the controller */
                ESP_LOGI(DS4_TAG, "pairing: controller in pairing mode, pairing again");
                esp_bt_gap_remove_bond_device(gFoundAddr);
                gPairState = kPairConnecting;
                if (esp_bt_hid_host_connect(gFoundAddr) != ESP_OK) {
                    gPairState = kPairIdle;
                    gNextAttempt = nowMs() + DS4_RETRY_MS;
                }
            }
        }
        else if ((param->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STOPPED) && (gPairState == kPairDiscovering)) {
            if (gFound) {
                char str[18];
                formatMac(gFoundAddr, str);
                ESP_LOGI(DS4_TAG, "pairing: found %s", str);
                gPairState = kPairConnecting;
                if (esp_bt_hid_host_connect(gFoundAddr) == ESP_OK)
                    break;
            }
            gPairState = kPairIdle;
            gNextAttempt = nowMs() + DS4_RETRY_MS;
        }
        break;

    case ESP_BT_GAP_AUTH_CMPL_EVT:
        if (param->auth_cmpl.stat == ESP_BT_STATUS_SUCCESS)
            ESP_LOGI(DS4_TAG, "paired with %s", param->auth_cmpl.device_name);
        else
            setError("pairing failed (0x%x)", param->auth_cmpl.stat);
        break;

    case ESP_BT_GAP_PIN_REQ_EVT: {
        esp_bt_pin_code_t pin = { '0', '0', '0', '0' };
        esp_bt_gap_pin_reply(param->pin_req.bda, true, 4, pin);
        } break;

    case ESP_BT_GAP_CFM_REQ_EVT:
        esp_bt_gap_ssp_confirm_reply(param->cfm_req.bda, true);
        break;

    default:
        break;
    }
}

static void registerBondedControllers(void) {
    int count = esp_bt_gap_get_bond_device_num();
    if (count <= 0) return;

    esp_bd_addr_t *list = c_malloc(sizeof(esp_bd_addr_t) * count);
    if (!list) return;

    esp_hidh_hid_info_t *info = c_calloc(1, sizeof(esp_hidh_hid_info_t));
    if (info && (esp_bt_gap_get_bond_device_list(&count, list) == ESP_OK)) {
        info->attr_mask = ESP_HIDH_DEV_ATTR_VIRTUAL_CABLE | ESP_HIDH_DEV_ATTR_RECONNECT_INITIATE;
        info->sub_class = 0x08;
        info->app_id = ESP_HIDH_APP_ID_GAMEPAD;
        info->vendor_id = 0x054C;
        info->product_id = 0x05C4;
        info->version = 0x0100;
        info->dl_len = sizeof(gDescriptor);
        memcpy(info->dsc_list, gDescriptor, sizeof(gDescriptor));
        for (int i = 0; i < count; i++)
            esp_bt_hid_host_set_info(list[i], info);
    }
    c_free(info);
    c_free(list);
}

static void hidhCallback(esp_hidh_cb_event_t event, esp_hidh_cb_param_t *param) {
    switch (event) {
    case ESP_HIDH_INIT_EVT:
        if (param->init.status == ESP_HIDH_OK) {
            registerBondedControllers();
            gHidReady = 1;
        }
        else
            setError("hid host init failed (0x%x)", param->init.status);
        break;

    case ESP_HIDH_OPEN_EVT:
        if ((param->open.status == ESP_HIDH_OK) && (param->open.conn_status == ESP_HIDH_CONN_STATE_CONNECTED)) {
            if (gHasTarget && memcmp(param->open.bd_addr, gTarget, ESP_BD_ADDR_LEN)) {
                char str[18];
                formatMac(param->open.bd_addr, str);
                setError("rejected %s: not the DS4 MAC", str);
                esp_bt_hid_host_disconnect(param->open.bd_addr);
                break;
            }
            portENTER_CRITICAL(&gMux);
            memcpy(gPeer, param->open.bd_addr, ESP_BD_ADDR_LEN);
            memset(&gState, 0, sizeof(gState));
            portEXIT_CRITICAL(&gMux);
            gPairState = kPairIdle;
            gConnected = 1;

            /* Reading a feature report switches the controller to full 0x11 reports */
            esp_bt_hid_host_get_report(param->open.bd_addr, ESP_HIDH_REPORT_TYPE_FEATURE, 0x02, 37);
            sendOutput();
        }
        else if (param->open.conn_status == ESP_HIDH_CONN_STATE_DISCONNECTED) {
            if (gPairState == kPairConnecting) {
                gPairState = kPairIdle;
                gNextAttempt = nowMs() + DS4_RETRY_MS;
            }
        }
        break;

    case ESP_HIDH_CLOSE_EVT:
    case ESP_HIDH_VC_UNPLUG_EVT:
        gConnected = 0;
        /* Do not resume rumbling on the next connection */
        portENTER_CRITICAL(&gMux);
        gOutput.smallRumble = gOutput.largeRumble = 0;
        portEXIT_CRITICAL(&gMux);
        break;

    case ESP_HIDH_DATA_IND_EVT:
        if (gConnected && (param->data_ind.status == ESP_HIDH_OK))
            parseInput(param->data_ind.data, param->data_ind.len);
        break;

    default:
        break;
    }
}

/********************************************************************************/
/*                  X S   I N   C   F U N C T I O N S                           */
/********************************************************************************/

/* Returns NULL on success, otherwise the name of the step that failed (*errOut set) */
static const char *startBluetooth(esp_err_t *errOut) {
    esp_err_t err;
    const char *step;
    esp_bt_controller_config_t cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();

    esp_bt_controller_mem_release(ESP_BT_MODE_BLE);

    step = "controller init";
    if ((err = esp_bt_controller_init(&cfg)) != ESP_OK) goto bail;
    step = "controller enable";
    if ((err = esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT)) != ESP_OK) goto bail;
    step = "bluedroid init";
    if ((err = esp_bluedroid_init()) != ESP_OK) goto bail;
    step = "bluedroid enable";
    if ((err = esp_bluedroid_enable()) != ESP_OK) goto bail;

    esp_bt_gap_register_callback(gapCallback);
    esp_bt_gap_set_device_name("ESP32 DS4 Host");

    esp_bt_sp_param_t param = ESP_BT_SP_IOCAP_MODE;
    esp_bt_io_cap_t iocap = ESP_BT_IO_CAP_NONE;
    esp_bt_gap_set_security_param(param, &iocap, sizeof(iocap));

    esp_bt_pin_code_t pin = { 0 };
    esp_bt_gap_set_pin(ESP_BT_PIN_TYPE_VARIABLE, 0, pin);

    step = "hid host register";
    if ((err = esp_bt_hid_host_register_callback(hidhCallback)) != ESP_OK) goto bail;
    step = "hid host init";
    if ((err = esp_bt_hid_host_init()) != ESP_OK) goto bail;

    /* Let the bonded controller connect to us when PS is pressed */
    esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_NON_DISCOVERABLE);
    return NULL;

bail:
    ESP_LOGE(DS4_TAG, "bluetooth %s failed: %s", step, esp_err_to_name(err));
    *errOut = err;
    return step;
}

/*
 * xs_ds4_init(ds4Mac: string): string | undefined (error message)
 *
 * ds4Mac: the DualShock4's own Bluetooth address. Only this controller is
 *         paired / accepted. Empty: pair with the first controller found,
 *         then keep using that bonded controller.
 * Bluetooth is started only once; later calls only update the address.
 */
void xs_ds4_init(xsMachine *the) {
    esp_bd_addr_t addr;
    if (parseMac(xsmcToString(xsArg(0)), addr)) {
        memcpy(gTarget, addr, sizeof(addr));
        gHasTarget = 1;
    }
    else
        gHasTarget = 0;

    if (!gInitialized) {
        gInitialized = 1;

        esp_err_t err = ESP_OK;
        const char *failed = startBluetooth(&err);
        /* Returned, not thrown: a throw also stops in xsbug */
        if (failed && (ESP_ERR_INVALID_STATE == err) && !strcmp(failed, "controller init"))
            snprintf(gInitError, sizeof(gInitError), "BT init failed (0x103): patch Moddable main.c");
        else if (failed)
            snprintf(gInitError, sizeof(gInitError), "BT %s failed (0x%x)", failed, (unsigned)err);
    }
    if (gInitError[0])
        xsmcSetString(xsResult, gInitError);
}

/*
 * xs_ds4_check_connection_changed(): integer
 * Returns: 1 = newly connected, -1 = newly disconnected, 0 = no change
 * Also drives the pairing retries.
 */
void xs_ds4_check_connection_changed(xsMachine *the) {
    service();

    uint8_t connected = gConnected;
    int change = 0;
    if (connected && !gPrevConnected)
        change = 1;
    else if (!connected && gPrevConnected)
        change = -1;
    gPrevConnected = connected;
    xsmcSetInteger(xsResult, change);
}

/*
 * xs_ds4_get_state(): string
 * "connected" | "starting" | "pairing" | "waiting"
 */
void xs_ds4_get_state(xsMachine *the) {
    if (gConnected)
        xsmcSetString(xsResult, "connected");
    else if (!gHidReady)
        xsmcSetString(xsResult, "starting");
    else if ((gPairState == kPairDiscovering) || (gPairState == kPairConnecting) || needsPairing())
        xsmcSetString(xsResult, "pairing");
    else
        xsmcSetString(xsResult, "waiting");
}

/*
 * xs_ds4_get_last_error(): string | undefined
 * The latest Bluetooth error, returned once after it occurs.
 */
void xs_ds4_get_last_error(xsMachine *the) {
    if (gLastErrorChanged) {
        gLastErrorChanged = 0;
        xsmcSetString(xsResult, gLastError);
    }
}

/*
 * xs_ds4_get_local_address(): string | undefined
 * The ESP32's Bluetooth address (stored in the DualShock4 after pairing).
 */
void xs_ds4_get_local_address(xsMachine *the) {
    const uint8_t *addr = gInitialized ? esp_bt_dev_get_address() : NULL;
    if (addr) {
        char str[18];
        formatMac(addr, str);
        xsmcSetString(xsResult, str);
    }
}

/*
 * xs_ds4_get_peer_address(): string | undefined
 * The connected DualShock4's Bluetooth address.
 */
void xs_ds4_get_peer_address(xsMachine *the) {
    if (gConnected) {
        char str[18];
        formatMac(gPeer, str);
        xsmcSetString(xsResult, str);
    }
}

/*
 * xs_ds4_unpair(): void
 * Removes all bonds so the next controller in pairing mode can be paired.
 */
void xs_ds4_unpair(xsMachine *the) {
    if (!gInitialized) return;

    if (gConnected)
        esp_bt_hid_host_disconnect(gPeer);

    int count = esp_bt_gap_get_bond_device_num();
    if (count > 0) {
        esp_bd_addr_t *list = c_malloc(sizeof(esp_bd_addr_t) * count);
        if (list) {
            if (esp_bt_gap_get_bond_device_list(&count, list) == ESP_OK) {
                for (int i = 0; i < count; i++)
                    esp_bt_gap_remove_bond_device(list[i]);
            }
            c_free(list);
        }
    }
    gNextAttempt = 0;
}

/*
 * xs_ds4_read(): object | null
 * { buttons: {...}, analog: {...}, status: {...} }
 */
void xs_ds4_read(xsMachine *the) {
    if (!gConnected) {
        xsmcSetNull(xsResult);
        return;
    }

    ds4_state_t s;
    portENTER_CRITICAL(&gMux);
    s = gState;
    portEXIT_CRITICAL(&gMux);

    /* xsVar(0) = temp value, xsVar(1) = sub-object */
    xsmcVars(2);
    xsmcSetNewObject(xsResult);

    /* --- buttons --- */
    xsmcSetNewObject(xsVar(1));
#define SET_BOOL(name) xsmcSetBoolean(xsVar(0), s.name); xsmcSet(xsVar(1), xsID(#name), xsVar(0))
    SET_BOOL(cross);
    SET_BOOL(circle);
    SET_BOOL(square);
    SET_BOOL(triangle);
    SET_BOOL(l1);
    SET_BOOL(r1);
    SET_BOOL(l2);
    SET_BOOL(r2);
    SET_BOOL(share);
    SET_BOOL(options);
    SET_BOOL(l3);
    SET_BOOL(r3);
    SET_BOOL(ps);
    SET_BOOL(touchpad);
    SET_BOOL(up);
    SET_BOOL(down);
    SET_BOOL(left);
    SET_BOOL(right);
    xsmcSet(xsResult, xsID("buttons"), xsVar(1));

    /* --- analog --- */
    xsmcSetNewObject(xsVar(1));
#define SET_INT(name) xsmcSetInteger(xsVar(0), s.name); xsmcSet(xsVar(1), xsID(#name), xsVar(0))
    SET_INT(lx);
    SET_INT(ly);
    SET_INT(rx);
    SET_INT(ry);
    SET_INT(l2_value);
    SET_INT(r2_value);
    xsmcSet(xsResult, xsID("analog"), xsVar(1));

    /* --- status --- */
    xsmcSetNewObject(xsVar(1));
    SET_INT(battery);
    SET_BOOL(charging);
    xsmcSet(xsResult, xsID("status"), xsVar(1));
#undef SET_BOOL
#undef SET_INT
}

void xs_ds4_set_led(xsMachine *the) {
    uint8_t r = clampByte(xsmcToInteger(xsArg(0)));
    uint8_t g = clampByte(xsmcToInteger(xsArg(1)));
    uint8_t b = clampByte(xsmcToInteger(xsArg(2)));
    portENTER_CRITICAL(&gMux);
    gOutput.r = r;
    gOutput.g = g;
    gOutput.b = b;
    portEXIT_CRITICAL(&gMux);
}

void xs_ds4_set_rumble(xsMachine *the) {
    uint8_t small = clampByte(xsmcToInteger(xsArg(0)));
    uint8_t large = clampByte(xsmcToInteger(xsArg(1)));
    portENTER_CRITICAL(&gMux);
    gOutput.smallRumble = small;
    gOutput.largeRumble = large;
    portEXIT_CRITICAL(&gMux);
}

void xs_ds4_set_flash_rate(xsMachine *the) {
    /* milliseconds -> units of 10ms (255 = 2.55s) */
    uint8_t on = clampByte(xsmcToInteger(xsArg(0)) / 10);
    uint8_t off = clampByte(xsmcToInteger(xsArg(1)) / 10);
    portENTER_CRITICAL(&gMux);
    gOutput.flashOn = on;
    gOutput.flashOff = off;
    portEXIT_CRITICAL(&gMux);
}

void xs_ds4_send_to_controller(xsMachine *the) {
    if (gConnected)
        sendOutput();
}
