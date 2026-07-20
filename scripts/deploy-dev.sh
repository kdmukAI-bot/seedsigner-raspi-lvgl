#!/usr/bin/env bash
# Local-dev deploy to a RUNNING SeedSigner Pi (rsync over SSH).
# ============================================================================
# Targets the SeedSigner OS *dev* image (>= build #114: libcamera, cpython-312,
# glibc 2.40). That image differs from the old Buster layout this script used to
# assume:
#   - it is ROOT-ONLY (there is no `pi` user) -> SS_DEVICE is root@...
#   - the app runs from a checkout on the writable data partition,
#     /mnt/data/seedsigner/src, which the image's /start.sh prefers over the
#     baked /opt/src app (see /etc/init.d/S02seedsigner -> /start.sh).
#   - the app is launched/stopped via the `seedsigner` init helper
#     (`seedsigner {start|stop|restart|status}` = /etc/init.d/S02seedsigner).
#
# Because the app is `exec python3 main.py` with CWD = /mnt/data/seedsigner/src,
# that dir is sys.path[0]. So the native .so is deployed straight INTO it — a
# bare `import seedsigner_lvgl_screens` then resolves with no PYTHONPATH/.pth.
#
# Pushes the app's deployable payload: the app code (incl. main.py, so a bare
# #114 image is provisioned on the first run), the cross-compiled .so(s), and the
# app's already-bundled language packs ($SS_APP_DIR/src/lang-packs).
#
# It points ONLY at the app. It does NOT know the pack repo, does NOT build packs,
# and does NOT branch on signed-vs-dev — whatever the app bundled is what deploys.
# An absent/empty src/lang-packs is a valid English-only deploy, not an error.
# (Populating the app's src/lang-packs is the pack-repo/app dev flow's job:
#  `build_packs.sh --out-dir $SS_APP_DIR/src/lang-packs` from the live pack checkout.)
#
# Boot behavior (image, not this script): S02seedsigner starts the app at boot
# via start-stop-daemon with NO respawn flag, and inittab only respawns login
# shells -- so the app auto-runs at startup but a killed/crashed app stays down
# until a manual `seedsigner start` or reboot. This script stops the app before
# syncing (frees GPIO/SPI + the in-use .so) and explicitly restarts it at the end.
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
#
# Sourcing alone does NOT give that precedence: a plain `VAR=value` line in .env
# overwrites an already-exported VAR, so `SS_APP_DIR=... ./deploy-dev.sh` would
# silently deploy whatever .env names instead — the wrong tree, with a successful
# -looking run. Snapshot the caller's SS_* vars and re-apply them after sourcing.
if [ -f "$REPO_ROOT/.env" ]; then
  declare -A _ss_override=()
  while IFS= read -r _k; do _ss_override["$_k"]="${!_k}"; done \
    < <(env | sed -n 's/^\(SS_[A-Za-z0-9_]*\)=.*/\1/p')
  set -a; . "$REPO_ROOT/.env"; set +a
  for _k in "${!_ss_override[@]}"; do printf -v "$_k" '%s' "${_ss_override[$_k]}"; done
  unset _ss_override _k
fi

# --- Config (env-driven) -----------------------------------------------------
: "${SS_DEVICE:?set SS_DEVICE (e.g. root@seedsigner.local) — see .env.example}"
: "${SS_APP_DIR:?set SS_APP_DIR — the seedsigner app checkout root — see .env.example}"
# The .so built by this repo (default: newest in src/). The `|| true` keeps a
# missing .so from killing the script here via pipefail+set -e — the preflight
# below owns that error and prints an actionable message.
SS_SO_PATH="${SS_SO_PATH:-$(ls -1t "$REPO_ROOT"/src/seedsigner_lvgl_screens*.so 2>/dev/null | head -n1 || true)}"
# Native cUR (BC-UR) `uUR` extension, if this build produced one. Optional: an
# absent uUR.so is a valid deploy — the app's helpers/ur2/decoder.py falls back
# to the pure-Python decoder when `import uUR` fails.
SS_UUR_SO_PATH="${SS_UUR_SO_PATH:-$(ls -1t "$REPO_ROOT"/src/uUR*.so 2>/dev/null | head -n1 || true)}"
# Device-side layout (#114 dev image; override only for an odd device).
# The app dir is /start.sh's DEV_SRC and the CWD at launch, so the .so co-locates
# there by default (sys.path[0] import — no PYTHONPATH needed).
SS_DEVICE_APP_DIR="${SS_DEVICE_APP_DIR:-/mnt/data/seedsigner/src}"
SS_DEVICE_SO_DIR="${SS_DEVICE_SO_DIR:-$SS_DEVICE_APP_DIR}"
# The `seedsigner` init helper on the dev image (stop/start around the sync).
SS_CTL="${SS_CTL:-seedsigner}"

# --- Preflight ---------------------------------------------------------------
[ -n "$SS_SO_PATH" ] && [ -f "$SS_SO_PATH" ] \
  || { echo "ERROR: .so not found. Build it (./run_build.sh) or set SS_SO_PATH." >&2; exit 1; }
[ -d "$SS_APP_DIR/src/seedsigner" ] \
  || { echo "ERROR: no src/seedsigner under SS_APP_DIR=$SS_APP_DIR" >&2; exit 1; }

RSYNC=(rsync -az --exclude='__pycache__' --exclude='*.pyc' --exclude='.DS_Store')

# --- Stop the app so the sync can replace the in-use .so and free GPIO/SPI ----
echo "==> [0/4] stop app  ($SS_DEVICE: $SS_CTL stop)"
ssh "$SS_DEVICE" "$SS_CTL stop" || true

# --- [1/4] app code (whole src/ -> provisions main.py on a bare image) -------
# Excludes: settings.json (preserve device-side state), VCS/build cruft, and
# lang-packs (handled explicitly in [3/4] to also clear a stale symlink).
echo "==> [1/4] app   -> $SS_DEVICE:$SS_DEVICE_APP_DIR/"
ssh "$SS_DEVICE" "mkdir -p '$SS_DEVICE_APP_DIR'"
"${RSYNC[@]}" --exclude='settings.json' --exclude='.git' --exclude='*.egg-info' \
  --exclude='lang-packs' \
  "$SS_APP_DIR/src/" "$SS_DEVICE:$SS_DEVICE_APP_DIR/"

# --- [2/4] native .so(s) co-located into the app dir (CWD import) ------------
echo "==> [2/4] .so   -> $SS_DEVICE:$SS_DEVICE_SO_DIR/  ($(basename "$SS_SO_PATH"))"
ssh "$SS_DEVICE" "mkdir -p '$SS_DEVICE_SO_DIR'"
"${RSYNC[@]}" "$SS_SO_PATH" "$SS_DEVICE:$SS_DEVICE_SO_DIR/"
if [ -n "$SS_UUR_SO_PATH" ] && [ -f "$SS_UUR_SO_PATH" ]; then
  echo "    + uUR -> $(basename "$SS_UUR_SO_PATH")"
  "${RSYNC[@]}" "$SS_UUR_SO_PATH" "$SS_DEVICE:$SS_DEVICE_SO_DIR/"
else
  echo "    (no uUR .so found — app uses the pure-Python ur2 decoder fallback)"
fi

# --- [3/4] language packs: the app reads packs at CWD-relative "lang-packs" ---
PACKS="$SS_APP_DIR/src/lang-packs"
if [ -d "$PACKS" ] && [ -n "$(ls -A "$PACKS" 2>/dev/null)" ]; then
  echo "==> [3/4] language packs <- $PACKS"
  # Deploy STRAIGHT there — no symlink. If an old deploy left a symlink, remove it
  # first (rsync would otherwise write THROUGH it into the old location).
  ssh "$SS_DEVICE" "[ -L '$SS_DEVICE_APP_DIR/lang-packs' ] && rm -f '$SS_DEVICE_APP_DIR/lang-packs' || true"
  "${RSYNC[@]}" "$PACKS"/ "$SS_DEVICE:$SS_DEVICE_APP_DIR/lang-packs/"
else
  echo "==> [3/4] no packs at $PACKS — English-only deploy (nothing to copy)"
fi

# --- [4/4] start the freshly-deployed app ------------------------------------
echo "==> [4/4] start app ($SS_DEVICE: $SS_CTL start)"
ssh "$SS_DEVICE" "$SS_CTL start"

echo ""
echo "==> Deployed and started. Handy on-device commands:"
echo "    ssh $SS_DEVICE '$SS_CTL status'   # running? (pid)"
echo "    ssh $SS_DEVICE '$SS_CTL restart'  # after a manual edit"
echo "    ssh $SS_DEVICE '$SS_CTL stop'     # stays down (no respawn) until start/reboot"
