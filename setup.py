from setuptools import Extension, setup

ext_modules = [
    Extension(
        "seedsigner_lvgl_native",
        sources=["native/python_bindings/module.c"],
        extra_compile_args=["-std=c11"],
    )
]

setup(ext_modules=ext_modules)
