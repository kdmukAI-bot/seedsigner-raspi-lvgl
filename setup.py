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

if not (SEEDSIGNER_DIR / "screens").is_dir():
    raise RuntimeError(
        f"Missing screens/ under {SEEDSIGNER_DIR} -- submodule not checked out, "
        f"or pinned before the seedsigner.cpp split reorg."
    )
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
    "seedsigner_icons_26_4bpp.c",  # top_nav contextual title icon (gui_constants 240 profile)
    "seedsigner_icons_36_4bpp.c",
    "seedsigner_icons_48_4bpp.c",
    "inconsolata_semibold_24_4bpp.c",  # keyboard/text-entry = 24px
    # Seed-word candidate font for seed_mnemonic_entry_screen = 22px
    # (button_font_size + 4). Referenced by the 240 profile in gui_constants.cpp;
    # without it the .so fails to dlopen (undefined inconsolata_semibold_22_4bpp).
    "inconsolata_semibold_22_4bpp.c",
]

font_paths = [str(SEEDSIGNER_DIR / "fonts" / f) for f in font_sources]

# --- Display-height profile -------------------------------------------------
# The native screens bake one logo asset per display-height profile (100x=240px,
# 133x=320px, 200x=480px) and pick the variant at runtime by px_multiplier.
# gui_constants.{h,cpp} #ifdef-gate the LV_IMAGE_DECLAREs and the get_logo()
# selectors on SUPPORT_DISPLAY_HEIGHT_<N>, so a build references only its own
# profile's logo symbols. The image .c files themselves are NOT guarded — each
# unconditionally defines its symbol — so the source list is what gates them.
# Unlike the firmware (CMake lists every variant and drops the unreferenced ones
# at link via --gc-sections), this extension does not link with --gc-sections, so
# it must compile in ONLY the active profile's image .c files. Derive both the
# SUPPORT_DISPLAY_HEIGHT_<N> macro and the image source set from one height value.
# The Pi panel is 240px tall (240x240; a 320x240 width switch keeps height 240).
DISPLAY_HEIGHT = int(os.environ.get("DISPLAY_HEIGHT", "240"))

# height -> image-asset filename suffix (matches the px_multiplier: 100/133/200).
_LOGO_SUFFIX_BY_HEIGHT = {240: "", 320: "_133x", 480: "_200x"}
if DISPLAY_HEIGHT not in _LOGO_SUFFIX_BY_HEIGHT:
    raise RuntimeError(
        f"Unsupported DISPLAY_HEIGHT={DISPLAY_HEIGHT}; expected one of "
        f"{sorted(_LOGO_SUFFIX_BY_HEIGHT)}"
    )
_logo_suffix = _LOGO_SUFFIX_BY_HEIGHT[DISPLAY_HEIGHT]

# Base SeedSigner wordmark (screensaver + splash) + HRF partner logo (splash) +
# Bitcoin logo (loading_screen spinner), for the active height only. Each is
# #ifdef-gated in gui_constants.cpp on SUPPORT_DISPLAY_HEIGHT_<N>, so only the
# active profile's .c must be compiled in (missing symbols fail at dlopen).
logo_sources = [
    str(SEEDSIGNER_DIR / "images" / f"seedsigner_logo_img{_logo_suffix}.c"),
    str(SEEDSIGNER_DIR / "images" / f"hrf_logo_img{_logo_suffix}.c"),
    str(SEEDSIGNER_DIR / "images" / f"btc_logo_img{_logo_suffix}.c"),
]

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

# Portable seedsigner component sources. The screens repo replaced its monolithic
# seedsigner.cpp with one file per screen under screens/ plus shared helpers
# (qr_core / screen_helpers / screen_scaffold). Every top-level .cpp/.c and every
# screens/*_screen.cpp under components/seedsigner/ is portable and compiled in --
# mirroring components/seedsigner/screen_sources.cmake (screens glob) so new screens
# are picked up mechanically, with no ESP32-only translation units in this dir to
# exclude. Camera *overlay* / *overlay_screen sources are UI-only (no camera driver)
# and link in even though the camera screens are not bound to Python. Sorted for a
# deterministic link order.
seedsigner_sources = sorted(
    glob.glob(str(SEEDSIGNER_DIR / "*.cpp"))
    + glob.glob(str(SEEDSIGNER_DIR / "*.c"))
    + glob.glob(str(SEEDSIGNER_DIR / "screens" / "*_screen.cpp"))
)

ext_modules = [
    Extension(
        "seedsigner_lvgl_screens",
        sources=[
            "native/python_bindings/module.cpp",
            # Every portable seedsigner component + per-screen source (see the
            # seedsigner_sources glob above). Replaces the former hand-maintained
            # list, which pivoted on the now-deleted seedsigner.cpp monolith.
            *seedsigner_sources,
            *logo_sources,
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
            # Native QR rendering (qr_display_screen). seedsigner.cpp calls the
            # LVGL-bundled Nayuki qrcodegen directly (not the lv_qrcode widget) to
            # control ECC/mode/quiet-zone; both qrcodegen.c and lv_qrcode.c are
            # already picked up by the LVGL src glob, so only the flag is needed.
            # Without it the screen compiles to a blank stub (#if !LV_USE_QRCODE).
            ("LV_USE_QRCODE", "1"),
            (f"SUPPORT_DISPLAY_HEIGHT_{DISPLAY_HEIGHT}", "1"),
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
