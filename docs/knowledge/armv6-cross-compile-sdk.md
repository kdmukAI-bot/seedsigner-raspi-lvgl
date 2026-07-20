# The ARMv6 cross-compile SDK: how the extension is built, and the traps in it

The extension is cross-compiled on x86 by the **SeedSigner OS buildroot toolchain**,
against that same buildroot's **target sysroot**. The compiler and the libraries are
the ones the device itself was built with, so ABI skew with the flashed image is
structurally impossible. No QEMU is involved.

The SDK image (`docker/Dockerfile.sdk`, produced by `docker/build_sdk_image.sh`)
carries the buildroot output; `docker/build_steps.sh` runs the build inside it.

## The SDK is one directory

`/output/host` holds the cross toolchain (`arm-Buildroot-linux-gnueabihf-gcc/g++`)
**and** buildroot's host Python 3.12.10 (with setuptools). `/output/staging` is a
**symlink into it** (`/output/host/arm-Buildroot-linux-gnueabihf/sysroot`), so that
one tree also supplies the target glibc, libstdc++, Python 3.12 headers +
sysconfigdata, libcamera, and zbar. Copying `host/` alone yields a complete SDK
(~1GB).

**The toolchain is path-locked.** Buildroot's compiler resolves its sysroot through
a baked-in absolute path, so `/output/host` must stay at exactly `/output/host`.
Relocating the tree breaks the link with no useful diagnostic; the SDK image
therefore reproduces the original paths rather than installing somewhere tidier.

## Making a host interpreter emit target artifacts

`setup.py` needs no cross-specific code. Two environment mechanisms do the work:

1. **`_PYTHON_SYSCONFIGDATA_NAME`** points the host interpreter at the *target*
   sysconfigdata, so `sysconfig` reports `SOABI=cpython-312-arm-linux-gnueabihf`
   and `EXT_SUFFIX=.cpython-312-arm-linux-gnueabihf.so`. setuptools then names and
   configures the artifact for the device — and the ABI gate in `build_steps.sh`
   needs no cross-specific branch, because the interpreter it queries already
   reports target values.
2. **`CROSS_BUILD` + `PYTHON_TARGET_INCLUDE` / `PYTHON_TARGET_LIBDIR`** (existing
   `setup.py` hooks) resolve `Python.h` and libraries inside the sysroot.

`PYTHON_TARGET_LDLIBRARY` is deliberately **not** set: a CPython extension leaves
Python symbols undefined at link and the interpreter resolves them at `dlopen`, so
`libpython` is never linked.

### The sysconfigdata must be patched, or the link fails

The target sysconfigdata reports **device-absolute** paths — `LIBDIR=/usr/lib`,
`INCLUDEPY=/usr/include/python3.12`. distutils puts `LIBDIR` on the link line as
`-L/usr/lib`, which on the x86 build host names the **host's** library directory.
Buildroot's compiler wrapper refuses it:

```
ERROR: unsafe header/library path used in cross-compilation: '-L/usr/lib'
```

That rejection is correct and must not be silenced — honouring `-L/usr/lib` would
link x86 libraries into an ARM artifact. `build_steps.sh` instead emits a patched
sysconfigdata that prefixes the sysroot onto every value **beginning with** `/usr`
(`LIBDIR` -> `/output/staging/usr/lib`). This is what `crossenv` does internally;
it is done inline to stay dependency-free and visible.

The "beginning with" rule is load-bearing. Values where `/usr` appears
**mid-string** — `MODULE_*_LDFLAGS`, already pointing at `.../sysroot/usr/lib` —
start with `-L`, so they are left alone. A blind substring replace corrupts them
into `.../sysroot/output/staging/usr/lib`.

## ccache: buildroot's copy shadows Debian's

Buildroot ships its **own** `ccache` at `/output/host/bin/ccache`, and the SDK
image puts that directory first on `PATH`, so it wins over `/usr/bin/ccache`. The
two disagree on where the cache lives:

| binary | default `cache_dir` |
|---|---|
| `/output/host/bin/ccache` (buildroot) | `/root/.buildroot-ccache` |
| `/usr/bin/ccache` (Debian) | `/root/.cache/ccache` |

The cache volume is mounted on `/root/.cache/ccache`. Without an explicit
`CCACHE_DIR` the build caches into the buildroot path — which is **not mounted**,
so `docker run --rm` discards it and every run starts cold at a 0% hit rate. The
failure is silent: builds succeed, just never faster.

The SDK image sets `ENV CCACHE_DIR=/root/.cache/ccache` and `build_steps.sh`
re-asserts it, then logs the resolved binary and its effective `cache_dir` so the
condition is visible in every build log. Working cache: **~174s cold, ~4s warm at
a 100% hit rate.**

## Test coverage in CI is zero, by construction

An ARMv6 `.so` cannot be imported on the x86 build host, so all native tests
self-skip (currently 64 of 64). The guard is `except ImportError`, not
`ModuleNotFoundError`, because a built-but-unloadable extension raises plain
`ImportError`.

Do not "fix" these to run in CI — the build host physically cannot load the
artifact. **A green CI run certifies compilation and ABI/ELF conformance only, never
that the code runs.** Functional validation happens on-device: deploy with
`scripts/deploy-dev.sh`, or copy the `.so` to a scratch dir over `rsync` and import
it there.

`rsync`, not `scp`: the device image ships no `sftp-server`, and modern `scp`
defaults to the SFTP protocol.

## Artifact gates

`verify_artifact()` in `build_steps.sh` is toolchain-agnostic and applies to both
extensions. It checks the target `EXT_SUFFIX`, an ARMv6-compatible `Tag_CPU_arch`,
and the `GLIBC`/`GLIBCXX` symbol-version ceilings from `versions.lock.toml` — a
too-new reference otherwise only surfaces as a `dlopen` failure on the device.

Two properties worth watching, both verified on real hardware:

- The main extension links **shared** `libstdc++` (it shares libcamera's C++ objects
  across the library boundary, so a static copy would split the ABI) and therefore
  emits external `GLIBCXX_*` — currently `GLIBCXX_3.4.32`, exactly what the device's
  `libstdc++.so.6.0.32` provides.
- `uUR` is pure C and must **never** inherit the camera link args. Its `DT_NEEDED`
  is only `libc` + the loader; a `libcamera` entry there would make `import uUR`
  fail everywhere off the device.

## Python packaging inside the SDK

Buildroot's host Python is built **without the `ssl` module**, so pip cannot reach
PyPI from inside the image. pytest therefore comes from Debian's `python3-pytest`
(pure Python) and is imported by buildroot's 3.12 via
`/usr/lib/python3/dist-packages` on `PYTHONPATH`.

That path is added for the **pytest command only**. Leaving it on `PYTHONPATH`
during the build lets Debian's 3.11-targeted setuptools shadow the buildroot
interpreter's own.

## Provenance

The SDK image is tagged and labelled with the SeedSigner OS release its sysroot came
from (`org.seedsigner.ss-os-commit`, `ss-os-describe`, `buildroot-commit`), and
writes the same to `/output/SS_OS_PROVENANCE`, which `build_steps.sh` echoes into
every build log. The claim "built against SeedSigner OS `<commit>`" is therefore
recorded per-artifact and independently checkable against that commit.

The image is currently produced by extracting a buildroot output tree, so
reproducibility rests on that pin rather than on an automated from-source rebuild.
See `docs/ci-rebase-seedsigner-os-plan.md` for the remaining production-grade
reproducibility work.
