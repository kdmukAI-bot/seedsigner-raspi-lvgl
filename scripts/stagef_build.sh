#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TARGET_ARCH="${TARGET_ARCH:-host}"
BUILD_MODE="host"
if [[ "${ROOT_DIR}" == /workspace/* ]]; then
  BUILD_MODE="docker"
fi

RUN_TS="${RUN_TS:-$(date -u +%Y%m%d-%H%M%S)}"
LOG_DIR="${ROOT_DIR}/logs/stagef"
mkdir -p "${LOG_DIR}"
LOG_FILE="${LOG_DIR}/${RUN_TS}_stagef_${BUILD_MODE}_${TARGET_ARCH}.log"
exec > >(tee -a "${LOG_FILE}")
exec 2>&1

echo "[stagef] run_ts=${RUN_TS} mode=${BUILD_MODE} target_arch=${TARGET_ARCH}"
echo "[stagef] log_file=${LOG_FILE}"

if [[ "${TARGET_ARCH}" == "host" ]]; then
  echo "[stagef] host mode -> running stagec host path"
  TARGET_ARCH=host RUN_TS="${RUN_TS}" bash "${ROOT_DIR}/scripts/stagec_build.sh"
  echo "[stagef] OK host path"
  exit 0
fi

if [[ "${TARGET_ARCH}" != "armv6-cpython" ]]; then
  echo "ERROR: unsupported TARGET_ARCH=${TARGET_ARCH}; expected host|armv6-cpython" >&2
  exit 2
fi

ABI_JSON="${ABI_JSON:-${ROOT_DIR}/docs/abi/dev-pi-abi.json}"
if [[ ! -f "${ABI_JSON}" ]]; then
  echo "ERROR: ABI_JSON not found: ${ABI_JSON}" >&2
  exit 3
fi

# Pull ABI fields from JSON (dev target is current priority).
PYTHON_TARGET_INCLUDE_DEFAULT="$(python3 - <<'PY' "${ABI_JSON}"
import json,sys
p=sys.argv[1]
with open(p,'r',encoding='utf-8') as f:
    d=json.load(f)
print(d.get('include_py',''))
PY
)"
PYTHON_TARGET_SOABI="$(python3 - <<'PY' "${ABI_JSON}"
import json,sys
p=sys.argv[1]
with open(p,'r',encoding='utf-8') as f:
    d=json.load(f)
print(d.get('soabi',''))
PY
)"
PYTHON_TARGET_EXT_SUFFIX="$(python3 - <<'PY' "${ABI_JSON}"
import json,sys
p=sys.argv[1]
with open(p,'r',encoding='utf-8') as f:
    d=json.load(f)
print(d.get('ext_suffix',''))
PY
)"

PYTHON_TARGET_INCLUDE="${PYTHON_TARGET_INCLUDE:-${PYTHON_TARGET_INCLUDE_DEFAULT}}"
if [[ -z "${PYTHON_TARGET_INCLUDE}" ]]; then
  echo "ERROR: PYTHON_TARGET_INCLUDE unresolved (ABI JSON missing include_py?)" >&2
  exit 4
fi

echo "[stagef] abi_json=${ABI_JSON}"
echo "[stagef] python_target_include=${PYTHON_TARGET_INCLUDE}"
echo "[stagef] python_target_soabi=${PYTHON_TARGET_SOABI}"
echo "[stagef] python_target_ext_suffix=${PYTHON_TARGET_EXT_SUFFIX}"

if [[ ! -d "${PYTHON_TARGET_INCLUDE}" ]]; then
  echo "ERROR: PYTHON_TARGET_INCLUDE does not exist on this build host/container: ${PYTHON_TARGET_INCLUDE}" >&2
  echo "[stagef] Provide mounted target headers path via PYTHON_TARGET_INCLUDE." >&2
  exit 5
fi

# Optional target libs for future linker tightening.
PYTHON_TARGET_LIBDIR="${PYTHON_TARGET_LIBDIR:-}"
PYTHON_TARGET_LDLIBRARY="${PYTHON_TARGET_LDLIBRARY:-}"

echo "[stagef] building armv6-cpython extension"

# Build in isolated venv for reproducibility.
VENV_DIR="${ROOT_DIR}/.venv-stagef"
python3 -m venv "${VENV_DIR}"
# shellcheck disable=SC1091
source "${VENV_DIR}/bin/activate"
python -m pip install -U pip setuptools wheel

export CC=arm-linux-gnueabihf-gcc
export CXX=arm-linux-gnueabihf-g++
export AR=arm-linux-gnueabihf-ar
export RANLIB=arm-linux-gnueabihf-ranlib
export STAGEF_CROSS=1
export PYTHON_TARGET_INCLUDE
export PYTHON_TARGET_LIBDIR
export PYTHON_TARGET_LDLIBRARY
export CFLAGS="${CFLAGS:-} -march=armv6zk -mtune=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard"
export CXXFLAGS="${CXXFLAGS:-} -march=armv6zk -mtune=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard"

# Build extension in place (this is the wiring step; packaging polish can follow).
python setup.py build_ext --inplace

ART="$(find "${ROOT_DIR}" -maxdepth 2 -type f -name 'seedsigner_lvgl_native*.so' | head -n1 || true)"
if [[ -z "${ART}" ]]; then
  echo "ERROR: no built extension artifact found" >&2
  exit 6
fi

echo "[stagef] artifact=${ART}"
if command -v file >/dev/null 2>&1; then
  file "${ART}" || true
fi

if command -v file >/dev/null 2>&1; then
  if ! file "${ART}" | grep -Eiq "ARM|arm"; then
    echo "ERROR: built extension is not ARM architecture" >&2
    exit 7
  fi
fi

META_DIR="${ROOT_DIR}/build/stagef-armv6-cpython"
mkdir -p "${META_DIR}"
python3 - <<'PY' "${META_DIR}/artifact-meta.json" "${ART}" "${PYTHON_TARGET_SOABI}" "${PYTHON_TARGET_EXT_SUFFIX}"
import json,os,sys
out,art,soabi,ext=sys.argv[1:5]
payload={
  "artifact": art,
  "artifact_basename": os.path.basename(art),
  "expected_soabi": soabi,
  "expected_ext_suffix": ext,
}
with open(out,'w',encoding='utf-8') as f:
  json.dump(payload,f,indent=2)
print(f"[stagef] wrote {out}")
PY

echo "[stagef] OK armv6-cpython wiring build completed"
