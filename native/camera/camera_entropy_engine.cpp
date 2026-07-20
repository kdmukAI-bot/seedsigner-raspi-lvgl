// camera_entropy_engine — native libcamera engine for the image-entropy flow. See
// camera_entropy_engine.h. Structured like camera_engine.cpp (manager thread does
// copy/flag/requeue-only; a blit worker converts + publishes; two mutexes so the manager
// thread never blocks the pump) but with ONE stream and, instead of a zbar decode worker,
// the blit worker chains each preview frame into the entropy digest. Capture pins the
// converged exposure/AWB and latches a stabilized frame (the "2B" path).
#include "camera_entropy_engine.h"
#include "camera_config.h"
#include "camera_error.h"

#include "camera_engine.h"          // camera_engine_is_running() — mutual-exclusion guard (§4.11)
#include "camera_preview_sink.h"    // shared RGB565 sink (session/dims/blit)
#include "entropy_coordinator.h"    // chain + latch state

#include <libcamera/camera.h>
#include <libcamera/camera_manager.h>
#include <libcamera/control_ids.h>
#include <libcamera/controls.h>
#include <libcamera/formats.h>
#include <libcamera/framebuffer_allocator.h>
#include <libcamera/geometry.h>
#include <libcamera/request.h>
#include <libcamera/stream.h>

#include <sys/mman.h>

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

using namespace libcamera;

namespace {

constexpr unsigned kBufferCount = 4;
// Frames to wait after arming before latching, so the pinned exposure/AWB has propagated
// through the ISP pipeline (a few frames of latency). The live AE was already converged,
// so this is just settle-the-lock time, not a convergence wait.
constexpr uint64_t kPinSettle = 3;
// Frames to actively re-enable auto AE/AWB after a reshoot before dropping the override.
constexpr int kReenableFrames = 3;

enum CapPhase { CAP_NONE = 0, CAP_PINNING = 1, CAP_LATCHED = 2 };

struct PlaneMap {
    void  *mem = nullptr;
    size_t len = 0;
};

struct Engine {
    std::unique_ptr<CameraManager>        manager;
    std::shared_ptr<Camera>               camera;
    std::unique_ptr<CameraConfiguration>  config;
    std::unique_ptr<FrameBufferAllocator> allocator;
    Stream *stream = nullptr;
    std::vector<std::unique_ptr<Request>> requests;
    std::map<int, PlaneMap>               maps;

    // preview frame geometry (== sink dims)
    int disp_w = 0, disp_h = 0;
    int disp_ystride = 0, disp_uvstride = 0;
    size_t disp_u_off = 0, disp_v_off = 0, disp_total = 0;
    int rotate = 90;

    // manager thread <-> blit worker
    std::mutex               in_mtx;
    std::condition_variable  in_cv;
    std::vector<uint8_t>     scratch;
    uint64_t                 in_gen = 0;

    // blit worker <-> pump
    std::mutex               out_mtx;
    std::vector<uint8_t>     ready_buf;
    bool                     dirty = false;

    std::thread              blit;
    std::atomic<bool>        running{false};
    std::atomic<uint64_t>    conv_count{0};   // converted frames (drives the pin-settle count)

    // latest converged 3A, updated by the manager thread each frame (for capture()'s pin)
    std::mutex meta_mtx;
    bool    have_meta = false, have_meta_cg = false;
    int32_t m_exp = 0;
    float   m_gain = 1.0f, m_rg = 1.0f, m_bg = 1.0f;

    // capture state (§ blit worker latches; manager thread applies the pin controls)
    std::mutex cap_mtx;
    CapPhase cap_phase = CAP_NONE;
    bool     have_pin = false, pin_has_cg = false;
    int32_t  pin_exp = 0;
    float    pin_gain = 1.0f, pin_rg = 1.0f, pin_bg = 1.0f;
    uint64_t converted_at_capture = 0;
    int      reenable_frames = 0;
};

Engine *g = nullptr;

// BT.601 studio-range YUV420 -> RGB565 with 0/90/180/270 rotation. Identical to the scan
// engine's convert (kept local so the two engines stay independent; correctness-sensitive
// so copied verbatim rather than re-derived).
void convert_yuv420_rgb565(const uint8_t *buf, int src_w, int src_h,
                           int y_stride, int uv_stride,
                           size_t u_off, size_t v_off, int rotate,
                           uint16_t *dst, int dst_w) {
    const uint8_t *Yp = buf;
    const uint8_t *Up = buf + u_off;
    const uint8_t *Vp = buf + v_off;

    for (int sy = 0; sy < src_h; ++sy) {
        const uint8_t *yrow = Yp + static_cast<size_t>(sy) * y_stride;
        const uint8_t *urow = Up + static_cast<size_t>(sy >> 1) * uv_stride;
        const uint8_t *vrow = Vp + static_cast<size_t>(sy >> 1) * uv_stride;
        for (int sx = 0; sx < src_w; ++sx) {
            const int c = 298 * (static_cast<int>(yrow[sx]) - 16);
            const int u = static_cast<int>(urow[sx >> 1]) - 128;
            const int v = static_cast<int>(vrow[sx >> 1]) - 128;
            int r = (c + 409 * v + 128) >> 8;
            int gg = (c - 100 * u - 208 * v + 128) >> 8;
            int b = (c + 516 * u + 128) >> 8;
            r  = r  < 0 ? 0 : (r  > 255 ? 255 : r);
            gg = gg < 0 ? 0 : (gg > 255 ? 255 : gg);
            b  = b  < 0 ? 0 : (b  > 255 ? 255 : b);
            const uint16_t px = static_cast<uint16_t>(
                ((r & 0xF8) << 8) | ((gg & 0xFC) << 3) | (b >> 3));

            int dx, dy;
            switch (rotate) {
                case 90:  dx = src_h - 1 - sy; dy = sx;             break;
                case 180: dx = src_w - 1 - sx; dy = src_h - 1 - sy; break;
                case 270: dx = sy;             dy = src_w - 1 - sx; break;
                default:  dx = sx;             dy = sy;             break;  // 0
            }
            dst[static_cast<size_t>(dy) * dst_w + dx] = px;
        }
    }
}

// --- blit worker ------------------------------------------------------------
void blit_worker() {
    std::vector<uint8_t> local_in(g->disp_total);
    std::vector<uint8_t> work(static_cast<size_t>(g->disp_w) * g->disp_h * 2);
    uint64_t last_gen = 0;

    while (g->running.load(std::memory_order_acquire)) {
        {
            std::unique_lock<std::mutex> lk(g->in_mtx);
            g->in_cv.wait(lk, [&] {
                return !g->running.load(std::memory_order_acquire) || g->in_gen != last_gen;
            });
            if (!g->running.load(std::memory_order_acquire)) {
                break;
            }
            last_gen = g->in_gen;
            std::memcpy(local_in.data(), g->scratch.data(), g->disp_total);
        }

        convert_yuv420_rgb565(local_in.data(), g->disp_w, g->disp_h,
                              g->disp_ystride, g->disp_uvstride,
                              g->disp_u_off, g->disp_v_off, g->rotate,
                              reinterpret_cast<uint16_t *>(work.data()), g->disp_w);
        const uint64_t cc = g->conv_count.fetch_add(1, std::memory_order_relaxed) + 1;

        CapPhase ph;
        uint64_t cap_at;
        {
            std::lock_guard<std::mutex> lk(g->cap_mtx);
            ph = g->cap_phase;
            cap_at = g->converted_at_capture;
        }

        if (ph == CAP_PINNING && (cc - cap_at) >= kPinSettle) {
            // The pinned-exposure frame is ready — latch it as the final image.
            entropy_coord_latch(work.data(), work.size());
            std::lock_guard<std::mutex> lk(g->cap_mtx);
            if (g->cap_phase == CAP_PINNING) {
                g->cap_phase = CAP_LATCHED;
            }
        } else if (ph == CAP_NONE) {
            entropy_coord_chain_frame(work.data(), work.size());
        }

        // Publish for display unless the frame is frozen on the latched image. The frame
        // that triggers the latch still read ph==CAP_PINNING above, so it is published once.
        if (ph != CAP_LATCHED) {
            std::lock_guard<std::mutex> lk(g->out_mtx);
            g->ready_buf.swap(work);
            g->dirty = true;
            // `work` now holds the previous ready_buf storage (right size) — reused.
        }
    }
}

// --- completion handler (libcamera manager thread) --------------------------
void on_request_completed(Request *req) {
    if (!g || !g->running.load(std::memory_order_acquire) ||
        req->status() == Request::RequestCancelled) {
        return;
    }

    // Track the latest converged 3A so capture() can pin it.
    {
        const ControlList &md = req->metadata();
        auto exp  = md.get(controls::ExposureTime);
        auto gain = md.get(controls::AnalogueGain);
        auto cg   = md.get(controls::ColourGains);
        std::lock_guard<std::mutex> lk(g->meta_mtx);
        if (exp)  g->m_exp  = *exp;
        if (gain) g->m_gain = *gain;
        if (cg) { g->m_rg = (*cg)[0]; g->m_bg = (*cg)[1]; g->have_meta_cg = true; }
        if (exp && gain) g->have_meta = true;
    }

    // Copy the preview YUV420 buffer out of the uncached dmabuf into cached scratch.
    const Request::BufferMap &bufs = req->buffers();
    auto it = bufs.find(g->stream);
    if (it != bufs.end()) {
        const FrameBuffer::Plane &p0 = it->second->planes()[0];
        auto mit = g->maps.find(p0.fd.get());
        if (mit != g->maps.end()) {
            const uint8_t *src = static_cast<const uint8_t *>(mit->second.mem) + p0.offset;
            {
                std::lock_guard<std::mutex> lk(g->in_mtx);
                std::memcpy(g->scratch.data(), src, g->disp_total);
                g->in_gen++;
            }
            g->in_cv.notify_one();
        }
    }

    if (!g->running.load(std::memory_order_acquire)) {
        return;
    }
    // reuse() FIRST — it clears the request's control list — then apply the per-request
    // capture controls: pin exposure/AWB while capturing, or actively re-enable auto for a
    // few frames after a reshoot. (Setting controls before reuse() would be wiped.)
    req->reuse(Request::ReuseBuffers);
    {
        CapPhase ph;
        bool hp, hcg;
        int32_t exp;
        float gain, rg, bg;
        bool reenable = false;
        {
            std::lock_guard<std::mutex> lk(g->cap_mtx);
            ph = g->cap_phase;
            hp = g->have_pin; hcg = g->pin_has_cg;
            exp = g->pin_exp; gain = g->pin_gain; rg = g->pin_rg; bg = g->pin_bg;
            if (ph == CAP_NONE && g->reenable_frames > 0) {
                reenable = true;
                g->reenable_frames--;
            }
        }
        if ((ph == CAP_PINNING || ph == CAP_LATCHED) && hp) {
            req->controls().set(controls::AeEnable, false);
            req->controls().set(controls::ExposureTime, exp);
            req->controls().set(controls::AnalogueGain, gain);
            if (hcg) {
                req->controls().set(controls::AwbEnable, false);
                req->controls().set(controls::ColourGains, Span<const float, 2>({rg, bg}));
            }
        } else if (reenable) {
            req->controls().set(controls::AeEnable, true);
            req->controls().set(controls::AwbEnable, true);
        }
    }
    g->camera->queueRequest(req);
}

int bringup_failed(int code) {
    if (g) {
        if (g->camera) {
            g->camera->stop();
            g->camera->release();
        }
        for (auto &kv : g->maps) {
            munmap(kv.second.mem, kv.second.len);
        }
        g->camera.reset();
        if (g->manager) {
            g->manager->stop();
        }
        delete g;
        g = nullptr;
    }
    return code;
}

}  // namespace

int camera_entropy_engine_start() {
    if (g) {
        return CAMERA_ERR_ALREADY_RUNNING;
    }
    if (camera_engine_is_running()) {
        return CAMERA_ERR_BUSY;
    }
    if (!camera_preview_session_active()) {
        return CAMERA_ERR_NO_SINK;
    }

    g = new Engine();
    // Same sticky device setting the scan engine reads (camera_config.h), so both
    // flows honour one rotation.
    g->rotate = camera_config_effective_rotation();
    g->conv_count.store(0);
    camera_preview_get_sink_dims(&g->disp_w, &g->disp_h);

    g->manager = std::make_unique<CameraManager>();
    if (g->manager->start()) {
        return bringup_failed(CAMERA_ERR_MANAGER);
    }
    if (g->manager->cameras().empty()) {
        return bringup_failed(CAMERA_ERR_NO_CAMERA);
    }
    g->camera = g->manager->cameras()[0];
    if (g->camera->acquire()) {
        return bringup_failed(CAMERA_ERR_ACQUIRE);
    }

    g->config = g->camera->generateConfiguration({ StreamRole::Viewfinder });
    if (!g->config || g->config->empty()) {
        return bringup_failed(CAMERA_ERR_CONFIG_GENERATE);
    }
    g->config->at(0).pixelFormat = formats::YUV420;
    g->config->at(0).size = Size(g->disp_w, g->disp_h);
    g->config->at(0).bufferCount = kBufferCount;
    if (g->config->validate() == CameraConfiguration::Invalid) {
        return bringup_failed(CAMERA_ERR_CONFIG_INVALID);
    }
    if (g->camera->configure(g->config.get())) {
        return bringup_failed(CAMERA_ERR_CONFIG_APPLY);
    }
    g->stream = g->config->at(0).stream();
    g->disp_ystride = g->config->at(0).stride;

    g->allocator = std::make_unique<FrameBufferAllocator>(g->camera);
    if (g->allocator->allocate(g->stream) < 0) {
        return bringup_failed(CAMERA_ERR_ALLOC);
    }

    const auto &dbufs = g->allocator->buffers(g->stream);
    if (dbufs.empty()) {
        return bringup_failed(CAMERA_ERR_NO_BUFFERS);
    }
    {
        const auto &planes = dbufs[0]->planes();
        const size_t base = planes[0].offset;
        g->disp_total   = planes.back().offset + planes.back().length - base;
        g->disp_u_off   = planes[1].offset - base;
        g->disp_v_off   = planes[2].offset - base;
        g->disp_uvstride = static_cast<int>(planes[1].length / (g->disp_h / 2));
        g->scratch.assign(g->disp_total, 0);
        g->ready_buf.assign(static_cast<size_t>(g->disp_w) * g->disp_h * 2, 0);
    }
    for (const std::unique_ptr<FrameBuffer> &buf : dbufs) {
        const FrameBuffer::Plane &p0 = buf->planes()[0];
        int fd = p0.fd.get();
        if (g->maps.count(fd)) {
            continue;
        }
        size_t len = buf->planes().back().offset + buf->planes().back().length;
        void *mem = mmap(nullptr, len, PROT_READ, MAP_SHARED, fd, 0);
        if (mem == MAP_FAILED) {
            return bringup_failed(CAMERA_ERR_MMAP);
        }
        g->maps[fd] = PlaneMap{ mem, len };
    }

    for (unsigned i = 0; i < dbufs.size(); ++i) {
        std::unique_ptr<Request> req = g->camera->createRequest(i);
        if (!req || req->addBuffer(g->stream, dbufs[i].get()) < 0) {
            return bringup_failed(CAMERA_ERR_REQUEST);
        }
        g->requests.push_back(std::move(req));
    }

    g->camera->requestCompleted.connect(&on_request_completed);

    // Centered-square ScalerCrop -> undistorted square FOV (same as the scan engine).
    ControlList controls(g->camera->controls());
    const ControlInfoMap &cinfo = g->camera->controls();
    auto sc = cinfo.find(&controls::ScalerCrop);
    if (sc != cinfo.end()) {
        Rectangle max = sc->second.max().get<Rectangle>();
        unsigned side = std::min(max.width, max.height);
        controls.set(controls::ScalerCrop,
                     Rectangle(max.x + static_cast<int>((max.width - side) / 2),
                               max.y + static_cast<int>((max.height - side) / 2),
                               side, side));
    }
    {
        int64_t fd = 1000000 / CAMERA_TARGET_FPS;
        controls.set(controls::FrameDurationLimits, Span<const int64_t, 2>({ fd, fd }));
    }

    // The optional caller seed is handed to the coordinator by the binding via
    // entropy_coord_reset() BEFORE start(); nothing to seed here.

    g->running.store(true, std::memory_order_release);
    g->blit = std::thread(blit_worker);

    if (g->camera->start(&controls)) {
        g->running.store(false, std::memory_order_release);
        g->in_cv.notify_all();
        if (g->blit.joinable()) {
            g->blit.join();
        }
        g->camera->requestCompleted.disconnect(&on_request_completed);
        return bringup_failed(CAMERA_ERR_START);
    }
    for (auto &req : g->requests) {
        g->camera->queueRequest(req.get());
    }
    return CAMERA_OK;
}

void camera_entropy_engine_stop() {
    if (!g) {
        return;
    }
    g->running.store(false, std::memory_order_release);
    if (g->camera) {
        g->camera->stop();
        g->camera->requestCompleted.disconnect(&on_request_completed);
    }
    g->in_cv.notify_all();
    if (g->blit.joinable()) {
        g->blit.join();
    }
    for (auto &kv : g->maps) {
        munmap(kv.second.mem, kv.second.len);
    }
    g->maps.clear();
    g->requests.clear();
    g->allocator.reset();
    if (g->camera) {
        g->camera->release();
        g->camera.reset();
    }
    g->config.reset();
    if (g->manager) {
        g->manager->stop();
        g->manager.reset();
    }
    delete g;
    g = nullptr;
    entropy_coord_wipe();
}

bool camera_entropy_engine_is_running() {
    return g != nullptr && g->running.load(std::memory_order_acquire);
}

void camera_entropy_engine_pump_consume() {
    if (!g) {
        return;
    }
    std::lock_guard<std::mutex> lk(g->out_mtx);
    if (g->dirty) {
        camera_preview_blit_rgb565(g->ready_buf.data(), g->ready_buf.size());
        g->dirty = false;
    }
}

void camera_entropy_engine_capture() {
    if (!g || !g->running.load(std::memory_order_acquire)) {
        return;
    }
    // Snapshot the converged 3A (outside cap_mtx to avoid nested locking).
    bool have_meta, have_cg;
    int32_t exp;
    float gain, rg, bg;
    {
        std::lock_guard<std::mutex> lk(g->meta_mtx);
        have_meta = g->have_meta;
        have_cg   = g->have_meta_cg;
        exp = g->m_exp; gain = g->m_gain; rg = g->m_rg; bg = g->m_bg;
    }
    {
        std::lock_guard<std::mutex> lk(g->cap_mtx);
        if (g->cap_phase != CAP_NONE) {
            return;  // already capturing
        }
        g->have_pin   = have_meta;
        g->pin_has_cg = have_meta && have_cg;
        g->pin_exp = exp; g->pin_gain = gain; g->pin_rg = rg; g->pin_bg = bg;
        g->converted_at_capture = g->conv_count.load(std::memory_order_relaxed);
        g->cap_phase = CAP_PINNING;
    }
    entropy_coord_arm();
}

void camera_entropy_engine_resume() {
    if (!g || !g->running.load(std::memory_order_acquire)) {
        return;
    }
    {
        std::lock_guard<std::mutex> lk(g->cap_mtx);
        g->cap_phase = CAP_NONE;
        g->have_pin = false;
        g->pin_has_cg = false;
        g->reenable_frames = kReenableFrames;
    }
    entropy_coord_resume();
}
