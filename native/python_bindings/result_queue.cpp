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

enum result_kind_t {
    RESULT_BUTTON_SELECTED = 0,
    RESULT_TOPNAV_BACK = 1,
    RESULT_TOPNAV_POWER = 2,
    RESULT_TEXT_ENTERED = 3,
    // qr_display_screen reports its final brightness (31..255) on exit via the
    // on_qr_brightness hook, carried in the index slot so the host can persist
    // SETTING__QR_BRIGHTNESS. Emitted just before the trailing topnav_back.
    RESULT_QR_BRIGHTNESS = 4,
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
        case RESULT_TOPNAV_BACK:
            return "topnav_back";
        case RESULT_TOPNAV_POWER:
            return "topnav_power";
        case RESULT_TEXT_ENTERED:
            return "text_entered";
        case RESULT_QR_BRIGHTNESS:
            return "qr_brightness";
        case RESULT_BUTTON_SELECTED:
        default:
            return "button_selected";
    }
}

extern "C" void seedsigner_lvgl_on_button_selected(uint32_t index, const char *label) {
    const char *safe_label = label ? label : "";

    // Reserved result codes (SEEDSIGNER_RET_* in seedsigner.h) arrive in the
    // index slot; `label` is informational only. Check the reserved codes first,
    // then treat index as a 0-based body-button position — the same order
    // SeedSigner's Python Views use.
    switch (index) {
        case SEEDSIGNER_RET_BACK_BUTTON:
            queue_push(RESULT_TOPNAV_BACK, -1, safe_label);
            return;
        case SEEDSIGNER_RET_POWER_BUTTON:
            queue_push(RESULT_TOPNAV_POWER, -1, safe_label);
            return;
        case SEEDSIGNER_RET_SCREENSAVER_DISMISS:
            // No dedicated result kind; preserve the prior behavior of surfacing
            // dismiss as a button_selected with no index (label carries detail).
            queue_push(RESULT_BUTTON_SELECTED, -1, safe_label);
            return;
        case SEEDSIGNER_RET_SPLASH_COMPLETE:
            // Opening splash finished/dismissed. Host-handled lifecycle event (not a
            // Python-routed button): surface it like screensaver dismiss — a
            // button_selected with no index; the "splash_complete" label identifies it.
            queue_push(RESULT_BUTTON_SELECTED, -1, safe_label);
            return;
        default:
            queue_push(RESULT_BUTTON_SELECTED, static_cast<int>(index), safe_label);
            return;
    }
}

// Text-entry confirm callback (e.g. seed_add_passphrase_screen). Overrides the
// weak default in the screens library. The entered text is delivered as the
// result label; index is -1 (no list position). Long values are truncated to
// RESULT_LABEL_MAX by queue_push().
extern "C" void seedsigner_lvgl_on_text_entered(const char *text) {
    queue_push(RESULT_TEXT_ENTERED, -1, text ? text : "");
}

// QR brightness persistence hook (overrides the weak no-op in the screens
// library). qr_display_screen fires this on exit with its final brightness
// (31..255) so the host can persist SETTING__QR_BRIGHTNESS. Surfaced as
// ("qr_brightness", value, ""); it lands in the queue just before the screen's
// trailing topnav_back result.
extern "C" void seedsigner_lvgl_on_qr_brightness(uint8_t brightness) {
    queue_push(RESULT_QR_BRIGHTNESS, static_cast<int>(brightness), "");
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
