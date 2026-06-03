#!/usr/bin/env python3
"""Shim: delegate to private implementation in lib/private/tools."""
import importlib.util
import sys
from pathlib import Path


def _load_and_run():
    root = Path(__file__).resolve().parents[0]
    private = root / ".." / "lib" / "private" / "tools" / "build_uboot_wingfw.py"
    private = private.resolve()
    spec = importlib.util.spec_from_file_location("_build_uboot_wingfw", str(private))
    if spec is None or spec.loader is None:
        raise SystemExit("private implementation not found")
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    if hasattr(mod, "main"):
        mod.main()
    else:
        raise SystemExit("private implementation missing entrypoint")


if __name__ == "__main__":
    _load_and_run()
