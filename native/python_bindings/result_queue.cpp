// Host result queue: the seedsigner_lvgl_on_* callbacks (strong overrides of the
// weak defaults in the screens library) push events here; Python drains them via
// poll_for_result(). Single-threaded by design — both push and poll happen on the
// thread that pumps LVGL.
#include "module_internal.h"

#include "seedsigner.h"

#include <cstdio>

#define RESULT_QUEUE_CAP 64
// 256 bytes accommodates a full BIP39 passphrase (text_entered results carry
// the entered string in the label field), not just short button labels.
#define RESULT_LABEL_MAX 256

// Result kinds — the shared poll-queue contract (design lead: the MicroPython
// builder, seedsigner-micropython-builder modseedsigner_bindings.c). Kinds 0..3
// mirror the ESP binding VERBATIM and both platforms return byte-identical
// (kind, index, label) tuples for them. Back/power are NOT distinct kinds; they
// arrive as button_selected carrying the RET_CODE__* sentinel in the index slot
// (see seedsigner_lvgl_on_button_selected). Kind 4 (aux_key) is the reserved next
// value in that shared numbering: only the Pi-HW-only io_test_screen emits it
// today (ESP never returns it yet), but the value is fixed so the ESP binding
// mirrors it verbatim if it ever forwards aux keys.
enum result_kind_t {
    RESULT_BUTTON_SELECTED = 0,
    RESULT_TEXT_ENTERED = 1,
    // qr_display_screen reports its final brightness (31..255) on exit via the
    // on_qr_brightness hook, carried in the index slot so the host can persist
    // SETTING__QR_BRIGHTNESS. Emitted just before the trailing back event
    // (button_selected, index 1000).
    RESULT_QR_BRIGHTNESS = 2,
    // qr_display_screen's density UI reports the chosen module scale
    // (px_per_module, 2..6) via the on_qr_density hook, carried in the index slot
    // — fired on every density change and once on exit. The host remaps
    // (vertical_resolution, px_per_module) -> max_fragment_len and restarts the
    // animated-QR fountain.
    RESULT_QR_DENSITY = 3,
    // An aux key (KEY1/KEY2/KEY3) was forwarded to the host — by io_test_screen's
    // self-owned input, or by the nav layer for a NAV_AUX_EMIT key. The key name
    // ("KEY1"/"KEY2"/"KEY3") rides in the label; index is 0. Surfaced as
    // ("aux_key", 0, "KEY1"). Pi-only consumer today (io_test is Pi-HW-only).
    RESULT_AUX_KEY = 4,
};

typedef struct {
    result_kind_t kind;
    int index;
    char label[RESULT_LABEL_MAX];
} result_event_t;

static result_event_t s_queue[RESULT_QUEUE_CAP];
static unsigned int s_head = 0;
static unsigned int s_tail = 0;
static unsigned int s_count = 0;

static void queue_push(result_kind_t kind, int index, const char *label) {
    result_event_t ev;
    ev.kind = kind;
    ev.index = index;
    if (!label) {
        label = "";
    }
    std::snprintf(ev.label, sizeof(ev.label), "%s", label);

    if (s_count == RESULT_QUEUE_CAP) {
        s_head = (s_head + 1) % RESULT_QUEUE_CAP;
        s_count--;
    }

    s_queue[s_tail] = ev;
    s_tail = (s_tail + 1) % RESULT_QUEUE_CAP;
    s_count++;
}

static const char *kind_to_event_name(result_kind_t kind) {
    switch (kind) {
        case RESULT_TEXT_ENTERED:
            return "text_entered";
        case RESULT_QR_BRIGHTNESS:
            return "qr_brightness";
        case RESULT_QR_DENSITY:
            return "qr_density";
        case RESULT_AUX_KEY:
            return "aux_key";
        case RESULT_BUTTON_SELECTED:
        default:
            return "button_selected";
    }
}

extern "C" void seedsigner_lvgl_on_button_selected(uint32_t index, const char *label) {
    // Pure pass-through, identical to the ESP binding: EVERY selection becomes one
    // button_selected event carrying the raw index. The reserved codes in
    // seedsigner.h ride in the index slot — SEEDSIGNER_RET_BACK_BUTTON (1000),
    // _POWER_BUTTON (1001), _SCREENSAVER_DISMISS (1100), _SPLASH_COMPLETE (1101) —
    // and the host distinguishes them by testing that sentinel index, NOT by a
    // distinct kind. This mirrors the MicroPython builder (the design lead) so
    // poll_for_result() returns byte-identical tuples on Pi and ESP32. `label` is
    // informational only. (queue_push tolerates a NULL label.)
    queue_push(RESULT_BUTTON_SELECTED, static_cast<int>(index), label);
}

// Text-entry confirm callback (e.g. seed_add_passphrase_screen). Overrides the
// weak default in the screens library. The entered text is delivered as the
// result label; index is 0 (matches the ESP binding; no list position). Long
// values are truncated to RESULT_LABEL_MAX by queue_push().
extern "C" void seedsigner_lvgl_on_text_entered(const char *text) {
    queue_push(RESULT_TEXT_ENTERED, 0, text ? text : "");
}

// QR brightness persistence hook (overrides the weak no-op in the screens
// library). qr_display_screen fires this on exit with its final brightness
// (31..255) so the host can persist SETTING__QR_BRIGHTNESS. Surfaced as
// ("qr_brightness", value, ""); it lands in the queue just before the screen's
// trailing back event (button_selected, index 1000).
extern "C" void seedsigner_lvgl_on_qr_brightness(uint8_t brightness) {
    queue_push(RESULT_QR_BRIGHTNESS, static_cast<int>(brightness), "");
}

// QR density hook (overrides the weak default in the screens library). The
// animated-QR density UI fires this on every px/module change AND once on exit
// (px_per_module, 2..6); the host re-resolves max_fragment_len, restarts the
// fountain via qr_display_set_frame(), and persists SETTING__QR_DENSITY.
// Surfaced as ("qr_density", value, ""). On exit the screen emits brightness,
// then density, then the back event (button_selected, index 1000) — this FIFO
// queue preserves that order.
//
// Signature verified against the screens repo (seedsigner.h):
// `void seedsigner_lvgl_on_qr_density(uint8_t px_per_module)`. Live as of the
// sources/seedsigner-lvgl-screens bump to the density-redesign main — the screen
// fires this from qr_display_screen's density slider (opt-in via density_control).
extern "C" void seedsigner_lvgl_on_qr_density(uint8_t px_per_module) {
    queue_push(RESULT_QR_DENSITY, static_cast<int>(px_per_module), "");
}

// Aux-key hook — strong override of the weak default in the screens library
// (components.cpp). io_test_screen's self-owned input forwards KEY1/KEY2/KEY3 here
// (and the nav layer forwards NAV_AUX_EMIT keys); each becomes ("aux_key", 0,
// key_name) so the host can react (io_test: KEY1 grab / KEY2 clear / KEY3 exit).
// Providing the strong override is REQUIRED, not an optimization — only the override
// routes the event into this queue. `key_name` is "KEY1"/"KEY2"/"KEY3" (queue_push
// tolerates a NULL label).
extern "C" void seedsigner_lvgl_on_aux_key(const char *key_name) {
    queue_push(RESULT_AUX_KEY, 0, key_name);
}

PyObject *py_poll_for_result(PyObject *self, PyObject *args) {
    (void)self;
    (void)args;

    if (s_count == 0) {
        Py_RETURN_NONE;
    }

    result_event_t ev = s_queue[s_head];
    s_head = (s_head + 1) % RESULT_QUEUE_CAP;
    s_count--;

    return Py_BuildValue("(sis)", kind_to_event_name(ev.kind), ev.index, ev.label);
}

PyObject *py_clear_result_queue(PyObject *self, PyObject *args) {
    (void)self;
    (void)args;
    s_head = 0;
    s_tail = 0;
    s_count = 0;
    Py_RETURN_NONE;
}

PyObject *py_debug_emit_result(PyObject *self, PyObject *args) {
    (void)self;
    const char *label = "";
    unsigned int index = 0;
    if (!PyArg_ParseTuple(args, "sI", &label, &index)) {
        return NULL;
    }
    seedsigner_lvgl_on_button_selected(index, label);
    Py_RETURN_NONE;
}

// Test helper: fire the on_qr_density callback from Python so the density result
// path is exercisable before the screens library ships a real call site. Mirrors
// _debug_emit_result for the button path.
PyObject *py_debug_emit_qr_density(PyObject *self, PyObject *args) {
    (void)self;
    unsigned int px_per_module = 0;
    if (!PyArg_ParseTuple(args, "I", &px_per_module)) {
        return NULL;
    }
    seedsigner_lvgl_on_qr_density(static_cast<uint8_t>(px_per_module));
    Py_RETURN_NONE;
}

// Test helper: fire the on_aux_key callback from Python so the aux-key result path
// is exercisable in the desktop build. The real call site (io_test_screen's keypad
// handler) is reachable only via hardware key events, so this mirrors
// _debug_emit_qr_density for the aux-key kind.
PyObject *py_debug_emit_aux_key(PyObject *self, PyObject *args) {
    (void)self;
    const char *key_name = "";
    if (!PyArg_ParseTuple(args, "s", &key_name)) {
        return NULL;
    }
    seedsigner_lvgl_on_aux_key(key_name);
    Py_RETURN_NONE;
}
