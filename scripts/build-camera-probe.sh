#!/usr/bin/env bash
# Cross-compile native/camera/probe/stream_probe.cpp with the SeedSigner OS SDK.
# ============================================================================
# A dry run of the camera engine's exact link recipe, useful for isolating
# libcamera problems from the rest of the extension:
#
#   - the buildroot cross toolchain compiles on x86; the binary runs on the device.
#   - links the sysroot's libcamera with SHARED libstdc++ (mandatory: C++ objects
#     cross the libcamera ABI, so a static copy would split ABIs).
#   - --allow-shlib-undefined: the sysroot libs reference GLIBC/GLIBCXX version
#     nodes the build host lacks; the device satisfies them, so the executable
#     link must not try to prove closure here.
#
# Output: build/stream_probe (ARMv6 ELF, run on the dev device only).
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WS_ROOT="${WS_ROOT:-$(cd "${ROOT_DIR}/.." && pwd)}"
IMAGE_TAG="${IMAGE_TAG:-ghcr.io/kdmukai-bot/seedsigner-raspi-lvgl/sdk-armv6:ss-os-0.8.0-81-gbfbd791}"

REL_REPO_PATH="$(realpath --relative-to="${WS_ROOT}" "${ROOT_DIR}")"
CONTAINER_REPO_DIR="/workspace/${REL_REPO_PATH}"
# The SDK carries the target sysroot; no separate extraction step is needed.
SYSROOT="${SYSROOT:-/output/staging}"
CROSS_TUPLE="${CROSS_TUPLE:-arm-Buildroot-linux-gnueabihf}"

mkdir -p "${ROOT_DIR}/build"

docker run --rm \
  -v "${WS_ROOT}:/workspace" \
  -w "${CONTAINER_REPO_DIR}" \
  "${IMAGE_TAG}" \
  bash -ec "
    ${CROSS_TUPLE}-g++ -std=c++17 -O2 \
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
