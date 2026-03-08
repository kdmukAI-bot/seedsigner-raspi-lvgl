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
  BUILD_DIR="${ROOT_DIR}/build/stageb-screen-core-${BUILD_MODE}-armv6"
else
  BUILD_DIR="${ROOT_DIR}/build/stageb-screen-core-${BUILD_MODE}-host"
fi

RUN_TS="${RUN_TS:-$(date -u +%Y%m%d-%H%M%S)}"
LOG_DIR="${ROOT_DIR}/logs/stageb"
mkdir -p "${LOG_DIR}"
LOG_FILE="${LOG_DIR}/${RUN_TS}_stageb_${BUILD_MODE}_${TARGET_ARCH}.log"

# Mirror output to console + log file.
exec > >(tee -a "${LOG_FILE}")
exec 2>&1

echo "[stageb] run_ts=${RUN_TS} mode=${BUILD_MODE} target_arch=${TARGET_ARCH}"
echo "[stageb] log_file=${LOG_FILE}"

echo "[stageb] resolving dependencies"
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

echo "[stageb] seedsigner_c_modules_dir=${SEEDSIGNER_C_MODULES_DIR}"
echo "[stageb] lvgl_root=${LVGL_ROOT}"
echo "[stageb] build_dir=${BUILD_DIR}"

if [[ ! -f "${SEEDSIGNER_C_MODULES_DIR}/components/seedsigner/seedsigner.cpp" ]]; then
  echo "ERROR: SEEDSIGNER_C_MODULES_DIR missing expected sources: ${SEEDSIGNER_C_MODULES_DIR}" >&2
  exit 2
fi

if [[ ! -f "${LVGL_ROOT}/lvgl.h" ]]; then
  echo "ERROR: LVGL_ROOT missing lvgl.h: ${LVGL_ROOT}" >&2
  exit 3
fi

mkdir -p "${BUILD_DIR}"

# Cross-builds are sensitive to toolchain flag cache; clear stale cache proactively.
if [[ "${TARGET_ARCH}" == "armv6" ]]; then
  rm -f "${BUILD_DIR}/CMakeCache.txt"
fi

CMAKE_ARGS=(
  -S "${ROOT_DIR}/native/screen_core_sanity"
  -B "${BUILD_DIR}"
  -DSEEDSIGNER_C_MODULES_DIR="${SEEDSIGNER_C_MODULES_DIR}"
  -DLVGL_ROOT="${LVGL_ROOT}"
  -DCMAKE_BUILD_TYPE=Release
)

if [[ "${TARGET_ARCH}" == "armv6" ]]; then
  TOOLCHAIN_FILE="${ROOT_DIR}/cmake/toolchains/armv6-rpi-linux-gnueabihf.cmake"
  if [[ ! -f "${TOOLCHAIN_FILE}" ]]; then
    echo "ERROR: toolchain file not found: ${TOOLCHAIN_FILE}" >&2
    exit 4
  fi
  CMAKE_ARGS+=("-DCMAKE_TOOLCHAIN_FILE=${TOOLCHAIN_FILE}")
fi

echo "[stageb] cmake configure"
cmake "${CMAKE_ARGS[@]}"

echo "[stageb] cmake build"
cmake --build "${BUILD_DIR}" -j"$(nproc)"

ARTIFACT="${BUILD_DIR}/screen_core_sanity"
echo "[stageb] artifact=${ARTIFACT}"
if command -v file >/dev/null 2>&1; then
  file "${ARTIFACT}" || true
fi

if [[ "${TARGET_ARCH}" == "armv6" ]]; then
  if command -v file >/dev/null 2>&1; then
    if ! file "${ARTIFACT}" | grep -Eiq "ARM|arm"; then
      echo "ERROR: artifact does not appear to be ARM architecture" >&2
      exit 5
    fi
  fi
fi

echo "[stageb] OK built ${ARTIFACT}"
