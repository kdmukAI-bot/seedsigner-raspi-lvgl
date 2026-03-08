# Production Parity Lock

Defines policy and process for pinning builder image dependencies to production `seedsigner-os`/Buildroot versions whenever practical.

## Policy

1. Prefer production-matched versions for:
   - Python runtime ABI targets
   - toolchain family
   - libc assumptions
   - build tools used for extension compilation
2. Document every deviation from production parity with:
   - what differs
   - why
   - risk/impact
   - planned convergence path
3. Keep pin set versioned in root `versions.lock.toml` (consumed by build scripts).

## Inputs

- Production OS repo: https://github.com/SeedSigner/seedsigner-os
- Production Buildroot repo: https://github.com/seedsigner/buildroot
- Dev ABI capture: `docs/abi/dev-pi-abi.json`
- Production ABI capture (when available): `docs/abi/prod-pi-abi.json`
- Temporary production inference: `docs/abi/prod-pi-abi.inferred.json`

## Lock workflow

1. Update `versions.lock.toml` from production refs.
2. Build/update GHCR base images from lock values.
3. Validate extension build + runtime smoke tests.
4. Record deviations here.

## Deviation register

| Component | Production target | Current builder | Status | Risk | Action |
|---|---|---|---|---|---|
| Python ABI (dev priority) | (pending runtime capture) | cpython-310-arm-linux-gnueabihf | TEMP | Medium | keep until prod runtime ABI captured |
| Emulated machine arch | armv6l (Pi Zero) | armv7l runtime, armv6 codegen flags | TEMP | Medium | enforce readelf v6 + on-device validation |
| Toolchain exact versions | TBD | TBD | OPEN | Medium | fill from seedsigner-os/buildroot refs |

## CCache policy

- CCache must be enabled in builder images by default.
- Build scripts must support:
  - show stats
  - clear cache
  - disable cache for reproducibility checks
- Cache controls should be documented in builder script help.
