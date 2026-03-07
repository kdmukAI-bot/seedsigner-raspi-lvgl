# TODO - seedsigner-raspi-lvgl

Implementation plan for a Raspberry Pi Zero target using an LVGL-based display layer.

## Goals
- Improve display performance vs current Python-only rendering path.
- Preserve SeedSigner UX/navigation behavior.
- Keep platform-specific code isolated from shared UI logic.
- Avoid changes to `seedsigner-c-modules` in this phase.

## Phase 0 - Baseline + constraints
1. Document target hardware matrix:
   - Fixed Pi Zero hardware target, display controller/version (ST7789 variant), resolution, rotation.
   - Hard-coded joystick/button GPIO mapping from existing Python hardware implementation.
2. Capture current baseline metrics using existing Python display path:
   - Full-screen update time.
   - Partial update latency.
   - Menu navigation latency (input-to-visible-change).
   - Optional FPS for representative animations/transitions.
3. Define success criteria (example):
   - >= 2x faster partial updates, or
   - noticeable reduction in input-to-render latency.

## Phase 1 - Architecture decisions
1. Choose runtime path:
   - **Preferred for performance:** native LVGL app (C/C++) with thin Python integration if needed.
   - Alternative: Python LVGL binding path if development speed is prioritized.
2. Define module boundaries:
   - `platform/pi_zero/` for SPI, GPIO input, timing, OS integration.
   - `ui/` for shared LVGL screen logic and navigation contracts.
3. Decide display buffer strategy:
   - single vs double buffer; full-screen vs partial dirty-area buffering.
4. Define font pipeline:
   - canonical TTF source + build-time generation of target font sizes.

## Phase 2 - Docker build foundation (moved earlier)
1. Create canonical Docker build image for Pi LVGL artifacts.
2. Add local build entrypoints (`make`/scripts) that run entirely in Docker.
3. Verify first compile artifact can be produced on non-Pi dev machines.
4. Add minimal CI workflow stub that reuses the same Docker build entrypoint.

## Phase 3 - Display backend implementation
1. Implement ST7789 transport layer compatible with Pi SPI stack.
2. Implement LVGL display driver glue:
   - `flush_cb` with area clipping and efficient transfer batching.
3. Add runtime tuning controls:
   - SPI clock, color depth, rotation, optional DMA path (if available).
4. Add display self-test screen:
   - solid fills, gradients, checkerboard, text render sanity.

## Phase 4 - Input backend implementation
1. Implement joystick GPIO event reader with debouncing.
2. Map events to LVGL input device:
   - `UP/DOWN/LEFT/RIGHT/SELECT`.
3. Implement agreed navigation behavior:
   - `UP` at top-of-content moves focus into top nav (Back/Power).
   - No dedicated hardware BACK required.
4. Add long-press support hooks (optional/future fallback BACK semantics).

## Phase 5 - UI integration
1. Bring up core SeedSigner-like screens in LVGL shell.
2. Ensure focus model consistency across lists/forms/dialogs.
3. Verify top-nav entry/exit behavior on every screen class.
4. Add a lightweight screen harness to exercise common transitions.

## Phase 6 - Performance optimization
1. Profile flush hot paths and Python/C boundary overhead.
2. Optimize dirty-region invalidation and redraw frequency.
3. Tune SPI transfer chunking and frame pacing.
4. Re-run baseline tests and compare against Phase 0 metrics.

## Phase 7 - Packaging + reproducibility
1. Add Docker-based reproducible build environment (local dev + CI parity).
2. Pin tool versions (LVGL, converter tools, dependencies).
3. Add reproducible build/run scripts that execute inside Docker.
4. Add launch scripts/service config for Pi deployment.
5. Document board/display-specific configuration profiles.

### Phase 7A - CI/CD build parity (GitHub Actions)
1. Add GitHub Actions workflow that uses the same Docker image/build entrypoint as local builds.
2. Ensure artifacts are produced deterministically and uploaded from CI.
3. Add quick validation job (import/binding smoke test + minimal screen path check).
4. Keep Dockerfile and CI workflow version-locked to avoid drift.

## Phase 8 - Validation + handoff
1. Functional test pass:
   - navigation, selection, top-nav behavior, power/back actions.
2. Stress test:
   - prolonged navigation and redraw stability.
3. Performance report:
   - side-by-side baseline vs LVGL metrics.
4. Final implementation notes + next-step recommendations.

## Future to-do (post-parity)
- Define a public LVGL screen interface spec that both platform bindings implement:
  - MicroPython C bindings (current/legacy path)
  - Pi/CPython bindings (new path)
- Keep initial spec aligned to current MicroPython behavior; refactor only after parity is stable.

---

## Immediate next 3 tasks
1. Create `docs/architecture.md` with chosen runtime + module boundaries.
2. Create `docs/navigation-contract.md` formalizing joystick/top-nav rules.
3. Create `docs/perf-baseline.md` and collect first baseline metrics on current Python path.
