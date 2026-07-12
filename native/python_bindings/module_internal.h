// Internal contract between the translation units of the seedsigner_lvgl_screens
// CPython extension. Every .cpp in this directory includes this header FIRST —
// it owns the PY_SSIZE_T_CLEAN-before-Python.h ordering requirement.
//
// Extension layout (one subsystem per file):
//   module.cpp          — PyMethodDef table + module init (the public API index)
//   lvgl_runtime.cpp    — LVGL lifecycle: init/shutdown/pump/tick, flush dispatch,
//                         resolution switch, save/restore/clear screen
//   display_st7789.cpp  — ST7789 SPI panel driver + GPIO output lines + flush flags
//   gpio_input.cpp      — joystick/key GPIO input lines + LVGL keypad indev callback
//   gpio_lines.cpp      — generic gpiochip-ioctl / sysfs GPIO line helpers
//   result_queue.cpp    — host result queue + seedsigner_lvgl_on_* callbacks
//   screens.cpp         — Python wrappers for the portable LVGL screens
//   locale_packs.cpp    — language-pack discovery/loading (set_locale & co.)
#ifndef SS_LVGL_MODULE_INTERNAL_H
#define SS_LVGL_MODULE_INTERNAL_H

#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include "lvgl.h"

#include <cstdint>
#include <string>
#include <vector>

// --- lvgl_runtime.cpp -------------------------------------------------------

// Throws std::runtime_error if lvgl_init() has not been called.
void require_lvgl_runtime();
bool lvgl_runtime_is_inited();
// Drive lv_timer_handler for duration_ms (sleep_ms between iterations).
// Returns 0 normally, -1 if a Python exception/signal is pending.
int lvgl_runtime_pump(unsigned int duration_ms, unsigned int sleep_ms);
// Load a fresh all-black screen and pump briefly so it reaches the panel.
// clean_sys_layer additionally clears lv_layer_sys() overlays first.
void lvgl_clear_to_black(bool clean_sys_layer);

// --- display_st7789.cpp -----------------------------------------------------

// True when the native ST7789 flush path is initialized and selected.
bool native_flush_active();
// Native-path flush body: debug logging, optional RGB565 byte swap, SPI blit.
// Catches and logs its own errors (a failed flush must not kill the pump loop).
void native_flush_blit(const lv_area_t *area, const uint8_t *px_map, size_t nbytes);
// Push the panel MADCTL/geometry for a new resolution (no-op unless the native
// flush path is active).
void display_update_resolution(uint32_t width, uint32_t height);
// Tear down SPI + GPIO output lines (clears the panel to black first when both
// the panel and the LVGL runtime are up). Idempotent.
void native_display_shutdown_internal();
bool native_gpiochip_ready();
int native_gpio_chip_fd();
// Open /dev/gpiochip0 for input-only use: display lines are NOT claimed, but the
// chip is marked ready so input line requests can proceed. Throws on failure.
int native_gpio_chip_open_for_input();

// --- gpio_input.cpp ---------------------------------------------------------

// Claim the joystick/key input lines from the already-open gpiochip. Logs and
// returns (no throw) when the gpiochip is not ready.
void native_input_init();
void native_input_shutdown();
// Open the gpiochip if needed (display lines unclaimed), then claim input lines.
// For use when an external driver owns the display. Throws on gpiochip failure.
void native_input_only_init();
// LVGL keypad indev read callback (registered by the runtime).
void native_input_read_cb(lv_indev_t *indev, lv_indev_data_t *data);

// --- gpio_lines.cpp ---------------------------------------------------------

void write_text_file(const std::string &path, const std::string &value);
void gpio_export_pin(int pin);
void gpio_unexport_pin(int pin);   // best-effort, never throws
void gpio_set_dir_out(int pin);
int gpiochip_request_line(int chip_fd, int pin, const char *consumer);
int gpiochip_request_input_line(int chip_fd, int pin, const char *consumer);
int gpio_line_read(int fd);

// --- locale_packs.cpp -------------------------------------------------------

// Filesystem pack-byte provider shared by set_locale and the locale picker's
// endonym-image rows: serves <font_dir>/<locale>/<file> verbatim. The loader
// copies what it keeps, so the scratch buffer only lives across one call.
struct FsPackCtx {
    std::string font_dir;
    std::vector<uint8_t> scratch;
};
bool fs_pack_provider(const char *locale, const char *file,
                      const uint8_t **bytes, size_t *len, void *user);

// --- Python entry points (the methods[] table in module.cpp) ----------------

// lvgl_runtime.cpp
PyObject *py_lvgl_init(PyObject *self, PyObject *args, PyObject *kwargs);
PyObject *py_lvgl_shutdown(PyObject *self, PyObject *args);
PyObject *py_set_resolution(PyObject *self, PyObject *args, PyObject *kwargs);
PyObject *py_display_size(PyObject *self, PyObject *args);
PyObject *py_lvgl_pump(PyObject *self, PyObject *args, PyObject *kwargs);
PyObject *py_set_flush_callback(PyObject *self, PyObject *args);
PyObject *py_save_screen(PyObject *self, PyObject *args);
PyObject *py_restore_screen(PyObject *self, PyObject *args);
PyObject *py_clear_screen(PyObject *self, PyObject *args);
PyObject *py_set_screensaver_timeout(PyObject *self, PyObject *args);

// display_st7789.cpp
PyObject *py_native_display_init(PyObject *self, PyObject *args, PyObject *kwargs);
PyObject *py_native_display_shutdown(PyObject *self, PyObject *args);
PyObject *py_native_display_test_pattern(PyObject *self, PyObject *args);
PyObject *py_native_debug_config(PyObject *self, PyObject *args, PyObject *kwargs);
PyObject *py_set_flush_mode(PyObject *self, PyObject *args);

// gpio_input.cpp
PyObject *py_native_input_init(PyObject *self, PyObject *args);

// result_queue.cpp
PyObject *py_poll_for_result(PyObject *self, PyObject *args);
PyObject *py_clear_result_queue(PyObject *self, PyObject *args);
PyObject *py_debug_emit_result(PyObject *self, PyObject *args);
PyObject *py_debug_emit_qr_density(PyObject *self, PyObject *args);

// locale_packs.cpp
PyObject *py_set_locale(PyObject *self, PyObject *args);
PyObject *py_unload_locale(PyObject *self, PyObject *args);
PyObject *py_discover_locale_packs(PyObject *self, PyObject *args);
PyObject *py_list_available_locales(PyObject *self, PyObject *args);

// screens.cpp — non-screen companions
PyObject *py_qr_display_set_frame(PyObject *self, PyObject *args);
PyObject *py_qr_display_is_tip_active(PyObject *self, PyObject *args);
PyObject *py_debug_last_path(PyObject *self, PyObject *args);

// screens.cpp — screen builders (all take cfg_dict except screensaver_screen)
PyObject *py_button_list_screen(PyObject *self, PyObject *args);
PyObject *py_main_menu_screen(PyObject *self, PyObject *args);
PyObject *py_large_icon_status_screen(PyObject *self, PyObject *args);
PyObject *py_keyboard_screen(PyObject *self, PyObject *args);
PyObject *py_seed_add_passphrase_screen(PyObject *self, PyObject *args);
PyObject *py_seed_mnemonic_entry_screen(PyObject *self, PyObject *args);
PyObject *py_seed_finalize_screen(PyObject *self, PyObject *args);
PyObject *py_seed_export_xpub_details_screen(PyObject *self, PyObject *args);
PyObject *py_seed_review_passphrase_screen(PyObject *self, PyObject *args);
PyObject *py_seed_words_screen(PyObject *self, PyObject *args);
PyObject *py_seed_transcribe_zoomed_qr_screen(PyObject *self, PyObject *args);
PyObject *py_seed_transcribe_whole_qr_screen(PyObject *self, PyObject *args);
PyObject *py_qr_display_screen(PyObject *self, PyObject *args);
PyObject *py_opening_splash_screen(PyObject *self, PyObject *args);
PyObject *py_loading_spinner_screen(PyObject *self, PyObject *args);
PyObject *py_psbt_overview_screen(PyObject *self, PyObject *args);
PyObject *py_psbt_address_details_screen(PyObject *self, PyObject *args);
PyObject *py_psbt_change_details_screen(PyObject *self, PyObject *args);
PyObject *py_psbt_math_screen(PyObject *self, PyObject *args);
PyObject *py_psbt_op_return_screen(PyObject *self, PyObject *args);
PyObject *py_multisig_wallet_descriptor_screen(PyObject *self, PyObject *args);
PyObject *py_seed_address_verification_screen(PyObject *self, PyObject *args);
PyObject *py_seed_sign_message_confirm_address_screen(PyObject *self, PyObject *args);
PyObject *py_seed_sign_message_confirm_message_screen(PyObject *self, PyObject *args);
PyObject *py_settings_qr_confirmation_screen(PyObject *self, PyObject *args);
PyObject *py_settings_locale_picker_screen(PyObject *self, PyObject *args);
PyObject *py_tools_address_explorer_address_list_screen(PyObject *self, PyObject *args);
PyObject *py_tools_calc_final_word_screen(PyObject *self, PyObject *args);
PyObject *py_tools_calc_final_word_done_screen(PyObject *self, PyObject *args);
PyObject *py_reset_screen(PyObject *self, PyObject *args);
PyObject *py_power_off_not_required_screen(PyObject *self, PyObject *args);
PyObject *py_donate_screen(PyObject *self, PyObject *args);
PyObject *py_io_test_screen(PyObject *self, PyObject *args);
PyObject *py_screensaver_screen(PyObject *self, PyObject *args);

#endif  // SS_LVGL_MODULE_INTERNAL_H
