// camera_scanner — nested Python module over the native camera engine, mirroring
// the ESP32 firmware's `camera_scanner` module VERBATIM (contract:
// seedsigner-micropython-builder/docs/camera-pipeline-phase2-poll-contract.md) so
// the app's scanner-injectable consumer (hardware/scan_consumer.run_scan) drives the
// Pi unchanged. Attached as `seedsigner_lvgl_screens.camera_scanner`; the app aliases
// it into sys.modules so `import camera_scanner` resolves on both platforms.
//
// Phase 1 implements the LIFECYCLE third of the contract — start()/stop()/is_running()
// — plus the FRAME_* status vocabulary. The decode-delivery surface (poll_new /
// read_status / report / report_complete) lands in Phase 2 with the native zbar
// worker; consumers already tolerate its absence.
//
// Only compiled/attached under the SS_CAMERA_ENGINE build (setup.py CAMERA_ENGINE=1),
// which links libcamera. The default build has no camera_scanner submodule.
#include "module_internal.h"

#include "camera_engine.h"
#include "camera_entropy_engine.h"  // camera_entropy_engine_is_running() — §4.11 mutual exclusion
#include "camera_preview_sink.h"  // camera_preview_session_active()
#include "scan_coordinator.h"     // NEW ring + status (Phase 2 decode delivery)

#include <stdexcept>
#include <string>
#include <vector>

// Frame-status vocabulary (mirrors the ESP scan_coordinator / Python DecodeQRStatus
// dot codes; identical ints to camera_preview_set_progress's frame_status). NOTE the
// report() arg order will be (status, percent) — the REVERSE of set_progress — when
// Phase 2 lands it; set_progress retires then so the two never coexist.
enum {
    CAM_SCAN_FRAME_NONE   = 0,
    CAM_SCAN_FRAME_NEW    = 1,
    CAM_SCAN_FRAME_REPEAT = 2,
    CAM_SCAN_FRAME_MISS   = 3,
};

// start(focus_assist=False, instructions_text=None, rotate=90, target_fps=15) -> None
// Owns the preview screen on the Pi like the ESP camera_scanner owns its overlay:
// builds the camera_preview screen (unless the caller already built one), flips it to
// the scanning status-bar state, then brings up the native capture engine. Raises
// OSError with a short reason on bring-up failure (matches the ESP contract; the app's
// run_scan_screen already treats OSError from start() as a recoverable camera error).
// `focus_assist` is accepted for contract parity and currently ignored (reserved). The
// no-arg form is the normal scan session, callable identically on both platforms.
static PyObject *mp_camera_scanner_start(PyObject *self, PyObject *args, PyObject *kwargs) {
    (void)self;
    static const char *kwlist[] = {"focus_assist", "instructions_text", "rotate", "target_fps", NULL};
    int focus_assist = 0;
    const char *instructions = NULL;
    int rotate = 90;
    int target_fps = 15;
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|pzii", const_cast<char **>(kwlist),
                                     &focus_assist, &instructions, &rotate, &target_fps)) {
        return NULL;
    }
    (void)focus_assist;  // reserved

    if (camera_engine_is_running()) {
        PyErr_SetString(PyExc_OSError, "camera scanner already running");
        return NULL;
    }
    if (camera_entropy_engine_is_running()) {
        // Both engines new their own CameraManager; libcamera fatally aborts on a second
        // one (§4.11), so never let the scanner start while entropy holds the camera.
        PyErr_SetString(PyExc_OSError, "camera busy (entropy running)");
        return NULL;
    }

    // Build the preview screen unless one is already active (the app may pre-build it
    // with localized instruction text). Screen build failures are RuntimeError (a
    // programming/runtime error), distinct from the OSError camera bring-up path.
    try {
        if (!camera_preview_session_active()) {
            camera_preview_build_session(instructions ? std::string(instructions) : std::string());
        }
    } catch (const std::exception &e) {
        PyErr_SetString(PyExc_RuntimeError, e.what());
        return NULL;
    }

    // Flip the overlay to the scanning status-bar state (subsumes set_scanning(True)).
    camera_preview_set_scanning_active(true);

    const char *err = camera_engine_start(rotate, target_fps);
    if (err) {
        // Roll back the screen we may have just built so a retry starts clean.
        camera_preview_close_session();
        PyErr_SetString(PyExc_OSError, err);
        return NULL;
    }
    Py_RETURN_NONE;
}

// stop() -> None. Stop the engine, join workers, release the camera, and end the
// preview session (drop overlay + sink). Idempotent.
static PyObject *mp_camera_scanner_stop(PyObject *self, PyObject *args) {
    (void)self;
    (void)args;
    camera_engine_stop();
    camera_preview_close_session();
    Py_RETURN_NONE;
}

// is_running() -> bool.
static PyObject *mp_camera_scanner_is_running(PyObject *self, PyObject *args) {
    (void)self;
    (void)args;
    return PyBool_FromLong(camera_engine_is_running() ? 1 : 0);
}

// _debug_stats() -> (frames_in, frames_conv, frames_pub). Lifetime counters for the
// last/current session: frames captured by the manager thread, converted by the blit
// worker, and published to the sink by the pump. Validation helper (Phase 1).
static PyObject *mp_camera_scanner_debug_stats(PyObject *self, PyObject *args) {
    (void)self;
    (void)args;
    uint64_t in = 0, conv = 0, pub = 0;
    camera_engine_stats(&in, &conv, &pub);
    return Py_BuildValue("(KKK)", (unsigned long long)in,
                         (unsigned long long)conv, (unsigned long long)pub);
}

// _debug_decode_stats() -> (attempts, hits): zbar decode passes run + passes that
// found a QR. attempts / elapsed is the decoder's frames-per-second.
static PyObject *mp_camera_scanner_debug_decode_stats(PyObject *self, PyObject *args) {
    (void)self;
    (void)args;
    uint64_t attempts = 0, hits = 0;
    camera_engine_decode_stats(&attempts, &hits);
    return Py_BuildValue("(KK)", (unsigned long long)attempts, (unsigned long long)hits);
}

// --- Phase 2 decode delivery (ESP camera_scanner poll contract) -------------

// poll_new() -> bytes | None. Drain one NEW decoded payload from the coordinator's
// ring (copied into a fresh bytes). None when empty. The consumer drains to empty
// each loop (§7a of the contract).
static PyObject *mp_camera_scanner_poll_new(PyObject *self, PyObject *args) {
    (void)self;
    (void)args;
    std::vector<uint8_t> payload;
    if (!scan_coord_poll_new(payload)) {
        Py_RETURN_NONE;
    }
    return PyBytes_FromStringAndSize(reinterpret_cast<const char *>(payload.data()),
                                     static_cast<Py_ssize_t>(payload.size()));
}

// read_status() -> structseq(latest, consecutive_misses, dropped_new, has_corners).
// A PyStructSequence (like os.stat_result) — attribute-compatible with the ESP
// MicroPython attrtuple, so the consumer's st.latest / st.consecutive_misses work
// identically on both platforms.
static PyTypeObject *s_status_type = nullptr;
static PyStructSequence_Field s_status_fields[] = {
    {const_cast<char *>("latest"),             const_cast<char *>("last frame FRAME_* classification")},
    {const_cast<char *>("consecutive_misses"), const_cast<char *>("sustained-MISS run length (Pi: always 0)")},
    {const_cast<char *>("dropped_new"),        const_cast<char *>("NEW payloads dropped on ring overflow")},
    {const_cast<char *>("has_corners"),        const_cast<char *>("reserved (Pi: always False)")},
    {nullptr, nullptr},
};
static PyStructSequence_Desc s_status_desc = {
    const_cast<char *>("camera_scanner.status"), nullptr, s_status_fields, 4,
};

static PyObject *mp_camera_scanner_read_status(PyObject *self, PyObject *args) {
    (void)self;
    (void)args;
    ScanStatus st;
    scan_coord_read_status(&st);
    PyObject *o = PyStructSequence_New(s_status_type);
    if (!o) {
        return NULL;
    }
    PyStructSequence_SetItem(o, 0, PyLong_FromLong(st.latest));
    PyStructSequence_SetItem(o, 1, PyLong_FromUnsignedLong(st.consecutive_misses));
    PyStructSequence_SetItem(o, 2, PyLong_FromUnsignedLong(st.dropped_new));
    PyStructSequence_SetItem(o, 3, PyBool_FromLong(st.has_corners ? 1 : 0));
    return o;
}

// report(status, percent) -> None. status is one of FRAME_*; percent 0..100. Drives
// the overlay dot + bar. NOTE the (status, percent) order (ESP contract) is the
// reverse of camera_preview_set_progress(percent, status).
static PyObject *mp_camera_scanner_report(PyObject *self, PyObject *args) {
    (void)self;
    int status = 0, percent = 0;
    if (!PyArg_ParseTuple(args, "ii", &status, &percent)) {
        return NULL;
    }
    camera_preview_report(status, percent);
    Py_RETURN_NONE;
}

// report_complete() -> None. Terminal: drive the bar to full + green.
static PyObject *mp_camera_scanner_report_complete(PyObject *self, PyObject *args) {
    (void)self;
    (void)args;
    camera_preview_report(CAM_SCAN_FRAME_NEW, 100);
    Py_RETURN_NONE;
}

static PyMethodDef camera_scanner_methods[] = {
    {"start", (PyCFunction)mp_camera_scanner_start, METH_VARARGS | METH_KEYWORDS,
     "start(focus_assist=False, instructions_text=None, rotate=90, target_fps=15): bring up the "
     "native camera preview + capture engine. Raises OSError on bring-up failure."},
    {"stop", mp_camera_scanner_stop, METH_NOARGS,
     "stop(): stop capture, release the camera, and end the preview session. Idempotent."},
    {"is_running", mp_camera_scanner_is_running, METH_NOARGS,
     "is_running() -> bool: True while the capture engine is live."},
    {"poll_new", mp_camera_scanner_poll_new, METH_NOARGS,
     "poll_new() -> bytes | None: drain one NEW decoded payload from the ring."},
    {"read_status", mp_camera_scanner_read_status, METH_NOARGS,
     "read_status() -> structseq(latest, consecutive_misses, dropped_new, has_corners)."},
    {"report", mp_camera_scanner_report, METH_VARARGS,
     "report(status, percent): drive the overlay dot + progress bar. status is FRAME_*."},
    {"report_complete", mp_camera_scanner_report_complete, METH_NOARGS,
     "report_complete(): terminal — drive the bar to full + green."},
    {"_debug_stats", mp_camera_scanner_debug_stats, METH_NOARGS,
     "_debug_stats() -> (frames_in, frames_conv, frames_pub): lifetime frame counters."},
    {"_debug_decode_stats", mp_camera_scanner_debug_decode_stats, METH_NOARGS,
     "_debug_decode_stats() -> (attempts, hits): zbar decode passes + QR-found passes."},
    {NULL, NULL, 0, NULL},
};

static struct PyModuleDef camera_scanner_def = {
    PyModuleDef_HEAD_INIT,
    "camera_scanner",
    "Native camera-scan pipeline (ESP camera_scanner contract; Phase 1 lifecycle).",
    -1,
    camera_scanner_methods,
};

int camera_scanner_attach(PyObject *parent) {
    PyObject *sub = PyModule_Create(&camera_scanner_def);
    if (!sub) {
        return -1;
    }
    PyModule_AddIntConstant(sub, "FRAME_NONE",   CAM_SCAN_FRAME_NONE);
    PyModule_AddIntConstant(sub, "FRAME_NEW",    CAM_SCAN_FRAME_NEW);
    PyModule_AddIntConstant(sub, "FRAME_REPEAT", CAM_SCAN_FRAME_REPEAT);
    PyModule_AddIntConstant(sub, "FRAME_MISS",   CAM_SCAN_FRAME_MISS);

    // read_status()'s attrtuple-compatible result type (built once).
    if (!s_status_type) {
        s_status_type = PyStructSequence_NewType(&s_status_desc);
        if (!s_status_type) {
            Py_DECREF(sub);
            return -1;
        }
    }

    // PyModule_AddObject steals the ref on success; guard so a failure doesn't leak.
    if (PyModule_AddObject(parent, "camera_scanner", sub) < 0) {
        Py_DECREF(sub);
        return -1;
    }
    return 0;
}
