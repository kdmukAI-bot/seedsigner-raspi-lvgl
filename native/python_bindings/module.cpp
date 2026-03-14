#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include "lvgl.h"
#include "seedsigner.h"
#include "input_profile.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <thread>
#include <cstdint>
#include <cerrno>
#include <vector>

#include <fcntl.h>
#include <linux/spi/spidev.h>
#include <linux/gpio.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define RESULT_QUEUE_CAP 64
#define RESULT_LABEL_MAX 128

enum result_kind_t {
    RESULT_BUTTON_SELECTED = 0,
    RESULT_TOPNAV_BACK = 1,
    RESULT_TOPNAV_POWER = 2,
};

typedef struct {
    result_kind_t kind;
    int index;
    char label[RESULT_LABEL_MAX];
} result_event_t;

static result_event_t s_queue[RESULT_QUEUE_CAP];
static unsigned int s_head = 0;
static unsigned int s_tail = 0;
static unsigned int s_count = 0;

static bool s_lvgl_inited = false;
static lv_disp_draw_buf_t s_draw_buf;
static lv_color_t s_buf1[240 * 10];
static lv_indev_t *s_input_indev = NULL;
static uint64_t s_last_tick_ms = 0;
static uint32_t s_hor_res = 240;
static uint32_t s_ver_res = 240;
static const char *s_last_path = "none";
static PyObject *s_flush_cb_py = NULL;

struct native_display_t {
    int spi_fd;
    int gpio_chip_fd;
    int dc_line_fd;
    int rst_line_fd;
    int bl_line_fd;
    bool gpiochip_ready;
    int dc_pin;
    int rst_pin;
    int bl_pin;
    uint32_t speed_hz;
    uint8_t mode;
    uint8_t bits_per_word;
    uint32_t width;
    uint32_t height;
    bool bgr;
    bool ready;
};

static native_display_t s_native = {
    .spi_fd = -1,
    .gpio_chip_fd = -1,
    .dc_line_fd = -1,
    .rst_line_fd = -1,
    .bl_line_fd = -1,
    .gpiochip_ready = false,
    .dc_pin = 25,
    .rst_pin = 27,
    .bl_pin = 24,
    .speed_hz = 40000000,
    .mode = 0,
    .bits_per_word = 8,
    .width = 240,
    .height = 240,
    .bgr = false,
    .ready = false,
};

typedef struct {
    int up_pin;
    int down_pin;
    int left_pin;
    int right_pin;
    int press_pin;
    int key1_pin;
    int key2_pin;
    int key3_pin;
    int up_fd;
    int down_fd;
    int left_fd;
    int right_fd;
    int press_fd;
    int key1_fd;
    int key2_fd;
    int key3_fd;
    bool ready;
    uint32_t last_key;
} native_input_t;

static native_input_t s_input = {
    .up_pin = 6,
    .down_pin = 19,
    .left_pin = 5,
    .right_pin = 26,
    .press_pin = 13,
    .key1_pin = 21,
    .key2_pin = 20,
    .key3_pin = 16,
    .up_fd = -1,
    .down_fd = -1,
    .left_fd = -1,
    .right_fd = -1,
    .press_fd = -1,
    .key1_fd = -1,
    .key2_fd = -1,
    .key3_fd = -1,
    .ready = false,
    .last_key = LV_KEY_ENTER,
};

static bool s_use_native_flush = false;
static bool s_native_lvgl_swap_bytes = true;
static bool s_native_debug_log = false;
static uint32_t s_native_flush_log_limit = 20;
static uint32_t s_native_flush_log_count = 0;

static void queue_push(result_kind_t kind, int index, const char *label) {
    result_event_t ev;
    ev.kind = kind;
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

static const char *kind_to_event_name(result_kind_t kind) {
    switch (kind) {
        case RESULT_TOPNAV_BACK:
            return "topnav_back";
        case RESULT_TOPNAV_POWER:
            return "topnav_power";
        case RESULT_BUTTON_SELECTED:
        default:
            return "button_selected";
    }
}

extern "C" void seedsigner_lvgl_on_button_selected(uint32_t index, const char *label) {
    const char *safe_label = label ? label : "";

    if (index == 0xFFFFFFFFu) {
        if (std::strcmp(safe_label, "topnav_back") == 0) {
            queue_push(RESULT_TOPNAV_BACK, -1, safe_label);
            return;
        }
        if (std::strcmp(safe_label, "topnav_power") == 0) {
            queue_push(RESULT_TOPNAV_POWER, -1, safe_label);
            return;
        }
        queue_push(RESULT_BUTTON_SELECTED, -1, safe_label);
        return;
    }

    queue_push(RESULT_BUTTON_SELECTED, static_cast<int>(index), safe_label);
}

static void write_text_file(const std::string &path, const std::string &value) {
    int fd = open(path.c_str(), O_WRONLY | O_CLOEXEC);
    if (fd < 0) {
        throw std::runtime_error("open failed: " + path + " errno=" + std::to_string(errno));
    }

    size_t off = 0;
    while (off < value.size()) {
        ssize_t n = write(fd, value.c_str() + off, value.size() - off);
        if (n < 0) {
            int e = errno;
            close(fd);
            throw std::runtime_error("write failed: " + path + " errno=" + std::to_string(e));
        }
        off += static_cast<size_t>(n);
    }

    close(fd);
}

static void gpio_export_pin(int pin) {
    try {
        write_text_file("/sys/class/gpio/export", std::to_string(pin));
    } catch (const std::exception &e) {
        std::string msg = e.what();
        if (msg.find("errno=16") != std::string::npos) {
            return;  // already exported
        }
        throw;
    }
}

static void gpio_unexport_pin(int pin) {
    try {
        write_text_file("/sys/class/gpio/unexport", std::to_string(pin));
    } catch (const std::exception &) {
        // best-effort cleanup
    }
}

static void gpio_set_dir_out(int pin) {
    write_text_file("/sys/class/gpio/gpio" + std::to_string(pin) + "/direction", "out");
}

static int gpiochip_request_line(int chip_fd, int pin, const char *consumer) {
    struct gpiohandle_request req;
    memset(&req, 0, sizeof(req));
    req.lineoffsets[0] = static_cast<unsigned int>(pin);
    req.flags = GPIOHANDLE_REQUEST_OUTPUT;
    req.lines = 1;
    req.default_values[0] = 0;
    strncpy(req.consumer_label, consumer, sizeof(req.consumer_label) - 1);
    if (ioctl(chip_fd, GPIO_GET_LINEHANDLE_IOCTL, &req) < 0) {
        throw std::runtime_error("GPIO_GET_LINEHANDLE_IOCTL failed pin=" + std::to_string(pin) + " errno=" + std::to_string(errno));
    }
    return req.fd;
}

static int gpiochip_request_input_line(int chip_fd, int pin, const char *consumer) {
    struct gpiohandle_request req;
    memset(&req, 0, sizeof(req));
    req.lineoffsets[0] = static_cast<unsigned int>(pin);
    req.flags = GPIOHANDLE_REQUEST_INPUT | GPIOHANDLE_REQUEST_BIAS_PULL_UP;
    req.lines = 1;
    strncpy(req.consumer_label, consumer, sizeof(req.consumer_label) - 1);
    if (ioctl(chip_fd, GPIO_GET_LINEHANDLE_IOCTL, &req) < 0) {
        // Kernel < 5.5 may not support bias flags; retry without.
        req.flags = GPIOHANDLE_REQUEST_INPUT;
        if (ioctl(chip_fd, GPIO_GET_LINEHANDLE_IOCTL, &req) < 0) {
            throw std::runtime_error("GPIO_GET_LINEHANDLE_IOCTL(input) failed pin=" + std::to_string(pin) + " errno=" + std::to_string(errno));
        }
        fprintf(stderr, "[seedsigner_lvgl_native] WARN: pull-up not supported for pin %d, using default bias\n", pin);
    }
    return req.fd;
}

static int gpio_line_read(int fd) {
    struct gpiohandle_data data;
    memset(&data, 0, sizeof(data));
    if (ioctl(fd, GPIOHANDLE_GET_LINE_VALUES_IOCTL, &data) < 0) {
        throw std::runtime_error("GPIOHANDLE_GET_LINE_VALUES_IOCTL failed errno=" + std::to_string(errno));
    }
    return data.values[0];
}

static void gpiochip_init_lines() {
    s_native.gpio_chip_fd = open("/dev/gpiochip0", O_RDWR | O_CLOEXEC);
    if (s_native.gpio_chip_fd < 0) {
        throw std::runtime_error("open /dev/gpiochip0 failed errno=" + std::to_string(errno));
    }

    s_native.dc_line_fd = gpiochip_request_line(s_native.gpio_chip_fd, s_native.dc_pin, "sslvgl-dc");
    s_native.rst_line_fd = gpiochip_request_line(s_native.gpio_chip_fd, s_native.rst_pin, "sslvgl-rst");
    s_native.bl_line_fd = gpiochip_request_line(s_native.gpio_chip_fd, s_native.bl_pin, "sslvgl-bl");
    s_native.gpiochip_ready = true;
}

static void gpiochip_shutdown_lines() {
    if (s_native.dc_line_fd >= 0) { close(s_native.dc_line_fd); s_native.dc_line_fd = -1; }
    if (s_native.rst_line_fd >= 0) { close(s_native.rst_line_fd); s_native.rst_line_fd = -1; }
    if (s_native.bl_line_fd >= 0) { close(s_native.bl_line_fd); s_native.bl_line_fd = -1; }
    if (s_native.gpio_chip_fd >= 0) { close(s_native.gpio_chip_fd); s_native.gpio_chip_fd = -1; }
    s_native.gpiochip_ready = false;
}

static int gpio_line_fd_for_pin(int pin) {
    if (pin == s_native.dc_pin) return s_native.dc_line_fd;
    if (pin == s_native.rst_pin) return s_native.rst_line_fd;
    if (pin == s_native.bl_pin) return s_native.bl_line_fd;
    return -1;
}

static void gpio_write_value(int pin, bool high) {
    if (s_native.gpiochip_ready) {
        int fd = gpio_line_fd_for_pin(pin);
        if (fd < 0) {
            throw std::runtime_error("gpio line fd missing for pin " + std::to_string(pin));
        }
        struct gpiohandle_data data;
        memset(&data, 0, sizeof(data));
        data.values[0] = high ? 1 : 0;
        if (ioctl(fd, GPIOHANDLE_SET_LINE_VALUES_IOCTL, &data) < 0) {
            throw std::runtime_error("GPIOHANDLE_SET_LINE_VALUES_IOCTL failed pin=" + std::to_string(pin) + " errno=" + std::to_string(errno));
        }
        return;
    }

    write_text_file("/sys/class/gpio/gpio" + std::to_string(pin) + "/value", high ? "1" : "0");
}

static void native_input_shutdown() {
    if (s_input.up_fd >= 0) { close(s_input.up_fd); s_input.up_fd = -1; }
    if (s_input.down_fd >= 0) { close(s_input.down_fd); s_input.down_fd = -1; }
    if (s_input.left_fd >= 0) { close(s_input.left_fd); s_input.left_fd = -1; }
    if (s_input.right_fd >= 0) { close(s_input.right_fd); s_input.right_fd = -1; }
    if (s_input.press_fd >= 0) { close(s_input.press_fd); s_input.press_fd = -1; }
    if (s_input.key1_fd >= 0) { close(s_input.key1_fd); s_input.key1_fd = -1; }
    if (s_input.key2_fd >= 0) { close(s_input.key2_fd); s_input.key2_fd = -1; }
    if (s_input.key3_fd >= 0) { close(s_input.key3_fd); s_input.key3_fd = -1; }
    s_input.ready = false;
}

static void native_input_init() {
    if (!s_native.gpiochip_ready) {
        fprintf(stderr, "[seedsigner_lvgl_native] input init skipped: gpiochip not ready\n");
        return;
    }
    native_input_shutdown();
    s_input.up_fd = gpiochip_request_input_line(s_native.gpio_chip_fd, s_input.up_pin, "sslvgl-in-up");
    s_input.down_fd = gpiochip_request_input_line(s_native.gpio_chip_fd, s_input.down_pin, "sslvgl-in-down");
    s_input.left_fd = gpiochip_request_input_line(s_native.gpio_chip_fd, s_input.left_pin, "sslvgl-in-left");
    s_input.right_fd = gpiochip_request_input_line(s_native.gpio_chip_fd, s_input.right_pin, "sslvgl-in-right");
    s_input.press_fd = gpiochip_request_input_line(s_native.gpio_chip_fd, s_input.press_pin, "sslvgl-in-press");
    s_input.key1_fd = gpiochip_request_input_line(s_native.gpio_chip_fd, s_input.key1_pin, "sslvgl-in-key1");
    s_input.key2_fd = gpiochip_request_input_line(s_native.gpio_chip_fd, s_input.key2_pin, "sslvgl-in-key2");
    s_input.key3_fd = gpiochip_request_input_line(s_native.gpio_chip_fd, s_input.key3_pin, "sslvgl-in-key3");
    s_input.ready = true;
    fprintf(stderr, "[seedsigner_lvgl_native] input init OK: 8 lines open (pull-up requested)\n");
}

static void native_input_read_cb(lv_indev_drv_t *drv, lv_indev_data_t *data) {
    (void)drv;
    if (!s_input.ready) {
        data->state = LV_INDEV_STATE_REL;
        data->key = s_input.last_key;
        return;
    }

    struct key_map_entry_t { int fd; uint32_t key; };
    const key_map_entry_t keys[] = {
        {s_input.up_fd, LV_KEY_UP},
        {s_input.down_fd, LV_KEY_DOWN},
        {s_input.left_fd, LV_KEY_LEFT},
        {s_input.right_fd, LV_KEY_RIGHT},
        {s_input.press_fd, LV_KEY_ENTER},
        {s_input.key1_fd, (uint32_t)'1'},
        {s_input.key2_fd, (uint32_t)'2'},
        {s_input.key3_fd, (uint32_t)'3'},
    };

    for (const auto &km : keys) {
        if (km.fd < 0) {
            continue;
        }
        int v = 1;
        try {
            v = gpio_line_read(km.fd);
        } catch (...) {
            continue;
        }
        if (v == 0) {  // active-low
            s_input.last_key = km.key;
            data->key = km.key;
            data->state = LV_INDEV_STATE_PRESSED;
            return;
        }
    }

    data->key = s_input.last_key;
    data->state = LV_INDEV_STATE_REL;
}

static void native_spi_write(const uint8_t *data, size_t len) {
    if (s_native.spi_fd < 0) {
        throw std::runtime_error("native SPI not initialized");
    }

    const size_t kChunk = 4096;
    size_t off = 0;
    while (off < len) {
        size_t nreq = (len - off > kChunk) ? kChunk : (len - off);

        struct spi_ioc_transfer tr;
        memset(&tr, 0, sizeof(tr));
        tr.tx_buf = reinterpret_cast<unsigned long>(data + off);
        tr.rx_buf = 0;
        tr.len = static_cast<__u32>(nreq);
        tr.speed_hz = s_native.speed_hz;
        tr.bits_per_word = s_native.bits_per_word;
        tr.delay_usecs = 0;
        tr.cs_change = 0;

        int rc = ioctl(s_native.spi_fd, SPI_IOC_MESSAGE(1), &tr);
        if (rc < 0) {
            if (errno == EINTR) {
                continue;
            }
            throw std::runtime_error("SPI_IOC_MESSAGE failed errno=" + std::to_string(errno));
        }
        off += nreq;
    }
}

static void native_cmd(uint8_t cmd) {
    gpio_write_value(s_native.dc_pin, false);
    native_spi_write(&cmd, 1);
}

static void native_data_byte(uint8_t b) {
    gpio_write_value(s_native.dc_pin, true);
    native_spi_write(&b, 1);
}

static void native_data_buf(const uint8_t *buf, size_t len) {
    gpio_write_value(s_native.dc_pin, true);
    native_spi_write(buf, len);
}

static void native_set_window(int x1, int y1, int x2, int y2) {
    native_cmd(0x2A);
    uint8_t col[] = {static_cast<uint8_t>((x1 >> 8) & 0xFF), static_cast<uint8_t>(x1 & 0xFF), static_cast<uint8_t>((x2 >> 8) & 0xFF), static_cast<uint8_t>(x2 & 0xFF)};
    native_data_buf(col, sizeof(col));

    native_cmd(0x2B);
    uint8_t row[] = {static_cast<uint8_t>((y1 >> 8) & 0xFF), static_cast<uint8_t>(y1 & 0xFF), static_cast<uint8_t>((y2 >> 8) & 0xFF), static_cast<uint8_t>(y2 & 0xFF)};
    native_data_buf(row, sizeof(row));

    native_cmd(0x2C);
}

static void native_hw_reset() {
    gpio_write_value(s_native.rst_pin, true);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    gpio_write_value(s_native.rst_pin, false);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    gpio_write_value(s_native.rst_pin, true);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
}

static void native_display_init_sequence() {
    native_cmd(0x36);
    native_data_byte(s_native.bgr ? 0x78 : 0x70);
    native_cmd(0x3A);
    native_data_byte(0x05);
    native_cmd(0x21);
    native_cmd(0x11);
    std::this_thread::sleep_for(std::chrono::milliseconds(120));
    native_cmd(0x29);
}

static void native_display_blit(int x1, int y1, int x2, int y2, const uint8_t *buf, size_t len) {
    native_set_window(x1, y1, x2, y2);
    native_data_buf(buf, len);
}

static void native_display_test_pattern() {
    if (!s_native.ready) {
        throw std::runtime_error("native display not initialized");
    }

    const uint16_t colors[] = {
        0xF800, // red
        0x07E0, // green
        0x001F, // blue
        0xFFFF, // white
        0x0000, // black
    };
    const int bands = static_cast<int>(sizeof(colors) / sizeof(colors[0]));
    int band_h = static_cast<int>(s_native.height) / bands;
    if (band_h <= 0) {
        band_h = 1;
    }

    std::vector<uint8_t> row(static_cast<size_t>(s_native.width) * 2);
    for (int b = 0; b < bands; ++b) {
        uint16_t c = colors[b];
        for (uint32_t x = 0; x < s_native.width; ++x) {
            row[2 * x] = static_cast<uint8_t>((c >> 8) & 0xFF);
            row[2 * x + 1] = static_cast<uint8_t>(c & 0xFF);
        }

        int y_start = b * band_h;
        int y_end = (b == bands - 1) ? static_cast<int>(s_native.height) - 1 : (y_start + band_h - 1);
        if (y_end < y_start) {
            continue;
        }

        for (int y = y_start; y <= y_end; ++y) {
            native_set_window(0, y, static_cast<int>(s_native.width) - 1, y);
            native_data_buf(row.data(), row.size());
        }
    }
}

static void flush_cb(lv_disp_drv_t *disp_drv, const lv_area_t *area, lv_color_t *color_p) {
    int w = area->x2 - area->x1 + 1;
    int h = area->y2 - area->y1 + 1;
    size_t nbytes = static_cast<size_t>(w) * static_cast<size_t>(h) * static_cast<size_t>(sizeof(lv_color_t));

    if (s_use_native_flush && s_native.ready) {
        size_t expected = static_cast<size_t>(w) * static_cast<size_t>(h) * 2;
        if (s_native_debug_log && s_native_flush_log_count < s_native_flush_log_limit) {
            fprintf(stderr, "[seedsigner_lvgl_native] flush #%u area=(%d,%d)-(%d,%d) w=%d h=%d nbytes=%zu expected=%zu\n",
                    s_native_flush_log_count + 1, area->x1, area->y1, area->x2, area->y2, w, h, nbytes, expected);
            s_native_flush_log_count++;
        }

        if (nbytes != expected) {
            fprintf(stderr, "[seedsigner_lvgl_native] WARN nbytes mismatch: got=%zu expected=%zu\n", nbytes, expected);
        }

        try {
            const uint8_t *src = reinterpret_cast<const uint8_t *>(color_p);
            std::vector<uint8_t> swapped;
            if (s_native_lvgl_swap_bytes) {
                swapped.resize(nbytes);
                for (size_t i = 0; i + 1 < nbytes; i += 2) {
                    swapped[i] = src[i + 1];
                    swapped[i + 1] = src[i];
                }
                src = swapped.data();
            }
            native_display_blit(area->x1, area->y1, area->x2, area->y2, src, nbytes);
        } catch (const std::exception &e) {
            fprintf(stderr, "[seedsigner_lvgl_native] native flush failed: %s\n", e.what());
        }
    } else if (s_flush_cb_py != NULL) {
        PyGILState_STATE gil = PyGILState_Ensure();

        PyObject *payload = PyBytes_FromStringAndSize(reinterpret_cast<const char *>(color_p), static_cast<Py_ssize_t>(nbytes));
        if (payload != NULL) {
            PyObject *ret = PyObject_CallFunction(s_flush_cb_py, "iiiiO", area->x1, area->y1, area->x2, area->y2, payload);
            if (ret == NULL) {
                PyErr_Print();
            } else {
                Py_DECREF(ret);
            }
            Py_DECREF(payload);
        } else {
            PyErr_Print();
        }

        PyGILState_Release(gil);
    }

    lv_disp_flush_ready(disp_drv);
}

static uint64_t now_ms() {
    auto now = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
}

static void lvgl_tick_update() {
    if (!s_lvgl_inited) {
        return;
    }

    uint64_t now = now_ms();
    if (s_last_tick_ms == 0) {
        s_last_tick_ms = now;
        return;
    }

    uint64_t delta = now - s_last_tick_ms;
    if (delta == 0) {
        return;
    }

    if (delta > 100) {
        delta = 100;
    }

    lv_tick_inc(static_cast<uint32_t>(delta));
    s_last_tick_ms = now;
}

static void ensure_lvgl_runtime() {
    if (s_lvgl_inited) {
        return;
    }

    lv_init();

    lv_disp_draw_buf_init(&s_draw_buf, s_buf1, NULL, (sizeof(s_buf1) / sizeof(s_buf1[0])));

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = s_hor_res;
    disp_drv.ver_res = s_ver_res;
    disp_drv.flush_cb = flush_cb;
    disp_drv.draw_buf = &s_draw_buf;
    lv_disp_drv_register(&disp_drv);

    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_KEYPAD;
    indev_drv.read_cb = native_input_read_cb;
    s_input_indev = lv_indev_drv_register(&indev_drv);

    input_profile_set_mode(INPUT_MODE_HARDWARE);

    s_last_tick_ms = now_ms();
    s_lvgl_inited = true;
}

static void require_lvgl_runtime() {
    if (!s_lvgl_inited) {
        throw std::runtime_error("LVGL runtime not initialized: call lvgl_init(hor_res=..., ver_res=...) first");
    }
}

static void lvgl_runtime_shutdown() {
    if (!s_lvgl_inited) {
        return;
    }

#if LV_USE_LOG
    LV_LOG_USER("lvgl runtime shutdown");
#endif
#if defined(LVGL_VERSION_MAJOR) && (LVGL_VERSION_MAJOR >= 8)
    lv_deinit();
#endif
    s_last_tick_ms = 0;
    s_lvgl_inited = false;
}

static void validate_cfg(PyObject *cfg) {
    if (!PyDict_Check(cfg)) {
        throw std::runtime_error("button_list_screen expects cfg_dict as dict");
    }

    PyObject *top_nav = PyDict_GetItemString(cfg, "top_nav");
    if (!top_nav || !PyDict_Check(top_nav)) {
        throw std::runtime_error("top_nav object is required");
    }

    PyObject *title = PyDict_GetItemString(top_nav, "title");
    if (!title || !PyUnicode_Check(title)) {
        throw std::runtime_error("top_nav.title is required and must be a string");
    }

    PyObject *button_list = PyDict_GetItemString(cfg, "button_list");
    if (!button_list || !PyList_Check(button_list)) {
        throw std::runtime_error("button_list is required and must be an array/list");
    }

    Py_ssize_t n = PyList_Size(button_list);
    for (Py_ssize_t i = 0; i < n; ++i) {
        PyObject *it = PyList_GetItem(button_list, i);  // borrowed
        if (!it) {
            throw std::runtime_error("button_list contains invalid item");
        }
        if (PyUnicode_Check(it)) {
            continue;
        }
        if (PyList_Check(it) || PyTuple_Check(it)) {
            if (PySequence_Size(it) <= 0) {
                throw std::runtime_error("button_list entries as array/tuple must not be empty");
            }
            PyObject *label0 = PySequence_GetItem(it, 0);
            if (!label0) {
                throw std::runtime_error("button_list array/tuple label missing");
            }
            bool ok = PyUnicode_Check(label0);
            Py_DECREF(label0);
            if (!ok) {
                throw std::runtime_error("button_list array/tuple index 0 must be string");
            }
            continue;
        }
        throw std::runtime_error("button_list entries must be string or array/tuple with string label at index 0");
    }
}

static const char *extract_first_label(PyObject *cfg, char *buf, size_t buf_size) {
    PyObject *button_list = PyDict_GetItemString(cfg, "button_list");
    if (!button_list || !PyList_Check(button_list) || PyList_Size(button_list) == 0) {
        std::snprintf(buf, buf_size, "%s", "staged_timeout_fallback");
        return buf;
    }

    PyObject *first = PyList_GetItem(button_list, 0);  // borrowed
    if (!first) {
        std::snprintf(buf, buf_size, "%s", "staged_timeout_fallback");
        return buf;
    }

    if (PyUnicode_Check(first)) {
        const char *s = PyUnicode_AsUTF8(first);
        if (s) {
            std::snprintf(buf, buf_size, "%s", s);
            return buf;
        }
    }

    if (PyList_Check(first) || PyTuple_Check(first)) {
        if (PySequence_Size(first) > 0) {
            PyObject *item0 = PySequence_GetItem(first, 0);
            if (item0 && PyUnicode_Check(item0)) {
                const char *s = PyUnicode_AsUTF8(item0);
                if (s) {
                    std::snprintf(buf, buf_size, "%s", s);
                    Py_DECREF(item0);
                    return buf;
                }
            }
            Py_XDECREF(item0);
        }
    }

    std::snprintf(buf, buf_size, "%s", "staged_timeout_fallback");
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
    PyObject *seps = Py_BuildValue("(ss)", ",", ":");
    PyDict_SetItemString(kwargs, "separators", seps);
    Py_DECREF(seps);

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

static void lvgl_runtime_pump(unsigned int duration_ms, unsigned int sleep_ms) {
    if (!s_lvgl_inited) {
        return;
    }

    auto start = std::chrono::steady_clock::now();
    while (true) {
        lvgl_tick_update();
        lv_timer_handler();

        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count();
        if (elapsed >= static_cast<long long>(duration_ms)) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms));
    }
}

static void run_lvgl_until_result_or_timeout(unsigned int timeout_ms) {
    auto start = std::chrono::steady_clock::now();
    while (s_count == 0) {
        lvgl_runtime_pump(5, 1);

        if (timeout_ms == 0) {
            continue;  // no timeout — run until a result arrives
        }
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count();
        if (elapsed >= static_cast<long long>(timeout_ms)) {
            break;
        }
    }
}

static PyObject *py_clear_result_queue(PyObject *self, PyObject *args) {
    (void)self;
    (void)args;
    s_head = 0;
    s_tail = 0;
    s_count = 0;
    Py_RETURN_NONE;
}

static PyObject *py_lvgl_init(PyObject *self, PyObject *args, PyObject *kwargs) {
    (void)self;
    static const char *kwlist[] = {"hor_res", "ver_res", NULL};
    unsigned int hor_res = 240;
    unsigned int ver_res = 240;
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|II", const_cast<char **>(kwlist), &hor_res, &ver_res)) {
        return NULL;
    }

    if (s_lvgl_inited) {
        Py_RETURN_NONE;
    }

    if (hor_res == 0 || ver_res == 0) {
        PyErr_SetString(PyExc_ValueError, "hor_res and ver_res must be > 0");
        return NULL;
    }

    s_hor_res = hor_res;
    s_ver_res = ver_res;
    ensure_lvgl_runtime();
    Py_RETURN_NONE;
}

static PyObject *py_lvgl_shutdown(PyObject *self, PyObject *args) {
    (void)self;
    (void)args;
    lvgl_runtime_shutdown();
    Py_RETURN_NONE;
}

static PyObject *py_lvgl_pump(PyObject *self, PyObject *args, PyObject *kwargs) {
    (void)self;
    static const char *kwlist[] = {"duration_ms", "sleep_ms", NULL};
    unsigned int duration_ms = 10;
    unsigned int sleep_ms = 1;
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|II", const_cast<char **>(kwlist), &duration_ms, &sleep_ms)) {
        return NULL;
    }

    require_lvgl_runtime();
    lvgl_runtime_pump(duration_ms, sleep_ms);
    Py_RETURN_NONE;
}

static PyObject *py_set_flush_callback(PyObject *self, PyObject *args) {
    (void)self;
    PyObject *cb = Py_None;
    if (!PyArg_ParseTuple(args, "|O", &cb)) {
        return NULL;
    }

    if (cb != Py_None && !PyCallable_Check(cb)) {
        PyErr_SetString(PyExc_TypeError, "flush callback must be callable or None");
        return NULL;
    }

    Py_XINCREF(cb == Py_None ? NULL : cb);
    Py_XDECREF(s_flush_cb_py);
    s_flush_cb_py = (cb == Py_None) ? NULL : cb;
    Py_RETURN_NONE;
}

static void native_display_shutdown_internal() {
    native_input_shutdown();

    if (s_native.spi_fd >= 0) {
        close(s_native.spi_fd);
        s_native.spi_fd = -1;
    }

    if (s_native.gpiochip_ready) {
        gpiochip_shutdown_lines();
    } else if (s_native.ready) {
        try { gpio_unexport_pin(s_native.dc_pin); } catch (...) {}
        try { gpio_unexport_pin(s_native.rst_pin); } catch (...) {}
        try { gpio_unexport_pin(s_native.bl_pin); } catch (...) {}
    }

    s_native.ready = false;
}

static PyObject *py_native_display_init(PyObject *self, PyObject *args, PyObject *kwargs) {
    (void)self;
    static const char *kwlist[] = {"width", "height", "dc_pin", "rst_pin", "bl_pin", "spi_path", "spi_speed_hz", "bgr", "lvgl_swap_bytes", NULL};
    unsigned int width = 240;
    unsigned int height = 240;
    int dc_pin = 25;
    int rst_pin = 27;
    int bl_pin = 24;
    const char *spi_path = "/dev/spidev0.0";
    unsigned int spi_speed_hz = 40000000;
    int bgr = 0;
    int lvgl_swap_bytes = 1;

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|IIiiisIpp", const_cast<char **>(kwlist),
                                     &width, &height, &dc_pin, &rst_pin, &bl_pin, &spi_path, &spi_speed_hz, &bgr, &lvgl_swap_bytes)) {
        return NULL;
    }

    try {
        native_display_shutdown_internal();

        s_native.width = width;
        s_native.height = height;
        s_native.dc_pin = dc_pin;
        s_native.rst_pin = rst_pin;
        s_native.bl_pin = bl_pin;
        s_native.speed_hz = spi_speed_hz;
        s_native.bgr = (bgr != 0);
        s_native_lvgl_swap_bytes = (lvgl_swap_bytes != 0);

        try {
            gpiochip_init_lines();
#if LV_USE_LOG
            LV_LOG_USER("native gpio backend: gpiochip");
#endif
        } catch (const std::exception &) {
            gpiochip_shutdown_lines();
            // Fallback for environments where gpiochip access is unavailable.
            gpio_export_pin(s_native.dc_pin);
            gpio_export_pin(s_native.rst_pin);
            gpio_export_pin(s_native.bl_pin);
            gpio_set_dir_out(s_native.dc_pin);
            gpio_set_dir_out(s_native.rst_pin);
            gpio_set_dir_out(s_native.bl_pin);
#if LV_USE_LOG
            LV_LOG_USER("native gpio backend: sysfs fallback");
#endif
        }

        s_native.spi_fd = open(spi_path, O_RDWR | O_CLOEXEC);
        if (s_native.spi_fd < 0) {
            throw std::runtime_error("failed to open spi path");
        }

        if (ioctl(s_native.spi_fd, SPI_IOC_WR_MODE, &s_native.mode) < 0) {
            throw std::runtime_error("failed to set SPI mode");
        }
        if (ioctl(s_native.spi_fd, SPI_IOC_WR_BITS_PER_WORD, &s_native.bits_per_word) < 0) {
            throw std::runtime_error("failed to set SPI bits-per-word");
        }
        if (ioctl(s_native.spi_fd, SPI_IOC_WR_MAX_SPEED_HZ, &s_native.speed_hz) < 0) {
            throw std::runtime_error("failed to set SPI speed");
        }

        gpio_write_value(s_native.bl_pin, true);
        native_hw_reset();
        native_display_init_sequence();

        // Input (joystick/buttons) is best-effort for now; display can run without it.
        try {
            native_input_init();
        } catch (const std::exception &e) {
            fprintf(stderr, "[seedsigner_lvgl_native] native input init skipped: %s\n", e.what());
            native_input_shutdown();
        }

        s_native.ready = true;
        s_use_native_flush = true;
    } catch (const std::exception &e) {
        native_display_shutdown_internal();
        PyErr_SetString(PyExc_RuntimeError, e.what());
        return NULL;
    }

    Py_RETURN_NONE;
}

static PyObject *py_native_display_shutdown(PyObject *self, PyObject *args) {
    (void)self;
    (void)args;
    native_display_shutdown_internal();
    s_use_native_flush = false;
    Py_RETURN_NONE;
}

static PyObject *py_set_flush_mode(PyObject *self, PyObject *args) {
    (void)self;
    const char *mode = NULL;
    if (!PyArg_ParseTuple(args, "s", &mode)) {
        return NULL;
    }

    if (std::strcmp(mode, "native") == 0) {
        s_use_native_flush = true;
    } else if (std::strcmp(mode, "python") == 0) {
        s_use_native_flush = false;
    } else {
        PyErr_SetString(PyExc_ValueError, "mode must be 'native' or 'python'");
        return NULL;
    }

    Py_RETURN_NONE;
}

static PyObject *py_native_debug_config(PyObject *self, PyObject *args, PyObject *kwargs) {
    (void)self;
    static const char *kwlist[] = {"enabled", "flush_log_limit", NULL};
    int enabled = 1;
    unsigned int flush_log_limit = 20;
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|pI", const_cast<char **>(kwlist), &enabled, &flush_log_limit)) {
        return NULL;
    }

    s_native_debug_log = (enabled != 0);
    s_native_flush_log_limit = flush_log_limit;
    s_native_flush_log_count = 0;
    Py_RETURN_NONE;
}

static PyObject *py_native_display_test_pattern(PyObject *self, PyObject *args) {
    (void)self;
    (void)args;
    try {
        native_display_test_pattern();
    } catch (const std::exception &e) {
        PyErr_SetString(PyExc_RuntimeError, e.what());
        return NULL;
    }
    Py_RETURN_NONE;
}

static PyObject *py_debug_last_path(PyObject *self, PyObject *args) {
    (void)self;
    (void)args;
    return PyUnicode_FromString(s_last_path);
}

static PyObject *py_debug_emit_result(PyObject *self, PyObject *args) {
    (void)self;
    const char *label = "";
    unsigned int index = 0;
    if (!PyArg_ParseTuple(args, "sI", &label, &index)) {
        return NULL;
    }
    seedsigner_lvgl_on_button_selected(index, label);
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

    return Py_BuildValue("(sis)", kind_to_event_name(ev.kind), ev.index, ev.label);
}

static unsigned int cfg_wait_timeout_ms(PyObject *cfg, unsigned int default_ms) {
    PyObject *obj = PyDict_GetItemString(cfg, "wait_timeout_ms");
    if (!obj) {
        return default_ms;
    }
    if (!PyLong_Check(obj)) {
        throw std::runtime_error("wait_timeout_ms must be an integer");
    }
    long v = PyLong_AsLong(obj);
    if (v < 0) {
        throw std::runtime_error("wait_timeout_ms must be >= 0");
    }
    return static_cast<unsigned int>(v);
}

static bool cfg_allow_timeout_fallback(PyObject *cfg, bool default_enabled) {
    PyObject *obj = PyDict_GetItemString(cfg, "allow_timeout_fallback");
    if (!obj) {
        return default_enabled;
    }
    int truth = PyObject_IsTrue(obj);
    if (truth < 0) {
        throw std::runtime_error("allow_timeout_fallback must be bool-like");
    }
    return truth != 0;
}

static PyObject *py_button_list_screen(PyObject *self, PyObject *args) {
    (void)self;

    PyObject *cfg = NULL;
    if (!PyArg_ParseTuple(args, "O", &cfg)) {
        return NULL;
    }

    try {
        validate_cfg(cfg);
        require_lvgl_runtime();

        const unsigned int wait_timeout_ms = cfg_wait_timeout_ms(cfg, 0);
        const bool allow_timeout_fallback = cfg_allow_timeout_fallback(cfg, false);

        std::string cfg_json = py_cfg_to_json(cfg);
        button_list_screen((void *)cfg_json.c_str());
        // SeedSigner C modules are the source of truth for navigation/focus wiring.
        // button_list_screen() already binds navigation via nav_bind(), including
        // indev/group ownership. Do not attach a parallel binding-layer group here.
        s_last_path = "compiled";

        run_lvgl_until_result_or_timeout(wait_timeout_ms);

        if (s_count == 0 && allow_timeout_fallback) {
            char label_buf[RESULT_LABEL_MAX];
            const char *label = extract_first_label(cfg, label_buf, sizeof(label_buf));
            queue_push(RESULT_BUTTON_SELECTED, 0, label);
            s_last_path = "fallback_timeout";
        }
    } catch (const std::exception &e) {
        PyErr_SetString(PyExc_RuntimeError, e.what());
        return NULL;
    }

    Py_RETURN_NONE;
}

static PyObject *py_main_menu_screen(PyObject *self, PyObject *args, PyObject *kwargs) {
    (void)self;
    static const char *kwlist[] = {"wait_timeout_ms", "allow_timeout_fallback", NULL};
    unsigned int wait_timeout_ms = 0;
    int allow_timeout_fallback = 0;
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|Ip", const_cast<char **>(kwlist),
                                     &wait_timeout_ms, &allow_timeout_fallback)) {
        return NULL;
    }

    try {
        require_lvgl_runtime();
        main_menu_screen(NULL);
        s_last_path = "compiled";

        run_lvgl_until_result_or_timeout(wait_timeout_ms);

        if (s_count == 0 && allow_timeout_fallback) {
            queue_push(RESULT_BUTTON_SELECTED, 0, "Scan");
            s_last_path = "fallback_timeout";
        }
    } catch (const std::exception &e) {
        PyErr_SetString(PyExc_RuntimeError, e.what());
        return NULL;
    }

    Py_RETURN_NONE;
}

static PyMethodDef methods[] = {
    {"lvgl_init", (PyCFunction)py_lvgl_init, METH_VARARGS | METH_KEYWORDS, "Initialize LVGL runtime."},
    {"lvgl_shutdown", py_lvgl_shutdown, METH_NOARGS, "Shutdown LVGL runtime."},
    {"native_display_init", (PyCFunction)py_native_display_init, METH_VARARGS | METH_KEYWORDS, "Init native ST7789 backend."},
    {"native_display_shutdown", py_native_display_shutdown, METH_NOARGS, "Shutdown native ST7789 backend."},
    {"native_display_test_pattern", py_native_display_test_pattern, METH_NOARGS, "Render native RGB565 test bands."},
    {"native_debug_config", (PyCFunction)py_native_debug_config, METH_VARARGS | METH_KEYWORDS, "Configure native flush debug logging."},
    {"set_flush_mode", py_set_flush_mode, METH_VARARGS, "Set flush mode: native|python."},
    {"set_flush_callback", py_set_flush_callback, METH_VARARGS, "Set display flush callback(area, bytes)."},
    {"button_list_screen", py_button_list_screen, METH_VARARGS, "Render button list screen with runtime loop."},
    {"main_menu_screen", (PyCFunction)py_main_menu_screen, METH_VARARGS | METH_KEYWORDS, "Render main menu screen (2x2 grid)."},
    {"clear_result_queue", py_clear_result_queue, METH_NOARGS, "Clear result queue."},
    {"poll_for_result", py_poll_for_result, METH_NOARGS, "Poll next result tuple or None."},
    {"_debug_last_path", py_debug_last_path, METH_NOARGS, "Debug helper for bridge path."},
    {"_debug_emit_result", py_debug_emit_result, METH_VARARGS, "Debug helper to inject callback-like events."},
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
