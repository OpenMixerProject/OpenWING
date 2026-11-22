#!/usr/bin/env python3
"""Stage the ARM hard-float EGL/GBM/Etnaviv runtime and its ELF dependencies."""
import re
import shutil
import subprocess
import sys
from pathlib import Path


def stage(ui, mesa, out):
    multiarch = Path("usr/lib/arm-linux-gnueabihf")
    search = [mesa / multiarch, Path("/") / multiarch]
    out.mkdir()
    dependencies = [ui]
    names = set()

    def install(source, relative):
        destination = out / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, destination)
        subprocess.run(["arm-linux-gnueabihf-strip", "--strip-unneeded", destination], check=True)
        dependencies.append(destination)

    def add_lib(name):
        if name not in names:
            names.add(name)
            source = next((d / name for d in search if (d / name).is_file()), None)
            if source is None:
                raise ValueError(f"missing ARM dependency: {name}")
            install(source, multiarch / name)

    # dlopen edges are not recorded as DT_NEEDED in the UI.
    for name in ("libEGL.so.1", "libGLESv2.so.2", "libgbm.so.1", "libfontconfig.so.1"):
        add_lib(name)
    # Without X11/GLX, Mesa 25 builds Etnaviv and i.MX kmsro into
    # libgallium. GBM dlopens this plugin, which pulls libgallium in.
    plugin = multiarch / "gbm/dri_gbm.so"
    install(mesa / plugin, plugin)

    while dependencies:
        elf = subprocess.check_output(["arm-linux-gnueabihf-readelf", "-hld", dependencies.pop()], text=True)
        if not re.search(r"Machine:\s+ARM$", elf, re.MULTILINE) or "hard-float ABI" not in elf:
            raise ValueError("dependency is not ARM hard-float")
        for name in re.findall(r"\(NEEDED\).*?\[(.*?)\]", elf):
            add_lib(name)
    loader = "ld-linux-armhf.so.3"
    add_lib(loader)
    (out / multiarch / loader).chmod(0o755)
    (out / "lib").mkdir()
    (out / "lib" / loader).symlink_to(f"../{multiarch}/{loader}")


if __name__ == "__main__":
    stage(*map(Path, sys.argv[1:]))
