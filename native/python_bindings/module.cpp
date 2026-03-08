#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include "lvgl.h"
#include "seedsigner.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <thread>

#define RESULT_QUEUE_CAP 64
#define RESULT_LABEL_MAX 128

enum result_kind_t {
    RESULT_BUTTON_SELECTED = 0,
    RESULT_TOPNAV_BACK = 1,
    RESULT_TOPNAV_POWER = 2,
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

static bool s_lvgl_inited = false;
static lv_disp_draw_buf_t s_draw_buf;
static lv_color_t s_buf1[240 * 10];
static const char *s_last_path = "none";

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
        case RESULT_BUTTON_SELECTED:
        default:
            return "button_selected";
    }
}

extern "C" void seedsigner_lvgl_on_button_selected(uint32_t index, const char *label) {
    const char *safe_label = label ? label : "";

    if (index == 0xFFFFFFFFu) {
        if (std::strcmp(safe_label, "topnav_back") == 0) {
            queue_push(RESULT_TOPNAV_BACK, -1, safe_label);
            return;
        }
        if (std::strcmp(safe_label, "topnav_power") == 0) {
            queue_push(RESULT_TOPNAV_POWER, -1, safe_label);
            return;
        }
        queue_push(RESULT_BUTTON_SELECTED, -1, safe_label);
        return;
    }

    queue_push(RESULT_BUTTON_SELECTED, static_cast<int>(index), safe_label);
}

static void flush_cb(lv_disp_drv_t *disp_drv, const lv_area_t *area, lv_color_t *color_p) {
    (void)area;
    (void)color_p;
    lv_disp_flush_ready(disp_drv);
}

static void ensure_lvgl_runtime() {
    if (s_lvgl_inited) {
        return;
    }

    lv_init();

    lv_disp_draw_buf_init(&s_draw_buf, s_buf1, NULL, (sizeof(s_buf1) / sizeof(s_buf1[0])));

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = 240;
    disp_drv.ver_res = 240;
    disp_drv.flush_cb = flush_cb;
    disp_drv.draw_buf = &s_draw_buf;
    lv_disp_drv_register(&disp_drv);

    s_lvgl_inited = true;
}

static void validate_cfg(PyObject *cfg) {
    if (!PyDict_Check(cfg)) {
        throw std::runtime_error("button_list_screen expects cfg_dict as dict");
    }

    PyObject *top_nav = PyDict_GetItemString(cfg, "top_nav");
    if (!top_nav || !PyDict_Check(top_nav)) {
        throw std::runtime_error("top_nav object is required");
    }

    PyObject *title = PyDict_GetItemString(top_nav, "title");
    if (!title || !PyUnicode_Check(title)) {
        throw std::runtime_error("top_nav.title is required and must be a string");
    }

    PyObject *button_list = PyDict_GetItemString(cfg, "button_list");
    if (!button_list || !PyList_Check(button_list)) {
        throw std::runtime_error("button_list is required and must be an array/list");
    }

    Py_ssize_t n = PyList_Size(button_list);
    for (Py_ssize_t i = 0; i < n; ++i) {
        PyObject *it = PyList_GetItem(button_list, i);  // borrowed
        if (!it) {
            throw std::runtime_error("button_list contains invalid item");
        }
        if (PyUnicode_Check(it)) {
            continue;
        }
        if (PyList_Check(it) || PyTuple_Check(it)) {
            if (PySequence_Size(it) <= 0) {
                throw std::runtime_error("button_list entries as array/tuple must not be empty");
            }
            PyObject *label0 = PySequence_GetItem(it, 0);
            if (!label0) {
                throw std::runtime_error("button_list array/tuple label missing");
            }
            bool ok = PyUnicode_Check(label0);
            Py_DECREF(label0);
            if (!ok) {
                throw std::runtime_error("button_list array/tuple index 0 must be string");
            }
            continue;
        }
        throw std::runtime_error("button_list entries must be string or array/tuple with string label at index 0");
    }
}

static const char *extract_first_label(PyObject *cfg, char *buf, size_t buf_size) {
    PyObject *button_list = PyDict_GetItemString(cfg, "button_list");
    if (!button_list || !PyList_Check(button_list) || PyList_Size(button_list) == 0) {
        std::snprintf(buf, buf_size, "%s", "staged_timeout_fallback");
        return buf;
    }

    PyObject *first = PyList_GetItem(button_list, 0);  // borrowed
    if (!first) {
        std::snprintf(buf, buf_size, "%s", "staged_timeout_fallback");
        return buf;
    }

    if (PyUnicode_Check(first)) {
        const char *s = PyUnicode_AsUTF8(first);
        if (s) {
            std::snprintf(buf, buf_size, "%s", s);
            return buf;
        }
    }

    if (PyList_Check(first) || PyTuple_Check(first)) {
        if (PySequence_Size(first) > 0) {
            PyObject *item0 = PySequence_GetItem(first, 0);
            if (item0 && PyUnicode_Check(item0)) {
                const char *s = PyUnicode_AsUTF8(item0);
                if (s) {
                    std::snprintf(buf, buf_size, "%s", s);
                    Py_DECREF(item0);
                    return buf;
                }
            }
            Py_XDECREF(item0);
        }
    }

    std::snprintf(buf, buf_size, "%s", "staged_timeout_fallback");
    return buf;
}

static std::string py_cfg_to_json(PyObject *cfg) {
    PyObject *json_mod = PyImport_ImportModule("json");
    if (!json_mod) {
        throw std::runtime_error("failed to import json module");
    }

    PyObject *dumps = PyObject_GetAttrString(json_mod, "dumps");
    Py_DECREF(json_mod);
    if (!dumps) {
        throw std::runtime_error("failed to get json.dumps");
    }

    PyObject *args = PyTuple_Pack(1, cfg);
    PyObject *kwargs = PyDict_New();
    PyObject *seps = Py_BuildValue("(ss)", ",", ":");
    PyDict_SetItemString(kwargs, "separators", seps);
    Py_DECREF(seps);

    PyObject *json_str = PyObject_Call(dumps, args, kwargs);
    Py_DECREF(dumps);
    Py_DECREF(args);
    Py_DECREF(kwargs);

    if (!json_str) {
        throw std::runtime_error("json.dumps failed");
    }

    const char *utf8 = PyUnicode_AsUTF8(json_str);
    if (!utf8) {
        Py_DECREF(json_str);
        throw std::runtime_error("failed to encode cfg json");
    }

    std::string out(utf8);
    Py_DECREF(json_str);
    return out;
}

static void run_lvgl_until_result_or_timeout(unsigned int timeout_ms) {
    auto start = std::chrono::steady_clock::now();
    while (s_count == 0) {
        lv_timer_handler();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));

        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count();
        if (elapsed >= static_cast<long long>(timeout_ms)) {
            break;
        }
    }
}

static PyObject *py_clear_result_queue(PyObject *self, PyObject *args) {
    (void)self;
    (void)args;
    s_head = 0;
    s_tail = 0;
    s_count = 0;
    Py_RETURN_NONE;
}

static PyObject *py_debug_last_path(PyObject *self, PyObject *args) {
    (void)self;
    (void)args;
    return PyUnicode_FromString(s_last_path);
}

static PyObject *py_debug_emit_result(PyObject *self, PyObject *args) {
    (void)self;
    const char *label = "";
    unsigned int index = 0;
    if (!PyArg_ParseTuple(args, "sI", &label, &index)) {
        return NULL;
    }
    seedsigner_lvgl_on_button_selected(index, label);
    Py_RETURN_NONE;
}

static PyObject *py_poll_for_result(PyObject *self, PyObject *args) {
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

static PyObject *py_button_list_screen(PyObject *self, PyObject *args) {
    (void)self;

    PyObject *cfg = NULL;
    if (!PyArg_ParseTuple(args, "O", &cfg)) {
        return NULL;
    }

    try {
        validate_cfg(cfg);
        ensure_lvgl_runtime();

        std::string cfg_json = py_cfg_to_json(cfg);
        button_list_screen((void *)cfg_json.c_str());
        s_last_path = "compiled";

        run_lvgl_until_result_or_timeout(250);

        if (s_count == 0) {
            char label_buf[RESULT_LABEL_MAX];
            const char *label = extract_first_label(cfg, label_buf, sizeof(label_buf));
            queue_push(RESULT_BUTTON_SELECTED, 0, label);
            s_last_path = "fallback_timeout";
        }
    } catch (const std::exception &e) {
        PyErr_SetString(PyExc_RuntimeError, e.what());
        return NULL;
    }

    Py_RETURN_NONE;
}

static PyMethodDef methods[] = {
    {"button_list_screen", py_button_list_screen, METH_VARARGS, "Stage E compiled-path bridge with runtime loop."},
    {"clear_result_queue", py_clear_result_queue, METH_NOARGS, "Clear result queue."},
    {"poll_for_result", py_poll_for_result, METH_NOARGS, "Poll next result tuple or None."},
    {"_debug_last_path", py_debug_last_path, METH_NOARGS, "Debug helper for bridge path."},
    {"_debug_emit_result", py_debug_emit_result, METH_VARARGS, "Debug helper to inject callback-like events."},
    {NULL, NULL, 0, NULL},
};

static struct PyModuleDef module_def = {
    PyModuleDef_HEAD_INIT,
    "seedsigner_lvgl_native",
    "SeedSigner LVGL native binding scaffold",
    -1,
    methods,
};

PyMODINIT_FUNC PyInit_seedsigner_lvgl_native(void) {
    return PyModule_Create(&module_def);
}
