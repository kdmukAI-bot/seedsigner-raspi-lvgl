#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
IMAGE_TAG="seedsigner-raspi-lvgl-stagef-emu:local"
WS_ROOT="${WS_ROOT:-$(cd "${ROOT_DIR}/../.." && pwd)}"
DOCKERFILE="${ROOT_DIR}/docker/Dockerfile.stagef-emu"
PLATFORM="${PLATFORM:-linux/arm/v7}"

RUN_TS="${RUN_TS:-$(date -u +%Y%m%d-%H%M%S)}"
LOG_DIR="${ROOT_DIR}/logs/stagef"
mkdir -p "${LOG_DIR}"
LOG_FILE="${LOG_DIR}/${RUN_TS}_stagef_emu_docker_driver.log"
exec > >(tee -a "${LOG_FILE}")
exec 2>&1

echo "[stagef-emu-docker] run_ts=${RUN_TS} platform=${PLATFORM}"
echo "[stagef-emu-docker] log_file=${LOG_FILE}"

docker run --privileged --rm tonistiigi/binfmt --install arm >/dev/null

docker build --platform "${PLATFORM}" -f "${DOCKERFILE}" -t "${IMAGE_TAG}" "${ROOT_DIR}"

docker run --rm --platform "${PLATFORM}" \
  -v "${WS_ROOT}:/workspace" \
  -w /workspace/dev/seedsigner-raspi-lvgl \
  -e RUN_TS="${RUN_TS}" \
  -e SEEDSIGNER_C_MODULES_DIR="${SEEDSIGNER_C_MODULES_DIR:-/workspace/dev/seedsigner-micropython-builder/sources/seedsigner-c-modules}" \
  -e LVGL_ROOT="${LVGL_ROOT:-}" \
  "${IMAGE_TAG}" \
  bash ./scripts/stagef_emu_build.sh

echo "[stagef-emu-docker] OK"
