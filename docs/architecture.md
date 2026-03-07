# Architecture (Pi Zero LVGL Screen Runtime)

Defines the implementation architecture for `seedsigner-raspi-lvgl`.

## Scope

- Replace Python Screen implementations with compiled LVGL screen bindings on Pi Zero.
- Preserve existing Python View/business logic call patterns.
- Match MicroPython C binding semantics for screen invocation + polling.
- Do not modify `seedsigner-c-modules` in this phase.

## Hard requirements

1. Direct binding call model (no request/response server layer).
2. Same invocation style as MicroPython tests:
   - `seedsigner_lvgl.button_list_screen(cfg_dict)`
3. Same polling approach for results/events:
   - `seedsigner_lvgl.clear_result_queue()`
   - `seedsigner_lvgl.poll_for_result()`
4. Navigation semantics from `docs/navigation-contract.md`.
5. Hardware/pin/timing profile from `docs/hardware-profile.md`.
6. Pi Zero UI compile setting:
   - `PX_MULTIPLIER=100` (no scaling).

## Component boundaries

### 1) Python View layer (existing app logic)
Responsibilities:
- Orchestrates screen flow.
- Calls bound LVGL screen functions.
- Polls for events/results.
- Handles business decisions from returned values.

Non-responsibilities:
- Focus navigation internals.
- Low-level GPIO/SPI handling.

### 2) Pi binding facade (`seedsigner_lvgl` for CPython)
Responsibilities:
- Expose Python binding function signatures as an exact match to MicroPython equivalents.
- Marshal Python dict/list/tuple configs into LVGL runtime inputs.
- Surface queue-based events/results back to Python.
- Keep return/event semantics parity with current MicroPython behavior.

### 3) LVGL screen runtime (compiled)
Responsibilities:
- Render screen UI (e.g., `button_list_screen`).
- Own focus state and single-active-selection invariant.
- Apply top-nav/content navigation region rules.
- Emit standardized result events to binding queue.

### 4) Platform backend (Pi-specific)
Responsibilities:
- ST7789 display flush path over SPI.
- GPIO input polling and debounced key states.
- Hold-repeat timing semantics:
  - first repeat: 225 ms
  - repeat interval threshold: 250 ms

## Runtime data flow

1. View calls `seedsigner_lvgl.button_list_screen(cfg_dict)`.
2. Binding validates config and invokes compiled LVGL screen function.
3. LVGL runtime initializes screen + focus state (single active control).
4. Input loop processes joystick/buttons from GPIO backend.
5. LVGL updates selection/focus + triggers action handling.
6. Binding queue receives result/event payloads.
7. View polls via `poll_for_result()` until terminal selection/result.

## First screen target: `button_list_screen`

Required config support:
- `top_nav.title`
- `top_nav.show_back_button`
- `top_nav.show_power_button`
- `button_list` as either:
  - list of strings, or
  - list of `(label, value)` tuples

Expected behavior:
- Top nav contains back or power or neither (never both active actions at once in behavior model).
- `UP` at top content transfers focus to top nav (if present).
- `DOWN` from top nav returns focus to previous content element.
- `SELECT` on body item returns selection payload expected by View code.

## Error handling

- Invalid configs produce deterministic structured errors compatible with current Python handling.
- No crashes on empty/non-focusable layouts; stable fallback focus behavior required.

## Build/release strategy (reproducibility)

- Use a Dockerized build environment as the canonical build path.
- Developers should be able to build locally via the same containerized entrypoint used in CI.
- GitHub Actions must invoke the same Docker build flow (no separate bespoke CI-only build logic).
- Goal: local/CI parity similar to `seedsigner-micropython-builder` conventions.

## Performance goals (initial)

- Maintain perceived responsiveness at least equal to current Python path.
- Improve input-to-visible-change latency via compiled screen rendering.
- Preserve deterministic UI behavior under repeated key holds.

## Milestones (first working demo)

### M1 — Binding skeleton
- [ ] Create CPython `seedsigner_lvgl` module with stubs:
  - `button_list_screen(cfg_dict)`
  - `clear_result_queue()`
  - `poll_for_result()`
- [ ] Queue API works from Python REPL.

### M2 — Display backend bring-up
- [ ] ST7789 init + flush on Pi Zero using documented pins/SPI settings.
- [ ] Verify 240x240 output and `PX_MULTIPLIER=100`.
- [ ] Add simple render sanity test pattern.

### M3 — Input backend parity
- [ ] GPIO mapping hard-coded to documented SeedSigner Pi profile.
- [ ] Debounce + hold-repeat timings match `buttons.py` behavior.
- [ ] Emit directional/select/key1/key2/key3 events to LVGL runtime.

### M4 — `button_list_screen` vertical slice
- [ ] Render top nav + body list.
- [ ] Enforce single-active-focus and region transfer rules.
- [ ] Return selection result via poll queue in expected shape.

### M5 — Parity validation
- [ ] Run a Pi equivalent of `repl_test_button_list_flow.py`.
- [ ] Confirm expected flow across multi-step screen transitions.
- [ ] Document any semantic deltas before adding more screens.

## Durable references

- MicroPython bindings:
  - `/home/keith/.openclaw/workspace-mp-project-lead/dev/seedsigner-micropython-builder/sources/seedsigner-c-modules/bindings/modseedsigner_bindings.c`
- C screen implementation:
  - `/home/keith/.openclaw/workspace-mp-project-lead/dev/seedsigner-micropython-builder/sources/seedsigner-c-modules/components/seedsigner/seedsigner.cpp`
- Parity REPL flow:
  - `/home/keith/.openclaw/workspace-mp-project-lead/dev/seedsigner-micropython-builder/sources/seedsigner-c-modules/tests/repl_test_button_list_flow.py`
- Existing navigation contract:
  - `docs/navigation-contract.md`
- Existing interface contract:
  - `docs/interface-contract.md`
- Existing hardware profile:
  - `docs/hardware-profile.md`
