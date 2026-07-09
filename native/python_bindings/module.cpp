#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include "lvgl.h"
#include "seedsigner.h"
#include "gui_constants.h"
#include "input_profile.h"
#include "locale_loader.h"  // ss_load_locale / ss_unload_locale + ss_register_pack_manifest (i18n font packs)
#include "locale_picker.h"  // locale_picker_set_image_provider (endonym-image rows)
#include "overlay_manager.h"  // native screensaver idle-watch dispatcher

#include <chrono>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <thread>
#include <cstdint>
#include <cerrno>
#include <fstream>
#include <vector>

#include <nlohmann/json.hpp>  // manifest parse for list_available_locales

#include <dirent.h>
#include <fcntl.h>
#include <linux/spi/spidev.h>
#include <linux/gpio.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

#define RESULT_QUEUE_CAP 64
// 256 bytes accommodates a full BIP39 passphrase (text_entered results carry
// the entered string in the label field), not just short button labels.
#define RESULT_LABEL_MAX 256

enum result_kind_t {
    RESULT_BUTTON_SELECTED = 0,
    RESULT_TOPNAV_BACK = 1,
    RESULT_TOPNAV_POWER = 2,
    RESULT_TEXT_ENTERED = 3,
    // qr_display_screen reports its final brightness (31..255) on exit via the
    // on_qr_brightness hook, carried in the index slot so the host can persist
    // SETTING__QR_BRIGHTNESS. Emitted just before the trailing topnav_back.
    RESULT_QR_BRIGHTNESS = 4,
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
static std::vector<uint8_t> s_buf1;  // RGB565: 2 bytes/pixel, sized at runtime
static lv_display_t *s_disp = NULL;
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
    // The SeedSigner Pi Zero ST7789 panel is BGR-wired (matches SeedSigner's own
    // display drivers, which use BGR color order). Default to BGR so the MADCTL
    // BGR bit is set and RGB565 isn't red/blue-swapped (orange would otherwise
    // render light blue). Override per call via native_display_init(bgr=...).
    .bgr = true,
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
        case RESULT_TEXT_ENTERED:
            return "text_entered";
        case RESULT_QR_BRIGHTNESS:
            return "qr_brightness";
        case RESULT_BUTTON_SELECTED:
        default:
            return "button_selected";
    }
}

extern "C" void seedsigner_lvgl_on_button_selected(uint32_t index, const char *label) {
    const char *safe_label = label ? label : "";

    // Reserved result codes (SEEDSIGNER_RET_* in seedsigner.h) arrive in the
    // index slot; `label` is informational only. Check the reserved codes first,
    // then treat index as a 0-based body-button position — the same order
    // SeedSigner's Python Views use.
    switch (index) {
        case SEEDSIGNER_RET_BACK_BUTTON:
            queue_push(RESULT_TOPNAV_BACK, -1, safe_label);
            return;
        case SEEDSIGNER_RET_POWER_BUTTON:
            queue_push(RESULT_TOPNAV_POWER, -1, safe_label);
            return;
        case SEEDSIGNER_RET_SCREENSAVER_DISMISS:
            // No dedicated result kind; preserve the prior behavior of surfacing
            // dismiss as a button_selected with no index (label carries detail).
            queue_push(RESULT_BUTTON_SELECTED, -1, safe_label);
            return;
        case SEEDSIGNER_RET_SPLASH_COMPLETE:
            // Opening splash finished/dismissed. Host-handled lifecycle event (not a
            // Python-routed button): surface it like screensaver dismiss — a
            // button_selected with no index; the "splash_complete" label identifies it.
            queue_push(RESULT_BUTTON_SELECTED, -1, safe_label);
            return;
        default:
            queue_push(RESULT_BUTTON_SELECTED, static_cast<int>(index), safe_label);
            return;
    }
}

// Text-entry confirm callback (e.g. seed_add_passphrase_screen). Overrides the
// weak default in seedsigner.cpp. The entered text is delivered as the result
// label; index is -1 (no list position). Long values are truncated to
// RESULT_LABEL_MAX by queue_push().
extern "C" void seedsigner_lvgl_on_text_entered(const char *text) {
    queue_push(RESULT_TEXT_ENTERED, -1, text ? text : "");
}

// QR brightness persistence hook (overrides the weak no-op in seedsigner.cpp).
// qr_display_screen fires this on exit with its final brightness (31..255) so the
// host can persist SETTING__QR_BRIGHTNESS. Surfaced as ("qr_brightness", value, "");
// it lands in the queue just before the screen's trailing topnav_back result.
extern "C" void seedsigner_lvgl_on_qr_brightness(uint8_t brightness) {
    queue_push(RESULT_QR_BRIGHTNESS, static_cast<int>(brightness), "");
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
        fprintf(stderr, "[seedsigner_lvgl_screens] WARN: pull-up not supported for pin %d, using default bias\n", pin);
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
        fprintf(stderr, "[seedsigner_lvgl_screens] input init skipped: gpiochip not ready\n");
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
    fprintf(stderr, "[seedsigner_lvgl_screens] input init OK: 8 lines open (pull-up requested)\n");
}

// Open gpiochip and initialize only input lines (no display GPIO).
// For use when display is owned by an external driver (e.g. SeedSigner's Python ST7789).
static void native_input_only_init() {
    if (s_input.ready) {
        return;  // already initialized
    }
    if (s_native.gpio_chip_fd < 0) {
        s_native.gpio_chip_fd = open("/dev/gpiochip0", O_RDWR | O_CLOEXEC);
        if (s_native.gpio_chip_fd < 0) {
            throw std::runtime_error("open /dev/gpiochip0 failed errno=" + std::to_string(errno));
        }
        // Mark gpiochip as ready so native_input_init can use it,
        // but display lines are NOT claimed.
        s_native.gpiochip_ready = true;
    }
    native_input_init();
}

static void native_input_read_cb(lv_indev_t *indev, lv_indev_data_t *data) {
    (void)indev;
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

static uint8_t native_madctl_for_resolution() {
    // MADCTL register controls memory access direction (rotation).
    // 240x240: 0x70 (MX+MV+ML) — proven on Waveshare 1.3" hat
    // 320x240: 0x60 (MX+MV) — landscape rotation (from st7789_mpy rotation 1)
    uint8_t madctl = (s_native.width == 320 && s_native.height == 240) ? 0x60 : 0x70;
    if (s_native.bgr) {
        madctl |= 0x08;  // BGR bit
    }
    return madctl;
}

static void native_display_init_sequence() {
    // Full ST7789 initialization sequence (matches SeedSigner st7789_mpy.py).
    native_cmd(0x11);                                           // SLPOUT: exit sleep
    std::this_thread::sleep_for(std::chrono::milliseconds(120));
    native_cmd(0x13);                                           // NORON: normal display mode
    native_cmd(0xB6);                                           // Display function control
    uint8_t b6[] = {0x0A, 0x82};
    native_data_buf(b6, sizeof(b6));
    native_cmd(0x3A);                                           // COLMOD: 16-bit RGB565
    native_data_byte(0x55);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    native_cmd(0xB2);                                           // Porch control
    uint8_t b2[] = {0x0C, 0x0C, 0x00, 0x33, 0x33};
    native_data_buf(b2, sizeof(b2));
    native_cmd(0xB7);                                           // Gate control
    native_data_byte(0x35);
    native_cmd(0xBB);                                           // VCOMS setting
    native_data_byte(0x28);
    native_cmd(0xC0);                                           // Power control 1
    native_data_byte(0x0C);
    native_cmd(0xC2);                                           // Power control 2
    uint8_t c2[] = {0x01, 0xFF};
    native_data_buf(c2, sizeof(c2));
    native_cmd(0xC3);                                           // Power control 3
    native_data_byte(0x10);
    native_cmd(0xC4);                                           // Power control 4
    native_data_byte(0x20);
    native_cmd(0xC6);                                           // VCOM control 1
    native_data_byte(0x0F);
    native_cmd(0xD0);                                           // Power control A
    uint8_t d0[] = {0xA4, 0xA1};
    native_data_buf(d0, sizeof(d0));
    native_cmd(0xE0);                                           // Gamma positive
    uint8_t e0[] = {0xD0, 0x00, 0x02, 0x07, 0x0A, 0x28, 0x32, 0x44, 0x42, 0x06, 0x0E, 0x12, 0x14, 0x17};
    native_data_buf(e0, sizeof(e0));
    native_cmd(0xE1);                                           // Gamma negative
    uint8_t e1[] = {0xD0, 0x00, 0x02, 0x07, 0x0A, 0x28, 0x31, 0x54, 0x47, 0x0E, 0x1C, 0x17, 0x1B, 0x1E};
    native_data_buf(e1, sizeof(e1));
    native_cmd(0x21);                                           // INVON: display inversion
    native_cmd(0x36);                                           // MADCTL: rotation/color order
    native_data_byte(native_madctl_for_resolution());
    native_cmd(0x29);                                           // DISPON: display on
    std::this_thread::sleep_for(std::chrono::milliseconds(120));
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

static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
    int w = area->x2 - area->x1 + 1;
    int h = area->y2 - area->y1 + 1;
    size_t nbytes = static_cast<size_t>(w) * static_cast<size_t>(h) * 2;  // RGB565: 2 bytes/pixel

    if (s_use_native_flush && s_native.ready) {
        if (s_native_debug_log && s_native_flush_log_count < s_native_flush_log_limit) {
            fprintf(stderr, "[seedsigner_lvgl_screens] flush #%u area=(%d,%d)-(%d,%d) w=%d h=%d nbytes=%zu\n",
                    s_native_flush_log_count + 1, area->x1, area->y1, area->x2, area->y2, w, h, nbytes);
            s_native_flush_log_count++;
        }

        try {
            const uint8_t *src = px_map;
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
            fprintf(stderr, "[seedsigner_lvgl_screens] native flush failed: %s\n", e.what());
        }
    } else if (s_flush_cb_py != NULL) {
        PyGILState_STATE gil = PyGILState_Ensure();

        PyObject *payload = PyBytes_FromStringAndSize(reinterpret_cast<const char *>(px_map), static_cast<Py_ssize_t>(nbytes));
        if (payload != NULL) {
            PyObject *ret = PyObject_CallFunction(s_flush_cb_py, "iiiiO", area->x1, area->y1, area->x2, area->y2, payload);
            if (ret == NULL) {
                // Preserve KeyboardInterrupt so the pump loop can exit.
                // Only print/clear non-interrupt exceptions.
                if (!PyErr_ExceptionMatches(PyExc_KeyboardInterrupt)) {
                    PyErr_Print();
                }
            } else {
                Py_DECREF(ret);
            }
            Py_DECREF(payload);
        } else {
            if (!PyErr_ExceptionMatches(PyExc_KeyboardInterrupt)) {
                PyErr_Print();
            }
        }

        PyGILState_Release(gil);
    }

    lv_display_flush_ready(disp);
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

    // lv_init() MUST precede set_display(): the i18n baked floor rasterizes its
    // five translated-text role fonts (OpenSans Western via tiny_ttf) inside
    // set_display(), but only once lv_is_initialized() is true. With the reverse
    // order those role fonts stay null, and the first Fallback-pack load (e.g.
    // ru, which chains under the baseline) dereferences null -> segfault. (The
    // old pre-i18n floor was static bitmap fonts, non-null at init, so order
    // didn't matter then.)
    lv_init();
    set_display(s_hor_res, s_ver_res);

    s_buf1.assign(static_cast<size_t>(s_hor_res) * s_ver_res * 2, 0);

    s_disp = lv_display_create(s_hor_res, s_ver_res);
    lv_display_set_flush_cb(s_disp, flush_cb);
    lv_display_set_color_format(s_disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(s_disp, s_buf1.data(), NULL, s_buf1.size(),
                           LV_DISPLAY_RENDER_MODE_FULL);

    s_input_indev = lv_indev_create();
    lv_indev_set_type(s_input_indev, LV_INDEV_TYPE_KEYPAD);
    lv_indev_set_read_cb(s_input_indev, native_input_read_cb);

    input_profile_set_mode(INPUT_MODE_HARDWARE);

    // Start the native overlay manager (screensaver idle-watch) now that the
    // display + input devices exist — its contract requires both. The dispatcher
    // runs inside lv_timer_handler() (pumped by lvgl_pump). The idle timeout is
    // configured later by the Python runtime via set_screensaver_timeout().
    overlay_manager_init();

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
    s_disp = NULL;
    s_buf1.clear();
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
        // Object form: { "label": str, "icon"?: str, "icon_color"?: str,
        // "right_icon"?: str, "label_color"?: str, ... }. The native
        // read_button_list_items() parser (lvgl-screens) accepts this shape for
        // per-button icons / colors / checkbox state; only the structural
        // requirement (a string "label") is enforced here — the optional keys
        // ride through json.dumps to the shared parser.
        if (PyDict_Check(it)) {
            PyObject *label = PyDict_GetItemString(it, "label");
            if (!label || !PyUnicode_Check(label)) {
                throw std::runtime_error("button_list object entries require a string \"label\"");
            }
            continue;
        }
        throw std::runtime_error("button_list entries must be string, array/tuple with string label at index 0, or object with string \"label\"");
    }

    // Optional per-screen screensaver policy (view-owned). When present it must be
    // a bool; it rides in the cfg JSON to the shared parser, which stamps the
    // screen's root object so the overlay dispatcher honors it. Absent = default
    // (screensaver allowed), applied by the shared parser.
    PyObject *allow_screensaver = PyDict_GetItemString(cfg, "allow_screensaver");
    if (allow_screensaver && !PyBool_Check(allow_screensaver)) {
        throw std::runtime_error("allow_screensaver must be a bool");
    }
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

// Returns 0 normally, -1 if a Python signal is pending.
static int lvgl_runtime_pump(unsigned int duration_ms, unsigned int sleep_ms) {
    if (!s_lvgl_inited) {
        return 0;
    }

    auto start = std::chrono::steady_clock::now();
    while (true) {
        lvgl_tick_update();
        lv_timer_handler();

        // Check for exceptions raised inside flush callbacks (e.g.
        // KeyboardInterrupt) or new pending signals (Ctrl+C).
        if (PyErr_Occurred() || PyErr_CheckSignals() != 0) {
            return -1;
        }

        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count();
        if (elapsed >= static_cast<long long>(duration_ms)) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms));
    }
    return 0;
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

static PyObject *py_set_resolution(PyObject *self, PyObject *args, PyObject *kwargs) {
    (void)self;
    static const char *kwlist[] = {"width", "height", NULL};
    unsigned int width = 0;
    unsigned int height = 0;
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "II", const_cast<char **>(kwlist), &width, &height)) {
        return NULL;
    }

    if (!s_lvgl_inited) {
        PyErr_SetString(PyExc_RuntimeError, "LVGL runtime not initialized: call lvgl_init() first");
        return NULL;
    }

    if (width == s_hor_res && height == s_ver_res) {
        Py_RETURN_NONE;  // already at requested resolution
    }

    // Update the active display profile (aborts if no profile matches).
    set_display(width, height);

    s_hor_res = width;
    s_ver_res = height;

    // Delete the current LVGL display (also deletes all screens).
    if (s_disp) {
        lv_display_delete(s_disp);
        s_disp = NULL;
    }

    // Resize the draw buffer for the new resolution.
    s_buf1.assign(static_cast<size_t>(width) * height * 2, 0);

    // Create a new LVGL display at the new resolution.
    s_disp = lv_display_create(width, height);
    lv_display_set_flush_cb(s_disp, flush_cb);
    lv_display_set_color_format(s_disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(s_disp, s_buf1.data(), NULL, s_buf1.size(),
                           LV_DISPLAY_RENDER_MODE_FULL);

    // lv_display_delete sets all associated indevs' display to NULL, which
    // silently disables them.  Reassign every indev to the new display.
    lv_indev_t *indev = NULL;
    while ((indev = lv_indev_get_next(indev)) != NULL) {
        lv_indev_set_display(indev, s_disp);
    }

    // If native display is active, send the new MADCTL for the target rotation.
    if (s_use_native_flush && s_native.ready) {
        s_native.width = width;
        s_native.height = height;
        native_cmd(0x36);
        native_data_byte(native_madctl_for_resolution());
    }

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
    if (lvgl_runtime_pump(duration_ms, sleep_ms) < 0) {
        return NULL;
    }
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
    // Clear display to black before tearing down hardware.
    if (s_native.ready && s_lvgl_inited) {
        lv_obj_clean(lv_layer_sys());
        lv_obj_t *scr = lv_obj_create(NULL);
        lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
        lv_scr_load(scr);
        lvgl_runtime_pump(50, 5);
    }

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
    unsigned int spi_speed_hz = 62500000;
    int bgr = 1;  // BGR by default — the Pi 0 ST7789 panel is BGR-wired (see s_native)
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
            fprintf(stderr, "[seedsigner_lvgl_screens] native input init skipped: %s\n", e.what());
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

static PyObject *py_native_input_init(PyObject *self, PyObject *args) {
    (void)self;
    (void)args;
    try {
        native_input_only_init();
    } catch (const std::exception &e) {
        PyErr_SetString(PyExc_RuntimeError, e.what());
        return NULL;
    }
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

// Pure builder. The native screen functions below build the LVGL widget tree and
// return immediately; the unified Python loop (lvgl_pump + poll_for_result) drives
// the event loop and the overlay manager's screensaver dispatcher. They no longer
// run an internal wait-for-result loop — that mechanism (and its dead
// allow_timeout_fallback result synthesis) was retired with the runner collapse.
static PyObject *py_button_list_screen(PyObject *self, PyObject *args) {
    (void)self;

    PyObject *cfg = NULL;
    if (!PyArg_ParseTuple(args, "O", &cfg)) {
        return NULL;
    }

    try {
        validate_cfg(cfg);
        require_lvgl_runtime();

        std::string cfg_json = py_cfg_to_json(cfg);
        button_list_screen((void *)cfg_json.c_str());
        // SeedSigner C modules are the source of truth for navigation/focus wiring.
        // button_list_screen() already binds navigation via nav_bind(), including
        // indev/group ownership. Do not attach a parallel binding-layer group here.
        s_last_path = "compiled";
    } catch (const std::exception &e) {
        PyErr_SetString(PyExc_RuntimeError, e.what());
        return NULL;
    }

    Py_RETURN_NONE;
}

static PyObject *py_seed_add_passphrase_screen(PyObject *self, PyObject *args) {
    (void)self;

    PyObject *cfg = NULL;
    if (!PyArg_ParseTuple(args, "|O", &cfg)) {
        return NULL;
    }

    if (cfg && cfg != Py_None && !PyDict_Check(cfg)) {
        PyErr_SetString(PyExc_RuntimeError,
                        "seed_add_passphrase_screen expects cfg_dict as dict");
        return NULL;
    }

    try {
        require_lvgl_runtime();

        // The screen accepts an optional JSON ctx (top_nav, initial_text,
        // max_length, input mode override). With no cfg, pass NULL and let the
        // C side apply its defaults ("Enter Passphrase").
        if (cfg && cfg != Py_None) {
            std::string cfg_json = py_cfg_to_json(cfg);
            seed_add_passphrase_screen((void *)cfg_json.c_str());
        } else {
            seed_add_passphrase_screen(NULL);
        }
        s_last_path = "compiled";
    } catch (const std::exception &e) {
        PyErr_SetString(PyExc_RuntimeError, e.what());
        return NULL;
    }

    Py_RETURN_NONE;
}

// Generalized keyboard entry screen. One native function backs several SeedSigner
// keyboard flows via cfg: BIP-85 child index, custom derivation path, dice-roll and
// coin-flip entropy (see the keys/keys_to_values/return_after_n_chars/
// title_keystroke_template contract in seedsigner.cpp). The completed string is
// delivered as a text_entered result; a top-nav back emits topnav_back.
static PyObject *py_keyboard_screen(PyObject *self, PyObject *args) {
    (void)self;

    PyObject *cfg = NULL;
    if (!PyArg_ParseTuple(args, "O", &cfg)) {
        return NULL;
    }
    if (!PyDict_Check(cfg)) {
        PyErr_SetString(PyExc_RuntimeError, "keyboard_screen expects cfg_dict as dict");
        return NULL;
    }

    try {
        require_lvgl_runtime();
        std::string cfg_json = py_cfg_to_json(cfg);
        keyboard_screen((void *)cfg_json.c_str());
        s_last_path = "compiled";
    } catch (const std::exception &e) {
        PyErr_SetString(PyExc_RuntimeError, e.what());
        return NULL;
    }

    Py_RETURN_NONE;
}

// BIP39 seed-word entry screen (autocomplete keyboard over a wordlist). cfg requires
// a "wordlist" (array of candidate words); optional "initial_letters" /
// "initial_selected_word" prefill state. The accepted word is delivered as a
// text_entered result; a top-nav back emits topnav_back.
static PyObject *py_seed_mnemonic_entry_screen(PyObject *self, PyObject *args) {
    (void)self;

    PyObject *cfg = NULL;
    if (!PyArg_ParseTuple(args, "O", &cfg)) {
        return NULL;
    }
    if (!PyDict_Check(cfg)) {
        PyErr_SetString(PyExc_RuntimeError, "seed_mnemonic_entry_screen expects cfg_dict as dict");
        return NULL;
    }

    try {
        require_lvgl_runtime();
        std::string cfg_json = py_cfg_to_json(cfg);
        seed_mnemonic_entry_screen((void *)cfg_json.c_str());
        s_last_path = "compiled";
    } catch (const std::exception &e) {
        PyErr_SetString(PyExc_RuntimeError, e.what());
        return NULL;
    }

    Py_RETURN_NONE;
}

// Finalize-seed screen shown after a seed loads: a fingerprint readout above a
// bottom-pinned button list (Done / BIP-39 Passphrase, no back button). cfg requires
// a "fingerprint" string; optional "fingerprint_label", "top_nav", and "button_list"
// (defaults to ["Done"]). Structurally a button_list_screen — buttons emit
// button_selected (Done=0, ...); an optional power button emits topnav_power.
static PyObject *py_seed_finalize_screen(PyObject *self, PyObject *args) {
    (void)self;

    PyObject *cfg = NULL;
    if (!PyArg_ParseTuple(args, "O", &cfg)) {
        return NULL;
    }
    if (!PyDict_Check(cfg)) {
        PyErr_SetString(PyExc_RuntimeError, "seed_finalize_screen expects cfg_dict as dict");
        return NULL;
    }

    try {
        // Lenient (no button_list-style validate_cfg): the native screen applies its
        // own defaults (title, ["Done"]) and raises if the required fingerprint is absent.
        require_lvgl_runtime();
        std::string cfg_json = py_cfg_to_json(cfg);
        seed_finalize_screen((void *)cfg_json.c_str());
        s_last_path = "compiled";
    } catch (const std::exception &e) {
        PyErr_SetString(PyExc_RuntimeError, e.what());
        return NULL;
    }

    Py_RETURN_NONE;
}

// seed_export_xpub_details_screen: the xpub-export summary — fingerprint, derivation
// path, and the (host-truncated-on-native-side) xpub as IconTextLines under a pulsing
// yellow privacy-warning edge, above a bottom button list. cfg requires "fingerprint"
// and "xpub" strings; optional "derivation_path" (default m/84'/0'/0'), the three field
// labels (fingerprint_label/derivation_label/xpub_label), "top_nav", and "button_list"
// (defaults ["Export xpub"]). Buttons emit button_selected; back emits topnav_back.
static PyObject *py_seed_export_xpub_details_screen(PyObject *self, PyObject *args) {
    (void)self;

    PyObject *cfg = NULL;
    if (!PyArg_ParseTuple(args, "O", &cfg)) {
        return NULL;
    }
    if (!PyDict_Check(cfg)) {
        PyErr_SetString(PyExc_RuntimeError, "seed_export_xpub_details_screen expects cfg_dict as dict");
        return NULL;
    }

    try {
        // Lenient: the native screen defaults title/labels/button_list and raises if the
        // required "fingerprint"/"xpub" strings are absent.
        require_lvgl_runtime();
        std::string cfg_json = py_cfg_to_json(cfg);
        seed_export_xpub_details_screen((void *)cfg_json.c_str());
        s_last_path = "compiled";
    } catch (const std::exception &e) {
        PyErr_SetString(PyExc_RuntimeError, e.what());
        return NULL;
    }

    Py_RETURN_NONE;
}

// seed_review_passphrase_screen: the entered BIP-39 passphrase (orange, fixed-width,
// centered, up to 3 lines) above a fingerprint IconTextLine spelling out how it changes
// the seed's fingerprint (without >> with). cfg requires a "passphrase" string; optional
// "fingerprint_without"/"fingerprint_with", "changes_fingerprint_label", "top_nav", and
// "button_list" (defaults ["Done"]). Buttons emit button_selected; back emits topnav_back.
static PyObject *py_seed_review_passphrase_screen(PyObject *self, PyObject *args) {
    (void)self;

    PyObject *cfg = NULL;
    if (!PyArg_ParseTuple(args, "O", &cfg)) {
        return NULL;
    }
    if (!PyDict_Check(cfg)) {
        PyErr_SetString(PyExc_RuntimeError, "seed_review_passphrase_screen expects cfg_dict as dict");
        return NULL;
    }

    try {
        // Lenient: the native screen defaults title/button_list and raises if the
        // required "passphrase" string is absent.
        require_lvgl_runtime();
        std::string cfg_json = py_cfg_to_json(cfg);
        seed_review_passphrase_screen((void *)cfg_json.c_str());
        s_last_path = "compiled";
    } catch (const std::exception &e) {
        PyErr_SetString(PyExc_RuntimeError, e.what());
        return NULL;
    }

    Py_RETURN_NONE;
}

// seed_words_screen: one host-paginated page of a seed's words, each numbered in a
// rounded chip, under a pulsing orange dire-warning edge (the seed is on screen), above
// a bottom button list. cfg requires a non-empty "words" array; optional "page_index"
// (default 0), "num_pages" (default 1) for the default "Seed Words: n/N" title,
// "start_number" (1-based number of this page's first word), "top_nav", and "button_list"
// (defaults ["Done"]). Buttons emit button_selected; back emits topnav_back.
static PyObject *py_seed_words_screen(PyObject *self, PyObject *args) {
    (void)self;

    PyObject *cfg = NULL;
    if (!PyArg_ParseTuple(args, "O", &cfg)) {
        return NULL;
    }
    if (!PyDict_Check(cfg)) {
        PyErr_SetString(PyExc_RuntimeError, "seed_words_screen expects cfg_dict as dict");
        return NULL;
    }

    try {
        // Lenient: the native screen defaults title/button_list and raises if the
        // required non-empty "words" array is absent.
        require_lvgl_runtime();
        std::string cfg_json = py_cfg_to_json(cfg);
        seed_words_screen((void *)cfg_json.c_str());
        s_last_path = "compiled";
    } catch (const std::exception &e) {
        PyErr_SetString(PyExc_RuntimeError, e.what());
        return NULL;
    }

    Py_RETURN_NONE;
}

// seed_transcribe_zoomed_qr_screen: a full-bleed, pannable zoomed view of a SeedQR /
// CompactSeedQR for hand transcription — one highlighted zone window over the dimmed QR
// field, the encoded pattern mask-matched to the Pi's python-qrcode so a hand copy is
// pixel-identical. cfg requires a non-empty "qr_data" string; optional "qr_mode"
// (numeric|alphanumeric|byte|auto, default numeric), "data_encoding" (utf8|hex|base64,
// default utf8), "exit_text" (pre-translated bottom hint), and "initial_zone_x"/
// "initial_zone_y" (default 0). Purely static — no host frame push. Exit (joystick click /
// any non-arrow key / close X) emits topnav_back.
static PyObject *py_seed_transcribe_zoomed_qr_screen(PyObject *self, PyObject *args) {
    (void)self;

    PyObject *cfg = NULL;
    if (!PyArg_ParseTuple(args, "O", &cfg)) {
        return NULL;
    }
    if (!PyDict_Check(cfg)) {
        PyErr_SetString(PyExc_RuntimeError, "seed_transcribe_zoomed_qr_screen expects cfg_dict as dict");
        return NULL;
    }

    try {
        // Lenient: the native screen raises if the required non-empty "qr_data" string is
        // absent (or qr_mode/data_encoding are invalid); every other field defaults.
        require_lvgl_runtime();
        std::string cfg_json = py_cfg_to_json(cfg);
        seed_transcribe_zoomed_qr_screen((void *)cfg_json.c_str());
        s_last_path = "compiled";
    } catch (const std::exception &e) {
        PyErr_SetString(PyExc_RuntimeError, e.what());
        return NULL;
    }

    Py_RETURN_NONE;
}

// Native QR display screen (static and host-driven animated). cfg requires "qr_data"
// plus qr_mode/data_encoding/border/brightness options (see qr_display_screen in
// seedsigner.cpp). For a STATIC QR the host builds once and does nothing further. For
// an ANIMATED QR the host pushes each frame with qr_display_set_frame() and honors
// qr_display_is_tip_active() (hold while true). On exit the screen emits a
// qr_brightness result (final brightness) followed by topnav_back.
static PyObject *py_qr_display_screen(PyObject *self, PyObject *args) {
    (void)self;

    PyObject *cfg = NULL;
    if (!PyArg_ParseTuple(args, "O", &cfg)) {
        return NULL;
    }
    if (!PyDict_Check(cfg)) {
        PyErr_SetString(PyExc_RuntimeError, "qr_display_screen expects cfg_dict as dict");
        return NULL;
    }

    try {
        require_lvgl_runtime();
        std::string cfg_json = py_cfg_to_json(cfg);
        qr_display_screen((void *)cfg_json.c_str());
        s_last_path = "compiled";
    } catch (const std::exception &e) {
        PyErr_SetString(PyExc_RuntimeError, e.what());
        return NULL;
    }

    Py_RETURN_NONE;
}

// Push the next animated-QR frame into the live qr_display_screen. Accepts bytes
// (raw payload, e.g. a CompactSeedQR chunk) or str (UTF-8 encoded, e.g. a BBQr /
// UR fragment). Re-encodes + repaints in place using the screen's qr_mode. Safe
// no-op on the native side when no QR screen is active.
static PyObject *py_qr_display_set_frame(PyObject *self, PyObject *args) {
    (void)self;

    PyObject *obj = NULL;
    if (!PyArg_ParseTuple(args, "O", &obj)) {
        return NULL;
    }

    char *data = NULL;
    Py_ssize_t len = 0;
    if (PyBytes_Check(obj)) {
        if (PyBytes_AsStringAndSize(obj, &data, &len) < 0) {
            return NULL;
        }
    } else if (PyUnicode_Check(obj)) {
        data = const_cast<char *>(PyUnicode_AsUTF8AndSize(obj, &len));
        if (!data) {
            return NULL;
        }
    } else {
        PyErr_SetString(PyExc_TypeError, "qr_display_set_frame expects bytes or str");
        return NULL;
    }

    try {
        require_lvgl_runtime();
        qr_display_set_frame((const void *)data, static_cast<size_t>(len));
    } catch (const std::exception &e) {
        PyErr_SetString(PyExc_RuntimeError, e.what());
        return NULL;
    }

    Py_RETURN_NONE;
}

// True while the QR screen's brightness tip/panel is showing. The host's animation
// frame driver polls this and holds (does not advance frames) while true, then
// restarts the sequence from frame 0 when it clears. False when no QR screen active.
static PyObject *py_qr_display_is_tip_active(PyObject *self, PyObject *args) {
    (void)self;
    (void)args;
    if (qr_display_is_tip_active()) {
        Py_RETURN_TRUE;
    }
    Py_RETURN_FALSE;
}

// Large-icon status screen: a hero icon + headline + body over an optional bottom
// button list. cfg["status_type"] selects the preset icon+color ("success", "warning",
// "dire_warning", "error") OR "custom" — where the CALLER supplies the hero glyph and
// color via cfg["icon"] (a raw glyph string, same convention as button/top-nav icons)
// and cfg["icon_color"] (hex). The custom mode is what powers PSBTFinalize's SIGN prompt
// and the microSD notification without a bespoke entry point. On the Pi the hero glyph
// renders in the baked 48px seedsigner icon font (PUA 0xE900-0xE923), which includes
// SIGN (0xe921) and MICROSD (0xe91f); a glyph outside that range renders as tofu (see
// docs/knowledge/custom-large-icon-glyph-range.md).
static PyObject *py_large_icon_status_screen(PyObject *self, PyObject *args) {
    (void)self;

    PyObject *cfg = NULL;
    if (!PyArg_ParseTuple(args, "O", &cfg)) {
        return NULL;
    }
    if (!PyDict_Check(cfg)) {
        PyErr_SetString(PyExc_RuntimeError, "large_icon_status_screen expects cfg_dict as dict");
        return NULL;
    }

    try {
        // Lenient (no button_list-style validate_cfg): the native screen applies its
        // status_type defaults for any missing top_nav/button_list/icon fields, and for
        // status_type "custom" reads the caller-supplied icon glyph + icon_color.
        require_lvgl_runtime();
        std::string cfg_json = py_cfg_to_json(cfg);
        large_icon_status_screen((void *)cfg_json.c_str());
        s_last_path = "compiled";
    } catch (const std::exception &e) {
        PyErr_SetString(PyExc_RuntimeError, e.what());
        return NULL;
    }

    Py_RETURN_NONE;
}

static PyObject *py_main_menu_screen(PyObject *self, PyObject *args) {
    (void)self;

    // The home menu now localizes its title + 4 button labels, passed as an OPTIONAL cfg
    // dict (top_nav + button_list); with no cfg the C side uses its English defaults
    // (RFC 7396 merge-patch).
    PyObject *cfg = NULL;
    if (!PyArg_ParseTuple(args, "|O", &cfg)) {
        return NULL;
    }
    if (cfg && cfg != Py_None && !PyDict_Check(cfg)) {
        PyErr_SetString(PyExc_RuntimeError, "main_menu_screen expects cfg_dict as dict");
        return NULL;
    }

    try {
        require_lvgl_runtime();
        if (cfg && cfg != Py_None) {
            std::string cfg_json = py_cfg_to_json(cfg);
            main_menu_screen((void *)cfg_json.c_str());
        } else {
            main_menu_screen(NULL);
        }
        s_last_path = "compiled";
    } catch (const std::exception &e) {
        PyErr_SetString(PyExc_RuntimeError, e.what());
        return NULL;
    }

    Py_RETURN_NONE;
}

// Opening splash screen (parity with Python OpeningSplashScreen). Optional cfg dict
// (RFC 7396 merge-patch over English defaults) localizes version/sponsor text and
// toggles partner logos / boot-logo handoff. Pure builder: the timed logo reveal is
// driven by lv_anim + lv_timer; on completion (or dismissal) it emits a
// button_selected(-1, "splash_complete") result the host loop watches for.
static PyObject *py_splash_screen(PyObject *self, PyObject *args) {
    (void)self;

    PyObject *cfg = NULL;
    if (!PyArg_ParseTuple(args, "|O", &cfg)) {
        return NULL;
    }
    if (cfg && cfg != Py_None && !PyDict_Check(cfg)) {
        PyErr_SetString(PyExc_RuntimeError, "splash_screen expects cfg_dict as dict");
        return NULL;
    }

    try {
        require_lvgl_runtime();
        if (cfg && cfg != Py_None) {
            std::string cfg_json = py_cfg_to_json(cfg);
            splash_screen((void *)cfg_json.c_str());
        } else {
            splash_screen(NULL);
        }
        s_last_path = "compiled";
    } catch (const std::exception &e) {
        PyErr_SetString(PyExc_RuntimeError, e.what());
        return NULL;
    }

    Py_RETURN_NONE;
}

// Loading spinner (LVGL port of Python's LoadingScreenThread), shown while the host
// runs a long blocking task (seed gen, PSBT signing). Optional cfg: {"text": "..."}.
// Pure builder, fire-and-forget: it takes no input and returns no result — the comet
// self-animates on an lv_timer and is torn down when the next screen loads. There is
// deliberately no stop/hide call (matching the ESP32 binding).
//
// Pi Zero caveat (blended display): unlike the ESP32 — where a dedicated display task
// keeps ticking lv_timer_handler so the comet spins with zero host involvement — the Pi
// has no background pump; LVGL only advances when the host calls lvgl_pump. If the host
// builds this and then blocks in its task, nothing pumps and the comet freezes. So on
// CPython/Pi the host must keep pumping (a background pump thread) for the duration, or
// stay on the PIL LoadingScreenThread until the display cutover. See the host-side note
// seedsigner/docs/architecture/loading-screen-native-integration.md.
static PyObject *py_loading_screen(PyObject *self, PyObject *args) {
    (void)self;

    PyObject *cfg = NULL;
    if (!PyArg_ParseTuple(args, "|O", &cfg)) {
        return NULL;
    }
    if (cfg && cfg != Py_None && !PyDict_Check(cfg)) {
        PyErr_SetString(PyExc_RuntimeError, "loading_screen expects cfg_dict as dict");
        return NULL;
    }

    try {
        require_lvgl_runtime();
        if (cfg && cfg != Py_None) {
            std::string cfg_json = py_cfg_to_json(cfg);
            loading_screen((void *)cfg_json.c_str());
        } else {
            loading_screen(NULL);
        }
        s_last_path = "compiled";
    } catch (const std::exception &e) {
        PyErr_SetString(PyExc_RuntimeError, e.what());
        return NULL;
    }

    Py_RETURN_NONE;
}

// --- PSBT transaction-review screens --------------------------------------
// The four native screens SeedSigner's PSBT-review flow walks through, each a pure
// builder that takes a required cfg dict and emits button_selected / topnav_back like
// the other button-list screens. They follow the same host-formats / C-renders split
// as seed_finalize/large_icon_status: the host owns all i18n + number/address
// formatting (btc_amount strings, digit grouping, address derivation) and passes the
// finished pieces; these screens only lay them out, so the Pi and ESP32 can never
// disagree on how a value rounds or an address truncates. See the psbt_* screens in
// seedsigner.cpp for the full cfg contracts.

// psbt_overview_screen: the animated transaction pictogram (inputs -> center bar ->
// destinations) under a BtcAmount headline, over a bottom-pinned action button. cfg
// describes the structure (num_inputs, destination_addresses, num_change_outputs,
// has_op_return, ...) plus optional btc_amount + translated labels.
static PyObject *py_psbt_overview_screen(PyObject *self, PyObject *args) {
    (void)self;

    PyObject *cfg = NULL;
    if (!PyArg_ParseTuple(args, "O", &cfg)) {
        return NULL;
    }
    if (!PyDict_Check(cfg)) {
        PyErr_SetString(PyExc_RuntimeError, "psbt_overview_screen expects cfg_dict as dict");
        return NULL;
    }

    try {
        // Lenient (no button_list-style validate_cfg): the native screen applies its
        // own defaults (title, single "Review details" button) for missing fields.
        require_lvgl_runtime();
        std::string cfg_json = py_cfg_to_json(cfg);
        psbt_overview_screen((void *)cfg_json.c_str());
        s_last_path = "compiled";
    } catch (const std::exception &e) {
        PyErr_SetString(PyExc_RuntimeError, e.what());
        return NULL;
    }

    Py_RETURN_NONE;
}

// psbt_address_details_screen: one recipient's amount over its full (wrapped) address,
// centered above the action button. cfg requires an "address" string; optional
// btc_amount, top_nav, and button_list (defaults ["Next"]).
static PyObject *py_psbt_address_details_screen(PyObject *self, PyObject *args) {
    (void)self;

    PyObject *cfg = NULL;
    if (!PyArg_ParseTuple(args, "O", &cfg)) {
        return NULL;
    }
    if (!PyDict_Check(cfg)) {
        PyErr_SetString(PyExc_RuntimeError, "psbt_address_details_screen expects cfg_dict as dict");
        return NULL;
    }

    try {
        // Lenient: the native screen defaults title/button_list and raises if the
        // required "address" string is absent.
        require_lvgl_runtime();
        std::string cfg_json = py_cfg_to_json(cfg);
        psbt_address_details_screen((void *)cfg_json.c_str());
        s_last_path = "compiled";
    } catch (const std::exception &e) {
        PyErr_SetString(PyExc_RuntimeError, e.what());
        return NULL;
    }

    Py_RETURN_NONE;
}

// psbt_change_details_screen: the change / self-receive output — amount, an
// address-type label ("change address #N"), the single-line address, and an optional
// "Address verified!" line. cfg requires an "address" string; optional
// address_type_label, is_verified, verified_text, btc_amount, button_list.
static PyObject *py_psbt_change_details_screen(PyObject *self, PyObject *args) {
    (void)self;

    PyObject *cfg = NULL;
    if (!PyArg_ParseTuple(args, "O", &cfg)) {
        return NULL;
    }
    if (!PyDict_Check(cfg)) {
        PyErr_SetString(PyExc_RuntimeError, "psbt_change_details_screen expects cfg_dict as dict");
        return NULL;
    }

    try {
        // Lenient: the native screen defaults title/button_list and raises if the
        // required "address" string is absent.
        require_lvgl_runtime();
        std::string cfg_json = py_cfg_to_json(cfg);
        psbt_change_details_screen((void *)cfg_json.c_str());
        s_last_path = "compiled";
    } catch (const std::exception &e) {
        PyErr_SetString(PyExc_RuntimeError, e.what());
        return NULL;
    }

    Py_RETURN_NONE;
}

// psbt_math_screen: the fee "math" — a right-aligned fixed-width equation of the input
// total minus recipients minus fee, ruled off, equalling the change. The host passes
// each amount as an already-formatted (unpadded) number string; cfg carries
// amounts{input,spend,fee,change}, denomination ("btc"|"sats"), num_recipients, and
// translated labels. All fields optional (the native screen fills sensible defaults).
static PyObject *py_psbt_math_screen(PyObject *self, PyObject *args) {
    (void)self;

    PyObject *cfg = NULL;
    if (!PyArg_ParseTuple(args, "O", &cfg)) {
        return NULL;
    }
    if (!PyDict_Check(cfg)) {
        PyErr_SetString(PyExc_RuntimeError, "psbt_math_screen expects cfg_dict as dict");
        return NULL;
    }

    try {
        require_lvgl_runtime();
        std::string cfg_json = py_cfg_to_json(cfg);
        psbt_math_screen((void *)cfg_json.c_str());
        s_last_path = "compiled";
    } catch (const std::exception &e) {
        PyErr_SetString(PyExc_RuntimeError, e.what());
        return NULL;
    }

    Py_RETURN_NONE;
}

// Dead under the unified runner (the overlay manager owns the screensaver now);
// kept registered as a handy manual-test builder. Pure builder like the rest.
static PyObject *py_screensaver_screen(PyObject *self, PyObject *args) {
    (void)self;
    (void)args;
    try {
        require_lvgl_runtime();
        screensaver_screen(NULL);
        s_last_path = "compiled";
    } catch (const std::exception &e) {
        PyErr_SetString(PyExc_RuntimeError, e.what());
        return NULL;
    }

    Py_RETURN_NONE;
}

// --- Screen save / restore ------------------------------------------------
// General-purpose mechanism for preserving the active LVGL screen across
// an overlay (e.g. screensaver, modal dialog). The Python side decides
// when to save and restore; the C side just holds the pointer.

static lv_obj_t  *s_saved_screen = NULL;
static lv_group_t *s_saved_group = NULL;

static PyObject *py_save_screen(PyObject *self, PyObject *args) {
    (void)self; (void)args;
    try {
        require_lvgl_runtime();
        s_saved_screen = lv_scr_act();

        // Save the current indev group.
        s_saved_group = NULL;
        lv_indev_t *indev = NULL;
        while ((indev = lv_indev_get_next(indev)) != NULL) {
            if (lv_indev_get_type(indev) == LV_INDEV_TYPE_KEYPAD ||
                lv_indev_get_type(indev) == LV_INDEV_TYPE_ENCODER) {
                s_saved_group = lv_indev_get_group(indev);
                break;
            }
        }
    } catch (const std::exception &e) {
        PyErr_SetString(PyExc_RuntimeError, e.what());
        return NULL;
    }
    Py_RETURN_NONE;
}

static PyObject *py_restore_screen(PyObject *self, PyObject *args) {
    (void)self; (void)args;
    try {
        require_lvgl_runtime();
        if (!s_saved_screen) {
            Py_RETURN_NONE;  // nothing saved — no-op
        }

        lv_obj_t *cur_scr = lv_scr_act();

        // Restore the saved indev group BEFORE deleting the overlay screen,
        // since deletion frees the overlay's group.
        if (s_saved_group) {
            lv_indev_t *indev = NULL;
            while ((indev = lv_indev_get_next(indev)) != NULL) {
                if (lv_indev_get_type(indev) == LV_INDEV_TYPE_KEYPAD ||
                    lv_indev_get_type(indev) == LV_INDEV_TYPE_ENCODER) {
                    lv_indev_set_group(indev, s_saved_group);
                }
            }
        }

        if (cur_scr != s_saved_screen) {
            lv_scr_load(s_saved_screen);
            // Synchronously delete the overlay screen now that the saved
            // screen is active and the indev group is restored.
            lv_obj_delete(cur_scr);
        }

        s_saved_screen = NULL;
        s_saved_group = NULL;
    } catch (const std::exception &e) {
        PyErr_SetString(PyExc_RuntimeError, e.what());
        return NULL;
    }
    Py_RETURN_NONE;
}

static PyObject *py_clear_screen(PyObject *self, PyObject *args) {
    (void)self;
    (void)args;
    try {
        require_lvgl_runtime();
        lv_obj_t *scr = lv_obj_create(NULL);
        lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
        lv_scr_load(scr);
        lvgl_runtime_pump(50, 5);
    } catch (const std::exception &e) {
        PyErr_SetString(PyExc_RuntimeError, e.what());
        return NULL;
    }
    Py_RETURN_NONE;
}

// --- Locale / font-pack loading -------------------------------------------
// The shared loader (ss_load_locale) owns all the orchestration: clearing the
// previous locale, registering each role font at the right px, and — for
// complex scripts — loading runs.bin and installing the glyph run table. The
// ONE per-host piece is acquiring the pack bytes. On the Pi Zero that's a plain
// filesystem read of <font_dir>/<locale>/<file>; this is the exact reference
// provider from the screenshot generator. On the real signing device this same
// seam is where pack-signature verification will live (see locale_loader.h).

struct FsPackCtx {
    std::string font_dir;
    std::vector<uint8_t> scratch;  // reused per file; loader copies what it keeps
};

static bool fs_pack_provider(const char *locale, const char *file,
                             const uint8_t **bytes, size_t *len, void *user) {
    FsPackCtx *ctx = static_cast<FsPackCtx *>(user);
    std::string path = ctx->font_dir + "/" + locale + "/" + file;
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        fprintf(stderr, "missing pack file: %s\n", path.c_str());
        return false;
    }
    ctx->scratch.assign((std::istreambuf_iterator<char>(in)),
                        std::istreambuf_iterator<char>());
    *bytes = ctx->scratch.data();
    *len = ctx->scratch.size();
    return true;
}

// set_locale(locale, font_dir="lang-packs") -> bool
//
// Switch the active locale, loading its font packs from <font_dir>/<locale>/.
// Returns True on success. On any missing/unreadable pack the loader restores
// the baked Western floor and this returns False rather than raising: a missing
// pack is a recoverable "fall back to English" condition, not a programming
// error. (Bad argument types still raise TypeError via PyArg_ParseTuple.)
static PyObject *py_set_locale(PyObject *self, PyObject *args) {
    (void)self;

    const char *locale = NULL;
    const char *font_dir = "lang-packs";
    if (!PyArg_ParseTuple(args, "s|s", &locale, &font_dir)) {
        return NULL;
    }

    try {
        // Font registration rasterizes via tiny_ttf, which needs a live LVGL
        // runtime (lv_init + an allocator). Guard it the same way screens do.
        require_lvgl_runtime();

        FsPackCtx ctx;
        ctx.font_dir = font_dir;

        bool ok = ss_load_locale(locale, fs_pack_provider, &ctx);
        return PyBool_FromLong(ok ? 1 : 0);
    } catch (const std::exception &e) {
        PyErr_SetString(PyExc_RuntimeError, e.what());
        return NULL;
    }
}

// unload_locale() -> None
//
// Clear everything set_locale installed (fonts, glyph runs, owned buffers) and
// restore the baked Western floor.
static PyObject *py_unload_locale(PyObject *self, PyObject *args) {
    (void)self;
    (void)args;
    try {
        require_lvgl_runtime();
        ss_unload_locale();
    } catch (const std::exception &e) {
        PyErr_SetString(PyExc_RuntimeError, e.what());
        return NULL;
    }
    Py_RETURN_NONE;
}

// --- Language-pack discovery (SD / packs partition) -----------------------
// Packs live at <font_dir>/<locale>/ — the exact layout set_locale reads through
// fs_pack_provider. Each pack ships a self-describing manifest.json. On the Pi the
// packs live on a user-writable, cross-platform FAT/exFAT volume, so discovery
// treats every directory entry as hostile input: desktop-OS metadata is skipped,
// a half-copied or malformed pack is silently omitted, and one bad manifest never
// aborts the scan (ss_register_pack_manifest itself fails closed on bad JSON).

// True for directory names that are never language packs — dotfiles (covers the
// macOS junk: .DS_Store, ._* AppleDouble, .Spotlight-V100, .Trashes, .fseventsd —
// and "."/".."), plus the Windows metadata dirs. A real locale code never starts
// with a dot.
static bool is_junk_pack_dir(const char *name) {
    if (!name || name[0] == '\0') return true;
    if (name[0] == '.') return true;
    if (std::strcmp(name, "System Volume Information") == 0) return true;
    if (std::strcmp(name, "$RECYCLE.BIN") == 0) return true;
    if (std::strcmp(name, "found.000") == 0) return true;  // chkdsk recovery
    return false;
}

// Immediate, non-junk subdirectory names of `dir`. Returns false only if `dir`
// itself can't be opened — an absent packs partition means "no packs", not an
// error. d_type is unreliable on FAT/exFAT (often DT_UNKNOWN), so stat() decides.
static bool list_pack_dirs(const std::string &dir, std::vector<std::string> &out) {
    DIR *d = opendir(dir.c_str());
    if (!d) return false;
    struct dirent *ent;
    while ((ent = readdir(d)) != nullptr) {
        if (is_junk_pack_dir(ent->d_name)) continue;
        std::string sub = dir + "/" + ent->d_name;
        struct stat st;
        if (stat(sub.c_str(), &st) != 0) continue;   // vanished mid-scan / unreadable
        if (!S_ISDIR(st.st_mode)) continue;
        out.push_back(ent->d_name);
    }
    closedir(d);
    return true;
}

// Read a whole (small) file into `out`. False on any open failure — a missing or
// half-copied manifest.json is a skip, not a crash.
static bool read_file_string(const std::string &path, std::string &out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    out.assign((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return true;
}

// discover_locale_packs(font_dir="lang-packs") -> int
//
// Enumerate <font_dir>/<locale>/manifest.json and register each pack with the
// shared loader so ss_load_locale() / set_locale() work for a locale NOT compiled
// in (the "drop a pack on the card, no rebuild" path). Clears any prior runtime
// registrations first, so it doubles as a rescan on card insert. Returns the count
// registered. Never raises on bad packs (they are skipped); registration is pure
// data, so no live LVGL runtime is required.
static PyObject *py_discover_locale_packs(PyObject *self, PyObject *args) {
    (void)self;
    const char *font_dir = "lang-packs";
    if (!PyArg_ParseTuple(args, "|s", &font_dir)) {
        return NULL;
    }

    ss_clear_pack_manifests();

    std::string base = font_dir;
    std::vector<std::string> dirs;
    list_pack_dirs(base, dirs);

    long count = 0;
    for (const std::string &name : dirs) {
        std::string bytes;
        if (!read_file_string(base + "/" + name + "/manifest.json", bytes)) {
            continue;  // no manifest (not a pack) / half-copied
        }
        if (ss_register_pack_manifest(bytes.data(), bytes.size())) {
            count++;
        }
    }
    return PyLong_FromLong(count);
}

// list_available_locales(font_dir="lang-packs") -> list[dict]
//
// One dict per pack present under <font_dir>, for the seedsigner app to assemble
// the locale-picker cfg from (unioned with the baked-Latin locales it knows from
// its own .mo catalogs). Each dict:
//   {"code": "<locale>",              # what you pass to set_locale()
//    "endonym": "<native name>"|None, # from the manifest
//    "image": "endonym_<h>.bin"|None, # pre-rendered native-script image for the
//                                     # ACTIVE display height, if the pack ships one
//    "has_image": bool}               # True => render the native name as that image
//                                     # (non-Latin script); False => live text.
// Pure read: it parses manifests but does NOT register them — call
// discover_locale_packs() for that. Malformed/half-copied packs are skipped.
static void dict_set_str(PyObject *d, const char *key, const std::string &val) {
    PyObject *v = PyUnicode_FromString(val.c_str());
    PyDict_SetItemString(d, key, v);
    Py_DECREF(v);
}

static void dict_set_str_or_none(PyObject *d, const char *key, const std::string &val) {
    if (val.empty()) {
        PyDict_SetItemString(d, key, Py_None);  // borrows; SetItemString increfs
        return;
    }
    dict_set_str(d, key, val);
}

static PyObject *py_list_available_locales(PyObject *self, PyObject *args) {
    (void)self;
    const char *font_dir = "lang-packs";
    if (!PyArg_ParseTuple(args, "|s", &font_dir)) {
        return NULL;
    }

    // active_profile() (below) aborts if no display profile is set, and a profile
    // is only installed by lvgl_init()/set_resolution(). Gate on the runtime so a
    // premature call raises a catchable error instead of abort()ing the process.
    try {
        require_lvgl_runtime();
    } catch (const std::exception &e) {
        PyErr_SetString(PyExc_RuntimeError, e.what());
        return NULL;
    }

    // Endonym images are pre-rendered per display height; report the one matching
    // the active profile.
    const int height = active_profile().height;

    std::string base = font_dir;
    std::vector<std::string> dirs;
    list_pack_dirs(base, dirs);

    PyObject *result = PyList_New(0);
    if (!result) return NULL;

    for (const std::string &name : dirs) {
        std::string bytes;
        if (!read_file_string(base + "/" + name + "/manifest.json", bytes)) {
            continue;
        }

        nlohmann::json m;
        try {
            m = nlohmann::json::parse(bytes);
        } catch (...) {
            continue;  // malformed manifest -> skip (fail closed)
        }
        if (!m.is_object()) continue;

        const std::string code = m.value("locale", std::string());
        if (code.empty()) continue;  // a pack with no locale is unusable
        const std::string endonym = m.value("endonym", std::string());

        std::string image_file;
        if (height > 0 && m.contains("endonym_images") && m["endonym_images"].is_object()) {
            const nlohmann::json &imgs = m["endonym_images"];
            auto it = imgs.find(std::to_string(height));
            if (it != imgs.end() && it->is_object()) {
                image_file = it->value("file", std::string());
            }
        }

        PyObject *entry = PyDict_New();
        if (!entry) { Py_DECREF(result); return NULL; }
        dict_set_str(entry, "code", code);
        dict_set_str_or_none(entry, "endonym", endonym);
        dict_set_str_or_none(entry, "image", image_file);
        PyObject *has = PyBool_FromLong(image_file.empty() ? 0 : 1);
        PyDict_SetItemString(entry, "has_image", has);
        Py_DECREF(has);

        PyList_Append(result, entry);
        Py_DECREF(entry);
    }
    return result;
}

// locale_picker_screen(cfg) -> None
//
// The language-selection screen. Each row is "English | native"; the native name
// is live text (Latin, baked floor) or a pre-rendered endonym image for non-Latin
// scripts (a row whose cfg carries "image"). Those image bytes are fetched through
// the SAME filesystem pack provider set_locale uses — it serves
// <font_dir>/<locale>/endonym_<h>.bin verbatim. font_dir comes from the optional
// top-level cfg "font_dir" key (default "lang-packs", matching set_locale).
// Selection fires the standard button_selected(row_index) result — the host maps
// the index back to the locale it placed there. Poll for the result like
// button_list_screen.
static PyObject *py_locale_picker_screen(PyObject *self, PyObject *args) {
    (void)self;

    PyObject *cfg = NULL;
    if (!PyArg_ParseTuple(args, "O", &cfg)) {
        return NULL;
    }
    if (!PyDict_Check(cfg)) {
        PyErr_SetString(PyExc_RuntimeError, "locale_picker_screen expects cfg_dict as dict");
        return NULL;
    }

    try {
        require_lvgl_runtime();

        // Point the picker's endonym-image provider at the filesystem pack store —
        // the exact seam ss_load_locale uses. A function-local static ctx outlives
        // the build call; the picker parses + copies each image during the build, so
        // the provider bytes need only be valid across this call (like ss_load_locale).
        static FsPackCtx picker_ctx;
        PyObject *fd = PyDict_GetItemString(cfg, "font_dir");  // borrowed
        picker_ctx.font_dir = (fd && PyUnicode_Check(fd)) ? PyUnicode_AsUTF8(fd) : "lang-packs";
        locale_picker_set_image_provider(fs_pack_provider, &picker_ctx);

        std::string cfg_json = py_cfg_to_json(cfg);
        // locale_picker_screen() binds navigation itself (nav_bind), same as
        // button_list_screen — do not attach a parallel binding-layer group here.
        locale_picker_screen((void *)cfg_json.c_str());
        s_last_path = "compiled";
    } catch (const std::exception &e) {
        PyErr_SetString(PyExc_RuntimeError, e.what());
        return NULL;
    }

    Py_RETURN_NONE;
}

// set_screensaver_timeout(ms) -> None
//
// Idle time (ms) before the native overlay manager activates the screensaver;
// 0 disables it. The Python runtime calls this once at init with the configured
// screensaver_activation_ms. Per-screen opt-out is declarative (the cfg's
// allow_screensaver bool), so there is no suspend/resume export.
static PyObject *py_set_screensaver_timeout(PyObject *self, PyObject *args) {
    (void)self;
    unsigned int ms = 0;
    if (!PyArg_ParseTuple(args, "I", &ms)) {
        return NULL;
    }
    overlay_manager_set_screensaver_timeout(static_cast<uint32_t>(ms));
    Py_RETURN_NONE;
}

// ---------------------------------------------------------------------------
// Remaining SeedSigner-flow screen bindings (sign-message, address verify /
// explorer, calc-final-word, multisig descriptor, settings-QR, whole-QR
// transcribe). Each mirrors the standard lenient cfg_dict pattern and, through
// the shared scaffold, returns button_selected / topnav_back exactly like
// button_list_screen -- no new result types.
//
// The C entry points they call are declared in seedsigner.h and defined in
// per-screen .cpp files in seedsigner-lvgl-screens, which is mid-reorg (the
// seedsigner.cpp split into one screen per file). At the current submodule pin
// those declarations and sources are absent, so this batch does NOT compile
// until the reorg lands, the sources/ pin advances past it, and setup.py wires
// the sources (see the PENDING block in setup.py). The JSON contracts below are
// frozen -- only the compilation paths change.
// ---------------------------------------------------------------------------

// multisig_wallet_descriptor_screen: the "Descriptor Loaded" review -- a policy
// line over the participating fingerprints (monospace), above a bottom button
// list. cfg all-optional: "policy" (str), "signing_keys" (str) OR "fingerprints"
// (str array), "policy_label"/"signing_keys_label", "top_nav.title" (default
// "Descriptor Loaded"), "button_list" (default ["OK"]).
static PyObject *py_multisig_wallet_descriptor_screen(PyObject *self, PyObject *args) {
    (void)self;

    PyObject *cfg = NULL;
    if (!PyArg_ParseTuple(args, "O", &cfg)) {
        return NULL;
    }
    if (!PyDict_Check(cfg)) {
        PyErr_SetString(PyExc_RuntimeError, "multisig_wallet_descriptor_screen expects cfg_dict as dict");
        return NULL;
    }

    try {
        require_lvgl_runtime();
        std::string cfg_json = py_cfg_to_json(cfg);
        multisig_wallet_descriptor_screen((void *)cfg_json.c_str());
        s_last_path = "compiled";
    } catch (const std::exception &e) {
        PyErr_SetString(PyExc_RuntimeError, e.what());
        return NULL;
    }

    Py_RETURN_NONE;
}

// seed_address_verification_screen: the live "Verify Address" scan -- the address
// (type/network-colored) with a progress readout while derivation indexes are
// scanned. cfg requires "address" and "type_network" strings; optional "network"
// (default "mainnet"), "progress_text", "top_nav.title" (default "Verify Address",
// back hidden), "button_list" (default ["Skip 10","Cancel"]).
static PyObject *py_seed_address_verification_screen(PyObject *self, PyObject *args) {
    (void)self;

    PyObject *cfg = NULL;
    if (!PyArg_ParseTuple(args, "O", &cfg)) {
        return NULL;
    }
    if (!PyDict_Check(cfg)) {
        PyErr_SetString(PyExc_RuntimeError, "seed_address_verification_screen expects cfg_dict as dict");
        return NULL;
    }

    try {
        require_lvgl_runtime();
        std::string cfg_json = py_cfg_to_json(cfg);
        seed_address_verification_screen((void *)cfg_json.c_str());
        s_last_path = "compiled";
    } catch (const std::exception &e) {
        PyErr_SetString(PyExc_RuntimeError, e.what());
        return NULL;
    }

    Py_RETURN_NONE;
}

// seed_sign_message_confirm_address_screen: sign-message step 2 -- the deriving
// path over the derived address, above a bottom button list. cfg requires
// "derivation_path" and "address" strings; optional "derivation_path_label"
// (default "derivation path"), "top_nav.title" (default "Confirm Address"),
// "button_list" (default ["Sign message"]).
static PyObject *py_seed_sign_message_confirm_address_screen(PyObject *self, PyObject *args) {
    (void)self;

    PyObject *cfg = NULL;
    if (!PyArg_ParseTuple(args, "O", &cfg)) {
        return NULL;
    }
    if (!PyDict_Check(cfg)) {
        PyErr_SetString(PyExc_RuntimeError, "seed_sign_message_confirm_address_screen expects cfg_dict as dict");
        return NULL;
    }

    try {
        require_lvgl_runtime();
        std::string cfg_json = py_cfg_to_json(cfg);
        seed_sign_message_confirm_address_screen((void *)cfg_json.c_str());
        s_last_path = "compiled";
    } catch (const std::exception &e) {
        PyErr_SetString(PyExc_RuntimeError, e.what());
        return NULL;
    }

    Py_RETURN_NONE;
}

// seed_sign_message_confirm_message_screen: sign-message step 1 -- the message
// text to be signed. Re-enters the public button_list_screen and overlays the
// message. cfg optional "message" (str) plus the standard "top_nav" /
// "button_list" (default ["Next"]) / "is_bottom_list" chrome keys.
static PyObject *py_seed_sign_message_confirm_message_screen(PyObject *self, PyObject *args) {
    (void)self;

    PyObject *cfg = NULL;
    if (!PyArg_ParseTuple(args, "O", &cfg)) {
        return NULL;
    }
    if (!PyDict_Check(cfg)) {
        PyErr_SetString(PyExc_RuntimeError, "seed_sign_message_confirm_message_screen expects cfg_dict as dict");
        return NULL;
    }

    try {
        require_lvgl_runtime();
        std::string cfg_json = py_cfg_to_json(cfg);
        seed_sign_message_confirm_message_screen((void *)cfg_json.c_str());
        s_last_path = "compiled";
    } catch (const std::exception &e) {
        PyErr_SetString(PyExc_RuntimeError, e.what());
        return NULL;
    }

    Py_RETURN_NONE;
}

// seed_transcribe_whole_qr_screen: the "whole QR" overview step of SeedQR
// transcription -- the full QR rendered small under a title, the precursor to
// seed_transcribe_zoomed_qr_screen. cfg requires a non-empty "qr_data" string;
// optional "qr_mode" (numeric|alphanumeric|byte|auto, default auto),
// "data_encoding" (utf8|hex, default utf8), "border" (default 1), "top_nav.title"
// (default "Transcribe SeedQR"), "button_list". Needs LV_USE_QRCODE (enabled).
static PyObject *py_seed_transcribe_whole_qr_screen(PyObject *self, PyObject *args) {
    (void)self;

    PyObject *cfg = NULL;
    if (!PyArg_ParseTuple(args, "O", &cfg)) {
        return NULL;
    }
    if (!PyDict_Check(cfg)) {
        PyErr_SetString(PyExc_RuntimeError, "seed_transcribe_whole_qr_screen expects cfg_dict as dict");
        return NULL;
    }

    try {
        require_lvgl_runtime();
        std::string cfg_json = py_cfg_to_json(cfg);
        seed_transcribe_whole_qr_screen((void *)cfg_json.c_str());
        s_last_path = "compiled";
    } catch (const std::exception &e) {
        PyErr_SetString(PyExc_RuntimeError, e.what());
        return NULL;
    }

    Py_RETURN_NONE;
}

// settings_qr_confirmation_screen: post-SettingsQR-import confirmation -- an
// optional status line naming the applied config, above a single action.
// Re-enters button_list_screen with empty intro text. cfg optional "config_name"
// and "status_message" strings; "top_nav.title" (default "Settings QR", back
// hidden), "button_list" (default ["Home"]).
static PyObject *py_settings_qr_confirmation_screen(PyObject *self, PyObject *args) {
    (void)self;

    PyObject *cfg = NULL;
    if (!PyArg_ParseTuple(args, "O", &cfg)) {
        return NULL;
    }
    if (!PyDict_Check(cfg)) {
        PyErr_SetString(PyExc_RuntimeError, "settings_qr_confirmation_screen expects cfg_dict as dict");
        return NULL;
    }

    try {
        require_lvgl_runtime();
        std::string cfg_json = py_cfg_to_json(cfg);
        settings_qr_confirmation_screen((void *)cfg_json.c_str());
        s_last_path = "compiled";
    } catch (const std::exception &e) {
        PyErr_SetString(PyExc_RuntimeError, e.what());
        return NULL;
    }

    Py_RETURN_NONE;
}

// tools_address_explorer_address_list_screen: the Address Explorer list -- a
// scrolling column of derived addresses (each a button) plus a "Next N" paginate
// action, in monospace (is_bottom_list=false). cfg optional "addresses" (str
// array), "start_index" (default 0), "initial_selected_index" (default 0),
// "next_label", "top_nav.title" (default "Receive Addrs"; host also passes
// "Change Addrs"). Result button_selected(index) -- address row or paginate.
static PyObject *py_tools_address_explorer_address_list_screen(PyObject *self, PyObject *args) {
    (void)self;

    PyObject *cfg = NULL;
    if (!PyArg_ParseTuple(args, "O", &cfg)) {
        return NULL;
    }
    if (!PyDict_Check(cfg)) {
        PyErr_SetString(PyExc_RuntimeError, "tools_address_explorer_address_list_screen expects cfg_dict as dict");
        return NULL;
    }

    try {
        require_lvgl_runtime();
        std::string cfg_json = py_cfg_to_json(cfg);
        tools_address_explorer_address_list_screen((void *)cfg_json.c_str());
        s_last_path = "compiled";
    } catch (const std::exception &e) {
        PyErr_SetString(PyExc_RuntimeError, e.what());
        return NULL;
    }

    Py_RETURN_NONE;
}

// tools_calc_final_word_screen: BIP-39 calc-final-word entry -- the running input,
// the computed final word, and the checksum-bit breakdown (discarded bits dimmed),
// above a "Next" button. Re-enters button_list_screen and overlays the breakdown.
// cfg optional "your_input_text", "final_word_text", "checksum_label" (default
// "Checksum"), "selected_final_bits", "checksum_bits", "has_selected_word"
// (default true), "top_nav.title" (default "Final Word Calc"), "button_list"
// (default ["Next"]).
static PyObject *py_tools_calc_final_word_screen(PyObject *self, PyObject *args) {
    (void)self;

    PyObject *cfg = NULL;
    if (!PyArg_ParseTuple(args, "O", &cfg)) {
        return NULL;
    }
    if (!PyDict_Check(cfg)) {
        PyErr_SetString(PyExc_RuntimeError, "tools_calc_final_word_screen expects cfg_dict as dict");
        return NULL;
    }

    try {
        require_lvgl_runtime();
        std::string cfg_json = py_cfg_to_json(cfg);
        tools_calc_final_word_screen((void *)cfg_json.c_str());
        s_last_path = "compiled";
    } catch (const std::exception &e) {
        PyErr_SetString(PyExc_RuntimeError, e.what());
        return NULL;
    }

    Py_RETURN_NONE;
}

// tools_calc_final_word_done_screen: calc-final-word result -- the computed final
// word with a centered master-fingerprint readout, above a bottom button list.
// cfg requires "final_word" and "fingerprint" strings; optional "fingerprint_label"
// (default "fingerprint"), "mnemonic_word_length" (12 -> title "12th Word", else
// "24th Word"), "button_list".
static PyObject *py_tools_calc_final_word_done_screen(PyObject *self, PyObject *args) {
    (void)self;

    PyObject *cfg = NULL;
    if (!PyArg_ParseTuple(args, "O", &cfg)) {
        return NULL;
    }
    if (!PyDict_Check(cfg)) {
        PyErr_SetString(PyExc_RuntimeError, "tools_calc_final_word_done_screen expects cfg_dict as dict");
        return NULL;
    }

    try {
        require_lvgl_runtime();
        std::string cfg_json = py_cfg_to_json(cfg);
        tools_calc_final_word_done_screen((void *)cfg_json.c_str());
        s_last_path = "compiled";
    } catch (const std::exception &e) {
        PyErr_SetString(PyExc_RuntimeError, e.what());
        return NULL;
    }

    Py_RETURN_NONE;
}

static PyMethodDef methods[] = {
    {"lvgl_init", (PyCFunction)py_lvgl_init, METH_VARARGS | METH_KEYWORDS, "Initialize LVGL runtime."},
    {"lvgl_shutdown", py_lvgl_shutdown, METH_NOARGS, "Shutdown LVGL runtime."},
    {"set_resolution", (PyCFunction)py_set_resolution, METH_VARARGS | METH_KEYWORDS, "Switch LVGL display resolution (e.g. 240x240 to 320x240)."},
    {"lvgl_pump", (PyCFunction)py_lvgl_pump, METH_VARARGS | METH_KEYWORDS, "Pump LVGL timers/input for duration_ms."},
    {"native_display_init", (PyCFunction)py_native_display_init, METH_VARARGS | METH_KEYWORDS, "Init native ST7789 backend."},
    {"native_input_init", py_native_input_init, METH_NOARGS, "Init GPIO input only (no display)."},
    {"save_screen", py_save_screen, METH_NOARGS, "Save active LVGL screen for later restore."},
    {"restore_screen", py_restore_screen, METH_NOARGS, "Restore previously saved LVGL screen."},
    {"clear_screen", py_clear_screen, METH_NOARGS, "Clear display to black."},
    {"native_display_shutdown", py_native_display_shutdown, METH_NOARGS, "Shutdown native ST7789 backend."},
    {"native_display_test_pattern", py_native_display_test_pattern, METH_NOARGS, "Render native RGB565 test bands."},
    {"native_debug_config", (PyCFunction)py_native_debug_config, METH_VARARGS | METH_KEYWORDS, "Configure native flush debug logging."},
    {"set_flush_mode", py_set_flush_mode, METH_VARARGS, "Set flush mode: native|python."},
    {"set_flush_callback", py_set_flush_callback, METH_VARARGS, "Set display flush callback(area, bytes)."},
    {"set_locale", py_set_locale, METH_VARARGS, "set_locale(locale, font_dir='lang-packs'): load locale font packs from <font_dir>/<locale>/. Returns True on success, False if a pack is missing (falls back to baked English)."},
    {"unload_locale", py_unload_locale, METH_NOARGS, "Clear loaded locale packs and restore the baked Western floor."},
    {"discover_locale_packs", py_discover_locale_packs, METH_VARARGS, "discover_locale_packs(font_dir='lang-packs'): (re)scan <font_dir>/<locale>/manifest.json and register each pack so set_locale works for not-compiled-in locales. Returns count registered. Skips desktop-OS junk and bad/half-copied packs."},
    {"list_available_locales", py_list_available_locales, METH_VARARGS, "list_available_locales(font_dir='lang-packs'): list of {code, endonym, image, has_image} for each pack present under <font_dir>, for assembling the locale picker cfg."},
    {"locale_picker_screen", py_locale_picker_screen, METH_VARARGS, "Build the language-selection picker (rows carry live-text or pre-rendered endonym images; result is button_selected(index)). cfg may set 'font_dir' (default 'lang-packs')."},
    {"set_screensaver_timeout", py_set_screensaver_timeout, METH_VARARGS, "set_screensaver_timeout(ms): idle ms before the native screensaver activates (0 disables)."},
    {"button_list_screen", py_button_list_screen, METH_VARARGS, "Build the button list screen (returns immediately; pump + poll for the result)."},
    {"large_icon_status_screen", py_large_icon_status_screen, METH_VARARGS, "Build the large-icon status screen (returns immediately; pump + poll for the result). status_type is 'success'|'warning'|'dire_warning'|'error', or 'custom' with a caller-supplied 'icon' glyph + 'icon_color' (powers PSBTFinalize / microSD notification)."},
    {"main_menu_screen", py_main_menu_screen, METH_VARARGS, "Build the main menu screen (2x2 grid; optional cfg localizes title + labels); returns immediately."},
    {"seed_add_passphrase_screen", py_seed_add_passphrase_screen, METH_VARARGS, "Build the BIP39 passphrase entry screen; result is text_entered or topnav_back."},
    {"keyboard_screen", py_keyboard_screen, METH_VARARGS, "Build the generalized keyboard entry screen (BIP-85 index / derivation path / dice / coin flip); result is text_entered or topnav_back."},
    {"seed_mnemonic_entry_screen", py_seed_mnemonic_entry_screen, METH_VARARGS, "Build the BIP39 seed-word entry screen (autocomplete over cfg['wordlist']); result is text_entered (chosen word) or topnav_back."},
    {"seed_finalize_screen", py_seed_finalize_screen, METH_VARARGS, "Build the finalize-seed screen (fingerprint readout + bottom button list; cfg requires 'fingerprint'); result is button_selected."},
    {"seed_export_xpub_details_screen", py_seed_export_xpub_details_screen, METH_VARARGS, "Build the xpub-export summary (fingerprint/derivation/truncated-xpub IconTextLines + yellow privacy edge; cfg requires 'fingerprint' and 'xpub'); result is button_selected or topnav_back."},
    {"seed_review_passphrase_screen", py_seed_review_passphrase_screen, METH_VARARGS, "Build the review-passphrase screen (orange fixed-width passphrase + changes-fingerprint readout; cfg requires 'passphrase'); result is button_selected or topnav_back."},
    {"seed_words_screen", py_seed_words_screen, METH_VARARGS, "Build one host-paginated page of seed words (numbered chips + orange dire-warning edge; cfg requires a non-empty 'words' array); result is button_selected or topnav_back."},
    {"seed_transcribe_zoomed_qr_screen", py_seed_transcribe_zoomed_qr_screen, METH_VARARGS, "Build the zoomed, pannable SeedQR transcription screen (one highlighted zone window over the dimmed QR field; pattern mask-matched to python-qrcode; cfg requires 'qr_data', optional qr_mode/data_encoding/exit_text/initial_zone_x/initial_zone_y); static, result is topnav_back on exit."},
    {"qr_display_screen", py_qr_display_screen, METH_VARARGS, "Build the native QR display screen (static or animated); result is qr_brightness then topnav_back on exit."},
    {"qr_display_set_frame", py_qr_display_set_frame, METH_VARARGS, "Push the next animated-QR frame (bytes or str) into the live qr_display_screen."},
    {"qr_display_is_tip_active", py_qr_display_is_tip_active, METH_NOARGS, "True while the QR brightness tip/panel is up; the animation driver holds while true."},
    {"splash_screen", py_splash_screen, METH_VARARGS, "Build the opening splash (optional cfg localizes version/sponsor + toggles partner logos); emits button_selected(-1, 'splash_complete') on completion."},
    {"loading_screen", py_loading_screen, METH_VARARGS, "Build the self-animating loading spinner (optional cfg {'text':...}); pure builder, fire-and-forget — no result, torn down when the next screen loads."},
    {"psbt_overview_screen", py_psbt_overview_screen, METH_VARARGS, "Build the animated PSBT transaction-overview pictogram (inputs->center bar->destinations) + BtcAmount headline; result is button_selected (Review details) or topnav_back."},
    {"psbt_address_details_screen", py_psbt_address_details_screen, METH_VARARGS, "Build the per-recipient address-review screen (amount over the full wrapped address; cfg requires 'address'); result is button_selected or topnav_back."},
    {"psbt_change_details_screen", py_psbt_change_details_screen, METH_VARARGS, "Build the change/self-receive review screen (amount + address-type label + address + optional 'Address verified!'; cfg requires 'address'); result is button_selected or topnav_back."},
    {"psbt_math_screen", py_psbt_math_screen, METH_VARARGS, "Build the fee-math equation screen (input - recipients - fee = change; host passes formatted amount strings); result is button_selected or topnav_back."},
    // Remaining SeedSigner-flow screens (sources pending the seedsigner.cpp reorg; see setup.py PENDING block). Frozen JSON contracts.
    {"multisig_wallet_descriptor_screen", py_multisig_wallet_descriptor_screen, METH_VARARGS, "Build the multisig wallet-descriptor review (policy + participating fingerprints; all cfg optional: policy/signing_keys|fingerprints/labels/top_nav; button_list default ['OK']); result is button_selected or topnav_back."},
    {"seed_address_verification_screen", py_seed_address_verification_screen, METH_VARARGS, "Build the verify-address scan screen (address + live progress; cfg requires 'address' and 'type_network', optional network/progress_text; buttons default ['Skip 10','Cancel']); result is button_selected or topnav_back."},
    {"seed_sign_message_confirm_address_screen", py_seed_sign_message_confirm_address_screen, METH_VARARGS, "Build the sign-message confirm-address screen (derivation path + address; cfg requires 'derivation_path' and 'address'; button default ['Sign message']); result is button_selected or topnav_back."},
    {"seed_sign_message_confirm_message_screen", py_seed_sign_message_confirm_message_screen, METH_VARARGS, "Build the sign-message confirm-message screen (message text over a Next button; optional 'message' + standard button_list chrome); result is button_selected or topnav_back."},
    {"seed_transcribe_whole_qr_screen", py_seed_transcribe_whole_qr_screen, METH_VARARGS, "Build the whole-QR SeedQR transcription overview (full QR + title; precursor to the zoomed screen; cfg requires 'qr_data', optional qr_mode/data_encoding/border); result is button_selected or topnav_back."},
    {"settings_qr_confirmation_screen", py_settings_qr_confirmation_screen, METH_VARARGS, "Build the settings-QR import confirmation (optional config_name/status_message; button default ['Home']); result is button_selected or topnav_back."},
    {"tools_address_explorer_address_list_screen", py_tools_address_explorer_address_list_screen, METH_VARARGS, "Build the address-explorer address list (scrolling addresses[] + 'Next N' paginate; optional start_index/initial_selected_index/next_label; title default 'Receive Addrs'); result is button_selected(index) or topnav_back."},
    {"tools_calc_final_word_screen", py_tools_calc_final_word_screen, METH_VARARGS, "Build the calc-final-word entry screen (input + computed word + checksum-bit breakdown; all fields optional; button default ['Next']); result is button_selected or topnav_back."},
    {"tools_calc_final_word_done_screen", py_tools_calc_final_word_done_screen, METH_VARARGS, "Build the calc-final-word result (final word + fingerprint readout; cfg requires 'final_word' and 'fingerprint', optional fingerprint_label/mnemonic_word_length); result is button_selected or topnav_back."},
    {"screensaver_screen", py_screensaver_screen, METH_NOARGS, "Build the screensaver (bouncing logo); returns immediately. Manual-test helper (the overlay manager owns the screensaver at runtime)."},
    {"clear_result_queue", py_clear_result_queue, METH_NOARGS, "Clear result queue."},
    {"poll_for_result", py_poll_for_result, METH_NOARGS, "Poll next result tuple or None."},
    {"_debug_last_path", py_debug_last_path, METH_NOARGS, "Debug helper for bridge path."},
    {"_debug_emit_result", py_debug_emit_result, METH_VARARGS, "Debug helper to inject callback-like events."},
    {NULL, NULL, 0, NULL},
};

static struct PyModuleDef module_def = {
    PyModuleDef_HEAD_INIT,
    "seedsigner_lvgl_screens",
    "SeedSigner LVGL native binding scaffold",
    -1,
    methods,
};

PyMODINIT_FUNC PyInit_seedsigner_lvgl_screens(void) {
    return PyModule_Create(&module_def);
}
