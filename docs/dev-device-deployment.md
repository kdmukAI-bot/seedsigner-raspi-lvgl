# Dev device deployment (Pi Zero)

How the LVGL build gets onto the development SeedSigner and how it runs there.
This is operational logistics for the maintainer's dev box, not a production
install guide. For the short build→deploy loop see the "Local dev workflow"
section of the top-level `README.md`; this doc is the reference detail.

The dev device is a Pi Zero on the LAN running the **SeedSigner OS dev image**
(build ≥ #114: libcamera, CPython 3.12, glibc 2.40). That image is **root-only**
— there is no `pi` user — so the ssh target is `root@seedsigner.local` (or the
device IP), key-based.

> Why this lives in `seedsigner-raspi-lvgl` and not in `seedsigner`: the upstream
> `seedsigner` repo is Python-only, and all of the LVGL / native-module machinery
> exists only in the bot fork. Keeping these notes here avoids polluting the
> shared `seedsigner` repo with fork-specific dev logistics.

## Device layout: one tree, co-located `.so`

The app and the native module live in a **single** tree on the device's writable
data partition:

| Path | What it is | Needed at runtime? |
|---|---|---|
| `/mnt/data/seedsigner/src/` | The SeedSigner Python app (`main.py` + `seedsigner/` package), a plain deployed copy (not a git checkout). This is the working directory `main.py` runs from. | yes |
| `/mnt/data/seedsigner/src/seedsigner_lvgl_screens*.so` | The compiled LVGL extension, **co-located inside the app dir**. | yes (imported by the app) |
| `/mnt/data/seedsigner/src/uUR*.so` | The native BC-UR decoder, also co-located. | optional (the app falls back to the pure-Python decoder) |
| `/mnt/data/seedsigner/src/seedsigner/version.json` | Version data (name/fork/hash/timestamp). SeedSigner OS reads version info **only** from this file. See "version.json". | yes for version display |
| `/mnt/data/seedsigner/src/lang-packs/` | Per-locale language packs (fonts, `runs.bin`, endonym images, manifest, `.mo`), read CWD-relative. See "Language packs". | optional (absent ⇒ English-only) |

The app does `import seedsigner_lvgl_screens` — the compiled extension directly;
there is no Python facade layer. Because the app's working directory at launch is
`/mnt/data/seedsigner/src` (i.e. `sys.path[0]`) and the `.so` sits **in** that
directory, a bare import resolves with **no `PYTHONPATH` and no `.pth` file**.

The image's `/start.sh` prefers this dev checkout (`/mnt/data/seedsigner/src/main.py`)
over the app baked into the image at `/opt/src`, using the checkout's virtualenv
(`.venv` or `venv`) if one exists, else `/usr/bin/python3`.

### Why not `pip install` the raspi-lvgl package?

`setup.py` declares the extension as a **from-source build** — its source list is
the `native/python_bindings/*.cpp` bindings + every portable screen source + a
`glob` of the *entire* LVGL C library. So `pip install .` / `pip install -e .`
compiles all of LVGL **on whatever machine runs it**. On a single-core ARMv6 Pi
Zero with 512 MB RAM that is impractically slow and can OOM, it needs the
`sources/seedsigner-lvgl-screens` submodule fully checked out plus a C++17
toolchain, and it throws away the cross-compiled `.so` the Docker QEMU build
already produced. The cross-compile flow exists precisely to **avoid** building
on-device; co-locating the prebuilt `.so` in the app dir is the lightweight
alternative.

## Deploying a new build

`scripts/deploy-dev.sh` pushes the app code, the `.so`(s), a regenerated
`version.json`, and any language packs over SSH/rsync:

```bash
./run_build.sh                               # cross-compile the .so (when native/ or the screens submodule changed)
scripts/deploy-dev.sh 192.168.1.50           # bare host/IP -> root@ prepended
scripts/deploy-dev.sh root@seedsigner.local  # or a full ssh target
scripts/deploy-dev.sh -a /path/to/seedsigner # deploy a specific app checkout
scripts/deploy-dev.sh --help                 # all flags
```

Config precedence is **CLI args > environment > `.env`** (copy `.env.example`):

| Key | Set by | Meaning |
|---|---|---|
| `SS_DEVICE` | positional arg / `-d` / env / `.env` | ssh target of the dev Pi |
| `SS_APP_DIR` | `-a` / env / `.env` | the seedsigner **app** checkout to deploy |
| `SS_SO_PATH`, `SS_UUR_SO_PATH` | env | override the `.so`(s) (default: newest in `src/`) |
| `SS_DEVICE_APP_DIR`, `SS_CTL`, `SS_PYTHON` | env | device-side path / init-helper / python overrides |
| `SS_SKIP_VERSIONFILE=1` | env | skip version.json generation |

The script: (prep) regenerates `version.json`; (0) stops the app; (1) rsyncs the
app `src/`; (2) co-locates the `.so`(+`uUR`) in the app dir; (3) deploys language
packs if present; (4) starts the app. Drops are **additive** (never
`rsync --delete`). `settings.json` (device config) and `.git` are excluded, so
device state and any submodule gitlink are left alone.

The `.so` filename encodes the target ABI: `cpython-312-arm-linux-gnueabihf`
(CPython 3.12, 32-bit ARM hard-float) — it must match the device's Python.

> This is the **local-dev** deploy (rsync onto a *running* Pi). It is **not** how
> SeedSigner ships: SeedSigner OS bakes its own buildroot image that assembles
> these pieces itself and never runs this script.

## version.json

On SeedSigner OS (`uname -n == seedsigner-os`) the app reads version data — name,
fork, short commit hash, timestamp — **only** from `src/seedsigner/version.json`,
never from `git` at runtime. The deploy strips `.git`, so without this file the
opening splash and Settings › Version have nothing to show.

`deploy-dev.sh` regenerates it before syncing, the same way a release image does,
by running the app's own `tools/write_versionfile.py` against the app checkout's
git state with `SEEDSIGNER_OS_BUILDER=1` (which makes `timestamp` the git **commit**
time rather than a source-file mtime). Because it derives the data from `git`,
**`SS_APP_DIR` must be a git checkout** — a `git archive` export or a bare code
copy has no `.git`, and the step is skipped (the on-device version then shows as
unavailable). Set `SS_SKIP_VERSIONFILE=1` to skip it deliberately.

Verify on the device:

```bash
cat /mnt/data/seedsigner/src/seedsigner/version.json
```

## Language packs

For the runtime model (manifest-driven fonts, the picker, where translated text
comes from), see [`language-support.md`](language-support.md). This section covers
deployment only.

The deploy **points only at the app**: it rsyncs the app's already-bundled payload
`$SS_APP_DIR/src/lang-packs` straight to the device. This repo **does not know the
pack repo, does not build packs, and does not branch on signed-vs-dev** — whatever
the app bundled is what deploys. An **absent/empty `src/lang-packs` is a valid
English-only deploy**, not an error (the app degrades to the baked English floor).

Packs are produced by [`seedsigner-language-packs`](https://github.com/kdmukAI-bot/seedsigner-language-packs)
and materialized into the app from a **live pack-repo checkout** — that build step,
not this script, populates the app's payload:

```bash
# From your seedsigner-language-packs checkout (Docker or native toolchain):
scripts/build_packs.sh --out-dir "$SS_APP_DIR/src/lang-packs"
```

The app reads packs at `"lang-packs"` **relative to its working directory**
(`LOCALE_PACK_DIR` in `seedsigner/gui/lvgl_screen_runner.py`) — CWD-relative so the
same code works on ESP32 (`/sd`) and Pi. The app runs from
`/mnt/data/seedsigner/src`, so the script deploys packs **straight** to
`/mnt/data/seedsigner/src/lang-packs` — a real directory, **no symlink**, no packs
beside the `.so`. (If an older deploy left a symlink there, the script removes it
first — rsyncing onto a symlink writes *through* it into the old location instead
of replacing it.)

Verify on the device:

```bash
cd /mnt/data/seedsigner/src && python3 -c "import os; print(sorted(os.listdir('lang-packs')))"
```

On Pi, translated **text** currently loads from the app's bundled
`resources/seedsigner-translations/l10n` (`get_catalog_root()` on CPython); the
packs' `.mo` are the ESP32 text source. Packs supply the **fonts/endonyms** on Pi
regardless.

## Running and device control

The app is managed by the `seedsigner` init helper (`/etc/init.d/S02seedsigner`,
which runs `/start.sh` → `exec python3 main.py` with the working directory set to
`/mnt/data/seedsigner/src`):

```bash
ssh root@seedsigner.local 'seedsigner status'    # running? (pid)
ssh root@seedsigner.local 'seedsigner restart'   # after a manual on-device edit
ssh root@seedsigner.local 'seedsigner stop'      # stays down until start/reboot
```

The app auto-runs at boot, but via `start-stop-daemon` with **no respawn** — a
killed or crashed app stays down until `seedsigner start` or a reboot.
`deploy-dev.sh` stops the app before syncing (to free the in-use `.so`, GPIO, and
SPI) and starts it again at the end.

To see a crash on boot, run `main.py` directly and capture its output (the init
helper does not redirect logs to a file):

```bash
ssh root@seedsigner.local
seedsigner stop
cd /mnt/data/seedsigner/src && python3 main.py    # Ctrl-C to stop; a crash prints its traceback
```

## What's kept on the device (and what isn't)

Only the app `src/` tree — with the co-located `.so`(s), `version.json`, and
optional `lang-packs/` — is needed at runtime. The build scaffolding in this repo
(`docker/`, `scripts/`, `native/`, `sources/`, `cmake/`, `setup.py`, etc.) is
host-only, is never imported, and can be absent from the device; the host repo
remains the source of truth. Deliberately kept as on-device diagnostics: this
repo's `tests/` (e.g. `tests/pi_input_hardware_test.py`).
