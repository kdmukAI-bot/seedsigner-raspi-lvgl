#!/usr/bin/env bash
# Convenience launcher for the interactive io_test_screen tester on the Pi Zero
# dev image (#114). The running app (python3 main.py) owns the ST7789 + GPIO, so
# this stops it for the duration and restarts it on exit -- including Ctrl+C/kill.
#
# The #114 dev image has no systemd/sudo; it uses SysV init.d. The app is
# controlled via /etc/init.d/S02seedsigner {start|stop} (pidfile-tracked).
set -u

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
INIT=/etc/init.d/S02seedsigner

restore() {
    echo "[run] restarting seedsigner app ..."
    "$INIT" start
}
trap restore EXIT INT TERM

echo "[run] stopping seedsigner app (it owns the display/GPIO) ..."
"$INIT" stop

echo "[run] launching io_test_screen tester ..."
PYTHONPATH="$REPO/src" python3 -u "$REPO/tests/pi_io_test_screen.py"
