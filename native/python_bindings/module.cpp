#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include "lvgl.h"
#include "seedsigner.h"

#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>

#define RESULT_QUEUE_CAP 64
#define RESULT_LABEL_MAX 128

typedef struct {
    unsigned int index;
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

static void queue_push(unsigned int index, const char *label) {
    result_event_t ev;
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

extern "C" void seedsigner_lvgl_on_button_selected(uint32_t index, const char *label) {
    queue_push(index, label ? label : "");
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

static const char *extract_first_label(PyObject *cfg, char *buf, size_t buf_size) {
    PyObject *button_list = PyDict_GetItemString(cfg, "button_list");
    if (!button_list || !PyList_Check(button_list) || PyList_Size(button_list) == 0) {
        std::snprintf(buf, buf_size, "%s", "staged_stub");
        return buf;
    }

    PyObject *first = PyList_GetItem(button_list, 0);  // borrowed
    if (!first) {
        std::snprintf(buf, buf_size, "%s", "staged_stub");
        return buf;
    }

    if (PyUnicode_Check(first)) {
        const char *s = PyUnicode_AsUTF8(first);
        if (s) {
            std::snprintf(buf, buf_size, "%s", s);
            return buf;
        }
    }

    if (PyTuple_Check(first) && PyTuple_Size(first) >= 1) {
        PyObject *item0 = PyTuple_GetItem(first, 0);  // borrowed
        if (item0 && PyUnicode_Check(item0)) {
            const char *s = PyUnicode_AsUTF8(item0);
            if (s) {
                std::snprintf(buf, buf_size, "%s", s);
                return buf;
            }
        }
    }

    if (PyDict_Check(first)) {
        PyObject *label_obj = PyDict_GetItemString(first, "label");
        if (label_obj && PyUnicode_Check(label_obj)) {
            const char *s = PyUnicode_AsUTF8(label_obj);
            if (s) {
                std::snprintf(buf, buf_size, "%s", s);
                return buf;
            }
        }
    }

    std::snprintf(buf, buf_size, "%s", "staged_stub");
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
    PyDict_SetItemString(kwargs, "separators", Py_BuildValue("(ss)", ",", ":"));

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

static PyObject *py_poll_for_result(PyObject *self, PyObject *args) {
    (void)self;
    (void)args;

    if (s_count == 0) {
        Py_RETURN_NONE;
    }

    result_event_t ev = s_queue[s_head];
    s_head = (s_head + 1) % RESULT_QUEUE_CAP;
    s_count--;

    return Py_BuildValue("(sIs)", "button_selected", ev.index, ev.label);
}

static PyObject *py_button_list_screen(PyObject *self, PyObject *args) {
    (void)self;

    PyObject *cfg = NULL;
    if (!PyArg_ParseTuple(args, "O", &cfg)) {
        return NULL;
    }

    if (!PyDict_Check(cfg)) {
        PyErr_SetString(PyExc_TypeError, "button_list_screen expects a dict");
        return NULL;
    }

    try {
        ensure_lvgl_runtime();
        std::string cfg_json = py_cfg_to_json(cfg);
        button_list_screen((void *)cfg_json.c_str());
        s_last_path = "compiled";

        // Stage D fallback: until full input loop/runtime is wired, emit deterministic
        // first-button selection if compiled path produced no callback events.
        if (s_count == 0) {
            char label_buf[RESULT_LABEL_MAX];
            const char *label = extract_first_label(cfg, label_buf, sizeof(label_buf));
            queue_push(0, label);
        }
    } catch (const std::exception &e) {
        PyErr_SetString(PyExc_RuntimeError, e.what());
        return NULL;
    }

    Py_RETURN_NONE;
}

static PyMethodDef methods[] = {
    {"button_list_screen", py_button_list_screen, METH_VARARGS, "Stage D compiled-path bridge."},
    {"clear_result_queue", py_clear_result_queue, METH_NOARGS, "Clear result queue."},
    {"poll_for_result", py_poll_for_result, METH_NOARGS, "Poll next result tuple or None."},
    {"_debug_last_path", py_debug_last_path, METH_NOARGS, "Debug helper for bridge path."},
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
