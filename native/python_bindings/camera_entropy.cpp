// camera_entropy — nested Python module over the native image-entropy engine, mirroring
// the ESP32 firmware's `camera_entropy` module VERBATIM (bindings/modcamera_entropy.c) so
// the app's image-entropy drive loop (run_image_entropy_screen) drives the Pi UNCHANGED.
// Attached as `seedsigner_lvgl_screens.camera_entropy`; the app aliases it into sys.modules
// so `import camera_entropy` resolves on both platforms.
//
// Contract (identical to the ESP):
//   start(seed_hash=None)   live preview + chaining begins  (Pi adds optional rotate/target_fps)
//   frames_chained()        live count for a progress indicator
//   capture()               pin exposure + latch the final frame
//   get_result()            (chain, frame, n) | None  (poll a few ms after capture)
//   resume()                discard the latch, resume chaining (reshoot)
//   stop() / is_running() / set_labels(...)
// The host computes the entropy as sha256(chain + frame); the chain EXCLUDES the latched
// final image (see helpers/mnemonic_generation.generate_mnemonic_from_camera_entropy).
//
// Only compiled/attached under the SS_CAMERA_ENGINE build (setup.py CAMERA_ENGINE=1).
#include "module_internal.h"

#include "camera_entropy_engine.h"   // native libcamera entropy engine (Python-free)
#include "camera_engine.h"           // camera_engine_is_running() — §4.11 mutual-exclusion guard
#include "entropy_coordinator.h"     // chain + latch state (get_result / frames_chained)
#include "camera_preview_sink.h"     // camera_preview_session_active / _get_sink_dims (shared sink)

#include <stdexcept>
#include <string>

// camera_entropy_phase_t ints (mirrors the portable overlay enum; kept local so this Python
// TU need not pull in lvgl.h — camera_preview.cpp owns the overlay includes).
enum { CAM_ENTROPY_PHASE_PREVIEW = 0, CAM_ENTROPY_PHASE_CAPTURING = 1, CAM_ENTROPY_PHASE_CONFIRM = 2 };

// Overlay strings: host-provided + already localized, set BEFORE start() (ESP contract).
// Persist across sessions until changed. preview/confirm_instructions are the Pi
// HARDWARE-mode (joystick) bottom lines — the ESP's touch overlay omits them; they extend
// set_labels as optional trailing args, so the ESP's 2-arg call still works verbatim.
static std::string s_capturing;
static std::string s_accept;
static std::string s_preview_instr;
static std::string s_confirm_instr;

// One-shot: the first get_result() after capture() builds the CONFIRM review image and
// flips the overlay to CONFIRM (so repeated polls don't re-trigger). Mirrors the ESP's
// s_await_confirm.
static bool s_await_confirm = false;

// start(seed_hash=None, rotate=90, target_fps=15) -> None. Optional 32-byte caller
// uniqueness seed (bytes, NOT an entropy source). Raises OSError on bring-up failure,
// matching the ESP contract (the app treats OSError from start() as a recoverable camera
// error). rotate/target_fps are Pi-only optional extras (the ESP has no analog); the
// platform-agnostic start()/start(seed) call works on both.
static PyObject *mp_camera_entropy_start(PyObject *self, PyObject *args, PyObject *kwargs) {
    (void)self;
    static const char *kwlist[] = {"seed_hash", "rotate", "target_fps", NULL};
    PyObject *seed_obj = NULL;
    int rotate = 90;
    int target_fps = 15;
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|Oii", const_cast<char **>(kwlist),
                                     &seed_obj, &rotate, &target_fps)) {
        return NULL;
    }

    Py_buffer view;
    bool have_view = false;
    const uint8_t *seed = NULL;
    size_t seed_len = 0;
    if (seed_obj && seed_obj != Py_None) {
        if (PyObject_GetBuffer(seed_obj, &view, PyBUF_SIMPLE) < 0) {
            return NULL;
        }
        have_view = true;
        if (view.len != 32) {
            PyBuffer_Release(&view);
            PyErr_SetString(PyExc_ValueError, "seed_hash must be 32 bytes");
            return NULL;
        }
        seed = static_cast<const uint8_t *>(view.buf);
        seed_len = 32;
    }

    if (camera_entropy_engine_is_running()) {
        if (have_view) PyBuffer_Release(&view);
        PyErr_SetString(PyExc_OSError, "camera entropy already running");
        return NULL;
    }
    if (camera_engine_is_running()) {
        // Refuse EARLY — before resetting the coordinator or touching the (scanner-owned)
        // session — so the error path never tears down the scanner's screen (§4.11: both
        // engines new their own CameraManager; a second one fatally aborts libcamera).
        if (have_view) PyBuffer_Release(&view);
        PyErr_SetString(PyExc_OSError, "camera busy (scanner running)");
        return NULL;
    }

    // Seed the chain BEFORE the engine starts (the blit worker begins chaining immediately).
    // entropy_coord_reset copies the seed into the SHA-256 ctx, so the buffer can be freed now.
    entropy_coord_reset(seed, seed_len);
    if (have_view) PyBuffer_Release(&view);

    s_await_confirm = false;

    // Build the entropy preview screen + overlay unless one is already active.
    try {
        if (!camera_preview_session_active()) {
            camera_entropy_build_session(s_preview_instr, s_confirm_instr, s_capturing, s_accept);
        }
    } catch (const std::exception &e) {
        PyErr_SetString(PyExc_RuntimeError, e.what());
        return NULL;
    }

    const char *err = camera_entropy_engine_start(rotate, target_fps);
    if (err) {
        camera_preview_close_session();  // roll back the screen so a retry starts clean
        PyErr_SetString(PyExc_OSError, err);
        return NULL;
    }
    Py_RETURN_NONE;
}

// stop() -> None. Stop the engine, release the camera, end the preview session. Idempotent.
static PyObject *mp_camera_entropy_stop(PyObject *self, PyObject *args) {
    (void)self;
    (void)args;
    camera_entropy_engine_stop();
    camera_preview_close_session();
    s_await_confirm = false;
    Py_RETURN_NONE;
}

// set_labels(capturing_text=None, accept_label=None, preview_instructions=None,
//            confirm_instructions=None) -> None. Supply the overlay's localized strings
// BEFORE start(). Either/any arg may be None. The ESP's 2-arg (capturing, accept) call
// works unchanged; the two trailing args are the Pi HARDWARE-mode bottom lines.
static PyObject *mp_camera_entropy_set_labels(PyObject *self, PyObject *args, PyObject *kwargs) {
    (void)self;
    static const char *kwlist[] = {"capturing_text", "accept_label",
                                   "preview_instructions", "confirm_instructions", NULL};
    const char *capturing = NULL, *accept = NULL, *preview = NULL, *confirm = NULL;
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|zzzz", const_cast<char **>(kwlist),
                                     &capturing, &accept, &preview, &confirm)) {
        return NULL;
    }
    s_capturing     = capturing ? capturing : "";
    s_accept        = accept    ? accept    : "";
    s_preview_instr = preview   ? preview   : "";
    s_confirm_instr = confirm   ? confirm   : "";
    Py_RETURN_NONE;
}

// is_running() -> bool.
static PyObject *mp_camera_entropy_is_running(PyObject *self, PyObject *args) {
    (void)self;
    (void)args;
    return PyBool_FromLong(camera_entropy_engine_is_running() ? 1 : 0);
}

// frames_chained() -> int. Live count of preview frames mixed into the chain.
static PyObject *mp_camera_entropy_frames_chained(PyObject *self, PyObject *args) {
    (void)self;
    (void)args;
    return PyLong_FromUnsignedLong(entropy_coord_frames_chained());
}

// capture() -> None. Arm the pinned-exposure capture + show the "Capturing…" transient;
// get_result() flips to CONFIRM once the frame is latched.
static PyObject *mp_camera_entropy_capture(PyObject *self, PyObject *args) {
    (void)self;
    (void)args;
    camera_entropy_engine_capture();
    camera_entropy_set_phase(CAM_ENTROPY_PHASE_CAPTURING);
    s_await_confirm = true;
    Py_RETURN_NONE;
}

// get_result() -> (chain: bytes, frame: bytes, n: int) | None. None until the latch
// completes after capture() (poll a few ms). On the first success, build the display-filling
// CONFIRM review image (color-preserving contrast stretch) and advance the overlay to CONFIRM.
static PyObject *mp_camera_entropy_get_result(PyObject *self, PyObject *args) {
    (void)self;
    (void)args;
    const uint8_t *chain = NULL, *frame = NULL;
    size_t chain_len = 0, frame_len = 0;
    uint32_t n = 0;
    if (!entropy_coord_get_result(&chain, &chain_len, &frame, &frame_len, &n)) {
        Py_RETURN_NONE;
    }
    if (s_await_confirm) {
        s_await_confirm = false;
        int sw = 0, sh = 0;
        camera_preview_get_sink_dims(&sw, &sh);
        camera_entropy_build_confirm_image(frame, sw, sh);  // DISPLAY-ONLY (not chained)
        camera_entropy_set_phase(CAM_ENTROPY_PHASE_CONFIRM);
    }
    return Py_BuildValue("(y#y#I)",
                         reinterpret_cast<const char *>(chain), (Py_ssize_t)chain_len,
                         reinterpret_cast<const char *>(frame), (Py_ssize_t)frame_len,
                         (unsigned)n);
}

// resume() -> None. Discard the latch, unfreeze/re-enable auto exposure, resume chaining.
static PyObject *mp_camera_entropy_resume(PyObject *self, PyObject *args) {
    (void)self;
    (void)args;
    camera_entropy_engine_resume();
    camera_entropy_set_phase(CAM_ENTROPY_PHASE_PREVIEW);
    s_await_confirm = false;
    Py_RETURN_NONE;
}

static PyMethodDef camera_entropy_methods[] = {
    {"start", (PyCFunction)mp_camera_entropy_start, METH_VARARGS | METH_KEYWORDS,
     "start(seed_hash=None, rotate=90, target_fps=15): bring up the native entropy preview + "
     "capture engine and begin chaining. Raises OSError on bring-up failure."},
    {"stop", mp_camera_entropy_stop, METH_NOARGS,
     "stop(): stop capture, release the camera, end the preview session. Idempotent."},
    {"set_labels", (PyCFunction)mp_camera_entropy_set_labels, METH_VARARGS | METH_KEYWORDS,
     "set_labels(capturing_text=None, accept_label=None, preview_instructions=None, "
     "confirm_instructions=None): supply the overlay's localized strings before start()."},
    {"is_running", mp_camera_entropy_is_running, METH_NOARGS,
     "is_running() -> bool: True while the entropy engine is live."},
    {"frames_chained", mp_camera_entropy_frames_chained, METH_NOARGS,
     "frames_chained() -> int: preview frames chained into the digest so far."},
    {"capture", mp_camera_entropy_capture, METH_NOARGS,
     "capture(): pin exposure + latch the final frame."},
    {"get_result", mp_camera_entropy_get_result, METH_NOARGS,
     "get_result() -> (chain, frame, n) | None: the frozen result after capture()."},
    {"resume", mp_camera_entropy_resume, METH_NOARGS,
     "resume(): discard the latch and resume chaining (reshoot)."},
    {NULL, NULL, 0, NULL},
};

static struct PyModuleDef camera_entropy_def = {
    PyModuleDef_HEAD_INIT,
    "camera_entropy",
    "Native image-entropy capture pipeline (ESP camera_entropy contract).",
    -1,
    camera_entropy_methods,
};

int camera_entropy_attach(PyObject *parent) {
    PyObject *sub = PyModule_Create(&camera_entropy_def);
    if (!sub) {
        return -1;
    }
    // PyModule_AddObject steals the ref on success; guard so a failure doesn't leak.
    if (PyModule_AddObject(parent, "camera_entropy", sub) < 0) {
        Py_DECREF(sub);
        return -1;
    }
    return 0;
}
