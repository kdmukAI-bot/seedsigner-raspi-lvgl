#!/usr/bin/env python3
"""Capture Python ABI/runtime metadata for Stage F planning.

Run on target device and redirect output to JSON file.
Example:
  python scripts/capture_python_abi.py > docs/abi/dev-pi-abi.json
"""

from __future__ import annotations

import json
import platform
import subprocess
import sys
import sysconfig


def cmd_output(cmd: list[str]) -> str:
    try:
        return subprocess.check_output(cmd, stderr=subprocess.STDOUT, text=True).strip()
    except Exception as e:
        return f"ERROR: {e}"


def main() -> int:
    data = {
        "python_version": sys.version,
        "implementation": platform.python_implementation(),
        "executable": sys.executable,
        "platform_machine": platform.machine(),
        "platform": platform.platform(),
        "maxsize": sys.maxsize,
        "byteorder": sys.byteorder,
        "soabi": sysconfig.get_config_var("SOABI"),
        "ext_suffix": sysconfig.get_config_var("EXT_SUFFIX"),
        "include_py": sysconfig.get_config_var("INCLUDEPY"),
        "platinclude": sysconfig.get_config_var("PLATINCLUDE"),
        "libdir": sysconfig.get_config_var("LIBDIR"),
        "ldlibrary": sysconfig.get_config_var("LDLIBRARY"),
        "multiarch": sysconfig.get_config_var("MULTIARCH"),
        "config_args": sysconfig.get_config_var("CONFIG_ARGS"),
        "ldd_version": cmd_output(["ldd", "--version"]),
        "uname": cmd_output(["uname", "-a"]),
    }

    print(json.dumps(data, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
