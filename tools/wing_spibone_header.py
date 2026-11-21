#!/usr/bin/env python3
"""Generate the manual loader's identity and payload pins from the built T55 CSR map."""

import csv
import json
import sys
from collections import Counter
from pathlib import Path

from stage_wing_dsp import DSP_BOARD_PROFILE, DSP_IMAGES, digest
from stage_wing_t55 import FPGA_IMAGE


def generate(bundle):
    manifest = json.loads((bundle / "manifest.json").read_text())
    if manifest.get("benchmark_only") is not False or manifest["num_aes50_ports"] != 1 or any(
        {entry["aes50"] for entry in manifest["channels"][direction] if "aes50" in entry} != {"B"}
        for direction in ("sources", "destinations")
    ):
        raise ValueError("T55 SPIBone V1 requires the non-benchmark image with only physical AES50 port B (IC10)")
    profile = json.loads((bundle / DSP_BOARD_PROFILE).read_text())
    slots = Counter({core["id"]: core["slots_used"] for core in profile["dsp"]})
    for direction in ("sources", "destinations"):
        channels = manifest["channels"][direction]
        if Counter(entry["dsp"] for entry in channels if "dsp" in entry) != slots:
            raise ValueError(f"T55 routing {direction} exceed or omit the pinned SHARC profile slots")
    if profile["bclk_hz"] > 28_125_000:
        raise ValueError("SPORT BCLK exceeds the SHARC slave-TX maximum at 450 MHz CCLK; need a revised paired profile")
    if manifest["audio"] != {name: value for name, value in profile.items() if name != "dsp"}:
        raise ValueError("FPGA manifest audio metadata disagrees with the paired SHARC profile")
    transport = manifest["transport"]
    expected_transport = {
        "device": "/dev/spibone0", "compatible": "openmixer,wing-spibone", "chip_select": 1,
        "mode": 0, "max_hz": 2000000, "protocol": "litex-spibone-burst", "address_unit": "byte",
    }
    if any(transport[key] != value for key, value in expected_transport.items()):
        raise ValueError("T55 SPIBone transport disagrees with the WING DTS")
    with (bundle / "csr.csv").open(newline="") as stream:
        rows = {row[1]: row[2:] for row in csv.reader(stream)
                if row and row[0] in ("csr_register", "memory_region", "constant")}
    fabric_hz = manifest["fabric_clock_hz"]
    if (type(fabric_hz) is not int or fabric_hz <= 0 or
        fabric_hz != int(rows["config_clock_frequency"][0], 0) or manifest["rmii_clock_hz"] != 50_000_000):
        raise ValueError("T55 clock requirements disagree with the generated fabric clock or 50 MHz RMII")
    routing = manifest["routing"]
    if routing["base"] != 0x80000000 or int(rows["routing"][0], 0) != routing["base"]:
        raise ValueError("expected the generated WING routing window at 0x80000000")

    # The kernel must bootstrap at these ABI addresses; validate both the manifest
    # and the generated map. The loader uses the validated generated values below.
    names = "magic abi_version build_id capabilities status dsp_reset_release dsp_audio_enable dsp_audio_valid".split()
    regs = []
    for index, name in enumerate(names):
        key = f"wing_{name}"
        matches = [reg for reg in manifest["registers"] if reg["name"] == key]
        addr, count, access = rows[key]
        offset, mode = int(addr, 0), "RO" if index < 5 else "RW"
        if len(matches) != 1 or count != "1" or access.upper() != mode or offset != 0x1800 + 4*index or any(
            matches[0][field] != value for field, value in {"offset": offset, "width": 32, "access": mode}.items()
        ):
            raise ValueError(f"unexpected T55 register: {key}")
        regs.append(matches[0])

    identity = (0x57494E47, 0x20000, manifest["build_id"], 0x0F)
    if (tuple(reg["value"] for reg in regs[:4]) != identity or
        (manifest["magic"], manifest["abi_version"], manifest["feature_flags"]) != (identity[0], identity[1], identity[3]) or
        regs[4]["bits"]["ready"] != 0 or
        [(reg["reset"], reg["write_mask"]) for reg in regs[5:]] != [(0, 11), (0, 15), (0, 15)]):
        raise ValueError("incompatible T55 identity, ready bit or DSP reset controls")
    if any((bundle / name).stat().st_size >= 65536 for name in DSP_IMAGES):
        raise ValueError("DSP loader plus target byte must fit one 65536-byte SPI message")

    lines = [f"#define FABRIC_BUILD_ID 0x{manifest['build_id']:08x}u",
             f"#define FABRIC_READY (1u << {regs[4]['bits']['ready']})"]
    for name, reg in zip(("FABRIC_STATUS", "RESET_RELEASE", "AUDIO_ENABLE", "AUDIO_VALID"), regs[4:]):
        lines.append(f"#define {name} 0x{reg['offset']:08x}u")
    lines.append("static const struct { uint32_t addr, val; } identity[] = {")
    lines.extend(f"    {{0x{reg['offset']:08x}u, 0x{reg['value']:08x}u}}," for reg in regs[:4])
    lines.extend(("};", "static const struct input inputs[] = {"))
    for name in (FPGA_IMAGE, "manifest.json", *DSP_IMAGES):
        path = bundle / name
        lines.append(f'    {{"{name}", {path.stat().st_size}, "{digest(path)}"}},')
    return "\n".join([*lines, "};", ""])


if __name__ == "__main__":
    try:
        sys.stdout.write(generate(Path(sys.argv[1])))
    except (OSError, ValueError, KeyError, TypeError) as error:
        sys.exit(f"[error] T55 SPIBone CSR contract: {error}")
