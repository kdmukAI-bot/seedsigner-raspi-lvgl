# Input and Navigation Behavior

Authoritative behavioral spec for Pi hardware integration in `seedsigner-raspi-lvgl`.

Defines expected behavior for joystick + KEY1/KEY2/KEY3 on Pi Zero hardware.
C-module implementation details live in `seedsigner-c-modules`; this document
defines the hardware-facing behavior contract.

---

## Hardware inputs

- Joystick directions: `UP`, `DOWN`, `LEFT`, `RIGHT`
- Joystick center click: `PRESS` (maps to `ENTER`/`ACTIVATE`)
- Auxiliary keys: `KEY1`, `KEY2`, `KEY3`

No touchscreen. No dedicated hardware BACK button.

---

## Focus zone model

Every interactive screen has two focus zones:
1. **TOP_NAV** — header controls (Back and/or Power)
2. **BODY** — screen content (buttons, list items, etc.)

Invariants:
- Focus is in exactly one zone at a time.
- Exactly one UI element is active/focused at any time (single-active-focus).
- Moving focus clears the previous element's active state, including across zones.

---

## Default input mapping

| Hardware | LVGL key | Logical action |
|----------|----------|----------------|
| UP | `LV_KEY_UP` | Navigate up / zone transition to TOP_NAV |
| DOWN | `LV_KEY_DOWN` | Navigate down / zone transition to BODY |
| LEFT | `LV_KEY_LEFT` | Navigate left within zone |
| RIGHT | `LV_KEY_RIGHT` | Navigate right within zone |
| PRESS | `LV_KEY_ENTER` | Activate focused control |
| KEY1 | `'1'` | Aux key (see policy below) |
| KEY2 | `'2'` | Aux key (see policy below) |
| KEY3 | `'3'` | Aux key (see policy below) |

KEY1/KEY2/KEY3 are sent as character codes `'1'`/`'2'`/`'3'`, recognized by
`is_aux_key()` in `navigation.cpp`. They are **not** mapped to generic LVGL
keys like ESC/HOME.

---

## Zone transfer rules

1. **BODY → TOP_NAV**: `UP` when focus is on the topmost body element
   transfers focus to TOP_NAV (if present and focusable).
2. **TOP_NAV → BODY**: `DOWN` from TOP_NAV returns to the previously
   focused body element (or the first element if none).
3. **Within zone**: directional keys navigate according to the zone's layout.
4. **ENTER**: activates the focused control regardless of zone.

---

## Directional behavior by layout

### Vertical body (e.g., button_list_screen)
- `UP`/`DOWN`: move through body controls.
- `LEFT`/`RIGHT`: no-op unless screen overrides.
- `UP` past top item → TOP_NAV.

### Horizontal body
- `LEFT`/`RIGHT`: move through body controls.
- `UP`/`DOWN`: zone transitions only.

### Grid/mixed
- All four directions follow neighbor relationships.
- Zone transitions still apply at grid boundaries.

---

## KEY1 / KEY2 / KEY3 policy

### Default (most screens)
All three keys act as `ENTER` — activate the focused control.

### Per-screen override
Screens may configure each key independently as:
- `enter` — activate focused control
- `noop` — ignored
- `emit` — forward to Python/controller layer, no LVGL action
- `custom` — screen-specific handler

This is configured via `nav_aux_policy_t` in the c-modules navigation system.

**Rule**: never globally hardwire KEY1/KEY3 to unrelated LVGL keys (ESC/HOME)
unless a screen explicitly requests it.

---

## Worked example: button_list_screen (3 body buttons)

Setup: TOP_NAV has `[Back]`, BODY has `[A] [B] [C]`, initial focus on `A`.

| Input | Result |
|-------|--------|
| `UP` on A | Focus → Back (TOP_NAV) |
| `DOWN` on Back | Focus → A (BODY) |
| `DOWN` on A | Focus → B |
| `DOWN` on B | Focus → C |
| `PRESS` on C | Activates C, emits `("button_selected", 2, "C")` |
| `PRESS` on Back | Emits `("topnav_back", -1, "topnav_back")` |

---

## Anti-patterns

- Global remaps that assume all screens are vertical lists.
- Forcing UP/DOWN to prev/next on horizontal layouts.
- Implicit behavior that differs by hardware without explicit config.

---

## Event bridge to Python

LVGL owns focus state, selection transitions, and zone movement internally.
Python View code receives:
- Activation events (`button_selected`, `topnav_back`, `topnav_power`)
- KEY1/KEY2/KEY3 events (when `emit` policy is active)

Directional navigation is internal to LVGL — not forwarded to Python unless
a screen explicitly enables it.

---

## Diagnostic tooling

- `scripts/pi_gpio_mapping_diagnostic.py` — confirms hardware pin
  interpretation before changing navigation behavior.
