# Input Button Behavior (Pi Zero / SeedSigner RaspPi LVGL)

Status: **authoritative behavioral spec for Pi hardware integration** in `seedsigner-raspi-lvgl`.

Scope:
- Defines expected behavior for joystick + KEY1/2/3 on Pi Zero hardware.
- Applies to native runtime integration used by this project.
- C-module implementation details may live in `seedsigner-c-modules`, but this document defines the hardware-facing behavior contract.

---

## 1) Hardware inputs

Pi controls considered in this project:
- Joystick directions: `UP`, `DOWN`, `LEFT`, `RIGHT`
- Joystick center click: `PRESS`
- Auxiliary keys: `KEY1`, `KEY2`, `KEY3`

GPIO mapping validation between BOARD and BCM/native line offsets has been verified by diagnostic tooling in this repo.

---

## 2) Core navigation model

Navigation uses **focus zones**:
- `TOP_NAV` zone (header controls)
- `BODY` zone (screen content controls)

General rule:
- `UP` / `DOWN` are used to move **between zones** when appropriate.
- Directional movement **within BODY** is layout-dependent (vertical/horizontal/grid).
- `PRESS` activates the currently focused control (`ENTER`).

---

## 3) Directional behavior by screen layout

### 3.1 Vertical body layout
Expected behavior:
- `UP` / `DOWN`: move focus through body controls.
- `LEFT` / `RIGHT`: no-op (unless screen explicitly overrides).
- `UP` into top boundary may move focus into `TOP_NAV` (if present/focusable).
- `DOWN` from `TOP_NAV` moves focus back into body.

### 3.2 Horizontal body layout
Expected behavior:
- `LEFT` / `RIGHT`: move focus through body controls.
- `UP` / `DOWN`: primarily zone transitions (`TOP_NAV` <-> `BODY`), not body traversal.
- `UP` from body enters `TOP_NAV` where allowed.
- `DOWN` from `TOP_NAV` returns to body.

### 3.3 Grid/mixed layout
Expected behavior:
- All four directions follow explicit neighbor relationships (or deterministic geometric fallback).
- Zone transitions still apply for top-nav integration.

---

## 4) Top-nav behavior (global assumption)

Assumption: screens generally include a top nav region.

Requirements:
- Top nav participates in focus when present.
- Navigation into/out of top nav is explicit and predictable.
- `ENTER` on top-nav controls triggers their mapped action (e.g., back/power where applicable).

---

## 5) KEY1 / KEY2 / KEY3 policy

These keys are intentionally flexible.

## 5.1 Default policy (most screens)
- `KEY1`, `KEY2`, `KEY3` behave as optional enter buttons.
- Equivalent to joystick `PRESS` (activate focused control).

## 5.2 Screen-specific override policy
Some screens may assign distinct per-key actions.

When override is enabled for a screen, each key can be configured as one of:
- `enter` (activate focused control)
- `noop` (ignored by LVGL action layer)
- `emit` (forward key event to Python/controller layer; no LVGL action)
- `custom` (screen-specific handler)

Interim requirement:
- Do **not** globally hardwire `KEY1/KEY3` to unrelated generic LVGL keys (e.g., ESC/HOME) unless a given screen explicitly wants that behavior.

---

## 6) Non-goals / anti-patterns

Avoid:
- Global remaps that assume all screens are linear vertical lists.
- Forcing `UP/DOWN` to prev/next on horizontal layouts.
- Implicit screen behavior that differs by hardware without explicit configuration.

---

## 7) Implementation notes (cross-project boundary)

This behavioral spec is maintained in `seedsigner-raspi-lvgl` because it is hardware-facing.

Implementation may require updates in:
- `seedsigner-raspi-lvgl` native integration code
- `seedsigner-c-modules` LVGL screen/input handling

Until both sides are aligned, this document is the contract used to evaluate correctness.

---

## 8) Diagnostic tooling

Repo diagnostic script:
- `scripts/pi_gpio_mapping_diagnostic.py`

Use it to confirm hardware pin interpretation before changing logical navigation behavior.
