#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
IMAGE_TAG="seedsigner-raspi-lvgl-stagec:local"
TARGET_ARCH="${TARGET_ARCH:-host}"
WS_ROOT="${WS_ROOT:-$(cd "${ROOT_DIR}/../.." && pwd)}"
DOCKERFILE="${ROOT_DIR}/docker/Dockerfile.stageb"

RUN_TS="${RUN_TS:-$(date -u +%Y%m%d-%H%M%S)}"
LOG_DIR="${ROOT_DIR}/logs/stagec"
mkdir -p "${LOG_DIR}"
LOG_FILE="${LOG_DIR}/${RUN_TS}_stagec_docker_driver_${TARGET_ARCH}.log"
exec > >(tee -a "${LOG_FILE}")
exec 2>&1

echo "[stagec-docker] run_ts=${RUN_TS} target_arch=${TARGET_ARCH}"
echo "[stagec-docker] log_file=${LOG_FILE}"

docker build -f "${DOCKERFILE}" -t "${IMAGE_TAG}" "${ROOT_DIR}"

docker run --rm \
  -v "${WS_ROOT}:/workspace" \
  -w /workspace/dev/seedsigner-raspi-lvgl \
  -e RUN_TS="${RUN_TS}" \
  -e TARGET_ARCH="${TARGET_ARCH}" \
  -e SEEDSIGNER_C_MODULES_DIR="${SEEDSIGNER_C_MODULES_DIR:-/workspace/dev/seedsigner-micropython-builder/sources/seedsigner-c-modules}" \
  -e LVGL_ROOT="${LVGL_ROOT:-}" \
  "${IMAGE_TAG}" \
  bash ./scripts/stagec_build.sh

echo "[stagec-docker] OK"
