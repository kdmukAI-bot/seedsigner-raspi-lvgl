# Python ABI Targets (Stage F)

This document records the canonical Python ABI target(s) for armv6 extension builds.

## Status
- Dev ABI capture available: `docs/abi/dev-pi-abi.json`.
- Production ABI currently inferred from `seedsigner-os` build definitions: `docs/abi/prod-pi-abi.inferred.json`.
- Production runtime capture remains recommended when available.

## Required input files
- `docs/abi/dev-pi-abi.json` ✅
- `docs/abi/prod-pi-abi.json` (pending runtime capture)
- `docs/abi/prod-pi-abi.inferred.json` (temporary planning input)

Generate on each device with:

```bash
python scripts/capture_python_abi.py > docs/abi/<env>-pi-abi.json
```

## Fields used for decision
- `python_version`
- `soabi`
- `ext_suffix`
- `include_py`
- `libdir`
- `ldlibrary`
- `multiarch`
- `ldd_version`
- `platform_machine`

## Canonical target decision (current, dev-priority)
- Target env: `dev Pi` (temporary priority)
- Target Python version: `3.10.10`
- Target SOABI: `cpython-310-arm-linux-gnueabihf`
- Target EXT_SUFFIX: `.cpython-310-arm-linux-gnueabihf.so`
- Toolchain/sysroot source: Stage F armv6-cpython wiring should target dev ABI first; production alignment can follow when runtime capture is available.

## Production ABI note (lower priority for now)
- Inferred production expectation from `seedsigner-os` + Buildroot: CPython `3.11.x` / `cpython-311-arm-linux-gnueabihf`.
- Treat as provisional until replaced by runtime capture from production device.

## Notes
- If dev/prod differ, prefer production ABI as canonical and validate dev compatibility explicitly.
- Keep this file updated whenever Python runtime or OS base changes.
