# Dev device deployment (Pi Zero)

How the LVGL build gets onto the development SeedSigner (a Pi Zero on the LAN at
`pi@seedsigner.local`, SSH pre-configured) and how to run it there. This is
operational logistics for the maintainer's dev box, not a production install
guide.

> Why this lives in `seedsigner-raspi-lvgl` and not in `seedsigner`: the
> upstream `seedsigner` repo is Python-only, and all of the LVGL / native-module
> machinery exists only in the bot fork. Keeping these notes here avoids
> polluting the shared `seedsigner` repo with fork-specific dev logistics.

## The two directories on the device

The app and the native module are **separate trees** on the Pi:

| Path | What it is | Needed at runtime? |
|---|---|---|
| `/home/pi/seedsigner/src/` | The SeedSigner Python app (`main.py` + `seedsigner/` package). A plain deployed copy — **not** a git checkout. | yes (this is the cwd `main.py` runs from) |
| `/home/pi/seedsigner-raspi-lvgl/src/` | The compiled `seedsigner_lvgl_screens*.so` extension + the `lang-packs/` font packs. | yes (imported by the app) |

The app does `import seedsigner_lvgl_screens` — the compiled extension (the `.so`)
directly. There is no Python facade layer; the native module is the public import.

## How the module gets on `sys.path`: a `.pth` file

The app runs from `/home/pi/seedsigner/src`, but the LVGL module lives under
`/home/pi/seedsigner-raspi-lvgl/src`. To bridge them **permanently** (so you
don't have to `export PYTHONPATH` every session), there is a one-line `.pth` file
in user-site:

```
# /home/pi/.local/lib/python3.10/site-packages/seedsigner_lvgl.pth
/home/pi/seedsigner-raspi-lvgl/src
```

Python reads `*.pth` files at startup and appends each listed directory to
`sys.path`. So `import seedsigner_lvgl_screens` resolves
with no environment fiddling. Verify without loading the (hardware-touching)
extension:

```bash
python3 -c "import sys; print('/home/pi/seedsigner-raspi-lvgl/src' in sys.path)"
# -> True
```

### Why not `pip install` the raspi-lvgl package?

`setup.py` declares the extension as a **from-source build** — its source list is
`module.cpp` + `seedsigner.cpp` + a `glob` of the *entire* LVGL C library. So
`pip install .` / `pip install -e .` compiles all of LVGL **on whatever machine
runs it**. On a single-core ARMv6 Pi Zero with 512 MB RAM that is impractically
slow and can OOM, it requires the `sources/seedsigner-c-modules` submodule fully
checked out plus a C++17 toolchain, and it throws away the cross-compiled `.so`
that the Docker QEMU build already produced. The whole point of the cross-compile
flow is to **avoid** building on-device. The `.pth` pointed at the prebuilt
artifacts is the correct lightweight alternative.

(There is no virtualenv on this device — it uses `~/.local` user-site. SeedSigner
itself is an editable `--user` install.)

## Deploying a new build

Two independent drops from the host, then run. Neither uses `rsync --delete`
(additive only — never removes device files); `settings.json` and `.git` are
excluded so device config and the translations submodule gitlink are left alone.

```bash
# 1. LVGL module: freshly cross-compiled .so + the font packs.
#    The .so is the import itself (no facade). lang-packs/ ships beside it — the
#    native set_locale reads <src>/lang-packs/<locale>/; its source is the
#    seedsigner-lvgl-screens repo.
rsync -az --exclude='__pycache__' --exclude='*.pyc' \
  /home/kdmukai/dev/seedsigner-raspi-lvgl/src/seedsigner_lvgl_screens.cpython-310-arm-linux-gnueabihf.so \
  /home/kdmukai/dev/seedsigner-lvgl-screens/lang-packs \
  pi@seedsigner.local:/home/pi/seedsigner-raspi-lvgl/src/

# 2. App code
rsync -az --exclude='__pycache__' --exclude='*.pyc' --exclude='settings.json' --exclude='.git' \
  /home/kdmukai/dev/seedsigner/src/seedsigner/ \
  pi@seedsigner.local:/home/pi/seedsigner/src/seedsigner/
```

The `.so` filename encodes the target ABI: `cpython-310-arm-linux-gnueabihf`
(CPython 3.10, 32-bit ARM hard-float) — it must match the device's Python.

## Running

Thanks to the `.pth`, no `PYTHONPATH` export is needed:

```bash
cd /home/pi/seedsigner/src
python main.py
```

(Historically this was launched with
`export PYTHONPATH=/home/pi/seedsigner-raspi-lvgl/src` before `main.py`; the
`.pth` makes that step permanent and unnecessary.)

The device normally runs SeedSigner via a `systemd` service
(`/etc/systemd/system/seedsigner.service`, `WorkingDirectory=…/seedsigner/src`,
`ExecStart=/usr/bin/python3 main.py`), but for LVGL dev work it is left
**inactive** and `main.py` is launched manually so logs are visible on the
terminal.

## What's kept on the device (and what isn't)

Only `src/` is needed under `seedsigner-raspi-lvgl/` at runtime. The build
scaffolding (`docker/`, `scripts/`, `native/`, `sources/`, `cmake/`, `setup.py`,
etc.) is host-only and can be removed from the device — it is never imported, and
the host repo remains the source of truth. Deliberately kept as on-device
diagnostics: `tests/` (e.g. `tests/pi_input_hardware_test.py`) and
`native_spi_sweep.py`.
