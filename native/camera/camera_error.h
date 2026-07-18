#ifndef SEEDSIGNER_CAMERA_ERROR_H
#define SEEDSIGNER_CAMERA_ERROR_H

// Stable, machine-readable camera error codes.
//
// The camera engines return one of these (CAMERA_OK on success); the Python
// bindings raise `OSError(code, message)` so the host branches on the numeric
// CODE (never the human message string, which is for logs only) and owns the
// translated, user-facing advice per code. Every distinct failure the engine can
// detect gets its own code even when the host collapses several to one advice
// screen today — so new host behavior never needs a native reword/refactor.
//
// Carried in OSError.errno. Values are offset well clear of POSIX errno (base
// 1000) so a stray errno read can never be mistaken for EPERM/ENOENT/etc.
//
// CROSS-PLATFORM CONTRACT: the ESP32/MicroPython firmware mirrors this enum
// VERBATIM (same names, same values) so `except OSError: if e.errno == ERR_*`
// works identically on both platforms. Keep the two in lockstep.

#ifdef __cplusplus
extern "C" {
#endif

// TWO LEVELS IN ONE NUMBER: the CATEGORY (the 100s band — what the host routes on to pick an
// advice screen) plus the specific CODE within it (what actually failed — for the log / debug
// line). category(code) == code / 100 * 100 == one of the CAMERA_CAT_* bases. The bands are the
// stable routing contract; specific codes within a band may grow without changing routing.
//
// The set is the UNION of what BOTH platforms can detect (Pi libcamera/V4L2 + ESP32
// esp_video/CSI/DVP). A [Pi]/[ESP] tag marks where a code originates; unmarked codes occur on
// both. Codes exist even where a platform currently can't produce one (e.g. the ESP pipeline
// collapses many root causes into one umbrella today) — defining the full space now means new
// host/firmware behavior never forces a native reword.
enum {
    CAMERA_OK = 0,

    // Category bases — the host routes on these (advice-screen selection).
    CAMERA_CAT_CONNECTION = 1000,  // camera hardware/link fault      -> "check the camera connection"
    CAMERA_CAT_CONFIG     = 1100,  // stream/format/decoder/capability -> system / internal error
    CAMERA_CAT_RESOURCE   = 1200,  // memory / buffers / worker tasks  -> transient, retry / restart
    CAMERA_CAT_STATE      = 1300,  // engine already running / busy    -> not user-facing
    CAMERA_CAT_RUNTIME    = 1400,  // camera lost / stalled after start -> reserved (mid-session)

    // === CONNECTION (1000s): "check the camera connection" =======================
    CAMERA_ERR_NO_CAMERA = CAMERA_CAT_CONNECTION,  // no camera enumerated / not detected / board has none
    CAMERA_ERR_SENSOR_PROBE,       // sensor on the bus but not identified (SCCB/I2C NAK, wrong chip id) [ESP]
    CAMERA_ERR_ACQUIRE,            // camera present but couldn't be opened/acquired (in use / bad state)
    CAMERA_ERR_CLOCK,              // sensor clock (XCLK / LEDC) setup failed [ESP]
    CAMERA_ERR_ISP,                // camera subsystem init failed: MIPI-CSI / ISP / LDO bring-up [ESP]
    CAMERA_ERR_MANAGER,            // camera framework manager failed to start (libcamera CameraManager) [Pi]
    CAMERA_ERR_START,              // camera->start() / VIDIOC_STREAMON failed

    // === CONFIG / capability (1100s): system / internal error ====================
    CAMERA_ERR_CONFIG_GENERATE = CAMERA_CAT_CONFIG,  // generateConfiguration() / VIDIOC_G_FMT failed / too few streams
    CAMERA_ERR_CONFIG_INVALID,     // configuration validate() == Invalid / geometry mismatch
    CAMERA_ERR_CONFIG_APPLY,       // configure() failed
    CAMERA_ERR_DECODER,            // QR decoder (zbar / quirc) create/resize failed
    CAMERA_ERR_NO_SINK,            // preview sink / camera screen / overlay not built
    CAMERA_ERR_NOT_SUPPORTED,      // camera interface unsupported on this board (e.g. DVP not yet wired) [ESP]

    // === RESOURCE (1200s): transient, retry / restart ============================
    CAMERA_ERR_ALLOC = CAMERA_CAT_RESOURCE,  // capture/display frame-buffer allocation failed (allocator / DMA / PSRAM)
    CAMERA_ERR_NO_BUFFERS,         // an allocated buffer set came back empty
    CAMERA_ERR_MMAP,               // mmap() / VIDIOC_REQBUFS failed
    CAMERA_ERR_REQUEST,            // request create / addBuffer / VIDIOC_QBUF failed
    CAMERA_ERR_NOMEM,              // out of memory for a control struct / RTOS primitive (mutex/sem) [ESP]
    CAMERA_ERR_TASK,               // a capture/decode/entropy worker thread or task failed to spawn

    // === STATE (1300s): not user-facing ==========================================
    CAMERA_ERR_ALREADY_RUNNING = CAMERA_CAT_STATE,  // this engine is already running
    CAMERA_ERR_BUSY,               // the other camera engine holds the device

    // === RUNTIME (1400s): reserved (not raised at start() — a future watchdog/poll) ===
    CAMERA_ERR_TIMEOUT = CAMERA_CAT_RUNTIME,  // no frames within the watchdog window (stall / DQBUF timeout)
    CAMERA_ERR_DISCONNECTED,       // camera lost after a successful start (sensor dropped)
};

// Short, static, English description of a code — LOGS ONLY (never user-facing;
// the host owns translated copy keyed on the code). Returns "unknown camera
// error" for an unrecognized code.
const char *camera_error_str(int code);

// The category band (a CAMERA_CAT_* base) a code belongs to — what the host routes on.
// Equals code/100*100 for any CAMERA_ERR_*; CAMERA_OK and unknown/<=0 return 0. The
// specific code + camera_error_str() carry the finer detail for a debug line.
int camera_error_category(int code);

#ifdef __cplusplus
}
#endif

// Expose every CAMERA_ERR_* as an int constant on a Python module `m`, so the host
// references e.g. `camera_scanner.ERR_NO_CAMERA` instead of a magic number. A macro
// (not a function) so this header stays Python-free for the engine translation units
// — it only expands inside the bindings, where <Python.h> is already included. Keep
// this list in sync with the enum above (and with the ESP mirror's constants).
#define CAMERA_ERROR_ADD_PY_CONSTANTS(m) do {                                      \
    /* category bands the host routes on: category = errno // 100 * 100 */          \
    PyModule_AddIntConstant((m), "CAT_CONNECTION",      CAMERA_CAT_CONNECTION);     \
    PyModule_AddIntConstant((m), "CAT_CONFIG",          CAMERA_CAT_CONFIG);         \
    PyModule_AddIntConstant((m), "CAT_RESOURCE",        CAMERA_CAT_RESOURCE);       \
    PyModule_AddIntConstant((m), "CAT_STATE",           CAMERA_CAT_STATE);          \
    PyModule_AddIntConstant((m), "CAT_RUNTIME",         CAMERA_CAT_RUNTIME);        \
    /* specific codes (the detail; carried in OSError.errno) */                     \
    PyModule_AddIntConstant((m), "ERR_NO_CAMERA",       CAMERA_ERR_NO_CAMERA);      \
    PyModule_AddIntConstant((m), "ERR_SENSOR_PROBE",    CAMERA_ERR_SENSOR_PROBE);   \
    PyModule_AddIntConstant((m), "ERR_ACQUIRE",         CAMERA_ERR_ACQUIRE);        \
    PyModule_AddIntConstant((m), "ERR_CLOCK",           CAMERA_ERR_CLOCK);          \
    PyModule_AddIntConstant((m), "ERR_ISP",             CAMERA_ERR_ISP);            \
    PyModule_AddIntConstant((m), "ERR_MANAGER",         CAMERA_ERR_MANAGER);        \
    PyModule_AddIntConstant((m), "ERR_CONFIG_GENERATE", CAMERA_ERR_CONFIG_GENERATE);\
    PyModule_AddIntConstant((m), "ERR_CONFIG_INVALID",  CAMERA_ERR_CONFIG_INVALID); \
    PyModule_AddIntConstant((m), "ERR_CONFIG_APPLY",    CAMERA_ERR_CONFIG_APPLY);   \
    PyModule_AddIntConstant((m), "ERR_ALLOC",           CAMERA_ERR_ALLOC);          \
    PyModule_AddIntConstant((m), "ERR_NO_BUFFERS",      CAMERA_ERR_NO_BUFFERS);     \
    PyModule_AddIntConstant((m), "ERR_MMAP",            CAMERA_ERR_MMAP);           \
    PyModule_AddIntConstant((m), "ERR_REQUEST",         CAMERA_ERR_REQUEST);        \
    PyModule_AddIntConstant((m), "ERR_NOMEM",           CAMERA_ERR_NOMEM);          \
    PyModule_AddIntConstant((m), "ERR_START",           CAMERA_ERR_START);          \
    PyModule_AddIntConstant((m), "ERR_TASK",            CAMERA_ERR_TASK);           \
    PyModule_AddIntConstant((m), "ERR_DECODER",         CAMERA_ERR_DECODER);        \
    PyModule_AddIntConstant((m), "ERR_NO_SINK",         CAMERA_ERR_NO_SINK);        \
    PyModule_AddIntConstant((m), "ERR_NOT_SUPPORTED",   CAMERA_ERR_NOT_SUPPORTED);  \
    PyModule_AddIntConstant((m), "ERR_ALREADY_RUNNING", CAMERA_ERR_ALREADY_RUNNING);\
    PyModule_AddIntConstant((m), "ERR_BUSY",            CAMERA_ERR_BUSY);           \
    PyModule_AddIntConstant((m), "ERR_TIMEOUT",         CAMERA_ERR_TIMEOUT);        \
    PyModule_AddIntConstant((m), "ERR_DISCONNECTED",    CAMERA_ERR_DISCONNECTED);   \
} while (0)

#endif  // SEEDSIGNER_CAMERA_ERROR_H
