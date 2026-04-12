/*
 * Battery Level Typing Behavior for ZMK
 *
 * キーを押すとバッテリー残量をキーストロークとして送信する: "L:50% R:80%"
 * ホストOS側のキーボードレイアウトはUS配列を前提としている
 */

#define DT_DRV_COMPAT zmk_behavior_battery_type

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <drivers/behavior.h>
#include <zmk/behavior.h>
#include <zmk/battery.h>
#include <zmk/endpoints.h>
#include <zmk/hid.h>
#include <zmk/keymap.h>

#if IS_ENABLED(CONFIG_ZMK_SPLIT_BLE_CENTRAL_BATTERY_LEVEL_FETCHING)
#include <zmk/split/central.h>
#endif

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

/* キーストローク間のディレイ（ms）。BLEで文字落ちする場合は値を大きくする */
#define KEYSTROKE_DELAY_MS 50

/* 出力フォーマット定義 */
#define FORMAT_PREFIX   "L="
#define FORMAT_MIDDLE   "%/R="
#define FORMAT_SUFFIX   "%"
#define FORMAT_NO_DATA  "--"

/* 文字→HIDキーコードのマッピング構造体 */
struct char_keycode {
    uint32_t keycode;
    bool shift;
};

/* HID Usage IDs */
#define HID_KEY_A          0x04
#define HID_KEY_B          0x05
#define HID_KEY_L          0x0F
#define HID_KEY_R          0x15
#define HID_KEY_1          0x1E
#define HID_KEY_2          0x1F
#define HID_KEY_3          0x20
#define HID_KEY_4          0x21
#define HID_KEY_5          0x22
#define HID_KEY_6          0x23
#define HID_KEY_7          0x24
#define HID_KEY_8          0x25
#define HID_KEY_9          0x26
#define HID_KEY_0          0x27
#define HID_KEY_SPACE      0x2C
#define HID_KEY_MINUS      0x2D
#define HID_KEY_SEMICOLON  0x33
#define HID_KEY_LSHIFT     0xE1

static const struct char_keycode CHAR_MAP[] = {
    ['0'] = { .keycode = HID_KEY_0, .shift = false },
    ['1'] = { .keycode = HID_KEY_1, .shift = false },
    /* ... 2〜9も同様 ... */
    ['L'] = { .keycode = HID_KEY_L, .shift = true },
    ['R'] = { .keycode = HID_KEY_R, .shift = true },
    [' '] = { .keycode = HID_KEY_SPACE, .shift = false },
    [':'] = { .keycode = HID_KEY_SEMICOLON, .shift = true },  /* US: Shift+; */
    ['%'] = { .keycode = HID_KEY_5, .shift = true },           /* US: Shift+5 */
    ['-'] = { .keycode = HID_KEY_MINUS, .shift = false },
};

#define CHAR_MAP_SIZE (sizeof(CHAR_MAP) / sizeof(CHAR_MAP[0]))


static const uint32_t HID_LSHIFT = HID_KEY_LSHIFT;

/* 1文字をキーストロークとして送信する */
static int send_char(char c) {
    if ((uint8_t)c >= CHAR_MAP_SIZE || CHAR_MAP[(uint8_t)c].keycode == 0) {
        LOG_WRN("No keycode mapping for char: 0x%02x", c);
        return -EINVAL;
    }

    const struct char_keycode *mapping = &CHAR_MAP[(uint8_t)c];

    /* Shiftが必要な文字はShiftを先に押す */
    if (mapping->shift) {
        zmk_hid_keyboard_press(HID_LSHIFT);
        zmk_endpoints_send_report(HID_USAGE_KEY);
        k_msleep(KEYSTROKE_DELAY_MS);
    }

    zmk_hid_keyboard_press(mapping->keycode);
    zmk_endpoints_send_report(HID_USAGE_KEY);
    k_msleep(KEYSTROKE_DELAY_MS);

    zmk_hid_keyboard_release(mapping->keycode);
    if (mapping->shift) {
        zmk_hid_keyboard_release(HID_LSHIFT);
    }
    zmk_endpoints_send_report(HID_USAGE_KEY);
    k_msleep(KEYSTROKE_DELAY_MS);

    return 0;
}

/* 文字列をキーストロークとして送信する */
static int send_string(const char *str) {
    for (int i = 0; str[i] != '\0'; i++) {
        int ret = send_char(str[i]);
        if (ret < 0) return ret;
    }
    return 0;
}

/* 数値を文字列に変換して送信する */
static int send_number(int value) {
    char buf[8];
    snprintf(buf, sizeof(buf), "%d", value);
    return send_string(buf);
}


/* セントラル側のバッテリー残量を取得 */
static int get_central_battery(void) {
    int soc = zmk_battery_state_of_charge();
    return (soc >= 0) ? soc : -1;
}

/* ペリフェラル側のバッテリー残量を取得 */
static int get_peripheral_battery(void) {
#if IS_ENABLED(CONFIG_ZMK_SPLIT_BLE_CENTRAL_BATTERY_LEVEL_FETCHING)
    uint8_t level = 0;
    int rc = zmk_split_central_get_peripheral_battery_level(0, &level);
    return (rc == 0) ? (int)level : -1;
#else
    return -1;
#endif
}

/* キー押下時の処理 */
static int on_keymap_binding_pressed(struct zmk_behavior_binding *binding,
                                     struct zmk_behavior_binding_event event) {
    int left_bat = get_central_battery();
    int right_bat = get_peripheral_battery();

    send_string(FORMAT_PREFIX);
    if (left_bat >= 0) {
        send_number(left_bat);
    } else {
        send_string(FORMAT_NO_DATA);
    }

    send_string(FORMAT_MIDDLE);
    if (right_bat >= 0) {
        send_number(right_bat);
    } else {
        send_string(FORMAT_NO_DATA);
    }

    send_string(FORMAT_SUFFIX);
    return ZMK_BEHAVIOR_OPAQUE;
}

static int on_keymap_binding_released(struct zmk_behavior_binding *binding,
                                      struct zmk_behavior_binding_event event) {
    return ZMK_BEHAVIOR_OPAQUE;
}

static int behavior_battery_type_init(const struct device *dev) { return 0; }

static const struct behavior_driver_api behavior_battery_type_driver_api = {
    .binding_pressed = on_keymap_binding_pressed,
    .binding_released = on_keymap_binding_released,
};

#define BATTERY_TYPE_INST(n)                                                    \
    BEHAVIOR_DT_INST_DEFINE(n, behavior_battery_type_init, NULL, NULL, NULL,    \
                            POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,   \
                            &behavior_battery_type_driver_api);

DT_INST_FOREACH_STATUS_OKAY(BATTERY_TYPE_INST)
