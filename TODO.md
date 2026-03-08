# TODO - seedsigner-raspi-lvgl (Reworked Plan)

Primary objective: run the **existing SeedSigner C/C++ LVGL screen code** on **Pi Zero (armv6l)** and access screens through Python bindings with MicroPython-parity signatures.

## Clarified direction (authoritative)
- Reuse LVGL screen implementation from `seedsigner-c-modules` (do not re-implement screen behavior in Python).
- Build C/C++ artifacts for Pi Zero architecture.
- Expose direct Python bindings (CPython) matching MicroPython call signatures.
- Keep Python View/business logic usage unchanged.

---

## Stage A — Codebase extraction boundary (portable core)
1. Identify minimal portable screen core from:
   - `components/seedsigner/*`
   - required LVGL glue/util files
2. Isolate ESP32/FreeRTOS/IDF-specific dependencies behind platform shims.
3. Define Pi platform shim interfaces:
   - timing/ticks
   - display flush
   - input polling
   - result queue bridge

Deliverable: buildable screen-core target decoupled from ESP32 runtime specifics.

## Stage B — Docker-first cross-build foundation (early)
1. Create canonical Docker image for Pi-target builds.
2. Ensure local machines can produce Pi artifacts from Docker only.
3. Add same Docker entrypoint to GitHub Actions.
4. Align image/toolchain assumptions with SeedSigner production Pi OS baseline (`seedsigner-os`, Buildroot).
5. Validate against dev Pi OS baseline (Raspberry Pi OS build instructions) and track any drift.
6. Pin toolchain + LVGL dependency versions for reproducibility in `versions.lock.toml`.
7. Enable ccache in builder images/scripts with clear/show/disable controls.

Deliverable: deterministic local+CI build path for Pi artifacts with explicit dev-vs-production OS parity checks and pinned dependency lock.

## Stage C — Pi platform backend (native runtime)
1. Display backend:
   - ST7789 SPI transport (240x240)
   - `PX_MULTIPLIER=100`
2. Input backend:
   - hard-coded board pins from SeedSigner Python drivers
   - hold-repeat timing parity (225ms / 250ms)
3. Runtime loop integration for LVGL tick/task handling.

Deliverable: native Pi runtime able to host compiled LVGL screens.

## Stage D — CPython bindings (exact parity surface)
1. Build Python extension/wrapper exposing exact MicroPython-equivalent signatures:
   - `button_list_screen(cfg_dict)`
   - `clear_result_queue()`
   - `poll_for_result()`
2. Preserve tuple/event semantics used by existing View code.
3. Keep direct function-call model (no request/response server envelope).

Deliverable: Python can call compiled screens directly on Pi.

## Stage E — Proof of concept (highest priority)
1. Wire compiled `button_list_screen` from C/C++ implementation through CPython bindings.
2. Run on actual Pi Zero hardware.
3. Demonstrate end-to-end flow:
   - invoke screen from Python
   - navigate with joystick/buttons
   - read selection via `poll_for_result()`
4. Verify parity against `repl_test_button_list_flow.py` behavior for equivalent scenario.

Deliverable: first real compiled-screen-on-Pi demo via Python bindings.

## Stage F — Expand parity + hardening
1. Add additional screens from C/C++ set.
2. Complete top-nav focus region parity.
3. Add debounce/filtering hardening where needed.
4. Add stress/perf measurements and report deltas vs current Python path.

Deliverable: stable, broader parity implementation.

---

## Immediate next tasks (re-prioritized)
1. Create `docs/porting-boundary.md` listing portable vs ESP32-specific code units.
2. Add Docker Pi cross-build skeleton (`Dockerfile`, build entrypoint, CI stub).
3. Build a minimal CPython extension that links compiled screen-core and exposes one callable screen binding.
4. Validate PoC on Pi Zero hardware with `button_list_screen` and poll-result flow.

## Deferred / now secondary
- Python-only screen behavior scaffolding remains useful for quick tests but is **not** the main implementation path.
