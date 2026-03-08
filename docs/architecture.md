# Architecture (Reworked): Compiled SeedSigner LVGL Screens on Pi Zero

## Objective
Run the **existing SeedSigner C/C++ LVGL screen implementation** on Pi Zero (armv6l), exposed to Python through bindings that match MicroPython usage.

## Non-goal
Rewriting screen logic in Python. Python should orchestrate Views/business flow; compiled C/C++ should own screen behavior/rendering.

## Core constraints
1. Reuse `seedsigner-c-modules` screen code as source of truth.
2. CPython binding signatures must match MicroPython equivalents.
3. Keep direct function-call model (`button_list_screen(cfg_dict)` etc.).
4. Build flow must be Docker-first and identical in local + GitHub Actions.
5. Pi target profile: 240x240 ST7789, joystick+KEY1/2/3, `PX_MULTIPLIER=100`.

## High-level architecture

### 1) Portable screen core (C/C++)
- Derived from `components/seedsigner/*` + required helpers.
- Contains screen logic, LVGL object/state handling, result emission hooks.
- Must be isolated from ESP-IDF/FreeRTOS specifics.

### 2) Platform shims
- **ESP32 shim** (existing path)
- **Pi shim** (new path)

Pi shim responsibilities:
- ST7789 flush and framebuffer transport over SPI
- input polling (GPIO active-low, repeat timing parity)
- time/tick integration for LVGL
- queue handoff to Python binding layer

### 3) CPython binding layer
- Native extension module (or equivalent compiled binding) named `seedsigner_lvgl`.
- Exposes MicroPython-parity call surface:
  - `button_list_screen(cfg_dict)`
  - `clear_result_queue()`
  - `poll_for_result()`
- Marshals Python dicts/tuples/lists into screen-core context.
- Returns queue tuples with stable parity semantics.

### 4) Python View layer
- Existing business logic calls bound screen functions directly.
- Polls for results exactly as current flow expects.

## Build/release model
- Canonical Docker build environment for Pi artifacts.
- Same entrypoint used by local dev and GitHub Actions.
- Produce deterministic outputs (pinned toolchain/deps).

## Proof-of-concept target (must-hit)
Compiled `button_list_screen` running on real Pi Zero, invoked from Python bindings, with interactive input and poll-for-result tuple return parity.

## Implementation sequence

### S1 — Porting boundary map
- Enumerate C/C++ units to keep vs shim/replace.
- Document external deps needing Pi alternatives.

### S2 — Docker Pi build skeleton
- Add Dockerfile + build script for armv6l target artifacts.
- Add CI stub calling same script.

### S3 — Minimal native binding
- Compile portable core + Pi shim into Python-loadable module.
- Expose one screen binding (`button_list_screen`) + result queue API.

### S4 — Pi hardware PoC
- Launch from Python on Pi Zero.
- Verify navigation + selection + queue return semantics.

### S5 — Parity expansion
- Add additional screens and top-nav focus-region parity.
- Harden input filtering/debounce as needed.

## Current status note
Existing Python-side scaffolding in this repo is considered temporary support code unless backed by compiled screen-core integration.

## Durable references
- C modules root:
  `/home/keith/.openclaw/workspace-mp-project-lead/dev/seedsigner-micropython-builder/sources/seedsigner-c-modules`
- MicroPython binding entrypoint:
  `/home/keith/.openclaw/workspace-mp-project-lead/dev/seedsigner-micropython-builder/sources/seedsigner-c-modules/bindings/modseedsigner_bindings.c`
- C screen implementation entrypoint:
  `/home/keith/.openclaw/workspace-mp-project-lead/dev/seedsigner-micropython-builder/sources/seedsigner-c-modules/components/seedsigner/seedsigner.cpp`
- Parity reference flow:
  `/home/keith/.openclaw/workspace-mp-project-lead/dev/seedsigner-micropython-builder/sources/seedsigner-c-modules/tests/repl_test_button_list_flow.py`
- Pi hardware reference:
  `docs/hardware-profile.md`
