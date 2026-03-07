# Navigation Contract

Defines canonical input semantics for the Raspberry Pi Zero + LVGL target.

This contract is intentionally independent of any one screen implementation so behavior remains consistent as screens are ported.

## Hardware inputs

Physical controls (mirroring current SeedSigner button model):
- D-pad: `UP`, `DOWN`, `LEFT`, `RIGHT`
- Center click: `SELECT`
- Side buttons: `KEY1`, `KEY2`, `KEY3`

No touchscreen is assumed.
No dedicated hardware BACK button is assumed.

## Logical actions

UI layers should consume logical actions, not raw GPIO events:
- `NAV_UP`
- `NAV_DOWN`
- `NAV_LEFT`
- `NAV_RIGHT`
- `ACTIVATE` (primary select/confirm)
- `TOPNAV_LEFT_ACTION`
- `TOPNAV_RIGHT_ACTION`

## Default input mapping

| Hardware input | Default logical action | Notes |
|---|---|---|
| `UP` | `NAV_UP` | At top boundary, transfers focus to top nav (see below). |
| `DOWN` | `NAV_DOWN` | If focus is in top nav, `DOWN` returns to content region. |
| `LEFT` | `NAV_LEFT` | Within content or top nav, moves to previous focus target. |
| `RIGHT` | `NAV_RIGHT` | Within content or top nav, moves to next focus target. |
| `SELECT` | `ACTIVATE` | Activates currently focused content/top-nav target. |
| `KEY1` | `TOPNAV_LEFT_ACTION` | Usually Back in top-nav model. Screen may override. |
| `KEY2` | `TOPNAV_RIGHT_ACTION` | Usually Power/Tools; screen may override. |
| `KEY3` | Screen-defined auxiliary action | Optional contextual shortcut. |

> Note: Side-button actions are screen/context aware; avoid hard-coding global behavior where it conflicts with top-nav state.

## Typical SeedSigner screen model (e.g., button_list_screen)

This contract explicitly supports the common SeedSigner layout pattern:
- A persistent `TOP_NAV` row with Back and/or Power actions.
- A `CONTENT` body containing one or more selectable buttons/items.

`button_list_screen`-style behavior is the reference interaction model for initial ports.

## Focus regions

Every interactive screen is split into regions:
1. `TOP_NAV` region (Back/Power style controls)
2. `CONTENT` region (list/form/body controls)

Focus must be in exactly one region at a time.
Only one UI element on screen may be selected/active at any time (single-active-focus invariant).

## Selection model (single active control)

- Joystick directional movement updates which control is selected.
- Selection is a visual + logical active state used for subsequent `SELECT` activation.
- Moving focus to a new element must clear selected/active state from the previous element.
- This applies across regions (`TOP_NAV` and `CONTENT`), not just within one region.

## Region transfer rules (mandatory)

1. **Content → Top Nav**
   - If current focus is at the first (topmost) navigable content element and user presses `UP`, transfer focus to `TOP_NAV`.

2. **Top Nav → Content**
   - Pressing `DOWN` from `TOP_NAV` transfers focus back to the previously focused content element, or first content element if no prior element exists.

3. **Intra-region navigation**
   - `LEFT/RIGHT/UP/DOWN` should navigate within the currently focused region according to that region’s layout.

4. **Activation**
   - `SELECT` activates the currently focused target regardless of region.

## Back behavior

Because there is no dedicated BACK hardware button:
- Preferred path: navigate to `TOP_NAV` and activate Back.
- No long-press fallback is supported in the current target scope.

## Worked example: `button_list_screen` (3 body buttons)

Assume:
- `TOP_NAV` has exactly one control: `[Back]` **or** `[Power]`, or no control.
- `CONTENT` has `[Option A] [Option B] [Option C]` (top-to-bottom)
- Initial focus is `Option A`

Example transitions (top-nav has `Back`):
1. `UP` on `Option A` → focus moves to `Back` in `TOP_NAV`.
2. `LEFT` or `RIGHT` in `TOP_NAV` → no-op (single target).
3. `DOWN` in `TOP_NAV` → focus returns to last content focus (`Option A`).
4. `DOWN` in `CONTENT` from `Option A` → `Option B`.
5. `DOWN` from `Option B` → `Option C`.
6. `SELECT` on a content option activates that option.
7. `SELECT` on `Back` pops current screen.

Variant:
- If top-nav has `Power`, `SELECT` on top-nav opens power action flow.
- If top-nav has no control, `UP` at top content remains a no-op unless screen defines a custom override.

Notes:
- Contract rule: top-nav presents at most one actionable control (`Back` or `Power`) on this target.
- If content has no focusable elements and top-nav has a control, focus remains in top-nav.

## Event handling requirements

1. Debounce physical inputs.
2. Support joystick hold-repeat for directional keys with explicit timing controls (initial delay + repeat interval), aligned with current `buttons.py` behavior.
3. Preserve deterministic ordering when multiple events are queued.
4. Ignore impossible transitions (e.g., navigation with no focusable elements) without crashing.

## Python interface boundary (event bridge)

The LVGL display/input layer must interoperate with existing Python application code.

Terminology alignment with current codebase:
- **View** = Python business logic/controller layer.
- **Screen** = display implementation/output layer.
- Target direction: replace Python Screen implementations with compiled LVGL Screens while preserving View contracts.

Interface compatibility requirement:
- Use the same JSON interface layer semantics as the MicroPython C bindings.
- From Python View code, invoking an LVGL Screen on Pi should look the same as invoking LVGL Screen bindings from MicroPython.
- Request/response shapes, action event names, and result payload formats must remain contract-compatible.

Required upstream events to Python:
- `SELECT` activation events
- `KEY1`, `KEY2`, `KEY3` press events
- Screen result payloads on completion (e.g., `button_list_screen`-style selected return value)

Directional navigation events (`UP/DOWN/LEFT/RIGHT`):
- Primarily consumed inside LVGL for focus/selection movement.
- May be optionally forwarded to Python as mirrored navigation events when a screen/workflow requires app-level handling.
- Forwarding policy should be explicit per screen/profile to avoid duplicate handling.

Recommended split:
- LVGL layer owns focus state, selected-state transitions, and top-nav/content region movement.
- Python layer owns screen/business actions triggered by activation/aux keys and any explicitly forwarded navigation hooks.

## Screen responsibilities

Each screen must define:
- focusable elements in content region
- top-nav controls exposed on that screen
- optional overrides for `KEY1/KEY2/KEY3`
- whether directional events are mirrored to Python for that screen

## Test checklist (minimum)

For every screen class (list, menu, dialog, data-entry):
1. `UP` at top content moves focus to top nav.
2. `DOWN` from top nav restores content focus.
3. `SELECT` triggers correct action in both regions.
4. `KEY1/KEY2/KEY3` execute expected context actions.
5. Rapid repeated directional input does not corrupt focus state.

## Versioning

When behavior changes, increment contract version in commit notes and update this document before merging.
