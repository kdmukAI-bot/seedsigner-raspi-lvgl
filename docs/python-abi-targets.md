# Python ABI Targets & Production Parity

Records the canonical Python ABI target for ARMv6 extension builds and the
policy for tracking production (`seedsigner-os`/Buildroot) versions.

## Canonical target (current, dev-priority)

- Target env: dev Pi
- Python: `3.10.10`, SOABI `cpython-310-arm-linux-gnueabihf`,
  EXT_SUFFIX `.cpython-310-arm-linux-gnueabihf.so`
- Enforced by `docker/build_steps.sh` against `versions.lock.toml` and the
  device capture `docs/abi/dev-pi-abi.json`.

Production note: `seedsigner-os` + Buildroot infer CPython `3.11.x`
(`cpython-311-arm-linux-gnueabihf`) — see
`docs/abi/prod-pi-abi.inferred.json`. Treat as provisional until replaced by a
runtime capture from a production device; when dev/prod diverge, production ABI
becomes canonical and dev compatibility is validated explicitly.

## ABI capture files

- `docs/abi/dev-pi-abi.json` — dev Pi runtime capture (present; consumed by the
  build gate)
- `docs/abi/prod-pi-abi.json` — production runtime capture (pending)
- `docs/abi/prod-pi-abi.inferred.json` — inferred from seedsigner-os build
  definitions (planning input only; consumed by no script)

Generate on a device with:

```bash
python -c "import json,platform,sysconfig; print(json.dumps({'python_version':platform.python_version(),'soabi':sysconfig.get_config_var('SOABI'),'ext_suffix':sysconfig.get_config_var('EXT_SUFFIX'),'platform_machine':platform.machine()},indent=2))" > docs/abi/<env>-pi-abi.json
```

## Production parity policy

1. Prefer production-matched versions for: Python runtime ABI, toolchain
   family, libc assumptions, extension build tools.
2. Pin set lives in `versions.lock.toml` (consumed by `docker/build_steps.sh`).
3. Document every deviation here.

### Deviation register

| Component | Production target | Current builder | Status | Action |
|---|---|---|---|---|
| Python ABI | 3.11.x inferred (runtime capture pending) | cpython-310-arm-linux-gnueabihf | TEMP | keep until prod runtime ABI captured |
| Emulated machine arch | armv6l (Pi Zero) | armv7l QEMU runtime, armv6 codegen flags | TEMP | enforced via readelf ARMv6 attribute gate + on-device validation |
| Builder glibc | 2.28 (target Pi) | 2.31 (bullseye base image) | OPEN | GLIBC symbol-version ceiling gate in build_steps.sh |
