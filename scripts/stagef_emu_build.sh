#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
RUN_TS="${RUN_TS:-$(date -u +%Y%m%d-%H%M%S)}"
LOG_DIR="${ROOT_DIR}/logs/stagef"
mkdir -p "${LOG_DIR}"
LOG_FILE="${LOG_DIR}/${RUN_TS}_stagef_emu_armv7.log"
exec > >(tee -a "${LOG_FILE}")
exec 2>&1

echo "[stagef-emu] run_ts=${RUN_TS}"
echo "[stagef-emu] log_file=${LOG_FILE}"

ABI_JSON="${ABI_JSON:-${ROOT_DIR}/docs/abi/dev-pi-abi.json}"
LOCK_FILE="${LOCK_FILE:-${ROOT_DIR}/versions.lock.toml}"

# Python 3.10 does not include tomllib; install lightweight parser for lock read.
python3 -m pip install --disable-pip-version-check -q tomli

python3 - <<'PY' "${ABI_JSON}" "${LOCK_FILE}"
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

print('[stagef-emu] machine=', machine)
print('[stagef-emu] soabi=', soabi)
print('[stagef-emu] ext_suffix=', ext_suffix)
print('[stagef-emu] include=', include)
print('[stagef-emu] target_soabi(json)=', target.get('soabi'))
print('[stagef-emu] target_ext_suffix(json)=', target.get('ext_suffix'))
print('[stagef-emu] lock_soabi=', lock_soabi)
print('[stagef-emu] lock_ext_suffix=', lock_ext)

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
print('[stagef-emu] lock toolchain OK')
PY

# Force Pi Zero-compatible ARMv6 code generation from lock values.
ARCH="$(python3 - <<'PY' "${LOCK_FILE}"
try:
    import tomllib
except Exception:
    import tomli as tomllib
import sys
with open(sys.argv[1], 'rb') as f:
    lock = tomllib.load(f)
print(lock['toolchain']['arch'])
PY
)"
TUNE="$(python3 - <<'PY' "${LOCK_FILE}"
try:
    import tomllib
except Exception:
    import tomli as tomllib
import sys
with open(sys.argv[1], 'rb') as f:
    lock = tomllib.load(f)
print(lock['toolchain']['tune'])
PY
)"
FPU="$(python3 - <<'PY' "${LOCK_FILE}"
try:
    import tomllib
except Exception:
    import tomli as tomllib
import sys
with open(sys.argv[1], 'rb') as f:
    lock = tomllib.load(f)
print(lock['toolchain']['fpu'])
PY
)"
FLOAT_ABI="$(python3 - <<'PY' "${LOCK_FILE}"
try:
    import tomllib
except Exception:
    import tomli as tomllib
import sys
with open(sys.argv[1], 'rb') as f:
    lock = tomllib.load(f)
print(lock['toolchain']['float_abi'])
PY
)"

export CFLAGS="${CFLAGS:-} -march=${ARCH} -mtune=${TUNE} -marm -mfpu=${FPU} -mfloat-abi=${FLOAT_ABI}"
export CXXFLAGS="${CXXFLAGS:-} -march=${ARCH} -mtune=${TUNE} -marm -mfpu=${FPU} -mfloat-abi=${FLOAT_ABI}"
export STAGEF_ARMV6_FORCE=1

VENV_DIR="${ROOT_DIR}/.venv-stagef-emu"
rm -rf "${VENV_DIR}"
python3 -m venv "${VENV_DIR}"
# shellcheck disable=SC1091
source "${VENV_DIR}/bin/activate"
python -m pip install -U pip setuptools wheel pytest

rm -f "${ROOT_DIR}"/src/seedsigner_lvgl_native*.so
rm -rf "${ROOT_DIR}"/build

# Force a fresh native extension build so architecture flags are applied deterministically.
python "${ROOT_DIR}/setup.py" build_ext --inplace --force

# Ensure tests import from local src tree.
PYTHONPATH="${ROOT_DIR}/src" python -m pytest -q "${ROOT_DIR}/tests/test_stagec_native_smoke.py"

ART="$(find "${ROOT_DIR}" -maxdepth 2 -type f -name 'seedsigner_lvgl_native*.so' | sort | tail -n1 || true)"
if [[ -z "${ART}" ]]; then
  echo "ERROR: no built extension artifact found" >&2
  exit 6
fi

if [[ "${ART}" != *"${target_ext_suffix:-}" ]]; then
  # target_ext_suffix is not a shell var; verify using python to avoid drift.
  python3 - <<'PY' "${ABI_JSON}" "${ART}"
import json,os,sys
abi_json,art=sys.argv[1:3]
with open(abi_json,'r',encoding='utf-8') as f:
    target=json.load(f)
ext=target.get('ext_suffix')
if ext and not art.endswith(ext):
    raise SystemExit(f"artifact suffix mismatch: {os.path.basename(art)} != *{ext}")
print('[stagef-emu] artifact suffix OK')
PY
fi

file "${ART}" || true

if command -v readelf >/dev/null 2>&1; then
  readelf -A "${ART}" | tee -a "${LOG_FILE}"
  if ! readelf -A "${ART}" | grep -Eq 'Tag_CPU_arch: v6|Tag_CPU_arch: v6KZ'; then
    echo "ERROR: artifact CPU arch attribute is not ARMv6-compatible" >&2
    exit 7
  fi
fi

echo "[stagef-emu] OK"
