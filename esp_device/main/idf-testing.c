/*
 * SPDX-FileCopyrightText: 2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#include <stdlib.h>
#include <device/usbd_pvt.h>
#include <hal/uart_types.h>
#include <driver/uart.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "tinyusb.h"
#include "class/hid/hid_device.h"
#include "driver/gpio.h"
#include "key_types.h"
#include "led_strip.h"
#include "sdkconfig.h"


// zero = no movement
#define POINTER_POS_MIN_VAL 1
#define POINTER_POS_MAX_VAL 32767 //  0x7fff according to usb spec
#define ECHO_UART_PORT_NUM      (UART_NUM_0)
#define ECHO_UART_BAUD_RATE     (460800)
#define ECHO_TASK_STACK_SIZE    (2048)


// Mouse Report Descriptor Template
#define TUD_HID_REPORT_DESC_MOUSE_ABS(...) \
  HID_USAGE_PAGE ( HID_USAGE_PAGE_DESKTOP      )                   ,\
  HID_USAGE      ( HID_USAGE_DESKTOP_MOUSE     )                   ,\
  HID_COLLECTION ( HID_COLLECTION_APPLICATION  )                   ,\
    /* Report ID if any */\
    __VA_ARGS__ \
    HID_USAGE      ( HID_USAGE_DESKTOP_POINTER )                   ,\
    HID_COLLECTION ( HID_COLLECTION_PHYSICAL   )                   ,\
      HID_USAGE_PAGE  ( HID_USAGE_PAGE_BUTTON  )                   ,\
        HID_USAGE_MIN   ( 1                                      ) ,\
        HID_USAGE_MAX   ( 5                                      ) ,\
        HID_LOGICAL_MIN ( 0                                      ) ,\
        HID_LOGICAL_MAX ( 1                                      ) ,\
        /* Left, Right, Middle, Backward, Forward buttons */ \
        HID_REPORT_COUNT( 5                                      ) ,\
        HID_REPORT_SIZE ( 1                                      ) ,\
        HID_INPUT       ( HID_DATA | HID_VARIABLE | HID_ABSOLUTE ) ,\
        /* 3 bit padding */ \
        HID_REPORT_COUNT( 1                                      ) ,\
        HID_REPORT_SIZE ( 3                                      ) ,\
        HID_INPUT       ( HID_CONSTANT                           ) ,\
      HID_USAGE_PAGE  ( HID_USAGE_PAGE_DESKTOP )                   ,\
        /* X, Y position [0, 32767] */ \
        HID_USAGE       ( HID_USAGE_DESKTOP_X                    ) ,\
        HID_USAGE       ( HID_USAGE_DESKTOP_Y                    ) ,\
        HID_LOGICAL_MIN ( 0                                      ) ,\
        HID_LOGICAL_MAX_N ( 32767, 2                             ) ,\
        HID_REPORT_COUNT( 2                                      ) ,\
        HID_REPORT_SIZE ( 16                                     ) ,\
        HID_INPUT       ( HID_DATA | HID_VARIABLE | HID_ABSOLUTE ) ,\
        /* Verital wheel scroll [-127, 127] */ \
        HID_USAGE       ( HID_USAGE_DESKTOP_WHEEL                )  ,\
        HID_LOGICAL_MIN ( 0x81                                   )  ,\
        HID_LOGICAL_MAX ( 0x7f                                   )  ,\
        HID_REPORT_COUNT( 1                                      )  ,\
        HID_REPORT_SIZE ( 8                                      )  ,\
        HID_INPUT       ( HID_DATA | HID_VARIABLE | HID_RELATIVE )  ,\
      HID_USAGE_PAGE  ( HID_USAGE_PAGE_CONSUMER ), \
       /* Horizontal wheel scroll [-127, 127] */ \
        HID_USAGE_N     ( HID_USAGE_CONSUMER_AC_PAN, 2           ), \
        HID_LOGICAL_MIN ( 0x81                                   ), \
        HID_LOGICAL_MAX ( 0x7f                                   ), \
        HID_REPORT_COUNT( 1                                      ), \
        HID_REPORT_SIZE ( 8                                      ), \
        HID_INPUT       ( HID_DATA | HID_VARIABLE | HID_RELATIVE ), \
    HID_COLLECTION_END                                            , \
  HID_COLLECTION_END \



#define APP_BUTTON (GPIO_NUM_0) // Use BOOT signal by default
static const char *TAG = "E";

/************* TinyUSB descriptors ****************/

#define TUSB_DESC_TOTAL_LEN      (TUD_CONFIG_DESC_LEN + CFG_TUD_HID * TUD_HID_DESC_LEN)

/**
 * @brief HID report descriptor
 *
 * In this example we implement Keyboard + Mouse HID device,
 * so we must define both report descriptors
*/
const uint8_t hid_report_descriptor[] = {
        TUD_HID_REPORT_DESC_KEYBOARD(HID_REPORT_ID(HID_ITF_PROTOCOL_KEYBOARD)),
        TUD_HID_REPORT_DESC_MOUSE_ABS(HID_REPORT_ID(HID_ITF_PROTOCOL_MOUSE))
};

/**
 * @brief Configuration descriptor
 *
 * This is a simple configuration descriptor that defines 1 configuration and 1 HID interface
 */
static const uint8_t hid_configuration_descriptor[] = {
        // Configuration number, interface count, string index, total length, attribute, power in mA
        TUD_CONFIG_DESCRIPTOR(1, 1, 0, TUSB_DESC_TOTAL_LEN, TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),

        // Interface number, string index, boot protocol, report descriptor len, EP In address, size & polling interval
        TUD_HID_DESCRIPTOR(0, 0, false, sizeof(hid_report_descriptor), 0x81, 16, 10),
};

/********* TinyUSB HID callbacks ***************/

// Invoked when received GET HID REPORT DESCRIPTOR request
// Application return pointer to descriptor, whose contents must exist long enough for transfer to complete
uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance) {
    // We use only one interface and one HID report descriptor, so we can ignore parameter 'instance'
    return hid_report_descriptor;
}

// Invoked when received GET_REPORT control request
// Application must fill buffer report's content and return its length.
// Return zero will cause the stack to STALL request
uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t *buffer,
                               uint16_t reqlen) {
    (void) instance;
    (void) report_id;
    (void) report_type;
    (void) buffer;
    (void) reqlen;

    return 0;
}

// Invoked when received SET_REPORT control request or
// received data on OUT endpoint ( Report ID = 0, Type = 0 )
void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t const *buffer,
                           uint16_t bufsize) {
}


typedef struct TU_ATTR_PACKED {
    uint8_t buttons; /**< buttons mask for currently pressed buttons in the mouse. */
    uint16_t x;       /**< Current x of the mouse. */
    uint16_t y;       /**< Current y on the mouse. */
    int8_t wheel;   /**< Current delta wheel movement on the mouse. */
    int8_t pan;     // using AC Pan
} hid_abs_mouse_report_t;

static led_strip_handle_t led_strip;

static void configure_led(void) {
    ESP_LOGI(TAG, "Initialize led");
    led_strip_config_t strip_config = {
            .strip_gpio_num = GPIO_NUM_48,
            .max_leds = 1,
    };
    led_strip_rmt_config_t rmt_config = {
            .resolution_hz = 10 * 1000 * 1000, // 10MHz
    };
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip));
    led_strip_clear(led_strip);
}


static uint8_t _buttons = 0;
static uint16_t _x = 0;
static uint16_t _y = 0;

static void usb_hid_move_to_pos(uint16_t x, uint16_t y) {
    _x = x;
    _y = y;
    hid_abs_mouse_report_t report =
            {
                    .buttons = _buttons,
                    .x       = x,
                    .y       = y,
                    .wheel   = 0,
                    .pan     = 0
            };
    tud_hid_n_report(0, HID_ITF_PROTOCOL_MOUSE, &report, sizeof(report));
}

static void set_mouse_buttons(uint8_t buttons) {
    _buttons = buttons;
}

static void unset_mouse_buttons(uint8_t buttons) {
    _buttons = _buttons & ~buttons;
}

static void usb_hid_mouse_button(uint8_t buttons) {
    set_mouse_buttons(buttons);
    usb_hid_move_to_pos(_x, _y);
}

static void usb_hid_mouse_button_up(uint8_t buttons) {
    unset_mouse_buttons(buttons);
    usb_hid_move_to_pos(_x, _y);
}

static void usb_hid_mouse_wheel(int8_t scroll, int8_t pan) {
    hid_abs_mouse_report_t report =
            {
                    .buttons = _buttons,
                    .x       = _x,
                    .y       = _y,
                    .wheel   = scroll,
                    .pan     = pan
            };
    tud_hid_n_report(0, HID_ITF_PROTOCOL_MOUSE, &report, sizeof(report));
}


#define BUF_SIZE (1024)


static void handle_abs() {
    uint8_t buf[2];
    int len = uart_read_bytes(ECHO_UART_PORT_NUM, buf, 2, 0);
    if (len != 2) { return; }

    uint16_t x = buf[0] << 8 | buf[1];

    len = uart_read_bytes(ECHO_UART_PORT_NUM, buf, 2, 0);
    if (len != 2) { return; }

    uint16_t y = buf[0] << 8 | buf[1];
    ESP_LOGI(TAG, "ABS: %i %i", x, y);

    usb_hid_move_to_pos(x, y);
}

// Converts synergy mouse number to bit
static uint8_t map_mouse_button(int8_t msg) {
    int8_t button;
    switch (msg) {
        case 0:
            button = 0;
            break;
        case 1:
            button = 1;
            break;
        case 2:
            button = 3;
            break;
        case 3:
            button = 2;
            break;
        default:
            button = msg;
    }
    return button;
}

static void handle_mouse_down() {
    int len;

    uint8_t buf;
    len = uart_read_bytes(ECHO_UART_PORT_NUM, &buf, 1, 0);
    if (len != 1) { return; }

    int8_t msg_button = (int8_t) buf;


    uint8_t button = map_mouse_button(msg_button);

    ESP_LOGI(TAG, "Mouse button %i (raw %i) bits %i", button, msg_button, 1 << (button - 1));

    usb_hid_mouse_button(1 << (button - 1));
}


static void handle_mouse_up() {
    int len;

    uint8_t buf;
    len = uart_read_bytes(ECHO_UART_PORT_NUM, &buf, 1, 0);
    if (len != 1) { return; }

    int8_t msg_button = (int8_t) buf;

    uint8_t button = map_mouse_button(msg_button);

    ESP_LOGI(TAG, "Mouse button up %i (raw %i) bits %i", button, msg_button, 1 << (button - 1));

    usb_hid_mouse_button_up(1 << (button - 1));

}


//#define button_state_count 6
static uint8_t server_button_state[0x200] = {0};
static uint8_t key_report[6] = {0};

//static void debug_buttons() {
//    for (int i = 0; i < button_state_count; i++) {
//        if (button_state[i] != 0) {
//            ESP_LOGI(TAG, "Button state %i = %i", i, button_state[i]);
//        }
//    }
//}

static void handle_key_down() {
    int len;

    uint8_t buf[2];
    len = uart_read_bytes(ECHO_UART_PORT_NUM, buf, 2, 0);
    if (len != 2) { return; }
    uint16_t id = buf[0] << 8 | buf[1];

    len = uart_read_bytes(ECHO_UART_PORT_NUM, buf, 2, 0);
    if (len != 2) { return; }
//    uint16_t _modifier_mask = buf[0] << 8 | buf[1];

    len = uart_read_bytes(ECHO_UART_PORT_NUM, buf, 2, 0);
    if (len != 2) { return; }
    uint16_t button = buf[0] << 8 | buf[1];

    uint8_t key = synergy_to_hid(id);
    ESP_LOGI(TAG, ">>>> Key down");
    ESP_LOGI(TAG, "Key down: id %i button: %i key: %i", id, button, key);
    if (key == 0) {
        return;
    }
    if (server_button_state[button] == key) {
	    // Key already pressed
	    return;
    }
    server_button_state[button] = key;


    ESP_LOGI(TAG, "Got keydown for %i", key);
    for (int i = 0; i < 6; i++) {
        if (key_report[i] == 0) {
            key_report[i] = key;
            break;
        }
    }

    for (int i = 0; i < 6; i++) {
        ESP_LOGI(TAG, "Button %i = %i", i, key_report[i]);
    }
    ESP_LOGI(TAG, "<<<< Key down");
    tud_hid_keyboard_report(HID_ITF_PROTOCOL_KEYBOARD, 0, key_report);

}

static void handle_key_up() {
    int len;

    uint8_t buf[2];
    len = uart_read_bytes(ECHO_UART_PORT_NUM, buf, 2, 0);
    if (len != 2) { return; }
    uint16_t id = buf[0] << 8 | buf[1];

    len = uart_read_bytes(ECHO_UART_PORT_NUM, buf, 2, 0);
    if (len != 2) { return; }
    // uint16_t mask = buf[0] << 8 | buf[1];

    len = uart_read_bytes(ECHO_UART_PORT_NUM, buf, 2, 0);
    if (len != 2) { return; }
    uint16_t button = buf[0] << 8 | buf[1];




    ESP_LOGI(TAG, ">>>> Key up");
    uint8_t key = server_button_state[button];
    ESP_LOGI(TAG, "Key up: id %i button: %i key: %i", id, button, key);
    server_button_state[button] = 0;
    for (int i = 0; i < 6; i++) {
        if (key_report[i] == key) {
            key_report[i] = 0;
            goto done;
        }
    }
    ESP_LOGE(TAG, "Got keyup for key with no corresponding keydown? %i", key);
    done:
    for (int i = 0; i < 6; i++) {
        ESP_LOGI(TAG, "Button %i = %i", i, key_report[i]);
    }
    ESP_LOGI(TAG, "<<<< Key up");

    static uint8_t empty_key_report[6] = {0};

    tud_hid_keyboard_report(HID_ITF_PROTOCOL_KEYBOARD, 0, empty_key_report);
}

static void handle_mouse_wheel() {
    int len;

    uint8_t buf[2];
    len = uart_read_bytes(ECHO_UART_PORT_NUM, buf, 2, 0);
    if (len != 2) { return; }
    int16_t x_delta = (int16_t) (buf[0] << 8 | buf[1]);
    x_delta = x_delta / 120;

    len = uart_read_bytes(ECHO_UART_PORT_NUM, buf, 2, 0);
    if (len != 2) { return; }
    int16_t y_delta = buf[0] << 8 | buf[1];
    y_delta = y_delta / 120;

    ESP_LOGI(TAG, "Sending scroll of %i %i", x_delta, y_delta);


    usb_hid_mouse_wheel((int8_t) y_delta, (int8_t) x_delta);
}


enum PacketKind {
    kAbsMove,
    kMouseDown,
    kMouseUp,
    kKeyDownEvent,
    kKeyUpEvent,
    kMouseWheel,
    kMouseEnter,
    kMouseLeave,
};


static void read_packet() {
    uint8_t kind_buf[1];
    int len = uart_read_bytes(ECHO_UART_PORT_NUM, kind_buf, 1, 20 / portTICK_PERIOD_MS);
    if (len == 0) {
        return;
    }
    if (len != 1) {
        ESP_LOGI(TAG, "Invalid packet kind size");
        return;
    }
    uint8_t kind = kind_buf[0];


    switch (kind) {
        case kAbsMove:
            handle_abs();
            break;
        case kMouseDown:
            handle_mouse_down();
            break;
        case kMouseUp:
            handle_mouse_up();
            break;
        case kKeyDownEvent:
            handle_key_down();
            break;
        case kKeyUpEvent:
            handle_key_up();
            break;
        case kMouseWheel:
            handle_mouse_wheel();
            break;
        case kMouseEnter:
            ESP_LOGI(TAG, "Enter");
            led_strip_set_pixel(led_strip, 0, 0, 16, 0);
            led_strip_refresh(led_strip);
            break;
        case kMouseLeave:
            ESP_LOGI(TAG, "Exit");
            led_strip_set_pixel(led_strip, 0, 0, 0, 16);
            led_strip_refresh(led_strip);
            int nx;
            if (_x > POINTER_POS_MAX_VAL / 2) {
                nx = POINTER_POS_MAX_VAL;
            } else {
                nx = POINTER_POS_MIN_VAL;
            }
            ESP_LOGI(TAG, "Exiting, clipping cursor to %i %i", nx, _y);
            usb_hid_move_to_pos(nx, _y);
            break;
    }
}


static void serial_read_task(void *arg) {
    uart_config_t uart_config = {
            .baud_rate = ECHO_UART_BAUD_RATE,
            .data_bits = UART_DATA_8_BITS,
            .parity    = UART_PARITY_DISABLE,
            .stop_bits = UART_STOP_BITS_1,
            .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
            .source_clk = UART_SCLK_DEFAULT,
    };
    int intr_alloc_flags = 0;

#if CONFIG_UART_ISR_IN_IRAM
    intr_alloc_flags = ESP_INTR_FLAG_IRAM;
#endif

    ESP_ERROR_CHECK(uart_driver_install(ECHO_UART_PORT_NUM, BUF_SIZE * 2, 0, 0, NULL, intr_alloc_flags));
    ESP_ERROR_CHECK(uart_param_config(ECHO_UART_PORT_NUM, &uart_config));

    bool isMounted = false;
    bool isUnounted = false;
    while (1) {
        if (tud_connected() && tud_mounted() && !tud_suspended()) {
            read_packet();
            if (!isMounted) {
                led_strip_set_pixel(led_strip, 0, 0, 0, 16);
                led_strip_refresh(led_strip);
                isMounted = true;
                isUnounted = false;
            }
        } else {
            if (!isUnounted) {
                led_strip_set_pixel(led_strip, 0, 16, 0, 0);
                led_strip_refresh(led_strip);
                isMounted = false;
                isUnounted = true;
            }
            vTaskDelay(pdMS_TO_TICKS(50));
        }
    }
}


void app_main(void) {
    init_synergy_hid_key_table();
    configure_led();
    led_strip_set_pixel(led_strip, 0, 16, 16, 16);
    led_strip_refresh(led_strip);
    vTaskDelay(pdMS_TO_TICKS(50));

    ESP_LOGI(TAG, "USB initialization");
    const tinyusb_config_t tusb_cfg = {
            .device_descriptor = NULL,
            .string_descriptor = NULL,
            .external_phy = false,
            .configuration_descriptor = hid_configuration_descriptor,
    };

    ESP_ERROR_CHECK(tinyusb_driver_install(&tusb_cfg));
    ESP_LOGI(TAG, "USB initialization DONE");


    xTaskCreate(serial_read_task, "uart_read_task", 2048 * 4, NULL, 10, NULL);
}
