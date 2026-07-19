#!/usr/bin/env bash
# Build the ARMv6 cross-compile SDK image from a SeedSigner OS buildroot output.
# ============================================================================
# The SDK carries the buildroot cross toolchain + the matching target sysroot, so
# the extension is compiled BY the device's toolchain AGAINST the device's libs --
# on x86 at native speed. One image supplies both, so the compiler and the linked
# libraries cannot drift apart or away from the flashed image.
#
# Run this ONLY when the SeedSigner OS pin moves. Per-CI-job builds just pull the
# resulting tag; they never rebuild the SDK.
#
# Source of the buildroot output (first match wins):
#   SS_OS_CONTAINER  name of a (stopped is fine) seedsigner-os build container
#                    holding /output/host  [default: seedsigner-os-build-images-1]
#   SS_OS_HOST_DIR   path to a buildroot output/host tree on the host
#
# Provenance: the image is tagged AND labelled with the seedsigner-os commit its
# sysroot came from, so any artifact traces back to an OS release. That claim is
# independently verifiable (check out that commit and rebuild the OS).
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SS_OS_DIR="${SS_OS_DIR:-$(cd "${ROOT_DIR}/../seedsigner-os" 2>/dev/null && pwd || echo "")}"
CONTAINER="${SS_OS_CONTAINER:-seedsigner-os-build-images-1}"
HOST_DIR="${SS_OS_HOST_DIR:-}"
IMAGE_REPO="${IMAGE_REPO:-ghcr.io/kdmukai-bot/seedsigner-raspi-lvgl/sdk-armv6}"

# --- Provenance from the seedsigner-os checkout ------------------------------
if [[ -n "${SS_OS_DIR}" && -d "${SS_OS_DIR}/.git" ]]; then
  SS_OS_COMMIT="$(git -C "${SS_OS_DIR}" rev-parse HEAD)"
  SS_OS_DESCRIBE="$(git -C "${SS_OS_DIR}" describe --tags --always)"
  BUILDROOT_COMMIT="$(git -C "${SS_OS_DIR}/opt/buildroot" rev-parse HEAD 2>/dev/null || echo unknown)"
  if [[ -n "$(git -C "${SS_OS_DIR}" status --porcelain)" ]]; then
    echo "[sdk] WARNING: ${SS_OS_DIR} has uncommitted changes -- the provenance" \
         "stamp (${SS_OS_DESCRIBE}) will NOT fully describe this sysroot." >&2
  fi
else
  echo "[sdk] WARNING: no seedsigner-os checkout found (set SS_OS_DIR)." \
       "Provenance will be stamped 'unknown'." >&2
  SS_OS_COMMIT=unknown; SS_OS_DESCRIBE=unknown; BUILDROOT_COMMIT=unknown
fi

IMAGE_TAG="${IMAGE_TAG:-${IMAGE_REPO}:ss-os-${SS_OS_DESCRIBE}}"

echo "[sdk] ss-os commit    = ${SS_OS_COMMIT}"
echo "[sdk] ss-os describe  = ${SS_OS_DESCRIBE}"
echo "[sdk] buildroot pin   = ${BUILDROOT_COMMIT}"
echo "[sdk] image tag       = ${IMAGE_TAG}"

# --- Assemble the build context ---------------------------------------------
CTX="$(mktemp -d)"
trap 'rm -rf "${CTX}"' EXIT
cp "${ROOT_DIR}/docker/Dockerfile.sdk" "${CTX}/Dockerfile"

if [[ -n "${HOST_DIR}" ]]; then
  echo "[sdk] source: host dir ${HOST_DIR}"
  [[ -x "${HOST_DIR}/bin/arm-Buildroot-linux-gnueabihf-gcc" ]] \
    || { echo "ERROR: no cross gcc under ${HOST_DIR}/bin" >&2; exit 1; }
  cp -a "${HOST_DIR}" "${CTX}/host"
else
  echo "[sdk] source: container ${CONTAINER} (/output/host, ~1GB -- this takes a minute)"
  docker inspect "${CONTAINER}" >/dev/null
  docker cp "${CONTAINER}:/output/host" "${CTX}/host"
fi

# `staging` is a symlink INTO host, so the single host/ tree is self-sufficient.
# Fail loudly here rather than at link time inside a CI job.
for probe in \
    bin/arm-Buildroot-linux-gnueabihf-g++ \
    bin/python3 \
    arm-Buildroot-linux-gnueabihf/sysroot/usr/include/python3.12/Python.h \
    arm-Buildroot-linux-gnueabihf/sysroot/usr/lib/libcamera.so \
    arm-Buildroot-linux-gnueabihf/sysroot/usr/lib/python3.12/_sysconfigdata__linux_arm-linux-gnueabihf.py; do
  [[ -e "${CTX}/host/${probe}" ]] || { echo "ERROR: extracted tree missing ${probe}" >&2; exit 1; }
done
echo "[sdk] extracted tree verified ($(du -sh "${CTX}/host" | cut -f1))"

# --- Build --------------------------------------------------------------------
docker build \
  --build-arg "SS_OS_COMMIT=${SS_OS_COMMIT}" \
  --build-arg "SS_OS_DESCRIBE=${SS_OS_DESCRIBE}" \
  --build-arg "BUILDROOT_COMMIT=${BUILDROOT_COMMIT}" \
  -t "${IMAGE_TAG}" \
  "${CTX}"

echo "[sdk] OK -> ${IMAGE_TAG}"
echo "[sdk] push it (public) so CI can pull:  docker push ${IMAGE_TAG}"
