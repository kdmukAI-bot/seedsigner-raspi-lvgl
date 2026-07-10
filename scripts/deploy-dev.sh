#!/usr/bin/env bash
# Local-dev deploy to a RUNNING SeedSigner Pi (rsync over SSH).
# ============================================================================
# Pushes the app's deployable payload: the cross-compiled .so, the app code, and
# the app's already-bundled language packs ($SS_APP_DIR/src/lang-packs).
#
# It points ONLY at the app. It does NOT know the pack repo, does NOT build packs,
# and does NOT branch on signed-vs-dev — whatever the app bundled is what deploys.
# An absent/empty src/lang-packs is a valid English-only deploy, not an error.
# (Populating the app's src/lang-packs is the pack-repo/app dev flow's job:
#  `build_packs.sh --out-dir $SS_APP_DIR/src/lang-packs` from the live pack checkout.)
#
# DEV-ONLY. This is the "rsync onto a live Pi" loop for the maintainer's dev box.
# It is NOT how SeedSigner ships: SeedSigner OS bakes its own buildroot image.
#
# Config: real environment variables win; otherwise values are read from a .env in
# the repo root (see .env.example). Usage:
#   scripts/deploy-dev.sh
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# Load .env (repo root) if present; process env still overrides it.
if [ -f "$REPO_ROOT/.env" ]; then set -a; . "$REPO_ROOT/.env"; set +a; fi

# --- Config (env-driven) -----------------------------------------------------
: "${SS_DEVICE:?set SS_DEVICE (e.g. pi@seedsigner.local) — see .env.example}"
: "${SS_APP_DIR:?set SS_APP_DIR — the seedsigner app checkout root — see .env.example}"
# The .so built by this repo (default: newest in src/). The `|| true` keeps a
# missing .so from killing the script here via pipefail+set -e — the preflight
# below owns that error and prints an actionable message.
SS_SO_PATH="${SS_SO_PATH:-$(ls -1t "$REPO_ROOT"/src/seedsigner_lvgl_screens*.so 2>/dev/null | head -n1 || true)}"
# Device-side layout (standard SeedSigner Pi paths; override only for an odd device).
SS_DEVICE_APP_DIR="${SS_DEVICE_APP_DIR:-/home/pi/seedsigner/src}"
SS_DEVICE_SO_DIR="${SS_DEVICE_SO_DIR:-/home/pi/seedsigner-raspi-lvgl/src}"

# --- Preflight ---------------------------------------------------------------
[ -n "$SS_SO_PATH" ] && [ -f "$SS_SO_PATH" ] \
  || { echo "ERROR: .so not found. Build it (./run_build.sh) or set SS_SO_PATH." >&2; exit 1; }
[ -d "$SS_APP_DIR/src/seedsigner" ] \
  || { echo "ERROR: no src/seedsigner under SS_APP_DIR=$SS_APP_DIR" >&2; exit 1; }

RSYNC=(rsync -az --exclude='__pycache__' --exclude='*.pyc' --exclude='.DS_Store')

echo "==> [1/3] .so   -> $SS_DEVICE:$SS_DEVICE_SO_DIR/"
"${RSYNC[@]}" "$SS_SO_PATH" "$SS_DEVICE:$SS_DEVICE_SO_DIR/"

echo "==> [2/3] app   -> $SS_DEVICE:$SS_DEVICE_APP_DIR/seedsigner/"
"${RSYNC[@]}" --exclude='settings.json' --exclude='.git' \
  "$SS_APP_DIR/src/seedsigner/" "$SS_DEVICE:$SS_DEVICE_APP_DIR/seedsigner/"

# --- [3/3] language packs: copy the app's payload straight into its CWD pack root
# The app reads packs at CWD-relative "lang-packs" (= $SS_DEVICE_APP_DIR/lang-packs).
PACKS="$SS_APP_DIR/src/lang-packs"
if [ -d "$PACKS" ] && [ -n "$(ls -A "$PACKS" 2>/dev/null)" ]; then
  echo "==> [3/3] language packs <- $PACKS"
  # Deploy STRAIGHT there — no symlink. If an old deploy left a symlink, remove it
  # first (rsync would otherwise write THROUGH it into the old location).
  ssh "$SS_DEVICE" "[ -L '$SS_DEVICE_APP_DIR/lang-packs' ] && rm -f '$SS_DEVICE_APP_DIR/lang-packs' || true"
  "${RSYNC[@]}" "$PACKS"/ "$SS_DEVICE:$SS_DEVICE_APP_DIR/lang-packs/"
else
  echo "==> [3/3] no packs at $PACKS — English-only deploy (nothing to copy)"
fi

echo ""
echo "==> Deployed. Restart SeedSigner on the device to pick up the changes:"
# The ^ anchor keeps pkill from matching the ssh wrapper's own command line
# (an unanchored pattern kills the ssh session itself).
echo "    ssh $SS_DEVICE 'pkill -f \"^python3 -u main.py\"; cd $SS_DEVICE_APP_DIR && setsid nohup python3 -u main.py </dev/null >/tmp/ss_run.log 2>&1 &'"
