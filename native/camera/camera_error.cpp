#include "camera_error.h"

// LOGS ONLY — never user-facing. The host owns translated copy keyed on the code.
const char *camera_error_str(int code) {
    switch (code) {
        case CAMERA_OK:                  return "ok";
        case CAMERA_ERR_NO_CAMERA:       return "no camera detected";
        case CAMERA_ERR_SENSOR_PROBE:    return "camera sensor probe failed";
        case CAMERA_ERR_ACQUIRE:         return "camera acquire failed";
        case CAMERA_ERR_CLOCK:           return "camera clock setup failed";
        case CAMERA_ERR_ISP:             return "camera subsystem (ISP/CSI) init failed";
        case CAMERA_ERR_MANAGER:         return "CameraManager start failed";
        case CAMERA_ERR_CONFIG_GENERATE: return "generateConfiguration failed";
        case CAMERA_ERR_CONFIG_INVALID:  return "stream configuration invalid";
        case CAMERA_ERR_CONFIG_APPLY:    return "camera configure failed";
        case CAMERA_ERR_ALLOC:           return "frame buffer allocation failed";
        case CAMERA_ERR_NO_BUFFERS:      return "no capture buffers";
        case CAMERA_ERR_MMAP:            return "buffer mmap/reqbufs failed";
        case CAMERA_ERR_REQUEST:         return "request assembly failed";
        case CAMERA_ERR_NOMEM:           return "out of memory (control/rtos primitive)";
        case CAMERA_ERR_START:           return "camera start failed";
        case CAMERA_ERR_TASK:            return "worker thread/task create failed";
        case CAMERA_ERR_DECODER:         return "QR decoder create failed";
        case CAMERA_ERR_NO_SINK:         return "no camera preview sink (build the screen first)";
        case CAMERA_ERR_NOT_SUPPORTED:   return "camera interface not supported on this board";
        case CAMERA_ERR_ALREADY_RUNNING: return "camera engine already running";
        case CAMERA_ERR_BUSY:            return "camera busy (other engine running)";
        case CAMERA_ERR_TIMEOUT:         return "camera frame timeout";
        case CAMERA_ERR_DISCONNECTED:    return "camera disconnected";
        default:                         return "unknown camera error";
    }
}

// The category band (CAMERA_CAT_*) a code belongs to. CAMERA_OK and any non-positive
// / unrecognized value map to 0.
int camera_error_category(int code) {
    if (code <= 0) {
        return 0;
    }
    return (code / 100) * 100;
}
