#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
RUN_TS="${RUN_TS:-$(date -u +%Y%m%d-%H%M%S)}"

echo "[build] run_ts=${RUN_TS}"

ABI_JSON="${ABI_JSON:-${ROOT_DIR}/docs/abi/dev-pi-abi.json}"
LOCK_FILE="${LOCK_FILE:-${ROOT_DIR}/versions.lock.toml}"

PYTHON_BIN="$(command -v python3 || true)"
if [[ -z "${PYTHON_BIN}" && -x /opt/python/bin/python3 ]]; then
  PYTHON_BIN="/opt/python/bin/python3"
fi
if [[ -z "${PYTHON_BIN}" ]]; then
  echo "ERROR: python3 not found in container" >&2
  exit 10
fi

# Lock parsing: stdlib tomllib on py311+, tomli on the py310 base image
# (installed only if the image doesn't already carry it).
"${PYTHON_BIN}" -c "import tomllib" 2>/dev/null \
  || "${PYTHON_BIN}" -c "import tomli" 2>/dev/null \
  || "${PYTHON_BIN}" -m pip install --disable-pip-version-check -q tomli

"${PYTHON_BIN}" - <<'PY' "${ABI_JSON}" "${LOCK_FILE}"
import json, platform, sysconfig, sys
try:
    import tomllib
except Exception:
    import tomli as tomllib
abi_json, lock_file = sys.argv[1:3]
with open(abi_json, 'r', encoding='utf-8') as f:
    target = json.load(f)
with open(lock_file, 'rb') as f:
    lock = tomllib.load(f)

machine = platform.machine()
soabi = sysconfig.get_config_var('SOABI')
ext_suffix = sysconfig.get_config_var('EXT_SUFFIX')
include = sysconfig.get_config_var('INCLUDEPY')

lock_soabi = lock.get('target_abi', {}).get('python_soabi')
lock_ext = lock.get('target_abi', {}).get('python_ext_suffix')
arch = lock.get('toolchain', {}).get('arch')
tune = lock.get('toolchain', {}).get('tune')
fpu = lock.get('toolchain', {}).get('fpu')
float_abi = lock.get('toolchain', {}).get('float_abi')

print('[build] machine=', machine)
print('[build] soabi=', soabi)
print('[build] ext_suffix=', ext_suffix)
print('[build] include=', include)
print('[build] target_soabi(json)=', target.get('soabi'))
print('[build] target_ext_suffix(json)=', target.get('ext_suffix'))
print('[build] lock_soabi=', lock_soabi)
print('[build] lock_ext_suffix=', lock_ext)

if target.get('soabi') != lock_soabi:
    raise SystemExit(f"ABI JSON vs lock mismatch for SOABI: {target.get('soabi')} != {lock_soabi}")
if target.get('ext_suffix') != lock_ext:
    raise SystemExit(f"ABI JSON vs lock mismatch for EXT_SUFFIX: {target.get('ext_suffix')} != {lock_ext}")
if soabi != lock_soabi:
    raise SystemExit(f"SOABI mismatch: got {soabi}, expected {lock_soabi}")
if ext_suffix != lock_ext:
    raise SystemExit(f"EXT_SUFFIX mismatch: got {ext_suffix}, expected {lock_ext}")

if not all([arch, tune, fpu, float_abi]):
    raise SystemExit('Lock file missing required toolchain fields')
print('[build] lock toolchain OK')
PY

# Compiler is now supplied by the locked py310-dev base image toolchain.
# No runtime apt/compiler workaround in this build path.
# Use ccache when available to speed up incremental rebuilds. Assign CC/CXX
# unconditionally: the base image bakes ENV CC=gcc / CXX=g++, so a
# "${CC:-ccache gcc}" default silently loses to it and the build runs uncached.
if command -v ccache >/dev/null 2>&1; then
  export CC="ccache gcc"
  export CXX="ccache g++"
  ccache --max-size=2G >/dev/null 2>&1 || true
  ccache --zero-stats >/dev/null 2>&1 || true
  echo "[build] ccache enabled"
else
  export CC="${CC:-gcc}"
  export CXX="${CXX:-g++}"
fi
echo "[build] compiler CC=${CC} CXX=${CXX}"

# Force Pi Zero-compatible ARMv6 code generation from lock values, and read
# the runtime-compat symbol ceilings for the artifact gates below.
read -r ARCH TUNE FPU FLOAT_ABI LOCK_MAX_GLIBC LOCK_MAX_GLIBCXX <<< "$("${PYTHON_BIN}" - <<'PY' "${LOCK_FILE}"
try:
    import tomllib
except Exception:
    import tomli as tomllib
import sys
with open(sys.argv[1], 'rb') as f:
    lock = tomllib.load(f)
tc = lock['toolchain']
abi = lock.get('target_abi', {})
print(tc['arch'], tc['tune'], tc['fpu'], tc['float_abi'],
      abi.get('max_glibc', 'GLIBC_2.28'), abi.get('max_glibcxx', 'GLIBCXX_3.4.21'))
PY
)"

export CFLAGS="${CFLAGS:-} -march=${ARCH} -mtune=${TUNE} -marm -mfpu=${FPU} -mfloat-abi=${FLOAT_ABI}"
export CXXFLAGS="${CXXFLAGS:-} -march=${ARCH} -mtune=${TUNE} -marm -mfpu=${FPU} -mfloat-abi=${FLOAT_ABI}"
export ARMV6_FORCE=1
# setup.py reads these so the lock stays the single source of the codegen flags.
export ARMV6_ARCH="${ARCH}" ARMV6_TUNE="${TUNE}" ARMV6_FPU="${FPU}" ARMV6_FLOAT_ABI="${FLOAT_ABI}"

VENV_DIR="/root/.venv-build"
if [[ ! -f "${VENV_DIR}/bin/activate" ]]; then
  "${PYTHON_BIN}" -m venv "${VENV_DIR}"
fi
# shellcheck disable=SC1091
source "${VENV_DIR}/bin/activate"
python -c "import setuptools, pytest" 2>/dev/null || python -m pip install --disable-pip-version-check -q pip setuptools pytest

# The package dir (pyproject: package-dir {"" = "src"}) holds only the built,
# git-ignored .so, so a fresh checkout has no src/ at all. Create it before the
# inplace build copies the extension there. Local trees carry src/ from prior
# builds, which is why a missing src/ only ever surfaced on a clean CI clone.
mkdir -p "${ROOT_DIR}/src"
rm -f "${ROOT_DIR}"/src/seedsigner_lvgl_screens*.so "${ROOT_DIR}"/src/uUR*.so

# Force a fresh native extension build so architecture flags are applied deterministically.
# Build artifacts go to /tmp/build to keep the project tree clean.
BUILD_DIR="/tmp/build"
rm -rf "${BUILD_DIR}"
python "${ROOT_DIR}/setup.py" build_ext --inplace --force --parallel "$(nproc)" \
  --build-temp "${BUILD_DIR}/temp" --build-lib "${BUILD_DIR}/lib"

# Cache effectiveness is load-bearing for CI wall-clock: surface hit/miss stats
# in every log so a silently-dead cache (empty dir, defeated CC) is visible.
if command -v ccache >/dev/null 2>&1; then
  echo "[build] ccache stats after build:"
  ccache --show-stats || true
fi

# Ensure tests import from local src tree. Runs the whole suite: pyproject's
# python_files restricts collection to test_*.py (the pi_*_test.py scripts are
# on-device manual harnesses, not collectable tests).
PYTHONPATH="${ROOT_DIR}/src" python -m pytest -q -p no:cacheprovider "${ROOT_DIR}/tests"

# Ceilings from versions.lock.toml (override via MAX_GLIBC{,XX} as needed).
MAX_GLIBCXX="${MAX_GLIBCXX:-${LOCK_MAX_GLIBCXX}}"
MAX_GLIBC="${MAX_GLIBC:-${LOCK_MAX_GLIBC}}"

# Run every runtime-compat gate on one built .so: target EXT_SUFFIX, ARMv6 CPU
# arch, and the GLIBCXX/GLIBC symbol-version ceilings (a too-new ref only ever
# fails at dlopen on the device). Applied to BOTH extensions this build emits.
verify_artifact() {
  local ART="$1" label="$2"
  echo "[build] --- verifying ${label}: $(basename "${ART}") ---"

  "${PYTHON_BIN}" - <<'PY' "${ABI_JSON}" "${ART}"
import json,os,sys
abi_json,art=sys.argv[1:3]
with open(abi_json,'r',encoding='utf-8') as f:
    target=json.load(f)
ext=target.get('ext_suffix')
if ext and not art.endswith(ext):
    raise SystemExit(f"artifact suffix mismatch: {os.path.basename(art)} != *{ext}")
print('[build] artifact suffix OK')
PY

  file "${ART}" || true

  if command -v readelf >/dev/null 2>&1; then
    local READELF_OUT
    READELF_OUT="$(readelf -A "${ART}")"
    echo "${READELF_OUT}"
    if ! echo "${READELF_OUT}" | grep -Eq 'Tag_CPU_arch: v6|Tag_CPU_arch: v6KZ'; then
      echo "ERROR: ${label} artifact CPU arch attribute is not ARMv6-compatible" >&2
      exit 7
    fi
  fi

  local FOUND_GLIBCXX
  FOUND_GLIBCXX="$(strings "${ART}" | grep -o 'GLIBCXX_[0-9.]*' | sort -V | tail -n1 || true)"
  if [[ -n "${FOUND_GLIBCXX}" ]]; then
    echo "[build] ${label} max_glibcxx_required=${FOUND_GLIBCXX} allowed=${MAX_GLIBCXX}"
    if [[ "$(printf '%s\n%s\n' "${MAX_GLIBCXX}" "${FOUND_GLIBCXX}" | sort -V | tail -n1)" != "${MAX_GLIBCXX}" ]]; then
      echo "ERROR: ${label} GLIBCXX requirement too new for target runtime: ${FOUND_GLIBCXX} > ${MAX_GLIBCXX}" >&2
      exit 8
    fi
  else
    echo "[build] ${label} no GLIBCXX symbols found (pure C / static libstdc++)"
  fi

  local FOUND_GLIBC
  FOUND_GLIBC="$(strings "${ART}" | grep -o 'GLIBC_[0-9.]*' | sort -V | tail -n1 || true)"
  if [[ -n "${FOUND_GLIBC}" ]]; then
    echo "[build] ${label} max_glibc_required=${FOUND_GLIBC} allowed=${MAX_GLIBC}"
    if [[ "$(printf '%s\n%s\n' "${MAX_GLIBC}" "${FOUND_GLIBC}" | sort -V | tail -n1)" != "${MAX_GLIBC}" ]]; then
      echo "ERROR: ${label} GLIBC requirement too new for target runtime: ${FOUND_GLIBC} > ${MAX_GLIBC}" >&2
      exit 9
    fi
  else
    echo "[build] ${label} no GLIBC versioned symbols found"
  fi
}

MAIN_ART="$(find "${ROOT_DIR}" -maxdepth 2 -type f -name 'seedsigner_lvgl_screens*.so' | sort | tail -n1 || true)"
if [[ -z "${MAIN_ART}" ]]; then
  echo "ERROR: no built seedsigner_lvgl_screens extension artifact found" >&2
  exit 6
fi
verify_artifact "${MAIN_ART}" "seedsigner_lvgl_screens"

UUR_ART="$(find "${ROOT_DIR}" -maxdepth 2 -type f -name 'uUR*.so' | sort | tail -n1 || true)"
if [[ -z "${UUR_ART}" ]]; then
  echo "ERROR: no built uUR extension artifact found" >&2
  exit 6
fi
verify_artifact "${UUR_ART}" "uUR"

echo "[build] OK"
