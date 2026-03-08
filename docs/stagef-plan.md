# Stage F Plan: armv6 CPython ABI + sysroot wiring

Goal: produce a Pi Zero-loadable `seedsigner_lvgl_native` extension artifact with reproducible build steps.

## Scope
- Wire exact target Python ABI and sysroot for armv6 builds.
- Keep `seedsigner-c-modules` read-only.
- Use Docker-first build path and timestamped logs.

## F1 — ABI inventory (mandatory first gate)
Collect target ABI facts on:
1. Dev Pi OS environment
2. Production `seedsigner-os` environment

Required fields:
- `python_version`
- `implementation`
- `sys.maxsize`/arch hint
- `sysconfig.get_config_var('SOABI')`
- `sysconfig.get_config_var('EXT_SUFFIX')`
- `INCLUDEPY`, `LIBDIR`, `LDLIBRARY`, `MULTIARCH`
- `platform.machine()`, `platform.platform()`
- `ldd --version` (libc baseline)

Output files to store in repo:
- `docs/abi/dev-pi-abi.json`
- `docs/abi/prod-pi-abi.json`

## F2 — Target ABI decision
Select canonical build target ABI from F1 outputs.
Document in `docs/python-abi-targets.md`:
- target Python version/ABI
- acceptable compatibility ranges
- risk notes if dev/prod differ

## F3 — Sysroot and Python dev artifacts
Define reproducible source of target armv6:
- Python headers (`Python.h`)
- libpython (if needed by linker mode)
- libc/sysroot matching target runtime assumptions

Document exact source/provenance in `docs/python-abi-targets.md`.

## F4 — Build mode: `TARGET_ARCH=armv6-cpython`
Add Stage F script mode that:
- uses armv6 cross-compiler
- points include/link paths to target Python artifacts/sysroot
- builds extension artifact for target ABI
- writes logs to `logs/stagef/<timestamp>_...`

## F5 — Artifact verification
For produced artifact:
- verify ARM ELF via `file`
- verify extension suffix/ABI alignment
- verify expected dynamic deps via `readelf`/`objdump`/`ldd` (where applicable)

## F6 — Pi Zero load test
On-device checks:
1. `import seedsigner_lvgl_native`
2. `button_list_screen({...})`
3. `poll_for_result()` returns expected tuple shape

Capture results in `docs/abi/pi-load-test.md`.

## F7 — CI integration
Add Stage F CI job:
- build armv6-cpython artifact
- upload artifact + logs
- mark experimental until on-device load test is stable

## Exit criteria
- Reproducible armv6 extension build exists.
- Artifact imports and runs minimal call path on Pi Zero.
- ABI/sysroot assumptions are documented and version-pinned.
