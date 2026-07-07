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
| `/home/pi/seedsigner-raspi-lvgl/src/` | The compiled `seedsigner_lvgl_screens*.so` extension. | yes (imported by the app) |
| `/home/pi/seedsigner/src/lang-packs/` | The per-locale language packs (fonts, `runs.bin`, endonym images, manifest, `.mo`). Deployed directly here — where the app reads them CWD-relative. See "Language packs". | yes |

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

`scripts/deploy-dev.sh` pushes all three pieces — the `.so`, the app code, and the
language packs — over SSH/rsync, driven entirely by env config (no hardcoded paths).
Copy `.env.example` to `.env`, set your paths, then:

```bash
./run_build.sh            # cross-compile the .so (only when native/ or the screens submodule changed)
scripts/deploy-dev.sh     # push .so + app + packs to $SS_DEVICE
```

`.env` keys (see `.env.example`): `SS_DEVICE` (target host) and `SS_APP_DIR` (app
checkout root) are required; `SS_SO_PATH` and the device-side path overrides are
optional. The `.so` and app-code drops are additive (never `rsync --delete`);
`settings.json` and `.git` are excluded so device config and the translations
submodule gitlink are left alone.

The `.so` filename encodes the target ABI: `cpython-310-arm-linux-gnueabihf`
(CPython 3.10, 32-bit ARM hard-float) — it must match the device's Python.

> This is the **local-dev** deploy (rsync onto a *running* Pi). It is **not** how
> SeedSigner ships: SeedSigner OS bakes its own buildroot image that assembles these
> pieces itself and never runs this script.

## Language packs

For the runtime model (manifest-driven fonts, the picker, where translated text comes from),
see [`language-support.md`](language-support.md). This section covers deployment only.

The deploy **points only at the app**: it rsyncs the app's already-bundled payload
`$SS_APP_DIR/src/lang-packs` straight to the device. This repo **does not know the pack
repo, does not build packs, and does not branch on signed-vs-dev** — whatever the app
bundled is what deploys. An **absent/empty `src/lang-packs` is a valid English-only
deploy**, not an error (the app degrades to the baked English floor).

Packs are produced by [`seedsigner-language-packs`](https://github.com/kdmukAI-bot/seedsigner-language-packs)
and materialized into the app from a **live pack-repo checkout** — that build step, not this
script, populates the app's payload:

```bash
# From your seedsigner-language-packs checkout (Docker or native toolchain):
scripts/build_packs.sh --out-dir "$SS_APP_DIR/src/lang-packs"
```

The app reads packs at `"lang-packs"` **relative to its working directory**
(`LOCALE_PACK_DIR` in `seedsigner/gui/lvgl_screen_runner.py`) — CWD-relative so the same
code works on ESP32 (`/sd`) and Pi. The app runs from `/home/pi/seedsigner/src`, so the
script deploys packs **straight** to `/home/pi/seedsigner/src/lang-packs` — a real
directory, **no symlink**, no packs beside the `.so`. (If an older deploy left a symlink
there, the script removes it first — rsyncing onto a symlink writes *through* it into the
old location instead of replacing it.)

Verify on the device:

```bash
cd /home/pi/seedsigner/src && python3 -c "import os; print(sorted(os.listdir('lang-packs')))"
```

On Pi, translated **text** currently loads from the app's bundled
`resources/seedsigner-translations/l10n` (`get_catalog_root()` on CPython); the packs'
`.mo` are the ESP32 text source. Packs supply the **fonts/endonyms** on Pi regardless.

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
