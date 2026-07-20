# Debugging the SeedSigner app on the Pi Zero: getting output, and reading a "freeze"

Techniques for diagnosing the app on a SeedSigner OS device, and the reasoning that
separates the three failure modes that all present identically as "it froze".

## The app produces no output as normally launched

`/start.sh` ends with a bare `exec ${PYTHON} main.py` — no redirection — and the init script
launches it with `start-stop-daemon -b`, so stdout and stderr go nowhere. There is no log
file. (`/start.sh` carries a commented-out `>> /dev/kmsg` variant.)

To capture output, stop the managed app and run it by hand:

```bash
ssh root@seedsigner.local 'seedsigner stop'
ssh root@seedsigner.local 'cd /mnt/data/seedsigner/src && \
  nohup python3 -X faulthandler -u main.py > /mnt/data/seedsigner/app.log 2>&1 &'
```

`-u` is required or logging sits in a buffer and the log looks frozen even when the app is
fine. `-X faulthandler` costs nothing and enables the thread dump below.

While running this way `seedsigner status` reports **not running** — it tracks a pidfile the
manual process never writes. Use `ps | grep main.py`. Restore the managed launch with
`seedsigner start` when finished, or the app will not come back correctly after a reboot.

## Distinguishing deadlock from starvation from a wedged syscall

All three look like a frozen UI. Three cheap probes separate them, cheapest first.

**1. Is it burning CPU?** Sample `utime` from `/proc/<pid>/stat` a few seconds apart:

```bash
read -r _ _ _ _ _ _ _ _ _ _ _ _ _ ut st rest < /proc/<pid>/stat; echo "$ut $st"
```

Rising means it is running and starved (or looping); flat means genuinely blocked. Roughly
400 jiffies per 4s wall on this single-core board is ~100% of the CPU. This one probe
usually decides the question — a deadlock cannot accumulate CPU time.

**2. What is each thread blocked on?** `/proc/<pid>/task/*/wchan` and `.../status` name the
kernel function each thread waits in — `futex_wait_queue_me` (a lock), `hrtimer_nanosleep`
(a sleep), `ppoll` (an event loop). Cheap, no tooling, but says nothing about *which code*.

**3. Python-level stacks.** With `-X faulthandler` enabled, `kill -ABRT <pid>` dumps a
traceback for **every** thread into the log. This is the probe that names the actual line.
It kills the process, so use it once the freeze is confirmed. `py-spy` is not present on the
device; `gdb` and `strace` are.

For the C layer, `gdb -p <pid> -batch -ex "thread apply all bt 12"` symbolises libcamera,
libzbar, libstdc++ and this repo's `.so` well enough to tell an idle worker
(`pthread_cond_wait`) from a busy one, which is usually the question.

## A missing LVGL pump looks exactly like a camera failure

The most misleading failure mode on this platform. On the Pi, LVGL only advances when Python
calls `lvgl_pump()` — there is no native display task (that is RASPI-5). **Three separate
things hang off that pump**, so a drive loop that forgets it breaks all three at once:

1. Rendering and flush — nothing repaints the panel.
2. **Camera preview** — `camera_engine_pump_consume()` is called from `lvgl_runtime_pump`. It
   is the only place an engine frame reaches the preview sink, so without a pump the capture
   and blit workers run perfectly and their output is discarded. The camera looks broken
   while being entirely healthy.
3. Input — LVGL reads the input device in its timer handler, so no button registers and the
   screen cannot be cancelled, which makes the freeze look terminal.

The tell: high CPU, a main thread sleeping in Python rather than blocked on a lock, and
engine workers in normal idle waits. That combination means frames are being produced and
nobody is consuming them — look at the drive loop, not the camera.

## Device access quirks

- **`rsync`, not `scp`.** The image ships no `sftp-server` and modern `scp` defaults to the
  SFTP protocol, so `scp` fails with "Connection closed". `scripts/deploy-dev.sh` uses rsync
  for this reason; `scp -O` also works.
- **Stop the app before running any on-device harness that calls `ensure_lvgl_runtime()`** or
  `native_display_init()`. GPIO lines are exclusive: a second process fails with
  `GPIO_GET_LINEHANDLE_IOCTL(input) failed pin=6 errno=16` (EBUSY). This is easy to misread
  as a bug in the thing being tested.
- **Reflashing changes the SSH host key**, and `seedsigner.local` and the IP are separate
  `known_hosts` entries — fixing one leaves the other failing. Verify both present the same
  fingerprint (`ssh-keyscan -t ed25519 <host> | ssh-keygen -lf -`) before re-accepting.
- No `timeout` or `pgrep` on the device (busybox); use `ps | grep '[m]ain.py'`.

## Capturing what the camera sees, without a screen

The entropy engine latches a raw RGB565 frame retrievable via
`camera_entropy.capture()` / `get_result()`. Pull it back and convert to PNG on the host to
inspect orientation or exposure — this is how camera rotation direction was verified
(a light source tracked across 0°/90°/180° captures proved the rotation is clockwise).

Comparing two captures is more reliable than judging one: separate captures differ by scene
and exposure noise, so an exact-match test will not score zero, but the correct rotation
scores roughly half the error of a wrong one.

## See also
- `docs/knowledge/armv6-cross-compile-sdk.md` — how the `.so` under test is built.
- `docs/interface-contract.md` — the binding surface these harnesses drive.
