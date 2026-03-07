# Interface Contract (Pi Zero LVGL ↔ Python View)

Defines the compatibility contract for invoking compiled LVGL Screens from Python View code.

## Purpose

Pi Zero LVGL screens must present the same direct binding call semantics as the existing MicroPython C bindings so Python View logic can remain unchanged.

## Compatibility goals

1. **Call shape parity**
   - Screen invocation payload structure matches MicroPython binding expectations.
2. **Event naming parity**
   - Action/event names are identical across platforms.
3. **Result parity**
   - Returned JSON payload shape matches existing View expectations (e.g., selected value contracts).
4. **Behavioral parity**
   - Equivalent user actions produce equivalent event/result sequences.

## Terminology

- **View**: Python business/controller logic.
- **Screen**: compiled LVGL display implementation.
- **Binding**: direct C-exposed function callable from Python (no request/response server layer).

## Binding model requirements

- Python calls screen functions directly via bindings (e.g., `seedsigner_lvgl.button_list_screen(cfg_dict)`).
- No generic invoke envelope, router, or middle request/response server is assumed.
- Config dict shape and return/result behavior must match MicroPython binding behavior.
- Invalid config must raise/return deterministic errors compatible with existing Python handling.
- For Pi Zero target, hardware pin assignments may be hard-coded to match the existing Python display/buttons implementation (single supported hardware profile).

## Shared-codebase direction

Target architecture is a single Python codebase shared across:
- Raspberry Pi Zero runtime (CPython)
- Microcontroller runtime (MicroPython)

The interface should remain stable and common across both environments, with platform-specific differences isolated to transport/runtime internals.

## Required call-signature parity

Screen invoke requests must match the existing MicroPython binding call signature, not a newly invented envelope.

Current `button_list_screen` call pattern (from MicroPython bindings/tests):
- Python calls: `seedsigner_lvgl.button_list_screen(cfg_dict)`
- `cfg_dict` contains:
  - `top_nav` object:
    - `title` (string)
    - `show_back_button` (bool)
    - `show_power_button` (bool)
  - `button_list`:
    - list of labels (strings), or
    - list of `(label, value)` tuples

Reference instantiation style (must be preserved on Pi):
```python
seedsigner_lvgl.button_list_screen({
    'top_nav': {
        'title': 'Favorite Type of Pet',
        'show_back_button': False,
        'show_power_button': False,
    },
    'button_list': [
        ('Dog', 'Dog'),
        ('Cat', 'Cat'),
        ('Fish', 'Fish'),
    ]
})
```

Typical use case on this target:
- `top_nav.show_back_button = true`
- `top_nav.show_power_button = false`
- 2–3 body buttons in `button_list`

Compatibility rule:
- Pi runtime must accept this same call style and `cfg_dict` shape and produce equivalent result behavior expected by existing View code.

## Direct call + result semantics

### 1) Direct screen call (no invoke envelope)

```python
seedsigner_lvgl.button_list_screen({
    'top_nav': {
        'title': 'Select network',
        'show_back_button': True,
        'show_power_button': False,
    },
    'button_list': [
        ('Mainnet', 'mainnet'),
        ('Testnet', 'testnet'),
    ],
})
```

### 2) Events/results exposed to View code

Required compatibility behavior:
- `SELECT`, `KEY1`, `KEY2`, `KEY3` actions are visible to View logic with MicroPython-equivalent semantics.
- Screen completion returns the selected value/result in the same structure expected by existing View flow.
- Directional navigation (`UP/DOWN/LEFT/RIGHT`) is primarily internal to LVGL; optional mirroring to View is allowed where required.

Polling model requirement (match MicroPython bindings):
- Python View code should poll for input/result events via a queue-style API, consistent with existing `clear_result_queue()` + `poll_for_result()` flow.
- Result/event payload shapes must match current MicroPython expectations (e.g., `("button_selected", index, label)` patterns where applicable).
- Do not replace polling with callback-only semantics in the shared View path.

### 3) Error behavior

- Invalid config should produce deterministic, structured errors compatible with existing Python-side handling.

## Screen contract rules

1. **Single active selection**
   - At most one active/selected control at a time across top-nav + content.
2. **Top-nav model**
   - Top-nav has at most one action: `back` or `power` or none.
3. **Navigation**
   - `UP` from top content row transfers focus to top-nav (when present).
   - `DOWN` from top-nav returns to prior content focus.
4. **No long-press actions**
   - Long-press action semantics are out of scope for current target.
5. **Hold-repeat support**
   - Directional hold-repeat supported with configured initial delay + repeat interval.

## Determinism requirements

- Given same screen config dict and same input sequence, output events/results must be deterministic.
- Result payload ordering and key naming must be stable.
- Time-dependent metadata should be excluded unless explicitly required.

## Minimum compatibility test cases

1. `button_list_screen` returns selected value in expected key.
2. `key1/key2/key3` propagate to View as expected events.
3. `select` activation emits/returns identical semantics to MicroPython bindings.
4. Top-nav transitions (`UP`/`DOWN`) match navigation contract.
5. Invalid params produce a stable structured error payload.

## Implementation note

This document defines the external contract. Platform-specific internals (threading, render loop, SPI backend details) must not leak into the JSON schema.
