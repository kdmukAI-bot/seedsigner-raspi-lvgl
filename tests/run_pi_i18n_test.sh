#!/usr/bin/env bash
# Convenience launcher for the interactive i18n locale tester on the Pi Zero.
# Stops seedsigner.service (it owns the display/GPIO) for the duration and
# restarts it on exit — including Ctrl+C and kill.
set -u

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

restore() {
    echo "[run] restarting seedsigner.service ..."
    sudo systemctl start seedsigner.service
}
trap restore EXIT INT TERM

echo "[run] stopping seedsigner.service ..."
sudo systemctl stop seedsigner.service

echo "[run] launching i18n locale tester ..."
PYTHONPATH="$REPO/src" python3 -u "$REPO/tests/pi_i18n_locale_test.py"
