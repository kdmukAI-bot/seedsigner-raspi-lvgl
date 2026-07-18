#!/usr/bin/env bash
# Populate sysroot/pi0-dev/ with the libcamera link/runtime set for the camera
# engine build, copied from a seedsigner-os buildroot output tree. The buildroot
# staging dir is the authoritative source: it is cross-compiled for the exact
# device target (ARMv6KZ/VFPv2 hard-float, glibc 2.40) and carries the exact
# libcamera the flashed image runs, so no separate Meson build of libcamera is
# needed and version skew with the device is structurally impossible.
#
# Source (one of, first match wins):
#   SS_OS_CONTAINER  name of a (stopped is fine) seedsigner-os build container
#                    holding /output/staging  [default: seedsigner-os-build-images-1]
#   SS_OS_STAGING    path to a buildroot staging dir on the host
#
# The sysroot is git-ignored build input, like the ccache volume: re-runnable,
# never committed. Re-run after any seedsigner-os image rebuild.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEST="${SYSROOT_DEST:-${ROOT_DIR}/sysroot/pi0-dev}"
CONTAINER="${SS_OS_CONTAINER:-seedsigner-os-build-images-1}"
STAGING="${SS_OS_STAGING:-}"

# Headers + the libs the engine links (-lcamera -lcamera-base) + the DT_NEEDED
# closure of libcamera.so (for full link-time resolution of the probe binary).
# libstdc++ is captured so the ELF gate can read the device's real GLIBCXX
# ceiling from the artifact instead of hardcoding it.
INCLUDE_PATHS=(usr/include/libcamera)
LIB_LINKS=(
  libcamera.so libcamera-base.so
  libcrypto.so.3 libyaml-0.so.2
  libstdc++.so.6 libgcc_s.so.1
)

if [[ -n "${STAGING}" ]]; then
  echo "[sysroot] source: staging dir ${STAGING}"
  rm -rf "${DEST}"
  mkdir -p "${DEST}/usr/include" "${DEST}/usr/lib"
  for p in "${INCLUDE_PATHS[@]}"; do
    cp -a "${STAGING}/${p}" "${DEST}/usr/include/"
  done
  for name in "${LIB_LINKS[@]}"; do
    # Copy the whole SONAME chain (dev link -> SONAME link -> real file).
    while [[ -n "${name}" ]]; do
      cp -a "${STAGING}/usr/lib/${name}" "${DEST}/usr/lib/"
      if [[ -L "${STAGING}/usr/lib/${name}" ]]; then
        name="$(readlink "${STAGING}/usr/lib/${name}")"
      else
        name=""
      fi
    done
  done
else
  echo "[sysroot] source: container ${CONTAINER}"
  docker inspect "${CONTAINER}" >/dev/null
  rm -rf "${DEST}"
  mkdir -p "${DEST}/usr/include" "${DEST}/usr/lib"
  for p in "${INCLUDE_PATHS[@]}"; do
    docker cp "${CONTAINER}:/output/staging/${p}" - | tar -x -C "${DEST}/usr/include"
  done
  for name in "${LIB_LINKS[@]}"; do
    # docker cp preserves symlinks as symlinks; follow the chain by hand until
    # the real file lands (dev link -> SONAME link -> real file). Buildroot
    # splits libs between staging usr/lib and lib; try both.
    while [[ -n "${name}" ]]; do
      copied=""
      for libdir in usr/lib lib; do
        if docker cp "${CONTAINER}:/output/staging/${libdir}/${name}" - \
            2>/dev/null | tar -x -C "${DEST}/usr/lib" 2>/dev/null; then
          copied=1
          break
        fi
      done
      if [[ -z "${copied}" ]]; then
        echo "[sysroot] WARN: ${name} not found in staging usr/lib or lib" >&2
        break
      fi
      if [[ -L "${DEST}/usr/lib/${name}" ]]; then
        name="$(readlink "${DEST}/usr/lib/${name}")"
      else
        name=""
      fi
    done
  done
fi

echo "[sysroot] contents:"
find "${DEST}" -maxdepth 3 -name "*.so*" | sort
echo "[sysroot] headers: $(find "${DEST}/usr/include" -name '*.h' | wc -l) files"
echo "[sysroot] OK -> ${DEST}"
