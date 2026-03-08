# Python ABI Targets (Stage F)

This document records the canonical Python ABI target(s) for armv6 extension builds.

## Status
- Pending ABI capture from dev/prod Pi environments.

## Required input files
- `docs/abi/dev-pi-abi.json`
- `docs/abi/prod-pi-abi.json`

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

## Canonical target decision (to fill)
- Target env: `TBD`
- Target Python version: `TBD`
- Target SOABI: `TBD`
- Target EXT_SUFFIX: `TBD`
- Toolchain/sysroot source: `TBD`

## Notes
- If dev/prod differ, prefer production ABI as canonical and validate dev compatibility explicitly.
- Keep this file updated whenever Python runtime or OS base changes.
