#!/usr/bin/env bash
# Shared CI preflight: verify the screens submodule (and its nested LVGL
# submodule) is checked out at a post-split commit before spending minutes on a
# QEMU build. Called by all three CI configs (.github/, .forgejo/,
# .gitlab-ci.yml) so a submodule reorg only ever updates this one file.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

if [[ ! -d "${ROOT}/sources/seedsigner-lvgl-screens/components/seedsigner/screens" ]]; then
  echo "ERROR: screens/ missing under sources/seedsigner-lvgl-screens —" \
       "submodule not checked out (git submodule update --init --recursive)" \
       "or pinned before the one-file-per-screen split" >&2
  exit 1
fi

if [[ ! -f "${ROOT}/sources/seedsigner-lvgl-screens/third_party/lvgl/lvgl.h" ]]; then
  echo "ERROR: lvgl.h missing — nested LVGL submodule not checked out" >&2
  exit 1
fi

echo "[preflight] OK"
