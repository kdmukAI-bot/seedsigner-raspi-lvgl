#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TARGET_ARCH="${TARGET_ARCH:-host}"

if [[ "${ROOT_DIR}" == /workspace/* ]]; then
  BUILD_MODE="docker"
else
  BUILD_MODE="host"
fi

if [[ "${TARGET_ARCH}" == "armv6" ]]; then
  BUILD_DIR="${ROOT_DIR}/build/stagec-native-${BUILD_MODE}-armv6"
else
  BUILD_DIR="${ROOT_DIR}/build/stagec-native-${BUILD_MODE}-host"
fi

RUN_TS="${RUN_TS:-$(date -u +%Y%m%d-%H%M%S)}"
LOG_DIR="${ROOT_DIR}/logs/stagec"
mkdir -p "${LOG_DIR}"
LOG_FILE="${LOG_DIR}/${RUN_TS}_stagec_${BUILD_MODE}_${TARGET_ARCH}.log"

exec > >(tee -a "${LOG_FILE}")
exec 2>&1

echo "[stagec] run_ts=${RUN_TS} mode=${BUILD_MODE} target_arch=${TARGET_ARCH}"
echo "[stagec] log_file=${LOG_FILE}"

DEFAULT_LOCAL_CMODULES="${ROOT_DIR}/../seedsigner-micropython-builder/sources/seedsigner-c-modules"
DEFAULT_DOCKER_CMODULES="/workspace/dev/seedsigner-micropython-builder/sources/seedsigner-c-modules"

if [[ -n "${SEEDSIGNER_C_MODULES_DIR:-}" ]]; then
  :
elif [[ -f "${DEFAULT_LOCAL_CMODULES}/components/seedsigner/seedsigner.cpp" ]]; then
  SEEDSIGNER_C_MODULES_DIR="${DEFAULT_LOCAL_CMODULES}"
else
  SEEDSIGNER_C_MODULES_DIR="${DEFAULT_DOCKER_CMODULES}"
fi

LVGL_ROOT="${LVGL_ROOT:-${SEEDSIGNER_C_MODULES_DIR}/../micropython/ports/esp32/managed_components/lvgl__lvgl}"

mkdir -p "${BUILD_DIR}"

CMAKE_ARGS=(
  -S "${ROOT_DIR}/native/python_bindings"
  -B "${BUILD_DIR}"
  -DSEEDSIGNER_C_MODULES_DIR="${SEEDSIGNER_C_MODULES_DIR}"
  -DLVGL_ROOT="${LVGL_ROOT}"
  -DCMAKE_BUILD_TYPE=Release
)

if [[ "${TARGET_ARCH}" == "armv6" ]]; then
  TOOLCHAIN_FILE="${ROOT_DIR}/cmake/toolchains/armv6-rpi-linux-gnueabihf.cmake"
  [[ -f "${TOOLCHAIN_FILE}" ]] || { echo "ERROR: toolchain file not found: ${TOOLCHAIN_FILE}"; exit 4; }
  rm -f "${BUILD_DIR}/CMakeCache.txt"
  CMAKE_ARGS+=("-DCMAKE_TOOLCHAIN_FILE=${TOOLCHAIN_FILE}")
  # For cross-arch validation, build placeholder shared object without CPython deps.
  CMAKE_ARGS+=("-DSTAGEC_NO_PYTHON=ON")
fi

echo "[stagec] cmake configure"
cmake "${CMAKE_ARGS[@]}"

echo "[stagec] cmake build"
cmake --build "${BUILD_DIR}" -j"$(nproc)"

ARTIFACT="$(find "${BUILD_DIR}" -maxdepth 1 -type f -name 'seedsigner_lvgl_native*.so' | head -n1)"
if [[ -z "${ARTIFACT}" ]]; then
  echo "ERROR: built extension artifact not found in ${BUILD_DIR}" >&2
  exit 5
fi

echo "[stagec] artifact=${ARTIFACT}"
if command -v file >/dev/null 2>&1; then
  file "${ARTIFACT}" || true
fi

echo "[stagec] OK built ${ARTIFACT}"
