// GPIO input backend: joystick (up/down/left/right/press) + KEY1/KEY2/KEY3,
// active-low with pull-up, claimed from the gpiochip owned by display_st7789.cpp.
// native_input_read_cb is the LVGL keypad indev callback the runtime registers.
#include "module_internal.h"

#include <cstdio>

typedef struct {
    int up_pin;
    int down_pin;
    int left_pin;
    int right_pin;
    int press_pin;
    int key1_pin;
    int key2_pin;
    int key3_pin;
    int up_fd;
    int down_fd;
    int left_fd;
    int right_fd;
    int press_fd;
    int key1_fd;
    int key2_fd;
    int key3_fd;
    bool ready;
    uint32_t last_key;
} native_input_t;

static native_input_t s_input = {
    .up_pin = 6,
    .down_pin = 19,
    .left_pin = 5,
    .right_pin = 26,
    .press_pin = 13,
    .key1_pin = 21,
    .key2_pin = 20,
    .key3_pin = 16,
    .up_fd = -1,
    .down_fd = -1,
    .left_fd = -1,
    .right_fd = -1,
    .press_fd = -1,
    .key1_fd = -1,
    .key2_fd = -1,
    .key3_fd = -1,
    .ready = false,
    .last_key = LV_KEY_ENTER,
};

void native_input_shutdown() {
    if (s_input.up_fd >= 0) { close(s_input.up_fd); s_input.up_fd = -1; }
    if (s_input.down_fd >= 0) { close(s_input.down_fd); s_input.down_fd = -1; }
    if (s_input.left_fd >= 0) { close(s_input.left_fd); s_input.left_fd = -1; }
    if (s_input.right_fd >= 0) { close(s_input.right_fd); s_input.right_fd = -1; }
    if (s_input.press_fd >= 0) { close(s_input.press_fd); s_input.press_fd = -1; }
    if (s_input.key1_fd >= 0) { close(s_input.key1_fd); s_input.key1_fd = -1; }
    if (s_input.key2_fd >= 0) { close(s_input.key2_fd); s_input.key2_fd = -1; }
    if (s_input.key3_fd >= 0) { close(s_input.key3_fd); s_input.key3_fd = -1; }
    s_input.ready = false;
}

void native_input_init() {
    if (!native_gpiochip_ready()) {
        fprintf(stderr, "[seedsigner_lvgl_screens] input init skipped: gpiochip not ready\n");
        return;
    }
    native_input_shutdown();
    int chip_fd = native_gpio_chip_fd();
    s_input.up_fd = gpiochip_request_input_line(chip_fd, s_input.up_pin, "sslvgl-in-up");
    s_input.down_fd = gpiochip_request_input_line(chip_fd, s_input.down_pin, "sslvgl-in-down");
    s_input.left_fd = gpiochip_request_input_line(chip_fd, s_input.left_pin, "sslvgl-in-left");
    s_input.right_fd = gpiochip_request_input_line(chip_fd, s_input.right_pin, "sslvgl-in-right");
    s_input.press_fd = gpiochip_request_input_line(chip_fd, s_input.press_pin, "sslvgl-in-press");
    s_input.key1_fd = gpiochip_request_input_line(chip_fd, s_input.key1_pin, "sslvgl-in-key1");
    s_input.key2_fd = gpiochip_request_input_line(chip_fd, s_input.key2_pin, "sslvgl-in-key2");
    s_input.key3_fd = gpiochip_request_input_line(chip_fd, s_input.key3_pin, "sslvgl-in-key3");
    s_input.ready = true;
    fprintf(stderr, "[seedsigner_lvgl_screens] input init OK: 8 lines open (pull-up requested)\n");
}

// Open gpiochip and initialize only input lines (no display GPIO).
// For use when display is owned by an external driver (e.g. SeedSigner's Python ST7789).
void native_input_only_init() {
    if (s_input.ready) {
        return;  // already initialized
    }
    native_gpio_chip_open_for_input();
    native_input_init();
}

void native_input_read_cb(lv_indev_t *indev, lv_indev_data_t *data) {
    (void)indev;
    if (!s_input.ready) {
        data->state = LV_INDEV_STATE_REL;
        data->key = s_input.last_key;
        return;
    }

    struct key_map_entry_t { int fd; uint32_t key; };
    const key_map_entry_t keys[] = {
        {s_input.up_fd, LV_KEY_UP},
        {s_input.down_fd, LV_KEY_DOWN},
        {s_input.left_fd, LV_KEY_LEFT},
        {s_input.right_fd, LV_KEY_RIGHT},
        {s_input.press_fd, LV_KEY_ENTER},
        {s_input.key1_fd, (uint32_t)'1'},
        {s_input.key2_fd, (uint32_t)'2'},
        {s_input.key3_fd, (uint32_t)'3'},
    };

    for (const auto &km : keys) {
        if (km.fd < 0) {
            continue;
        }
        int v = 1;
        try {
            v = gpio_line_read(km.fd);
        } catch (...) {
            continue;
        }
        if (v == 0) {  // active-low
            s_input.last_key = km.key;
            data->key = km.key;
            data->state = LV_INDEV_STATE_PRESSED;
            return;
        }
    }

    data->key = s_input.last_key;
    data->state = LV_INDEV_STATE_REL;
}

PyObject *py_native_input_init(PyObject *self, PyObject *args) {
    (void)self;
    (void)args;
    try {
        native_input_only_init();
    } catch (const std::exception &e) {
        PyErr_SetString(PyExc_RuntimeError, e.what());
        return NULL;
    }
    Py_RETURN_NONE;
}
