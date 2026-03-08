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

lvgl_sources = glob.glob(str(LVGL_ROOT / "src" / "**" / "*.c"), recursive=True)

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
        include_dirs=[
            str(LVGL_ROOT),
            str(SEEDSIGNER_DIR),
            str(NLOHMANN_JSON_INCLUDE_DIR),
        ],
        define_macros=[("LV_CONF_SKIP", "1")],
        extra_compile_args=["-std=c++17"],
        language="c++",
    )
]

setup(ext_modules=ext_modules)
