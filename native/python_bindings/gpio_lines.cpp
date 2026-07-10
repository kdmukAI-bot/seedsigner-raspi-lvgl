// Generic GPIO line helpers: modern gpiochip character-device ioctls with a
// sysfs fallback for environments where /dev/gpiochip0 is unavailable.
// Consumed by display_st7789.cpp (output lines) and gpio_input.cpp (input lines).
#include "module_internal.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <stdexcept>

#include <fcntl.h>
#include <linux/gpio.h>
#include <sys/ioctl.h>
#include <unistd.h>

void write_text_file(const std::string &path, const std::string &value) {
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

void gpio_export_pin(int pin) {
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

void gpio_unexport_pin(int pin) {
    try {
        write_text_file("/sys/class/gpio/unexport", std::to_string(pin));
    } catch (const std::exception &) {
        // best-effort cleanup
    }
}

void gpio_set_dir_out(int pin) {
    write_text_file("/sys/class/gpio/gpio" + std::to_string(pin) + "/direction", "out");
}

int gpiochip_request_line(int chip_fd, int pin, const char *consumer) {
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

int gpiochip_request_input_line(int chip_fd, int pin, const char *consumer) {
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

int gpio_line_read(int fd) {
    struct gpiohandle_data data;
    memset(&data, 0, sizeof(data));
    if (ioctl(fd, GPIOHANDLE_GET_LINE_VALUES_IOCTL, &data) < 0) {
        throw std::runtime_error("GPIOHANDLE_GET_LINE_VALUES_IOCTL failed errno=" + std::to_string(errno));
    }
    return data.values[0];
}
