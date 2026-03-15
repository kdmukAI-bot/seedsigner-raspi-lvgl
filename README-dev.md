# Developer Guide

## Local setup

```bash
git submodule update --init --recursive   # pulls seedsigner-c-modules + LVGL
```

## Build

```bash
./run_build.sh
```

This produces an ARMv6 CPython extension (`.so`) targeting the Pi Zero. The
build runs inside a Docker container under QEMU ARM emulation. Expect ~13
minutes for the first build; subsequent builds with a warm ccache complete
in ~1 minute.

## Pi hardware testing

See `docs/pi-hardware-test.md`.

---

## Build details

The build uses a pre-built base image hosted on GitHub Container Registry
(GHCR). The GHCR image contains a pinned Python toolchain and ARMv6 compiler,
ensuring reproducible builds across local dev machines and CI.

### Build caching

Local builds use Docker named volumes to persist caches across runs:

- **ccache** (`seedsigner-raspi-lvgl-ccache`) — compiled object cache, avoids recompiling unchanged translation units under QEMU emulation
- **venv** (`seedsigner-raspi-lvgl-venv`) — Python virtual environment with build dependencies

To reset caches:
```bash
docker volume rm seedsigner-raspi-lvgl-ccache seedsigner-raspi-lvgl-venv
```

CI builds use `actions/cache` for ccache persistence across workflow runs.

### Build logs

All builds write timestamped logs to `logs/`.

### Rebuilding the GHCR base image

This is rarely needed — only when changing the Python version or system
toolchain. The base image definition is `docker/Dockerfile.ghcr`.

```bash
PYTHON_VERSION=3.10.10 PY_SERIES=py310 ./docker/build_ghcr_base_image.sh
```

### Submodules

The repo includes `seedsigner-c-modules` as a git submodule under
`sources/seedsigner-c-modules`, which itself contains LVGL as a nested
submodule (`third_party/lvgl`). The `--recursive` flag is required.

### Environment variables

| Variable | Purpose | Default |
|----------|---------|---------|
| `IMAGE_TAG` | GHCR base image for ARMv6 builds | `py310-dev` tag |
| `LVGL_PERF_MONITOR` | Enable LVGL FPS/CPU overlay | `0` |
| `SEEDSIGNER_C_MODULES_DIR` | Path to c-modules source | `sources/seedsigner-c-modules` |
| `LVGL_ROOT` | Path to LVGL source | `sources/seedsigner-c-modules/third_party/lvgl` |
| `WS_ROOT` | Workspace root for Docker mounts | auto-detected |
| `LOCK_FILE` | Version lock file | `versions.lock.toml` |
| `ABI_JSON` | ABI reference file | `docs/abi/dev-pi-abi.json` |
| `CCACHE_HOST_DIR` | Host path for ccache (CI use) | Docker named volume |

#### Examples

Enable LVGL performance monitor overlay (displays FPS/CPU on screen):
```bash
LVGL_PERF_MONITOR=1 ./run_build.sh
```

Use a locally-built base image instead of the GHCR image:
```bash
IMAGE_TAG=seedsigner-raspi-lvgl/python-armv6:py310-dev-local ./run_build.sh
```
