# Interface Contract (Python ↔ seedsigner_lvgl_screens)

The contract for driving the compiled LVGL screens from Python on the Pi Zero.
The authoritative export list is `methods[]` in
`native/python_bindings/module.cpp` (each docstring is the short-form contract);
on any conflict, the code wins.

## Binding model

- Python imports the extension directly (`import seedsigner_lvgl_screens`) —
  there is no Python facade layer.
- Every screen function is a **pure builder**: it constructs the LVGL widget
  tree and returns immediately. Python drives the event loop:

```python
import seedsigner_lvgl_screens as mod

mod.lvgl_init(hor_res=240, ver_res=240)
mod.native_display_init()          # on Pi hardware
mod.set_screensaver_timeout(120_000)

mod.button_list_screen({
    "top_nav": {"title": "Select Network", "show_back_button": True},
    "button_list": [("Mainnet", "mainnet"), ("Testnet", "testnet")],
})
while True:
    mod.lvgl_pump(duration_ms=10)      # advance LVGL timers/input
    event = mod.poll_for_result()      # None until the user acts
    if event:
        break
# event == ("button_selected", 0, "Mainnet") etc.
```

- Config dicts are marshalled to JSON and parsed by the shared C code in
  `seedsigner-lvgl-screens` — the same parser the ESP32/MicroPython build uses,
  so both platforms accept identical cfgs.
- The host owns all i18n and number/address formatting; screens lay out
  pre-formatted strings.

## Result contract

Results are drained with `poll_for_result()` → tuple `(kind, index, label)` or
`None` when the queue is empty. `clear_result_queue()` empties it (call before
building a screen). The queue holds 64 events (oldest dropped) and labels are
truncated to 255 bytes.

| Kind | Tuple shape | Meaning |
|------|-------------|---------|
| `button_selected` | `("button_selected", i, label)` | Body button `i` (0-based) activated. `i == -1` for host-handled sentinels: screensaver dismiss and `("button_selected", -1, "splash_complete")` from the opening splash. |
| `topnav_back` | `("topnav_back", -1, "back")` | Top-nav back activated. |
| `topnav_power` | `("topnav_power", -1, "power")` | Top-nav power activated. |
| `text_entered` | `("text_entered", -1, text)` | Text-entry screen confirmed (passphrase, keyboard, mnemonic word). |
| `qr_brightness` | `("qr_brightness", value, "")` | qr_display_screen exit: final brightness (31..255) for persisting `SETTING__QR_BRIGHTNESS`; emitted just before the trailing `topnav_back`. |
| `qr_density` | `("qr_density", px_per_module, "")` | qr_display_screen density UI (`px_per_module`, 3..6). Emitted on **every** density change and once on exit (just before the trailing `topnav_back`). The host re-resolves `(vertical_resolution, px_per_module) → max_fragment_len`, restarts the animated-QR fountain via `qr_display_set_frame()`, and persists `SETTING__QR_DENSITY`. |

The `label` is informational only — never dispatch on it. The C side signals
outcomes through the index slot using the reserved codes in `seedsigner.h`
(`SEEDSIGNER_RET_BACK_BUTTON`=1000, `SEEDSIGNER_RET_POWER_BUTTON`=1001 — the
same values as `RET_CODE__*` in the SeedSigner Python app — plus
`SEEDSIGNER_RET_SCREENSAVER_DISMISS`=1100 and
`SEEDSIGNER_RET_SPLASH_COMPLETE`=1101); the bridge maps those to the kinds
above before Python sees them.

## Runtime / hardware methods

| Method | Contract |
|--------|----------|
| `lvgl_init(hor_res=240, ver_res=240)` | Start LVGL. Must precede every other call except `discover_locale_packs`. Idempotent. |
| `lvgl_shutdown()` | Tear down LVGL. |
| `lvgl_pump(duration_ms=10, sleep_ms=1)` | Advance LVGL timers/animations/input for the duration. Raises pending `KeyboardInterrupt`. |
| `set_resolution(width, height)` | Switch display profile (240x240 ↔ 320x240). Deletes all live screens. |
| `display_size()` | `(width, height)` of the active display profile. Raises `RuntimeError` before `lvgl_init()`. Lets the app read `vertical_resolution` identically on Pi and ESP32. |
| `native_display_init(width=240, height=240, dc_pin=25, rst_pin=27, bl_pin=24, spi_path="/dev/spidev0.0", spi_speed_hz=62500000, bgr=True, lvgl_swap_bytes=True)` | Init the ST7789 SPI panel + GPIO output lines + GPIO input; selects the native flush path. |
| `native_display_shutdown()` | Clear panel to black, release SPI + GPIO. |
| `native_input_init()` | Claim only the input lines (display owned by an external driver). |
| `set_flush_mode("native"\|"python")` / `set_flush_callback(cb)` | Route flushes to the ST7789 or to a Python callback `cb(x1, y1, x2, y2, rgb565_bytes)`. |
| `save_screen()` / `restore_screen()` | Preserve/restore the active screen + input group across a host-driven overlay. |
| `clear_screen()` | Load an all-black screen. |
| `set_screensaver_timeout(ms)` | Idle ms before the native screensaver overlay activates; 0 disables. Per-screen opt-out via cfg `allow_screensaver: false`. |
| `native_display_test_pattern()` / `native_debug_config(...)` | Hardware bring-up helpers. |
| `_debug_last_path()` / `_debug_emit_result(label, index)` / `_debug_emit_qr_density(px_per_module)` | Test helpers: build-path breadcrumb; inject a fake button event; fire the `on_qr_density` callback into the queue. |

Locale methods (`discover_locale_packs`, `list_available_locales`,
`set_locale`, `unload_locale`): see `docs/language-support.md`.

## Screen builders

All take one cfg dict and emit the results listed. "optional cfg" screens may
be called with no argument / `None` (C-side English defaults, RFC 7396
merge-patch). Full per-screen cfg contracts: the contract comments in
`native/python_bindings/screens.cpp` and the `methods[]` docstrings.

| Screen | Required cfg keys | Results |
|--------|-------------------|---------|
| `button_list_screen` | `top_nav.title`, `button_list` (strictly validated) | button_selected / topnav_back / topnav_power |
| `main_menu_screen` | — (optional cfg) | button_selected (0..3) |
| `large_icon_status_screen` | `status_type` preset or `"custom"` + `icon`/`icon_color` | button_selected / topnav_back |
| `keyboard_screen` | `keys`, … (see screens.cpp) | text_entered / topnav_back |
| `seed_add_passphrase_screen` | — (optional cfg) | text_entered / topnav_back |
| `seed_mnemonic_entry_screen` | `wordlist` | text_entered / topnav_back |
| `seed_finalize_screen` | `fingerprint` | button_selected |
| `seed_export_xpub_details_screen` | `fingerprint`, `xpub` | button_selected / topnav_back |
| `seed_review_passphrase_screen` | `passphrase` | button_selected / topnav_back |
| `seed_words_screen` | `words` (non-empty) | button_selected / topnav_back |
| `seed_transcribe_whole_qr_screen` | `qr_data` | button_selected / topnav_back |
| `seed_transcribe_zoomed_qr_screen` | `qr_data` | topnav_back |
| `qr_display_screen` | `qr_data` | qr_brightness, then topnav_back |
| `opening_splash_screen` | — (optional cfg) | button_selected(-1, "splash_complete") |
| `loading_spinner_screen` | — (optional cfg) | none (torn down by the next screen; host must keep pumping) |
| `psbt_overview_screen` | — | button_selected / topnav_back |
| `psbt_address_details_screen` | `address` | button_selected / topnav_back |
| `psbt_change_details_screen` | `address` | button_selected / topnav_back |
| `psbt_math_screen` | — | button_selected / topnav_back |
| `psbt_op_return_screen` | — | button_selected / topnav_back |
| `multisig_wallet_descriptor_screen` | — | button_selected / topnav_back |
| `seed_address_verification_screen` | `address`, `type_network` | button_selected / topnav_back |
| `seed_sign_message_confirm_address_screen` | `derivation_path`, `address` | button_selected / topnav_back |
| `seed_sign_message_confirm_message_screen` | — | button_selected / topnav_back |
| `settings_qr_confirmation_screen` | — | button_selected / topnav_back |
| `settings_locale_picker_screen` | rows cfg (`font_dir` optional) | button_selected(row index) |
| `tools_address_explorer_address_list_screen` | — | button_selected(row / paginate) / topnav_back |
| `tools_calc_final_word_screen` | — | button_selected / topnav_back |
| `tools_calc_final_word_done_screen` | `final_word`, `fingerprint` | button_selected / topnav_back |
| `reset_screen` | — | none (host tears down) |
| `power_off_not_required_screen` | — | topnav_back |
| `donate_screen` | — | topnav_back |
| `io_test_screen` | — | hardware-key driven (see known gaps) |
| `screensaver_screen` | (no cfg arg) | manual-test helper; overlay manager owns the runtime screensaver |

qr_display companions: `qr_display_set_frame(bytes|str)` pushes the next
animated-QR frame; `qr_display_is_tip_active()` is True while the brightness
panel is up (hold frames, restart the sequence when it clears).

### Common cfg conventions

- `top_nav` (dict): `title` (str), `show_back_button` (bool),
  `show_power_button` (bool).
- `button_list` entries: `"label"`, `(label, value)` tuple/list (index 0 must
  be the label string), or `{"label": ..., "icon"?, "icon_color"?,
  "right_icon"?, "label_color"?, ...}`.
- `allow_screensaver` (bool, default true): per-screen screensaver opt-out.
- `is_bottom_list` (bool): pin the button list to the viewport bottom.

## Input model (Pi hardware)

GPIO lines are active-low with pull-up, polled by the LVGL keypad indev:

| Hardware | BCM pin | LVGL key | Action |
|----------|---------|----------|--------|
| Joystick UP/DOWN/LEFT/RIGHT | 6 / 19 / 5 / 26 | `LV_KEY_UP/DOWN/LEFT/RIGHT` | Navigate within zone; UP past the top body element moves to TOP_NAV, DOWN returns to BODY |
| Joystick PRESS | 13 | `LV_KEY_ENTER` | Activate focused control |
| KEY1 / KEY2 / KEY3 | 21 / 20 / 16 | `'1'` / `'2'` / `'3'` | Aux keys (per-screen policy in the navigation system) |

Focus, zone transitions, and aux-key policy are owned by the navigation system
in `seedsigner-lvgl-screens` (`navigation.cpp`); the extension only registers
the indev and sets `INPUT_MODE_HARDWARE`. Directional movement is never
forwarded to Python — only the activation results in the table above.

## Error behavior

- Calling any screen before `lvgl_init()` raises `RuntimeError`.
- A non-dict cfg (or a missing required key) raises `RuntimeError` with a
  descriptive message.
- Bad argument types raise `TypeError` via the argument parser.
- `set_locale` returns `False` (does not raise) when a pack is missing.

## Known gaps

- `io_test_screen` forwards KEY1/2/3 to the host through the weak
  `seedsigner_lvgl_on_aux_key()` hook, which this extension does not yet
  override — no aux-key result reaches Python. Wire a result kind in
  `result_queue.cpp` before the app navigates to this screen.
