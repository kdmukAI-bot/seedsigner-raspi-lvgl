#!/usr/bin/env bash
# Build the Pi Zero extensions via the SeedSigner OS cross-compile SDK.
# ============================================================================
# Runs NATIVELY on x86 -- no QEMU, no --platform. The SDK image carries the
# SeedSigner OS buildroot cross toolchain and the matching target sysroot, so the
# artifacts are built by the device's own toolchain against the device's own libs.
#
# This is the SAME entry point CI uses (.github / .forgejo), so local and CI run
# one build path. Rebuild the SDK image only when the OS pin moves:
#   ./docker/build_sdk_image.sh
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WS_ROOT="${WS_ROOT:-$(cd "${ROOT_DIR}/.." && pwd)}"
# Pinned to the SeedSigner OS release whose sysroot it carries; bumping the OS
# means rebuilding the SDK and moving this tag in one commit.
IMAGE_TAG="${IMAGE_TAG:-ghcr.io/kdmukai-bot/seedsigner-raspi-lvgl/sdk-armv6:ss-os-0.8.0-81-gbfbd791}"

# Map host repo path into mounted container workspace deterministically.
REL_REPO_PATH="$(realpath --relative-to="${WS_ROOT}" "${ROOT_DIR}")"
CONTAINER_REPO_DIR="${CONTAINER_REPO_DIR:-/workspace/${REL_REPO_PATH}}"

RUN_TS="${RUN_TS:-$(date -u +%Y%m%d-%H%M%S)}"
LOG_DIR="${ROOT_DIR}/logs"
mkdir -p "${LOG_DIR}"
LOG_FILE="${LOG_DIR}/${RUN_TS}_build.log"
exec > >(tee -a "${LOG_FILE}")
exec 2>&1

echo "[run-build] run_ts=${RUN_TS}"
echo "[run-build] image=${IMAGE_TAG} (native x86 cross-compile)"
echo "[run-build] container_repo_dir=${CONTAINER_REPO_DIR}"
echo "[run-build] log_file=${LOG_FILE}"

docker image inspect "${IMAGE_TAG}" >/dev/null

# ccache volume: host path if set (CI), else a Docker named volume (local dev).
# No venv volume -- the SDK image already carries setuptools and pytest.
CCACHE_VOLUME="${CCACHE_HOST_DIR:-seedsigner-raspi-lvgl-ccache}"

docker run --rm \
  -v "${WS_ROOT}:/workspace" \
  -v "${CCACHE_VOLUME}:/root/.cache/ccache" \
  -w "${CONTAINER_REPO_DIR}" \
  -e RUN_TS="${RUN_TS}" \
  -e JOBS="${JOBS:-}" \
  -e ABI_JSON="${ABI_JSON:-${CONTAINER_REPO_DIR}/docs/abi/dev-pi-abi.json}" \
  -e LOCK_FILE="${LOCK_FILE:-${CONTAINER_REPO_DIR}/versions.lock.toml}" \
  -e SEEDSIGNER_LVGL_SCREENS_DIR="${SEEDSIGNER_LVGL_SCREENS_DIR:-${CONTAINER_REPO_DIR}/sources/seedsigner-lvgl-screens}" \
  -e LVGL_ROOT="${LVGL_ROOT:-${CONTAINER_REPO_DIR}/sources/seedsigner-lvgl-screens/third_party/lvgl}" \
  -e LVGL_PERF_MONITOR="${LVGL_PERF_MONITOR:-0}" \
  "${IMAGE_TAG}" \
  bash ./docker/build_steps.sh

echo "[run-build] OK"
