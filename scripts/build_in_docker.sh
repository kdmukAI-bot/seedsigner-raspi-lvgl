#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
IMAGE_TAG="seedsigner-raspi-lvgl-stageb:local"
TARGET_ARCH="${TARGET_ARCH:-host}"

# Workspace root containing sibling repos under dev/
WS_ROOT="${WS_ROOT:-$(cd "${ROOT_DIR}/../.." && pwd)}"
DOCKERFILE="${ROOT_DIR}/docker/Dockerfile.stageb"

RUN_TS="${RUN_TS:-$(date -u +%Y%m%d-%H%M%S)}"
LOG_DIR="${ROOT_DIR}/logs/stageb"
mkdir -p "${LOG_DIR}"
LOG_FILE="${LOG_DIR}/${RUN_TS}_stageb_docker_driver_${TARGET_ARCH}.log"

exec > >(tee -a "${LOG_FILE}")
exec 2>&1

echo "[stageb-docker] run_ts=${RUN_TS}"
echo "[stageb-docker] target_arch=${TARGET_ARCH}"
echo "[stageb-docker] log_file=${LOG_FILE}"
echo "[stageb-docker] ws_root=${WS_ROOT}"

DOCKER_BUILD=(docker build -f "${DOCKERFILE}" -t "${IMAGE_TAG}" "${ROOT_DIR}")
DOCKER_RUN=(
  docker run --rm
  -v "${WS_ROOT}:/workspace"
  -w /workspace/dev/seedsigner-raspi-lvgl
  -e RUN_TS="${RUN_TS}"
  -e TARGET_ARCH="${TARGET_ARCH}"
  -e SEEDSIGNER_C_MODULES_DIR="${SEEDSIGNER_C_MODULES_DIR:-/workspace/dev/seedsigner-micropython-builder/sources/seedsigner-c-modules}"
  -e LVGL_ROOT="${LVGL_ROOT:-}"
  "${IMAGE_TAG}"
  bash ./scripts/stageb_build.sh
)

echo "[stageb-docker] docker build"
"${DOCKER_BUILD[@]}"

echo "[stageb-docker] docker run"
"${DOCKER_RUN[@]}"

echo "[stageb-docker] OK"
