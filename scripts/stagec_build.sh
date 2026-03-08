#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TARGET_ARCH="${TARGET_ARCH:-host}"
BUILD_MODE="host"
if [[ "${ROOT_DIR}" == /workspace/* ]]; then
  BUILD_MODE="docker"
fi

RUN_TS="${RUN_TS:-$(date -u +%Y%m%d-%H%M%S)}"
LOG_DIR="${ROOT_DIR}/logs/stagec"
mkdir -p "${LOG_DIR}"
LOG_FILE="${LOG_DIR}/${RUN_TS}_stagec_${BUILD_MODE}_${TARGET_ARCH}.log"
exec > >(tee -a "${LOG_FILE}")
exec 2>&1

echo "[stagec] run_ts=${RUN_TS} mode=${BUILD_MODE} target_arch=${TARGET_ARCH}"
echo "[stagec] log_file=${LOG_FILE}"

if [[ "${TARGET_ARCH}" != "host" ]]; then
  echo "[stagec] NOTE: native CPython extension cross-build for ${TARGET_ARCH} is deferred (requires target Python headers/sysroot)."
  echo "[stagec] Running Stage B armv6 compile sanity as architecture gate."
  TARGET_ARCH="armv6" RUN_TS="${RUN_TS}" bash "${ROOT_DIR}/scripts/stageb_build.sh"
  echo "[stagec] DEFERRED: CPython extension for ${TARGET_ARCH}"
  exit 0
fi

VENV_DIR="${ROOT_DIR}/.venv-stagec"
python3 -m venv "${VENV_DIR}"
# shellcheck disable=SC1091
source "${VENV_DIR}/bin/activate"

python -m pip install -U pip setuptools wheel
python -m pip install -e "${ROOT_DIR}"
python -m pip install pytest

python -m pytest -q "${ROOT_DIR}/tests/test_stagec_native_smoke.py"

echo "[stagec] OK host native extension scaffold built and smoke-tested"
