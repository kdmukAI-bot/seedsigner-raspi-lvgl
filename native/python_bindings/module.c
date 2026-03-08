#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define RESULT_QUEUE_CAP 32
#define RESULT_LABEL_MAX 128

typedef struct {
    unsigned int index;
    char label[RESULT_LABEL_MAX];
} result_event_t;

static result_event_t s_queue[RESULT_QUEUE_CAP];
static unsigned int s_head = 0;
static unsigned int s_tail = 0;
static unsigned int s_count = 0;

static const char *s_last_path = "stub";

typedef void (*seedsigner_button_selected_cb_t)(uint32_t index, const char *label, void *ctx);
int seedsigner_lvgl_run_button_list_screen_json(
    const char *cfg_json,
    seedsigner_button_selected_cb_t cb,
    void *ctx
);

static void queue_push(unsigned int index, const char *label) {
    result_event_t ev;
    ev.index = index;
    if (!label) {
        label = "";
    }
    snprintf(ev.label, sizeof(ev.label), "%s", label);

    if (s_count == RESULT_QUEUE_CAP) {
        s_head = (s_head + 1) % RESULT_QUEUE_CAP;
        s_count--;
    }

    s_queue[s_tail] = ev;
    s_tail = (s_tail + 1) % RESULT_QUEUE_CAP;
    s_count++;
}

void seedsigner_lvgl_on_button_selected(uint32_t index, const char *label, void *ctx) {
    (void)ctx;
    queue_push((unsigned int)index, label);
}

static PyObject *py_clear_result_queue(PyObject *self, PyObject *args) {
    (void)self;
    (void)args;
    s_head = 0;
    s_tail = 0;
    s_count = 0;
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

    return Py_BuildValue("(sIs)", "button_selected", ev.index, ev.label);
}

static PyObject *py_debug_last_path(PyObject *self, PyObject *args) {
    (void)self;
    (void)args;
    return PyUnicode_FromString(s_last_path);
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

    PyObject *json_mod = PyImport_ImportModule("json");
    if (!json_mod) {
        return NULL;
    }

    PyObject *dumps_fn = PyObject_GetAttrString(json_mod, "dumps");
    Py_DECREF(json_mod);
    if (!dumps_fn) {
        return NULL;
    }

    PyObject *json_str_obj = PyObject_CallFunctionObjArgs(dumps_fn, cfg, NULL);
    Py_DECREF(dumps_fn);
    if (!json_str_obj) {
        return NULL;
    }

    const char *cfg_json = PyUnicode_AsUTF8(json_str_obj);
    if (!cfg_json) {
        Py_DECREF(json_str_obj);
        return NULL;
    }

    int rc = seedsigner_lvgl_run_button_list_screen_json(
        cfg_json,
        seedsigner_lvgl_on_button_selected,
        NULL
    );
    Py_DECREF(json_str_obj);

    if (rc < 0) {
        PyErr_SetString(PyExc_RuntimeError, "compiled button_list_screen bridge failed");
        return NULL;
    }

    s_last_path = "compiled";
    Py_RETURN_NONE;
}

static PyMethodDef methods[] = {
    {"button_list_screen", py_button_list_screen, METH_VARARGS, "Stage D compiled bridge callable."},
    {"clear_result_queue", py_clear_result_queue, METH_NOARGS, "Clear result queue."},
    {"poll_for_result", py_poll_for_result, METH_NOARGS, "Poll next result tuple or None."},
    {"_debug_last_path", py_debug_last_path, METH_NOARGS, "Debug helper: returns call path marker."},
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
