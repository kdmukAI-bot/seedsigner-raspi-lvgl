# Spec: Align the LVGL screen result contract with SeedSigner's return-code convention

**Audience:** the agent working in `seedsigner-c-modules`.
**Status:** proposal for review. Do not commit without the maintainer's sign-off.
**Owning repo for this change:** `seedsigner-c-modules` (it is the single source of truth
for the screen→host result contract). Both consumers — `seedsigner-raspi-lvgl` (CPython
`.so`) and `seedsigner-micropython-builder` (MicroPython module) — depend on this contract
and will be updated in lockstep (see "Migration & coordination").

---

## 1. Background

The portable LVGL screens report user interactions to the host through two weak C callbacks:

```c
void seedsigner_lvgl_on_button_selected(uint32_t index, const char *label);  // discrete choice
void seedsigner_lvgl_on_text_entered(const char *text);                      // free text
```

This two-callback split is correct and stays. It maps cleanly onto the only two return
*shapes* SeedSigner screens ever produce:

- **int-valued** outcomes — a body button was chosen, or a top-nav action fired.
- **string-valued** outcomes — a text-entry screen (passphrase, etc.) was confirmed.

(Bulk/acquired data — camera entropy frames, decoded QR payloads — never travels through
these callbacks; it flows through separate side-channel objects. Out of scope here.)

## 2. The problem being fixed

Today, `on_button_selected` overloads a magic sentinel plus a **string label** to signal
non-body events:

- `components.cpp` top-nav callback: `seedsigner_lvgl_on_button_selected(0xFFFFFFFFu, "topnav_back" | "topnav_power")`
- `seedsigner.cpp` screensaver: `seedsigner_lvgl_on_button_selected(0xFFFFFFFFu, "screensaver_dismiss")`

So the host must inspect `index == 0xFFFFFFFF` **and** `strcmp` the label to learn what
happened. This is stringly-typed, redundant, and — critically — does **not** match the
convention the shared SeedSigner Python code already uses.

SeedSigner's Python C&C layer (the *same* code that runs on both Pi Zero/CPython and
ESP32/MicroPython) already has a clean, uniform convention in
`seedsigner/src/seedsigner/gui/screens/screen.py`:

```python
RET_CODE__BACK_BUTTON  = 1000
RET_CODE__POWER_BUTTON = 1001
```

A button selection returns its **index** (`0..N-1`); back/power return reserved large
integers that can never collide with an index. The View just reads one int and compares.

The C side should speak that same language. Because there is exactly **one** consumer
codebase (the shared Python app), matching its established numeric convention is consistency
within one co-designed system — not inappropriate coupling.

## 3. The target contract

### 3.1 `on_button_selected(uint32_t index, const char *label)` — int-valued results

`index` is the **single load-bearing value**:

- `0 .. N-1` — body button position (0-based).
- `1000` — top-nav **back** (same value as Python `RET_CODE__BACK_BUTTON`).
- `1001` — top-nav **power** (same value as Python `RET_CODE__POWER_BUTTON`).
- `1100` — **screensaver dismiss** (a host-handled event, not routed to a Python View).

Button counts are tiny, so body indices never approach the reserved values — the same
assumption the Python side already makes. No formal band scheme or runtime enforcement is
needed; this is just the de-facto layout.

`label` becomes **informational only** (button text for body buttons; a short readable tag
like `"back"`/`"power"`/`"screensaver_dismiss"` otherwise). The host must not need to read
`label` to determine the outcome — keep it populated only for logging and the desktop tools'
status line.

### 3.2 `on_text_entered(const char *text)` — string-valued results

Unchanged. Text-entry screens call this on confirm with the entered string. **Backing out of
a text-entry screen still goes through `on_button_selected(1000, ...)`**, never through
`on_text_entered`. (The passphrase screen already does this — its OK key calls
`on_text_entered`, its top-nav back fires the back callback. Preserve that.)

### 3.3 Signatures do not change

Keep both callback signatures exactly as-is. This is a change in the *values* passed to
`index`, not an ABI change. That keeps the blast radius small and lets both bridges adapt by
reading `index` instead of `strcmp`-ing `label`.

## 4. Required changes in `seedsigner-c-modules`

Paths below are relative to the c-modules repo root. Line numbers are approximate (from the
current HEAD) — confirm by re-grepping.

1. **Define the three constants wherever is convenient** — a local `#define`/`const` near
   the call sites is fine; they do **not** need a public header. Yes, the values are
   hardcoded, but that's unavoidable and harmless: there are only the two Python-mirrored
   values (back/power) plus the local screensaver code, and the Python values haven't changed
   in the project's ~five-year history. A one-line comment noting that `1000`/`1001` match
   `RET_CODE__*` in `screen.py` is plenty. Example:

   ```c
   #define SEEDSIGNER_RET_BACK_BUTTON          1000u  /* matches RET_CODE__BACK_BUTTON in screen.py  */
   #define SEEDSIGNER_RET_POWER_BUTTON         1001u  /* matches RET_CODE__POWER_BUTTON              */
   #define SEEDSIGNER_RET_SCREENSAVER_DISMISS  1100u  /* host-handled, not Python-routed             */
   ```

2. **Update the top-nav callback** (`components/seedsigner/components.cpp`, the
   `top_nav_button_event_callback` around line 36–45). It currently always passes
   `0xFFFFFFFFu` with the label discriminating. Change it to pass the reserved code based on
   which top-nav button fired. The buttons are registered with `event_label` user-data
   (`"topnav_back"` / `"topnav_power"`, around lines 88/92) — map that to the numeric code
   (e.g. a small lookup, or register the numeric code as user-data instead of / alongside the
   string). End state: `on_button_selected(SEEDSIGNER_RET_BACK_BUTTON, "back")` etc.

3. **Update the screensaver dismiss calls** (`components/seedsigner/seedsigner.cpp`, around
   lines 1742 and 1810): `on_button_selected(0xFFFFFFFFu, "screensaver_dismiss")` →
   `on_button_selected(SEEDSIGNER_RET_SCREENSAVER_DISMISS, "screensaver_dismiss")`.

4. **Leave the body-button list call as-is** (`components.cpp` around line 299:
   `on_button_selected(selected_index, label_text)`) — it is already index-based and correct.

5. **Leave the text-entry calls as-is** (`seedsigner.cpp` around lines 1123 and 1233).

6. **Eliminate every remaining `0xFFFFFFFFu` sentinel** from the result path. After steps 2–3
   there should be none left in the screen code. Grep to confirm:
   `grep -rn "0xFFFFFFFF" components/seedsigner/`.

7. **Update the desktop reference consumers** so the dev tools still work:
   - `tools/screen_runner/screen_runner.cpp` (around line 240): its `on_button_selected`
     checks `index == 0xFFFFFFFFu` for the status line. Change it to recognize reserved codes
     (`index >= 1000`) and display the `label`/code accordingly.
   - `tools/screenshot_generator/screenshot_gen.cpp`: apply the equivalent update if it
     inspects `index`.
   - The weak **default** no-op implementations (`components.cpp` ~line 194, `seedsigner.cpp`
     ~line 744) need no change.

8. **Update tests and docs in c-modules** that assert or describe the old
   `0xFFFFFFFF`/`topnav_back` behavior. Grep `tests/`, `docs/`, and any README for
   `0xFFFFFFFF`, `topnav_back`, `topnav_power`, `screensaver_dismiss` and bring them in line.
   If there is a `docs/knowledge/` note describing the old contract, update it.

## 5. Out of scope / do not do

- Do **not** change either callback's signature.
- Do **not** route text through `on_button_selected`, or fold back/power into
  `on_text_entered`. The two-callback split is the design, not the thing being fixed.
- Do **not** introduce a new structured/composite return type. No current screen needs one
  (the Python "passphrase + is_back_button" dict decomposes into text-entered vs back).
- Do **not** edit the consumer bridges — they live in the other two repos and are updated
  separately (next section).

## 6. Migration & coordination (important)

This is a **flag-day change to a shared contract**: once c-modules emits `1000/1001/1100`
instead of `0xFFFFFFFF`, a consumer bridge still checking `index == 0xFFFFFFFF` will
misread the event. All three repos must move together:

1. **`seedsigner-c-modules`** — this spec (emit reserved codes).
2. **`seedsigner-raspi-lvgl`** — the CPython bridge maps the callback into its result queue;
   it currently special-cases `index == 0xFFFFFFFF` and `strcmp`s the label. It will be
   updated to key off `index` (>= 1000 = reserved). Handled by the maintainer / the agent in
   that repo.
3. **`seedsigner-micropython-builder`** — the MicroPython bridge, same adaptation.

The Python-side adapter (`seedsigner/.../gui/screens/lvgl_screens.py`, `_translate_event`)
already converts the *current* events into `RET_CODE__BACK_BUTTON`/`RET_CODE__POWER_BUTTON`.
After this change that adapter simplifies (for int results it can largely pass `index`
through, since the C code now speaks the View's numbers directly). That Python work is
tracked outside this spec.

**Deliver this as a single reviewable branch/PR in c-modules.** Do not merge until the
consuming bridges have matching branches ready, so the contract flips everywhere at once.

## 7. Acceptance checklist

- [ ] The three constants defined somewhere convenient, with a one-line comment noting
      `1000`/`1001` match `RET_CODE__*` in `screen.py`.
- [ ] Top-nav back/power emit `1000`/`1001` via `on_button_selected`; `label` informational.
- [ ] Screensaver dismiss emits `1100`.
- [ ] No `0xFFFFFFFF` remains in `components/seedsigner/`.
- [ ] Body-button and text-entry call sites unchanged in behavior.
- [ ] Desktop tools (`screen_runner`, `screenshot_generator`) updated to recognize reserved
      codes; both still build and run.
- [ ] c-modules tests/docs/README updated; no stale references to the old sentinel scheme.
- [ ] Callback signatures unchanged.
