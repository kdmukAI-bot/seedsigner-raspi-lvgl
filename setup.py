from __future__ import annotations

from pathlib import Path
import glob
import os

from setuptools import Extension, setup

ROOT = Path(__file__).resolve().parent

DEFAULT_CMODULES = ROOT.parent / "seedsigner-micropython-builder" / "sources" / "seedsigner-c-modules"
SEEDSIGNER_C_MODULES_DIR = Path(os.environ.get("SEEDSIGNER_C_MODULES_DIR", str(DEFAULT_CMODULES))).resolve()

_lvgl_env = os.environ.get("LVGL_ROOT")
if _lvgl_env:
    LVGL_ROOT = Path(_lvgl_env).resolve()
else:
    LVGL_ROOT = (SEEDSIGNER_C_MODULES_DIR.parent / "micropython" / "ports" / "esp32" / "managed_components" / "lvgl__lvgl").resolve()

SEEDSIGNER_DIR = SEEDSIGNER_C_MODULES_DIR / "components" / "seedsigner"
NLOHMANN_JSON_INCLUDE_DIR = SEEDSIGNER_C_MODULES_DIR / "components" / "nlohmann_json" / "include"

if not (SEEDSIGNER_DIR / "seedsigner.cpp").exists():
    raise RuntimeError(f"Missing seedsigner.cpp under {SEEDSIGNER_DIR}")
if not (LVGL_ROOT / "lvgl.h").exists():
    raise RuntimeError(f"Missing lvgl.h under {LVGL_ROOT}")

lvgl_sources_all = glob.glob(str(LVGL_ROOT / "src" / "**" / "*.c"), recursive=True)

# Exclude architecture/accelerator-specific backends that can force higher CPU attrs
# and are not needed for current Pi Zero portability path.
EXCLUDE_SUBSTRINGS = [
    "/src/draw/arm2d/",
    "/src/draw/nxp/",
    "/src/draw/renesas/",
    "/src/draw/swm341_dma2d/",
    "/src/draw/stm32_dma2d/",
    "/src/draw/sdl/",
]

lvgl_sources = [
    s for s in lvgl_sources_all
    if not any(ex in s for ex in EXCLUDE_SUBSTRINGS)
]

font_sources = [
    "opensans_regular_17_4bpp.c",
    "opensans_semibold_18_4bpp.c",
    "opensans_semibold_20_4bpp.c",
    "seedsigner_icons_24_4bpp.c",
    "opensans_regular_17_4bpp_125x.c",
    "opensans_semibold_18_4bpp_125x.c",
    "opensans_semibold_20_4bpp_125x.c",
    "seedsigner_icons_24_4bpp_125x.c",
    "opensans_regular_17_4bpp_150x.c",
    "opensans_semibold_18_4bpp_150x.c",
    "opensans_semibold_20_4bpp_150x.c",
    "seedsigner_icons_24_4bpp_150x.c",
    "seedsigner_icons_36_4bpp.c",
    "seedsigner_icons_36_4bpp_125x.c",
    "seedsigner_icons_36_4bpp_150x.c",
]

font_paths = [str(SEEDSIGNER_DIR / "fonts" / f) for f in font_sources]

stagef_cross = os.environ.get("STAGEF_CROSS", "0") == "1"
stagef_armv6_force = os.environ.get("STAGEF_ARMV6_FORCE", "0") == "1"
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
if stagef_armv6_force:
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
if stagef_cross and python_target_include:
    include_dirs.insert(0, python_target_include)
if stagef_cross and python_target_libdir:
    extra_link_args.extend([f"-L{python_target_libdir}"])
if stagef_cross and python_target_ldlibrary:
    if python_target_ldlibrary.startswith("lib") and python_target_ldlibrary.endswith((".a", ".so")):
        libname = python_target_ldlibrary[3:].split(".")[0]
        extra_link_args.extend([f"-l{libname}"])

ext_modules = [
    Extension(
        "seedsigner_lvgl_native",
        sources=[
            "native/python_bindings/module.cpp",
            str(SEEDSIGNER_DIR / "components.cpp"),
            str(SEEDSIGNER_DIR / "seedsigner.cpp"),
            *font_paths,
            *lvgl_sources,
        ],
        include_dirs=include_dirs,
        define_macros=[("LV_CONF_SKIP", "1")],
        extra_compile_args=extra_compile_args,
        extra_link_args=extra_link_args,
        language="c++",
    )
]

setup(ext_modules=ext_modules)
