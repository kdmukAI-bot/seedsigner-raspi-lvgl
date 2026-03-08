#define PY_SSIZE_T_CLEAN
#include <Python.h>

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

static const char *extract_first_label(PyObject *cfg, char *buf, size_t buf_size) {
    PyObject *button_list = PyDict_GetItemString(cfg, "button_list");
    if (!button_list || !PyList_Check(button_list) || PyList_Size(button_list) == 0) {
        snprintf(buf, buf_size, "%s", "stagec_stub");
        return buf;
    }

    PyObject *first = PyList_GetItem(button_list, 0);  // borrowed
    if (!first) {
        snprintf(buf, buf_size, "%s", "stagec_stub");
        return buf;
    }

    if (PyUnicode_Check(first)) {
        const char *s = PyUnicode_AsUTF8(first);
        if (s) {
            snprintf(buf, buf_size, "%s", s);
            return buf;
        }
    }

    if (PyTuple_Check(first) && PyTuple_Size(first) >= 1) {
        PyObject *item0 = PyTuple_GetItem(first, 0);  // borrowed
        if (item0 && PyUnicode_Check(item0)) {
            const char *s = PyUnicode_AsUTF8(item0);
            if (s) {
                snprintf(buf, buf_size, "%s", s);
                return buf;
            }
        }
    }

    if (PyDict_Check(first)) {
        PyObject *label_obj = PyDict_GetItemString(first, "label");
        if (label_obj && PyUnicode_Check(label_obj)) {
            const char *s = PyUnicode_AsUTF8(label_obj);
            if (s) {
                snprintf(buf, buf_size, "%s", s);
                return buf;
            }
        }
    }

    snprintf(buf, buf_size, "%s", "stagec_stub");
    return buf;
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

    // Stage C scaffold behavior: deterministic result based on first button label.
    char label_buf[RESULT_LABEL_MAX];
    const char *label = extract_first_label(cfg, label_buf, sizeof(label_buf));
    queue_push(0, label);
    Py_RETURN_NONE;
}

static PyMethodDef methods[] = {
    {"button_list_screen", py_button_list_screen, METH_VARARGS, "Stage C stub callable."},
    {"clear_result_queue", py_clear_result_queue, METH_NOARGS, "Clear result queue."},
    {"poll_for_result", py_poll_for_result, METH_NOARGS, "Poll next result tuple or None."},
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
