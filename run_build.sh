#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WS_ROOT="${WS_ROOT:-$(cd "${ROOT_DIR}/.." && pwd)}"
IMAGE_TAG="${IMAGE_TAG:-ghcr.io/${GHCR_OWNER:-kdmukai-bot}/seedsigner-raspi-lvgl/python-armv6:py310-dev}"
PLATFORM="${PLATFORM:-linux/arm/v6}"

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
echo "[run-build] image=${IMAGE_TAG} platform=${PLATFORM}"
echo "[run-build] container_repo_dir=${CONTAINER_REPO_DIR}"
echo "[run-build] log_file=${LOG_FILE}"

docker image inspect "${IMAGE_TAG}" >/dev/null

# Cache volumes: use host paths if set (for CI), otherwise Docker named volumes (for local).
CCACHE_VOLUME="${CCACHE_HOST_DIR:-seedsigner-raspi-lvgl-ccache}"
VENV_VOLUME="${VENV_HOST_DIR:-seedsigner-raspi-lvgl-venv}"
CONTAINER_VENV_DIR="${CONTAINER_REPO_DIR}/.venv-build"

docker run --rm --platform "${PLATFORM}" \
  -v "${WS_ROOT}:/workspace" \
  -v "${CCACHE_VOLUME}:/root/.cache/ccache" \
  -v "${VENV_VOLUME}:${CONTAINER_VENV_DIR}" \
  -w "${CONTAINER_REPO_DIR}" \
  -e RUN_TS="${RUN_TS}" \
  -e ABI_JSON="${ABI_JSON:-${CONTAINER_REPO_DIR}/docs/abi/dev-pi-abi.json}" \
  -e LOCK_FILE="${LOCK_FILE:-${CONTAINER_REPO_DIR}/versions.lock.toml}" \
  -e SEEDSIGNER_C_MODULES_DIR="${SEEDSIGNER_C_MODULES_DIR:-${CONTAINER_REPO_DIR}/sources/seedsigner-c-modules}" \
  -e LVGL_ROOT="${LVGL_ROOT:-${CONTAINER_REPO_DIR}/sources/seedsigner-c-modules/third_party/lvgl}" \
  -e LVGL_PERF_MONITOR="${LVGL_PERF_MONITOR:-0}" \
  "${IMAGE_TAG}" \
  bash ./docker/build_steps.sh

echo "[run-build] OK"
