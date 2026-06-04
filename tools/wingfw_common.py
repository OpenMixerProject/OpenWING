"""Public shim for wingfw crypto/parsing helpers.

The real implementation and any secret constants live in
`lib/private/tools/wingfw_common.py`. This module only dynamically loads the
private implementation at runtime and re-exports its symbols so existing
imports continue to work without exposing secrets in the public tree.
"""
from importlib import util
from pathlib import Path
import sys


def _load_private_module():
    root = Path(__file__).resolve().parents[0]
    private_path = root / ".." / "lib" / "private" / "tools" / "wingfw_common.py"
    private_path = private_path.resolve()
    spec = util.spec_from_file_location("_wingfw_common_private", str(private_path))
    if spec is None or spec.loader is None:
        raise ImportError("private wingfw implementation not found")
    mod = util.module_from_spec(spec)
    sys.path.insert(0, str(private_path.parent))
    spec.loader.exec_module(mod)
    return mod


_impl = None


def __getattr__(name):
    global _impl
    if _impl is None:
        _impl = _load_private_module()
    return getattr(_impl, name)
