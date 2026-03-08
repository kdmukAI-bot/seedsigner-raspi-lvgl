#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PLATFORM="${PLATFORM:-linux/arm/v6}"
PYTHON_VERSION="${PYTHON_VERSION:-3.10.10}"
PY_SERIES="${PY_SERIES:-py310}"
IMAGE_TAG_LOCAL="${IMAGE_TAG_LOCAL:-seedsigner-raspi-lvgl/python-armv6:${PY_SERIES}-local}"
DOCKERFILE="${ROOT_DIR}/docker/Dockerfile.python-armv6-base"

RUN_TS="${RUN_TS:-$(date -u +%Y%m%d-%H%M%S)}"
LOG_DIR="${ROOT_DIR}/logs/base-image"
mkdir -p "${LOG_DIR}"
LOG_FILE="${LOG_DIR}/${RUN_TS}_python-armv6-base_${PY_SERIES}.log"
exec > >(tee -a "${LOG_FILE}")
exec 2>&1

echo "[python-armv6-base] run_ts=${RUN_TS}"
echo "[python-armv6-base] platform=${PLATFORM} python_version=${PYTHON_VERSION}"
echo "[python-armv6-base] image_tag=${IMAGE_TAG_LOCAL}"

# Ensure binfmt is present for emulation builds on amd64 hosts.
docker run --privileged --rm tonistiigi/binfmt --install arm >/dev/null

docker build \
  --platform "${PLATFORM}" \
  -f "${DOCKERFILE}" \
  --build-arg "PYTHON_VERSION=${PYTHON_VERSION}" \
  -t "${IMAGE_TAG_LOCAL}" \
  "${ROOT_DIR}"

# Smoke capture from built image.
docker run --rm --platform "${PLATFORM}" "${IMAGE_TAG_LOCAL}" python3 - <<'PY'
import json,platform,sysconfig
print(json.dumps({
  'python_version': platform.python_version(),
  'machine': platform.machine(),
  'soabi': sysconfig.get_config_var('SOABI'),
  'ext_suffix': sysconfig.get_config_var('EXT_SUFFIX'),
  'include_py': sysconfig.get_config_var('INCLUDEPY'),
  'libdir': sysconfig.get_config_var('LIBDIR'),
  'ldlibrary': sysconfig.get_config_var('LDLIBRARY'),
}, indent=2))
PY

echo "[python-armv6-base] OK"
