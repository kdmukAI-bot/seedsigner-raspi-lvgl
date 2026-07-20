// stream_probe — standalone on-device libcamera probe (feasibility spec §7.1).
//
// Cross-compiled in the py312-dev build container against the extracted
// buildroot sysroot (scripts/build-camera-probe.sh) and run on the SeedSigner
// OS dev image over SSH. Its jobs:
//   1. Validate the exact toolchain + link line the Phase-1 camera engine uses
//      (container g++ + sysroot libcamera v0.3.2 + shared libstdc++).
//   2. Re-confirm the stream/format matrix from the C++ API:
//      (a) dual YUV420 480x480 + 240x240 with a centered-square ScalerCrop,
//      (b) RGB565 on the display stream (opportunistic zero-convert upgrade),
//      (c) R8/GREY on the decode stream (expected: falls back to raw Bayer).
//   3. Time uncached dmabuf access patterns (§4.9): linear memcpy to cached
//      scratch, strided rot90-pattern reads from the raw mapping vs from the
//      scratch copy, and whether DMA_BUF_IOCTL_SYNC changes the picture.
//
// Output is line-oriented "PROBE <key> = <value>" for easy transcript capture.

#include <libcamera/camera.h>
#include <libcamera/camera_manager.h>
#include <libcamera/control_ids.h>
#include <libcamera/formats.h>
#include <libcamera/framebuffer_allocator.h>
#include <libcamera/request.h>
#include <libcamera/stream.h>

#include <linux/dma-buf.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

using namespace libcamera;

static double now_ms() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

static const char *validation_name(CameraConfiguration::Status s) {
    switch (s) {
    case CameraConfiguration::Valid:    return "Valid";
    case CameraConfiguration::Adjusted: return "Adjusted";
    default:                            return "Invalid";
    }
}

// Build + validate a two-stream config and report what survived validation.
// Returns the config (possibly nullptr on Invalid) for optional capture use.
static std::unique_ptr<CameraConfiguration>
try_config(std::shared_ptr<Camera> &cam, const char *label,
           PixelFormat fmt0, Size size0, PixelFormat fmt1, Size size1) {
    std::unique_ptr<CameraConfiguration> cfg =
        cam->generateConfiguration({ StreamRole::VideoRecording, StreamRole::Viewfinder });
    if (!cfg || cfg->size() < 2) {
        printf("PROBE %s = config-generation-failed\n", label);
        return nullptr;
    }
    cfg->at(0).pixelFormat = fmt0;
    cfg->at(0).size = size0;
    cfg->at(1).pixelFormat = fmt1;
    cfg->at(1).size = size1;

    CameraConfiguration::Status st = cfg->validate();
    printf("PROBE %s.status = %s\n", label, validation_name(st));
    for (unsigned i = 0; i < 2; i++) {
        const StreamConfiguration &sc = cfg->at(i);
        printf("PROBE %s.stream%u = %s %ux%u stride=%u bufcount=%u\n", label, i,
               sc.pixelFormat.toString().c_str(), sc.size.width, sc.size.height,
               sc.stride, sc.bufferCount);
    }
    if (st == CameraConfiguration::Invalid)
        return nullptr;
    return cfg;
}

// ---- capture + timing state ------------------------------------------------

struct PlaneMap {
    uint8_t *mem = nullptr;
    size_t len = 0;
    int fd = -1;
};

static std::atomic<int> frames_done{0};
static std::atomic<uint64_t> seq_last{0};
static std::mutex req_mutex;
static std::vector<Request *> completed_reqs;

static void on_request_completed(Request *req) {
    if (req->status() == Request::RequestCancelled)
        return;
    frames_done.fetch_add(1);
    seq_last.store(req->sequence());
    std::lock_guard<std::mutex> lk(req_mutex);
    completed_reqs.push_back(req);
}

static void dmabuf_sync(int fd, bool start) {
    struct dma_buf_sync sync = {};
    sync.flags = (start ? DMA_BUF_SYNC_START : DMA_BUF_SYNC_END) | DMA_BUF_SYNC_READ;
    ioctl(fd, DMA_BUF_IOCTL_SYNC, &sync);
}

// Strided rot90-pattern read: walk the source column-major, the exact access
// pattern of a naive rotate-on-copy. Returns a checksum so the loop can't be
// optimized away.
static uint32_t strided_read(const uint8_t *src, int w, int h, int stride) {
    uint32_t acc = 0;
    for (int x = 0; x < w; x++)
        for (int y = 0; y < h; y++)
            acc += src[y * stride + x];
    return acc;
}

int main() {
    printf("PROBE build = g++ %d.%d glibc-compiled probe, libcamera link OK\n",
           __GNUC__, __GNUC_MINOR__);

    CameraManager mgr;
    if (mgr.start()) {
        printf("PROBE fatal = CameraManager start failed\n");
        return 1;
    }
    if (mgr.cameras().empty()) {
        printf("PROBE fatal = no cameras\n");
        return 1;
    }
    std::shared_ptr<Camera> cam = mgr.cameras()[0];
    printf("PROBE camera = %s\n", cam->id().c_str());
    if (cam->acquire()) {
        printf("PROBE fatal = acquire failed\n");
        return 1;
    }

    // (b) RGB565 on the display stream — firmware property, probe it first.
    try_config(cam, "rgb565_display",
               formats::YUV420, Size(480, 480), formats::RGB565, Size(240, 240));
    // (c) R8 on the decode stream.
    try_config(cam, "r8_decode",
               formats::R8, Size(480, 480), formats::YUV420, Size(240, 240));
    // (a) the primary design: dual YUV420, square.
    std::unique_ptr<CameraConfiguration> cfg =
        try_config(cam, "dual_yuv420",
                   formats::YUV420, Size(480, 480), formats::YUV420, Size(240, 240));
    if (!cfg) {
        printf("PROBE fatal = primary dual-YUV420 config invalid\n");
        cam->release();
        return 1;
    }
    if (cam->configure(cfg.get())) {
        printf("PROBE fatal = configure failed\n");
        cam->release();
        return 1;
    }

    // Centered-square ScalerCrop from the sensor's analogue crop maximum.
    const ControlInfoMap &ctrls = cam->controls();
    Rectangle crop;
    auto it = ctrls.find(&controls::ScalerCrop);
    if (it != ctrls.end()) {
        Rectangle max = it->second.max().get<Rectangle>();
        unsigned side = std::min(max.width, max.height);
        crop = Rectangle(max.x + (int)((max.width - side) / 2),
                         max.y + (int)((max.height - side) / 2), side, side);
        printf("PROBE scalercrop.max = %s\n", max.toString().c_str());
        printf("PROBE scalercrop.req = %s\n", crop.toString().c_str());
    } else {
        printf("PROBE scalercrop.max = UNAVAILABLE\n");
    }

    FrameBufferAllocator alloc(cam);
    for (unsigned i = 0; i < cfg->size(); i++) {
        if (alloc.allocate(cfg->at(i).stream()) < 0) {
            printf("PROBE fatal = buffer allocation failed (stream %u)\n", i);
            cam->release();
            return 1;
        }
    }

    // mmap every plane of the decode stream's buffers (plane 0 = Y).
    Stream *decode_stream = cfg->at(0).stream();
    const StreamConfiguration &dsc = cfg->at(0);
    std::vector<PlaneMap> ymaps;
    std::vector<std::unique_ptr<Request>> requests;
    for (const std::unique_ptr<FrameBuffer> &buf : alloc.buffers(decode_stream)) {
        const FrameBuffer::Plane &p = buf->planes()[0];
        PlaneMap m;
        m.fd = p.fd.get();
        m.len = p.length;
        m.mem = (uint8_t *)mmap(nullptr, p.length, PROT_READ, MAP_SHARED,
                                p.fd.get(), p.offset);
        if (m.mem == MAP_FAILED) {
            printf("PROBE fatal = mmap failed\n");
            cam->release();
            return 1;
        }
        ymaps.push_back(m);
    }
    printf("PROBE decode.ymap = %zu buffers, %zu bytes each\n",
           ymaps.size(), ymaps.empty() ? 0 : ymaps[0].len);

    // One request per buffer set (index-paired across streams).
    const auto &bufs0 = alloc.buffers(cfg->at(0).stream());
    const auto &bufs1 = alloc.buffers(cfg->at(1).stream());
    unsigned nreq = std::min(bufs0.size(), bufs1.size());
    for (unsigned i = 0; i < nreq; i++) {
        std::unique_ptr<Request> req = cam->createRequest(i);
        req->addBuffer(cfg->at(0).stream(), bufs0[i].get());
        req->addBuffer(cfg->at(1).stream(), bufs1[i].get());
        requests.push_back(std::move(req));
    }
    printf("PROBE requests = %u\n", nreq);

    cam->requestCompleted.connect(&on_request_completed);

    ControlList start_controls(ctrls);
    if (crop.width)
        start_controls.set(controls::ScalerCrop, crop);
    if (cam->start(&start_controls)) {
        printf("PROBE fatal = start failed\n");
        cam->release();
        return 1;
    }
    for (auto &req : requests)
        cam->queueRequest(req.get());

    // Capture ~120 frames; requeue from the main thread (poll the completed
    // list) so the completion handler stays copy/flag-only like the engine's.
    const int TARGET = 120;
    double t0 = now_ms();
    int timing_rounds = 0;
    double lin_ms = 0, lin_sync_ms = 0, strided_raw_ms = 0, strided_scratch_ms = 0;
    std::vector<uint8_t> scratch(dsc.stride * dsc.size.height);
    uint32_t sink = 0;  // defeat optimizer

    while (frames_done.load() < TARGET) {
        Request *req = nullptr;
        {
            std::lock_guard<std::mutex> lk(req_mutex);
            if (!completed_reqs.empty()) {
                req = completed_reqs.back();
                completed_reqs.pop_back();
            }
        }
        if (!req) {
            usleep(2000);
            continue;
        }

        // Timing rounds on the first few frames of buffer 0.
        if (timing_rounds < 5 && req->cookie() == 0) {
            PlaneMap &m = ymaps[0];
            int w = dsc.size.width, h = dsc.size.height, stride = dsc.stride;

            double a = now_ms();
            memcpy(scratch.data(), m.mem, (size_t)stride * h);
            double b = now_ms();
            lin_ms += b - a;

            a = now_ms();
            dmabuf_sync(m.fd, true);
            memcpy(scratch.data(), m.mem, (size_t)stride * h);
            dmabuf_sync(m.fd, false);
            b = now_ms();
            lin_sync_ms += b - a;

            a = now_ms();
            sink += strided_read(m.mem, w, h, stride);
            b = now_ms();
            strided_raw_ms += b - a;

            a = now_ms();
            sink += strided_read(scratch.data(), w, h, stride);
            b = now_ms();
            strided_scratch_ms += b - a;

            timing_rounds++;
        }

        req->reuse(Request::ReuseBuffers);
        cam->queueRequest(req);
    }
    double elapsed = now_ms() - t0;

    // Effective ScalerCrop as applied (from the last request's metadata is
    // per-request; read back the control info instead — good enough here).
    printf("PROBE capture.frames = %d in %.1f ms -> %.2f fps\n",
           TARGET, elapsed, TARGET * 1000.0 / elapsed);
    printf("PROBE capture.seq_last = %llu (gaps = drops at sensor rate)\n",
           (unsigned long long)seq_last.load());
    if (timing_rounds) {
        printf("PROBE timing.linear_memcpy = %.2f ms avg (Y %ux%u stride %u)\n",
               lin_ms / timing_rounds, dsc.size.width, dsc.size.height, dsc.stride);
        printf("PROBE timing.linear_memcpy_dmabuf_sync = %.2f ms avg\n",
               lin_sync_ms / timing_rounds);
        printf("PROBE timing.strided_rot90_raw_mmap = %.2f ms avg\n",
               strided_raw_ms / timing_rounds);
        printf("PROBE timing.strided_rot90_cached_scratch = %.2f ms avg\n",
               strided_scratch_ms / timing_rounds);
    }
    printf("PROBE checksum = %u\n", sink);

    cam->stop();
    for (PlaneMap &m : ymaps)
        munmap(m.mem, m.len);
    cam->release();
    cam.reset();
    mgr.stop();
    printf("PROBE done = OK\n");
    return 0;
}
