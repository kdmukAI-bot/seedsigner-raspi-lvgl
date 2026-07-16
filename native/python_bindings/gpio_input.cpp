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

// --- Held-key gate across screen transitions --------------------------------
// A key physically HELD while a screen transition takes over input must NOT
// register on the newly-loaded screen — that press was aimed at the PREVIOUS
// screen.
//
// We detect a transition as a change of the keypad indev's LVGL group — every
// screen swap repoints the group (via the screens' attach helper, the Pi
// runtime's save/restore, or the overlay manager) — and then swallow input
// until ALL keys are released, so only a FRESH press that begins after the new
// screen is up counts. Gating at the GPIO source covers repoint paths that do
// not latch lv_indev_wait_release() (e.g. py_restore_screen) and holds however
// the host drives the LVGL loop.
//
// Constraints this relies on / interplay:
// - Pointer inequality detects transitions only because screens create the new
//   group BEFORE deleting the old screen/group (load_screen_and_cleanup_previous
//   in the screens repo). A delete-then-create swap order could reuse the heap
//   address and silently defeat the check.
// - The first gated read reports RELEASED, which clears any pending
//   lv_indev_wait_release() latch (LVGL treats any non-PRESSED read as the
//   release). Safe: this gate keeps reporting RELEASED until all keys are
//   physically up, superseding the latch it defuses.
// - This protects ONLY the LVGL indev path. Screens still rendered via PIL on
//   the Pi read GPIO through the app's separate Python HardwareButtons reader
//   and are NOT covered — the 2026-07 "Address Explorer QR self-dismisses on a
//   held key" repro was that path (the Pi's QR display is still PIL; fix is
//   routing it native, tracked in the app repo's
//   docs/_integration/pi-pil-input-cutover-todo.md).
static lv_group_t *s_gate_last_group   = NULL;
static bool        s_gate_wait_release = false;

void native_input_read_cb(lv_indev_t *indev, lv_indev_data_t *data) {
    if (!s_input.ready) {
        data->state = LV_INDEV_STATE_REL;
        data->key = s_input.last_key;
        return;
    }

    // Arm the gate whenever the indev's group changes (a transition took over
    // input): whatever is held right now is a carry-over to be ignored.
    lv_group_t *cur_group = lv_indev_get_group(indev);
    if (cur_group != s_gate_last_group) {
        s_gate_last_group = cur_group;
        s_gate_wait_release = true;
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

    // Scan for the first pressed key (pin-order priority), as before.
    bool     any_pressed = false;
    uint32_t pressed_key = s_input.last_key;
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
            any_pressed = true;
            pressed_key = km.key;
            break;
        }
    }

    // Held-key gate: after a transition, report "released" while anything is
    // still held, then disarm on the first fully-released read so the next
    // physical press is seen as a fresh edge.
    if (s_gate_wait_release) {
        if (any_pressed) {
            data->key = s_input.last_key;
            data->state = LV_INDEV_STATE_REL;
            return;
        }
        s_gate_wait_release = false;
    }

    if (any_pressed) {
        s_input.last_key = pressed_key;
        data->key = pressed_key;
        data->state = LV_INDEV_STATE_PRESSED;
        return;
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
