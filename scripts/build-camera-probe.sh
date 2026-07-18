#!/usr/bin/env bash
# Cross-compile native/camera/probe/stream_probe.cpp in the py312-dev ARMv6
# container against the extracted device sysroot (scripts/extract-camera-sysroot.sh
# must have run). This is a dry run of the camera engine's exact link recipe:
#
#   - container g++ (Bullseye 8.4, glibc 2.31) compiles; the binary runs on the
#     device (glibc 2.40) — forward-compatible by construction.
#   - links the sysroot's libcamera v0.3.2 with SHARED libstdc++ (mandatory:
#     C++ objects cross the libcamera ABI, so a static copy would split ABIs).
#   - --allow-shlib-undefined: the sysroot libs reference GLIBC_2.38/GLIBCXX_3.4.32
#     version nodes the container's runtime doesn't have; the device satisfies
#     them, so the executable link must not try to prove closure here.
#
# Output: build/stream_probe (ARMv6 ELF, run on the dev device only).
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WS_ROOT="${WS_ROOT:-$(cd "${ROOT_DIR}/.." && pwd)}"
IMAGE_TAG="${IMAGE_TAG:-codeberg.org/kdmukai-bot/seedsigner-raspi-lvgl/python-armv6:py312-dev}"
PLATFORM="${PLATFORM:-linux/arm/v6}"

REL_REPO_PATH="$(realpath --relative-to="${WS_ROOT}" "${ROOT_DIR}")"
CONTAINER_REPO_DIR="/workspace/${REL_REPO_PATH}"
SYSROOT="${CONTAINER_REPO_DIR}/sysroot/pi0-dev"

[[ -f "${ROOT_DIR}/sysroot/pi0-dev/usr/lib/libcamera.so" ]] || {
  echo "ERROR: sysroot missing — run scripts/extract-camera-sysroot.sh first" >&2
  exit 1
}

mkdir -p "${ROOT_DIR}/build"

docker run --rm --platform "${PLATFORM}" \
  -v "${WS_ROOT}:/workspace" \
  -w "${CONTAINER_REPO_DIR}" \
  "${IMAGE_TAG}" \
  bash -ec "
    g++ -std=c++17 -O2 \
      -march=armv6zk -mtune=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard \
      -I ${SYSROOT}/usr/include/libcamera \
      -o build/stream_probe native/camera/probe/stream_probe.cpp \
      -L ${SYSROOT}/usr/lib -lcamera -lcamera-base -lpthread \
      -Wl,--allow-shlib-undefined \
      -Wl,-rpath-link,${SYSROOT}/usr/lib
    file build/stream_probe
    readelf -A build/stream_probe | grep -E 'Tag_CPU_arch:|Tag_FP_arch'
    readelf -d build/stream_probe | grep NEEDED
  "

echo "[probe] OK -> ${ROOT_DIR}/build/stream_probe"
