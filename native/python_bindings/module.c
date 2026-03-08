#define PY_SSIZE_T_CLEAN
#include <Python.h>

#define QUEUE_CAPACITY 32

static PyObject *event_queue[QUEUE_CAPACITY];
static int q_head = 0;
static int q_tail = 0;
static int q_size = 0;

static void queue_clear_internal(void) {
    while (q_size > 0) {
        PyObject *obj = event_queue[q_head];
        Py_XDECREF(obj);
        event_queue[q_head] = NULL;
        q_head = (q_head + 1) % QUEUE_CAPACITY;
        q_size--;
    }
    q_head = 0;
    q_tail = 0;
}

static int queue_push(PyObject *event_tuple) {
    if (q_size == QUEUE_CAPACITY) {
        return -1;
    }
    Py_INCREF(event_tuple);
    event_queue[q_tail] = event_tuple;
    q_tail = (q_tail + 1) % QUEUE_CAPACITY;
    q_size++;
    return 0;
}

static PyObject *queue_pop(void) {
    if (q_size == 0) {
        Py_RETURN_NONE;
    }
    PyObject *obj = event_queue[q_head];
    event_queue[q_head] = NULL;
    q_head = (q_head + 1) % QUEUE_CAPACITY;
    q_size--;
    return obj; // return owned ref from queue storage
}

static PyObject *py_clear_result_queue(PyObject *self, PyObject *args) {
    (void)self;
    (void)args;
    queue_clear_internal();
    Py_RETURN_NONE;
}

static PyObject *py_poll_for_result(PyObject *self, PyObject *args) {
    (void)self;
    (void)args;
    return queue_pop();
}

static PyObject *py_button_list_screen(PyObject *self, PyObject *args) {
    (void)self;
    PyObject *cfg_dict;
    if (!PyArg_ParseTuple(args, "O", &cfg_dict)) {
        return NULL;
    }

    if (!PyDict_Check(cfg_dict)) {
        PyErr_SetString(PyExc_TypeError, "cfg_dict must be a dict");
        return NULL;
    }

    PyObject *top_nav = PyDict_GetItemString(cfg_dict, "top_nav");
    PyObject *button_list = PyDict_GetItemString(cfg_dict, "button_list");
    if (top_nav == NULL || button_list == NULL) {
        PyErr_SetString(PyExc_ValueError, "cfg_dict must include 'top_nav' and 'button_list'");
        return NULL;
    }
    if (!PyDict_Check(top_nav)) {
        PyErr_SetString(PyExc_TypeError, "top_nav must be a dict");
        return NULL;
    }
    if (!PyList_Check(button_list)) {
        PyErr_SetString(PyExc_TypeError, "button_list must be a list");
        return NULL;
    }

    Py_ssize_t n = PyList_Size(button_list);
    if (n > 0) {
        PyObject *first = PyList_GetItem(button_list, 0); // borrowed
        PyObject *label = NULL;

        if (PyDict_Check(first)) {
            PyObject *raw = PyDict_GetItemString(first, "label"); // borrowed
            if (raw != NULL) {
                label = PyObject_Str(raw);
            }
        }

        if (label == NULL) {
            label = PyObject_Str(first);
        }

        if (label == NULL) {
            return NULL;
        }

        PyObject *event = PyTuple_New(3);
        if (event == NULL) {
            Py_DECREF(label);
            return NULL;
        }

        PyObject *ev_name = PyUnicode_FromString("button_selected");
        PyObject *ev_idx = PyLong_FromLong(0);
        if (ev_name == NULL || ev_idx == NULL) {
            Py_XDECREF(ev_name);
            Py_XDECREF(ev_idx);
            Py_DECREF(label);
            Py_DECREF(event);
            return NULL;
        }

        // PyTuple_SET_ITEM steals references
        PyTuple_SET_ITEM(event, 0, ev_name);
        PyTuple_SET_ITEM(event, 1, ev_idx);
        PyTuple_SET_ITEM(event, 2, label);

        if (queue_push(event) != 0) {
            Py_DECREF(event);
            PyErr_SetString(PyExc_RuntimeError, "native event queue is full");
            return NULL;
        }

        Py_DECREF(event);
    }

    Py_RETURN_NONE;
}

static PyMethodDef module_methods[] = {
    {"button_list_screen", py_button_list_screen, METH_VARARGS, "Stub button list screen entrypoint."},
    {"clear_result_queue", py_clear_result_queue, METH_NOARGS, "Clear native result queue."},
    {"poll_for_result", py_poll_for_result, METH_NOARGS, "Pop next native result tuple or None."},
    {NULL, NULL, 0, NULL},
};

static struct PyModuleDef module_def = {
    PyModuleDef_HEAD_INIT,
    "seedsigner_lvgl_native",
    "SeedSigner LVGL native CPython binding scaffold.",
    -1,
    module_methods,
};

PyMODINIT_FUNC PyInit_seedsigner_lvgl_native(void) {
    queue_clear_internal();
    return PyModule_Create(&module_def);
}
