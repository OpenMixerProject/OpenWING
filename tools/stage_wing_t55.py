#!/usr/bin/env python3
"""Stage trusted WING Compact T55 build outputs, without touching hardware."""

import argparse
import hashlib
import json
import re
import shutil
import subprocess
from pathlib import Path

from stage_wing_dsp import DSP_BOARD_PROFILE, DSP_BOARD_PROFILE_SHA256, digest, stage as stage_dsp

FPGA_IMAGE = "wing-fpga.hex.bin"
SHA256 = re.compile(r"[0-9a-f]{64}")
CSR_FILES = ("csr.csv", "csr.json", "software/include/generated/csr.h")
DSP_RUNTIME = {"device": "/dev/spidev1.0", "chip_select": 0, "abi": 1}
SPIBONE_UAPI_SHA256 = "ce3e41a488b0cf88b53303adc6f1d0bbd1facb4481f796f1bcba4aa462da6fc5"


def copy_input(source, destination):
    if not source.is_file() or not source.stat().st_size:
        raise ValueError(f"missing or empty input: {source}")
    shutil.copyfile(source, destination)


def verify_sums(path, directory, expected_names):
    seen = set()
    for line in path.read_text().splitlines():
        match = re.fullmatch(r"([0-9a-f]{64}) [ *]([^/]+)", line)
        if not match or match[2] not in expected_names or match[2] in seen:
            raise ValueError(f"unexpected checksum entry in {path}: {line!r}")
        if digest(directory / match[2]) != match[1]:
            raise ValueError(f"SHA-256 mismatch: {directory / match[2]}")
        seen.add(match[2])
    if seen != set(expected_names):
        raise ValueError(f"missing checksum entries in {path}: {set(expected_names) - seen}")


def unique_keys(pairs):
    obj = {}
    for key, value in pairs:
        if key in obj:
            raise ValueError(f"duplicate FPGA manifest key: {key}")
        obj[key] = value
    return obj


def stage(fpga, dsp, omc, output, spibone):
    stage_dsp(dsp, output)  # Requires a fresh directory and pins each DSP role.
    try:
        for name in (FPGA_IMAGE, "manifest.json"):
            copy_input(fpga / name, output / name)
        manifest = json.loads((output / "manifest.json").read_text(), object_pairs_hook=unique_keys)
        target = "wing-compact-t55f484-fork"
        if not isinstance(manifest, dict) or (
            type(manifest.get("schema_version")) is not int
            or manifest["schema_version"] != 2
            or manifest.get("target") != target
            or manifest.get("device") != "T55F484"
            or type(manifest.get("abi_version")) is not int
            or manifest["abi_version"] != 0x20000
        ):
            raise ValueError(f"expected a {target} schema 2 / ABI 2 manifest")
        for field in ("build_id", "feature_flags"):
            if type(manifest.get(field)) is not int or not 0 <= manifest[field] <= 0xFFFFFFFF:
                raise ValueError(f"manifest {field} must be a u32")
        bitstream = manifest.get("bitstream")
        if not isinstance(bitstream, dict) or bitstream.get("file") != FPGA_IMAGE or (
            bitstream.get("format") != "efinity-passive-spi-1x-hex-bin"
        ):
            raise ValueError(f"manifest must name {FPGA_IMAGE} in raw Efinity passive SPI 1x format")
        sha256 = bitstream.get("sha256")
        if not isinstance(sha256, str) or not SHA256.fullmatch(sha256) or digest(output / FPGA_IMAGE) != sha256:
            raise ValueError("manifest SHA-256 does not match the staged FPGA image")

        copy_input(omc / "omc", output / "omc")
        for name in ("SHA256SUMS", "provenance.txt"):
            copy_input(omc / name, output / f"omc.{name}")
        verify_sums(output / "omc.SHA256SUMS", output, ("omc",))
        # Older paired manifests advertise SS0 passthrough in capabilities but
        # did not describe its Linux endpoint. Bind it into the packaged manifest.
        if not manifest["feature_flags"] & 2 or manifest.get("dsp_runtime", DSP_RUNTIME) != DSP_RUNTIME:
            raise ValueError("T55 FPGA manifest has no compatible WDSP v1 transport")
        if "dsp_runtime" not in manifest:
            manifest["dsp_runtime"] = DSP_RUNTIME
            (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")

        stage_spibone(fpga, manifest, spibone, output)
        entries = sorted(path for path in output.rglob("*") if path.is_file())
        (output / "SHA256SUMS").write_text("".join(f"{digest(path)}  {path.relative_to(output)}\n" for path in entries))
    except Exception:
        shutil.rmtree(output)
        raise


def stage_spibone(fpga, manifest, source, output):
    profile = manifest.get("dsp_board_profile_sha256")
    if profile != DSP_BOARD_PROFILE_SHA256:
        raise ValueError("FPGA manifest does not bind the reviewed stock-direction SHARC board profile")
    csr_sha256 = manifest.get("csr_sha256")
    if not isinstance(csr_sha256, dict) or set(csr_sha256) != set(CSR_FILES):
        raise ValueError("SPIBone FPGA manifest must bind all three generated CSR files")
    for name in CSR_FILES:
        path = output / name
        path.parent.mkdir(parents=True, exist_ok=True)
        copy_input(fpga / name, path)
        if digest(path) != csr_sha256[name]:
            raise ValueError(f"generated CSR SHA-256 mismatch: {name}")
    if digest(fpga / DSP_BOARD_PROFILE) != profile:
        raise ValueError("FPGA and DSP board profiles do not match")

    try:
        git = ["git", "-C", str(source)]
        subprocess.run([*git, "merge-base", "--is-ancestor", "c56ef3ecdd7b2c2c0b7844788cd40c45964d87c4", "HEAD"], check=True)
        revision = subprocess.check_output([*git, "rev-parse", "HEAD"], text=True).strip()
    except subprocess.CalledProcessError as error:
        raise ValueError("SPIBone source checkout must descend from OpenAudioFPGA c56ef3e") from error
    sources = {name: subprocess.check_output([*git, "show", f"{revision}:driver/spibone/{name}"])
               for name in ("spibone.c", "spibone.h", "spibone_uapi.h")}
    if hashlib.sha256(sources["spibone_uapi.h"]).hexdigest() != SPIBONE_UAPI_SHA256:
        raise ValueError("SPIBone checkout is missing the reviewed WING opt-in UAPI")
    lines = (output / "omc.provenance.txt").read_text().splitlines()
    for key, value in (("transport", "spibone"), ("spibone_uapi_sha256", SPIBONE_UAPI_SHA256),
                       ("dsp_board_profile_sha256", profile)):
        if [line for line in lines if line.startswith(f"{key}=")] != [f"{key}={value}"]:
            raise ValueError(f"OMC provenance must declare exactly one {key}={value}")
    driver = output.parent / "spibone"
    driver.mkdir()
    try:
        for name, data in sources.items():
            (driver / name).write_bytes(data)
        provenance = [f"source_commit={revision}", *(f"{digest(driver / name)}  {name}" for name in sources)]
        (output / "spibone.provenance.txt").write_text("\n".join(provenance) + "\n")
    except OSError:
        shutil.rmtree(driver)
        raise


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("fpga", type=Path)
    parser.add_argument("dsp", type=Path)
    parser.add_argument("omc", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--spibone", type=Path, required=True, help="WING OpenAudioFPGA checkout for the kernel driver")
    args = parser.parse_args()
    try:
        stage(args.fpga, args.dsp, args.omc, args.output, args.spibone)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"[error] T55 inputs: {error}\n")


if __name__ == "__main__":
    main()
