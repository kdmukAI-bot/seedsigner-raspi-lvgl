#include <stdint.h>
#include <stddef.h>
#include <string.h>

// Minimal Stage D compiled bridge harness.
// This file intentionally models a compiled call chain boundary without
// mutating upstream seedsigner-c-modules.

typedef void (*seedsigner_button_selected_cb_t)(uint32_t index, const char *label, void *ctx);

static int extract_first_label_from_json(const char *json, char *out, size_t out_sz) {
    if (!json || !out || out_sz == 0) {
        return 0;
    }

    // Look for "label":"..." first.
    const char *label_key = "\"label\"";
    const char *p = strstr(json, label_key);
    if (p) {
        p = strchr(p + (int)strlen(label_key), ':');
        if (p) {
            while (*p == ':' || *p == ' ' || *p == '\t') p++;
            if (*p == '"') {
                p++;
                size_t i = 0;
                while (*p && *p != '"' && i + 1 < out_sz) {
                    out[i++] = *p++;
                }
                out[i] = '\0';
                return 1;
            }
        }
    }

    // Fallback: first string in button_list array, e.g. "button_list":["A", ...]
    const char *list_key = "\"button_list\"";
    p = strstr(json, list_key);
    if (p) {
        p = strchr(p, '[');
        if (p) {
            p = strchr(p, '"');
            if (p) {
                p++;
                size_t i = 0;
                while (*p && *p != '"' && i + 1 < out_sz) {
                    out[i++] = *p++;
                }
                out[i] = '\0';
                return i > 0;
            }
        }
    }

    return 0;
}

int seedsigner_lvgl_run_screen(
    const char *cfg_json,
    seedsigner_button_selected_cb_t cb,
    void *ctx
) {
    if (!cfg_json || !cb) {
        return -1;
    }

    char label[128];
    if (!extract_first_label_from_json(cfg_json, label, sizeof(label))) {
        return 0;  // no selection emitted
    }

    cb(0u, label, ctx);
    return 1;
}

int seedsigner_lvgl_run_button_list_screen_json(
    const char *cfg_json,
    seedsigner_button_selected_cb_t cb,
    void *ctx
) {
    // Stage D runtime wrapper equivalent; currently thin pass-through.
    return seedsigner_lvgl_run_screen(cfg_json, cb, ctx);
}
