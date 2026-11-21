#!/usr/bin/env python3
"""Stage the reviewed WING ADSP-21489 WDSP-v1 loader set, without hardware."""

import argparse
import hashlib
import shutil
from pathlib import Path


DSP_BOARD_PROFILE = "dsp_board_profile.json"
DSP_BOARD_PROFILE_SHA256 = "3b29934cdc1c704de69b6b028bc71387ab7c50cb74e811fb35ca0f64664a3e9d"
# The profile assigns DSP1=input, DSP2=bus, DSP3=fx1, DSP4=fx2. Pin each
# loader independently of the supplied checksums so a rewritten list cannot
# make a swap or an otherwise different build pass.
LOADERS = {
    "dsp1.ldr": "ef38864e9eb744a3f2a731497ea25c007fa33fad7774f97d4bd3b2dc5c44dba1",
    "dsp2.ldr": "35145d30348aa6b0b188ffa392c8b5666fa5e4e36192f0df1dadb6d3032cead0",
    "dsp3.ldr": "634ce612560507ae55fa1ce53b8a0516a11c724514d0f78ae85a5d01f07bb9bb",
    "dsp4.ldr": "f8145e8fdc1c878d79591273a0a37b8c01ab44e856eea70c61d6edaf911ccc3a",
}
DSP_IMAGES = tuple(LOADERS)
PROVENANCE = {
    "target": "ADSP-21489",
    "boot": "spi-slave",
    "format": "raw-binary",
    "dsp_runtime_abi": "wdsp-v1",
    "dsp_board_profile_sha256": DSP_BOARD_PROFILE_SHA256,
}


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def stage(dsp, output):
    output.mkdir()
    try:
        with (dsp / "SHA256SUMS").open() as stream:
            actual = stream.read().splitlines()
        sums = [f"{value}  {name}" for name, value in LOADERS.items()]
        if sorted(actual) != sorted(sums):
            raise ValueError("DSP SHA256SUMS does not name the four reviewed role-bound loaders")
        for name, expected in {**LOADERS, DSP_BOARD_PROFILE: DSP_BOARD_PROFILE_SHA256}.items():
            shutil.copyfile(dsp / name, output / name)
            if digest(output / name) != expected:
                raise ValueError(f"DSP SHA-256 mismatch: {name}")
        shutil.copyfile(dsp / "provenance.txt", output / "dsp.provenance.txt")
        lines = (output / "dsp.provenance.txt").read_text().splitlines()
        for key, value in PROVENANCE.items():
            if [line for line in lines if line.startswith(f"{key}=")] != [f"{key}={value}"]:
                raise ValueError(f"DSP provenance must declare exactly one {key}={value}")
        (output / "dsp.SHA256SUMS").write_text("\n".join(sums) + "\n")
    except Exception:
        shutil.rmtree(output)
        raise


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("dsp", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    try:
        stage(args.dsp, args.output)
    except (OSError, ValueError) as error:
        parser.exit(1, f"[error] WING DSP inputs: {error}\n")
