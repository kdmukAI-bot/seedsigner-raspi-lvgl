from __future__ import annotations

from pathlib import Path
import glob
import os

from setuptools import Extension, setup
from setuptools.command.build_ext import build_ext as _build_ext

ROOT = Path(__file__).resolve().parent

DEFAULT_CMODULES = ROOT / "sources" / "seedsigner-lvgl-screens"
SEEDSIGNER_LVGL_SCREENS_DIR = Path(os.environ.get("SEEDSIGNER_LVGL_SCREENS_DIR", str(DEFAULT_CMODULES))).resolve()

_lvgl_env = os.environ.get("LVGL_ROOT")
if _lvgl_env:
    LVGL_ROOT = Path(_lvgl_env).resolve()
else:
    LVGL_ROOT = (SEEDSIGNER_LVGL_SCREENS_DIR / "third_party" / "lvgl").resolve()

SEEDSIGNER_DIR = SEEDSIGNER_LVGL_SCREENS_DIR / "components" / "seedsigner"
NLOHMANN_JSON_INCLUDE_DIR = SEEDSIGNER_LVGL_SCREENS_DIR / "components" / "nlohmann_json" / "include"

if not (SEEDSIGNER_DIR / "seedsigner.cpp").exists():
    raise RuntimeError(f"Missing seedsigner.cpp under {SEEDSIGNER_DIR}")
if not (LVGL_ROOT / "lvgl.h").exists():
    raise RuntimeError(f"Missing lvgl.h under {LVGL_ROOT}")

lvgl_sources_all = glob.glob(str(LVGL_ROOT / "src" / "**" / "*.c"), recursive=True)

# Exclude architecture/accelerator-specific backends that can force higher CPU attrs
# and are not needed for current Pi Zero portability path.
EXCLUDE_SUBSTRINGS = [
    # Hardware-specific draw backends (not needed for Pi Zero SW rendering)
    "/src/draw/dma2d/",
    "/src/draw/espressif/",
    "/src/draw/eve/",
    "/src/draw/nanovg/",
    "/src/draw/nema_gfx/",
    "/src/draw/nxp/",
    "/src/draw/opengles/",
    "/src/draw/renesas/",
    "/src/draw/sdl/",
    "/src/draw/vg_lite/",
    # Architecture-specific SW blend backends (ARMv7+, RISC-V, etc.)
    "/src/draw/sw/blend/neon/",
    "/src/draw/sw/blend/helium/",
    "/src/draw/sw/blend/arm2d/",
    "/src/draw/sw/blend/riscv_v/",
    "/src/draw/sw/arm2d/",
]

lvgl_sources = [
    s for s in lvgl_sources_all
    if not any(ex in s for ex in EXCLUDE_SUBSTRINGS)
]

font_sources = [
    # Baked Western-Latin floor. On feat/font-i18n the five translated text roles
    # (title/button/body/...) are no longer pre-rasterized bitmaps; they are
    # rasterized at runtime via tiny_ttf from one compiled-in OpenSans Western TTF
    # subset (Regular + SemiBold), all sizes from the same blob. See
    # gui_constants.cpp (the post-lv_init() per-role tiny_ttf registration loop).
    "opensans_western_regular.c",
    "opensans_western_semibold.c",
    # Icons (PUA) + fixed-width Inconsolata keyboard/text-entry font stay bitmap
    # (240px profile — matches the host's SUPPORT_DISPLAY_HEIGHT_240 build).
    "seedsigner_icons_24_4bpp.c",
    "seedsigner_icons_36_4bpp.c",
    "seedsigner_icons_48_4bpp.c",
    "inconsolata_semibold_24_4bpp.c",
]

font_paths = [str(SEEDSIGNER_DIR / "fonts" / f) for f in font_sources]

cross_build = os.environ.get("CROSS_BUILD", "0") == "1"
armv6_force = os.environ.get("ARMV6_FORCE", "0") == "1"
python_target_include = os.environ.get("PYTHON_TARGET_INCLUDE", "").strip()
python_target_libdir = os.environ.get("PYTHON_TARGET_LIBDIR", "").strip()
python_target_ldlibrary = os.environ.get("PYTHON_TARGET_LDLIBRARY", "").strip()

include_dirs = [
    str(LVGL_ROOT),
    str(SEEDSIGNER_DIR),
    str(NLOHMANN_JSON_INCLUDE_DIR),
]

extra_link_args: list[str] = []
extra_compile_args: list[str] = ["-std=c++17"]
if armv6_force:
    extra_compile_args.extend([
        "-march=armv6zk",
        "-mtune=arm1176jzf-s",
        "-marm",
        "-mfpu=vfp",
        "-mfloat-abi=hard",
    ])
    # Avoid runtime dependency on host libstdc++/libgcc symbol versions.
    # This is critical for Pi Zero targets that often have older system toolchains.
    extra_link_args.extend([
        "-static-libstdc++",
        "-static-libgcc",
    ])
if cross_build and python_target_include:
    include_dirs.insert(0, python_target_include)
if cross_build and python_target_libdir:
    extra_link_args.extend([f"-L{python_target_libdir}"])
if cross_build and python_target_ldlibrary:
    if python_target_ldlibrary.startswith("lib") and python_target_ldlibrary.endswith((".a", ".so")):
        libname = python_target_ldlibrary[3:].split(".")[0]
        extra_link_args.extend([f"-l{libname}"])

ext_modules = [
    Extension(
        "seedsigner_lvgl_native",
        sources=[
            "native/python_bindings/module.cpp",
            str(SEEDSIGNER_DIR / "components.cpp"),
            str(SEEDSIGNER_DIR / "gui_constants.cpp"),
            str(SEEDSIGNER_DIR / "input_profile.cpp"),
            str(SEEDSIGNER_DIR / "navigation.cpp"),
            str(SEEDSIGNER_DIR / "seedsigner.cpp"),
            # i18n / font-pack layer (shared, host-agnostic). The host plugs into
            # ss_load_locale() via a filesystem pack-provider (see module.cpp).
            str(SEEDSIGNER_DIR / "locale_fonts.cpp"),     # canonical locale->font manifest
            str(SEEDSIGNER_DIR / "font_registry.cpp"),    # tiny_ttf role-font registration
            str(SEEDSIGNER_DIR / "locale_loader.cpp"),    # ss_load_locale orchestration
            str(SEEDSIGNER_DIR / "glyph_runs.cpp"),       # complex-script pre-shaped runs
            str(SEEDSIGNER_DIR / "stb_glyph_metrics.c"),  # glyph boxes for run rendering
            str(SEEDSIGNER_DIR / "images" / "seedsigner_logo_img.c"),
            *font_paths,
            *lvgl_sources,
        ],
        include_dirs=include_dirs,
        define_macros=[
            ("LV_CONF_SKIP", "1"),
            ("LV_USE_DRAW_SW_ASM", "LV_DRAW_SW_ASM_NONE"),
            # Use glibc malloc, not LVGL's 64KB builtin fixed pool (the LV_CONF_SKIP
            # default). The shared font code enables the tiny_ttf glyph cache
            # (SEEDSIGNER_TTF_CACHE_SIZE), which retains rasterized bitmaps; a 64KB
            # pool OOMs and LVGL's default assert handler spins. The Pi Zero has
            # 512MB, but LVGL only sees it through CLIB malloc. See
            # docs/knowledge/tiny-ttf-cache-needs-clib-malloc.md.
            ("LV_USE_STDLIB_MALLOC", "LV_STDLIB_CLIB"),
            # Runtime font loading: per-locale subset TTFs (and the baked OpenSans
            # Western floor) are rasterized from in-memory buffers via
            # lv_tiny_ttf_create_data_ex() — the registration seam the loader drives.
            ("LV_USE_TINY_TTF", "1"),
            # Farsi/Arabic i18n: BIDI = right-to-left reordering of mixed RTL/LTR
            # text; ARABIC_PERSIAN_CHARS = cursive base-letter -> presentation-form
            # shaping. Both must match the font packs (subset to the shaper's forms).
            ("LV_USE_BIDI", "1"),
            ("LV_USE_ARABIC_PERSIAN_CHARS", "1"),
            ("SUPPORT_DISPLAY_HEIGHT_240", "1"),
            *(
                [("LV_USE_SYSMON", "1"), ("LV_USE_PERF_MONITOR", "1")]
                if os.environ.get("LVGL_PERF_MONITOR", "0") == "1"
                else []
            ),
        ],
        extra_compile_args=extra_compile_args,
        extra_link_args=extra_link_args,
        language="c++",
    )
]

CXX_ONLY_FLAGS = {"-std=c++17"}


class build_ext(_build_ext):
    """Strip C++-only flags when compiling C source files."""

    def build_extensions(self):
        _original_compile = self.compiler._compile

        def _compile(obj, src, ext, cc_args, extra_postargs, pp_opts):
            if src.endswith(".c"):
                extra_postargs = [a for a in extra_postargs if a not in CXX_ONLY_FLAGS]
            return _original_compile(obj, src, ext, cc_args, extra_postargs, pp_opts)

        self.compiler._compile = _compile
        super().build_extensions()


setup(ext_modules=ext_modules, cmdclass={"build_ext": build_ext})
