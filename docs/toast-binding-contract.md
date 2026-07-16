# Toast overlay — Python binding contract (`show_toast` / `dismiss_toast`)

**Status:** authoritative contract for the native toast binding. **Implemented and
conformant** in this repo (`native/python_bindings/toast.cpp`, registered in `module.cpp`) —
verified against this contract 2026-07-15. The SeedSigner app-side landed on
`integration/lvgl-mpy` (2026-07-15); the native overlay shipped in `seedsigner-lvgl-screens`
(PR #74 @ `9dd91b1`).

This binding is **platform-symmetric**: `seedsigner-raspi-lvgl` (this repo, CPython `.so`)
and `seedsigner-micropython-builder` (ESP32 MicroPython) must expose it **identically** —
same function names, same cfg schema, same semantics — so the shared `seedsigner` app calls
it with **no platform branch**. The identical contract also lives in the builder repo at
`docs/toast-binding-contract.md`; keep the two in sync. This repo's committed
`docs/interface-contract.md` carries a one-paragraph summary under **Toast overlay** — this
document is the detailed spec it points to.

---

## Why a toast is not a screen

A toast is a transient banner pinned to the bottom of the display, built on the LVGL **top
layer** so it composites over whatever screen is live and survives screen swaps. It is
**not** a `run_screen`: there is no cfg-assembly/JSON path, no result event, no return
value, and it can be raised from a background producer thread. So it is bound as a plain
**fire-and-forget push**, not through the `run_cfg_screen` machinery.

It replaces the Pi's old PIL toast, which blacked out the LVGL UI (PIL had no live canvas to
composite onto).

---

## The Python surface (identical on both platforms)

```python
seedsigner_lvgl_screens.show_toast(cfg: dict) -> None     # raise or replace a toast
seedsigner_lvgl_screens.dismiss_toast() -> None           # dismiss the current toast, if any
```

> **Naming is normative — `show_toast` / `dismiss_toast` (verb-first).** The app calls
> `_lv.show_toast(cfg)` / `_lv.dismiss_toast()`. Do **not** name them `toast_show` /
> `toast_dismiss` (an earlier builder working-note sketch used that form — it is superseded
> by this contract). Identical names across platforms are the whole point.

### `show_toast(cfg)` — cfg schema

The cfg is the same dict shape the screen builders use, so it parses uniformly. It is
**policy-free**: the app resolves severity → glyph + colors (Python's
`Info`/`Success`/`Warning`/… toast subclasses) and passes the finished values; the library
renders exactly what it is told.

| key | type | required | default | meaning |
|---|---|---|---|---|
| `label_text` | `str` | **yes** | — | banner text; may contain `\n` for explicit line breaks (long lines also soft-wrap) |
| `icon` | `str` | no | *(none → text-only)* | a seedsigner-icon-font **PUA glyph** (e.g. `""`), i.e. a `SeedSignerIconConstants` value — the same glyph form button `icon`s use. Maps to the struct's `icon_glyph`. Omitted for an icon-less banner |
| `outline_color` | `int` | no | `0xFFFFFF` | `0xRRGGBB` — banner outline **and** icon color |
| `font_color` | `int` | no | `0xFFFFFF` | `0xRRGGBB` — message text color |
| `duration_ms` | `int` | no | `3000` | auto-dismiss delay in ms; **`0` = stay until dismissed/replaced** |

Colors cross the boundary as **`0xRRGGBB` ints** (a direct `uint32_t` for the native spec),
**not** hex strings. Absent optional keys are **omitted** by the app, and the native side
applies the defaults above.

In practice the app always sends `label_text`, `outline_color`, `font_color`, and
`duration_ms`; `icon` is present only for iconed toasts. The defaults exist for robustness,
not as a normal path.

### `dismiss_toast()`

Dismiss the currently-showing toast immediately (no-op if none). **LVGL-thread only** — see
Threading. The app does not call this from its producer threads; it is here for a future
LVGL-thread caller.

---

## Semantics the native overlay owns (do NOT reimplement in the binding or app)

- **Auto-dismiss** after `duration_ms` (`0` = persist until replaced/dismissed).
- **Input dismissal.** Hardware mode: **any** key/joystick press dismisses — and the press
  is **not consumed** (it also drives the underlying screen). Touch mode: a tap on the
  banner or a swipe-fling across it.
- **One at a time** — a newer toast replaces the current one (Python runs a single toast).
- **Screensaver coexistence** — showing a toast dismisses the screensaver if it is up
  ("new toasts break out of the screensaver"), and suppresses screensaver activation while
  the toast is showing. (Owned by `overlay_manager`; the app's `Controller` no longer
  coordinates this.)

---

## Threading (the critical constraint)

- **`show_toast` must be safe to call from any producer thread.** The app raises toasts
  from the SD-card detector thread **and** the main/pump thread. Route it through
  `overlay_manager_show_toast()`, which **stages** the request and drains it on the LVGL
  loop — never build widgets on the producer thread.
- **`dismiss_toast` is LVGL-thread only.** It wraps `toast_overlay_dismiss()`, which mutates
  the widget tree immediately. There is **no** thread-safe `overlay_manager_dismiss_toast()`
  today. The app therefore never dismisses cross-thread — a toast that should disappear
  "early" instead relies on `duration_ms` expiry or being replaced. If a thread-safe dismiss
  is ever required, add a marshalled `overlay_manager_dismiss_toast()` in
  `seedsigner-lvgl-screens` first, then relax this line.

---

## Native C API wrapped (`seedsigner-lvgl-screens`: `toast_overlay.h` / `overlay_manager.h`)

```c
typedef struct {
    const char *label_text;    // required; may contain '\n'
    const char *icon_glyph;    // seedsigner-icon PUA glyph, or NULL for text-only
    uint32_t    outline_color; // 0xRRGGBB — banner outline + icon color
    uint32_t    font_color;    // 0xRRGGBB — message text color
    uint32_t    duration_ms;   // auto-dismiss delay; 0 = stay until dismissed/replaced
} toast_overlay_spec_t;

void overlay_manager_show_toast(const toast_overlay_spec_t *spec); // thread-safe; staged, drained on the LVGL loop
void toast_overlay_show(const toast_overlay_spec_t *spec);         // LVGL-thread only, immediate (used by desktop tooling)
void toast_overlay_dismiss(void);                                  // LVGL-thread only
bool toast_overlay_is_active(void);
```

The spec's strings are only read during the call (the widget copies what it needs), so a
binding may build the struct on the stack from the cfg dict and pass a pointer to it.

---

## App-side caller (source of truth)

- `seedsigner/src/seedsigner/gui/lvgl_screen_runner.py` — `show_toast()` builds the cfg dict
  (icon glyph passed through as `icon`; PIL colors mapped to `0xRRGGBB` ints; `None` fields
  omitted) and calls `_lv.show_toast(cfg)`. `dismiss_toast()` wraps `_lv.dismiss_toast()`.
  Both no-op when the native module is absent (host/CI).
- `seedsigner/src/seedsigner/gui/toast.py` — the severity classes
  (`RemoveSDCardToastManagerThread`, `SDCardStateChangeToastManagerThread`, `InfoToast`,
  `SuccessToast`, `WarningToast`, `DireWarningToast`, `ErrorToast`) hold the policy
  (label / icon glyph / colors / duration) and push it via the runner seam.

---

## Implementation in this repo (Pi Zero `.so`) — DONE

Implemented in `native/python_bindings/toast.cpp`, registered in
`native/python_bindings/module.cpp` `methods[]`. As built (all conformant with the contract
above):

- `py_show_toast(cfg)` marshals the dict → `toast_overlay_spec_t` (`label_text` required str;
  `icon` str glyph or None → `icon_glyph`; `outline_color` / `font_color` read as **ints**
  via `PyLong_AsUnsignedLong`, default `0xFFFFFF`, masked to 24-bit; `duration_ms` int,
  default 3000) → **`overlay_manager_show_toast()`** (the deferred, thread-safe path).
- `py_dismiss_toast()` → `toast_overlay_dismiss()` (LVGL-pump thread).
- Exported as **`show_toast` / `dismiss_toast`** — names match the MicroPython binding.
- **Thread-safety:** the file supplies a real `std::mutex` behind the weak
  `overlay_manager_lock/unlock` hooks (robust even if a future producer drops the GIL around
  the call), so the SD-card detector thread and the pump thread can both raise toasts safely.

Background/wiring notes: `docs/toast-overlay-integration-todo.md`; a one-paragraph summary
also lives in `docs/interface-contract.md` under **Toast overlay**.

Remaining check (not a binding change): confirm the GPIO keypad indev's read path calls
`lv_display_trigger_activity` (the standard indev read does) so any hardware press dismisses
the toast via the idle clock — verify during device testing.

---

## Verification (once bound + rebuilt)

Device-verify on the Pi: each severity toast (info/success/warning/dire/error) renders with
the right icon + colors over a live LVGL screen; the SD-card toasts (insert/remove tip);
auto-dismiss after `duration_ms`; a key press dismisses without being swallowed; a toast
raised during the screensaver breaks it.
