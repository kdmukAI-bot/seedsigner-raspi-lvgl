#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WS_ROOT="${WS_ROOT:-$(cd "${ROOT_DIR}/../.." && pwd)}"
IMAGE_TAG="${IMAGE_TAG:-ghcr.io/${GHCR_OWNER:-kdmukai-bot}/seedsigner-raspi-lvgl/python-armv6:py310-dev}"
PLATFORM="${PLATFORM:-linux/arm/v6}"

RUN_TS="${RUN_TS:-$(date -u +%Y%m%d-%H%M%S)}"
LOG_DIR="${ROOT_DIR}/logs/stagef"
mkdir -p "${LOG_DIR}"
LOG_FILE="${LOG_DIR}/${RUN_TS}_stagef_local_base.log"
exec > >(tee -a "${LOG_FILE}")
exec 2>&1

echo "[stagef-local-base] run_ts=${RUN_TS}"
echo "[stagef-local-base] image=${IMAGE_TAG} platform=${PLATFORM}"
echo "[stagef-local-base] log_file=${LOG_FILE}"

docker image inspect "${IMAGE_TAG}" >/dev/null

docker run --rm --platform "${PLATFORM}" \
  -v "${WS_ROOT}:/workspace" \
  -w /workspace/dev/seedsigner-raspi-lvgl \
  -e RUN_TS="${RUN_TS}" \
  -e ABI_JSON="${ABI_JSON:-/workspace/dev/seedsigner-raspi-lvgl/docs/abi/dev-pi-abi.json}" \
  -e LOCK_FILE="${LOCK_FILE:-/workspace/dev/seedsigner-raspi-lvgl/versions.lock.toml}" \
  -e SEEDSIGNER_C_MODULES_DIR="${SEEDSIGNER_C_MODULES_DIR:-/workspace/dev/seedsigner-c-modules}" \
  -e LVGL_ROOT="${LVGL_ROOT:-/workspace/dev/lvgl}" \
  "${IMAGE_TAG}" \
  bash ./scripts/stagef_emu_build.sh

echo "[stagef-local-base] OK"
