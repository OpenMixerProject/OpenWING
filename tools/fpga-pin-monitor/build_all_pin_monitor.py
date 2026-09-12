#!/usr/bin/env python3
"""
===============================================================================
build_all_pin_monitor.py - Efinix Trion FPGA Bitstream Build Tool
===============================================================================

Overview:
---------
Automated build script that invokes the Efinix Efinity toolchain (synthesis,
placement, routing, bitstream generation) to compile the 252-pin telemetry
logic analyzer (`all_pin_monitor`) for both:
  - Efinix Trion T55F484
  - Efinix Trion T85F484

Build Pipeline Steps:
---------------------
1. Load GPIO Pin Inventory:
   Reads `data/T55F484-gpio-inventory.json` containing package balls, banks,
   and Efinix hardware resources. Excludes dedicated SPI interface balls
   (W1, V2, V1, V3) used by the i.MX6 host connection.

2. Generate Periphery Configuration & Constraints (SDC):
   Uses the Efinity Python DesignAPI (`efx_py`) to automatically create GPIO
   interfaces and assign physical package pins for all 252 probe inputs.

3. Synthesize & Place-and-Route:
   Invokes `efx_run` to execute logic synthesis (`efx_map`), timing-driven
   placement and routing (`efx_pnr`), and bitstream creation (`efx_pgm`).

4. Package Behringer Firmware Header:
   If a stock firmware image (`ngcfpga_48k_efiT55.bin.966a0056`) is present,
   extracts the 260-byte Behringer firmware header, patches the payload
   length field, and prepends it to create a `.firmware.bin` directly uploadable
   on the console using standard stock firmware tools.
===============================================================================
"""

import argparse
import json
import os
import shutil
import struct
import subprocess
import sys
from pathlib import Path

# Path to the Efinix Efinity software installation
EFINITY_HOME = Path("/mnt/games/efinity/efinity/2026.1")
NAME = "all_pin_monitor"
TIMING_MODEL = "C4"

HERE = Path(__file__).resolve().parent
REPO_ROOT = HERE.parent.parent

# Dedicated host ECSPI2 pins connected to i.MX6 Solo (must NOT be used as probes)
SPI_BALLS = {"W1", "V2", "V1", "V3"}


def make_project_xml(device: str) -> str:
    """
    Generates the Efinix Efinity XML project definition file for the target FPGA.
    Configures synthesis options, timing models, and bitstream generation settings.
    """
    return f"""<?xml version="1.0" encoding="UTF-8"?>
<efx:project name="{NAME}" description="Behringer WING All-Pin Monitor ({device})" last_change="0" sw_version="2026.1.132" last_run_state="" last_run_flow="" config_result_in_sync="true" design_ood="new" place_ood="new" route_ood="" xmlns:efx="http://www.efinixinc.com/enf_proj" xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" xsi:schemaLocation="http://www.efinixinc.com/enf_proj enf_proj.xsd">
  <efx:device_info><efx:family name="Trion"/><efx:device name="{device}"/><efx:timing_model name="{TIMING_MODEL}"/></efx:device_info>
  <efx:design_info def_veri_version="verilog_2k" def_vhdl_version="vhdl_2008"><efx:top_module name="{NAME}"/><efx:design_file name="{NAME}.v" version="default" library="default"/></efx:design_info>
  <efx:constraint_info><efx:sdc_file name="outflow/{NAME}.pt.sdc"/><efx:inter_file name="outflow/{NAME}.interface.csv"/></efx:constraint_info>
  <efx:sim_info/><efx:misc_info/><efx:ip_info/>
  <efx:synthesis tool_name="efx_map">
    <efx:param name="work_dir" value="work_syn" value_type="e_string"/>
    <efx:param name="write_efx_verilog" value="on" value_type="e_bool"/>
    <efx:param name="mode" value="speed" value_type="e_option"/>
    <efx:param name="max_threads" value="4" value_type="e_integer"/>
  </efx:synthesis>
  <efx:place_and_route tool_name="efx_pnr">
    <efx:param name="work_dir" value="work_pnr" value_type="e_string"/>
    <efx:param name="verbose" value="off" value_type="e_bool"/>
    <efx:param name="seed" value="1" value_type="e_integer"/>
    <efx:param name="placer_effort_level" value="2" value_type="e_option"/>
    <efx:param name="max_threads" value="4" value_type="e_integer"/>
  </efx:place_and_route>
  <efx:bitstream_generation tool_name="efx_pgm">
    <efx:param name="mode" value="active" value_type="e_string"/>
    <efx:param name="width" value="1" value_type="e_string"/>
    <efx:param name="enable_roms" value="smart" value_type="e_option"/>
    <efx:param name="spi_low_power_mode" value="on" value_type="e_bool"/>
    <efx:param name="io_weak_pullup" value="on" value_type="e_bool"/>
    <efx:param name="oscillator_clock_divider" value="DIV8" value_type="e_option"/>
    <efx:param name="bitstream_compression" value="off" value_type="e_bool"/>
    <efx:param name="generate_bit" value="on" value_type="e_bool"/>
    <efx:param name="generate_bitbin" value="on" value_type="e_bool"/>
    <efx:param name="generate_hex" value="off" value_type="e_bool"/>
    <efx:param name="generate_hexbin" value="off" value_type="e_bool"/>
  </efx:bitstream_generation>
</efx:project>
"""


def load_probes():
    """
    Loads all non-SPI GPIO pins from the T55F484 package pinout inventory.
    """
    inv_path = REPO_ROOT / "data" / "T55F484-gpio-inventory.json"
    inv = json.loads(inv_path.read_text())
    # Filter out pins reserved for the ECSPI2 communication interface
    probes = [p for p in inv if p["package_ball"] not in SPI_BALLS]
    return probes


def build_target(device: str, probes: list):
    """
    Executes the build flow for a specific silicon target (T55F484 or T85F484).
    """
    dev_tag = device[:3].lower()  # "t55" or "t85"
    project_dir = HERE / f"build_{dev_tag}"
    outflow_dir = project_dir / "outflow"

    print(f"\n=================================================================")
    print(f"   BUILDING {device} ALL-PIN MONITOR BITSTREAM ({dev_tag.upper()})")
    print(f"=================================================================")

    project_dir.mkdir(parents=True, exist_ok=True)
    outflow_dir.mkdir(parents=True, exist_ok=True)

    # Copy Verilog RTL source and generate XML project configuration
    shutil.copy2(HERE / f"{NAME}.v", project_dir / f"{NAME}.v")
    (project_dir / f"{NAME}.xml").write_text(make_project_xml(device))

    # Prepare Pin list for the Efinity Periphery Generator
    pins = [
        ("input",  "host_sck",  "W1"),  # ECSPI2 SCLK
        ("input",  "host_mosi", "V2"),  # ECSPI2 MOSI
        ("output", "host_miso", "V1"),  # ECSPI2 MISO
        ("input",  "host_cs_n", "V3"),  # ECSPI2 CS0
    ]
    for idx, p in enumerate(probes):
        pins.append(("input", f"probe_in[{idx}]", p["package_ball"]))

    # Write Python script for Efinity DesignAPI
    gen_script = project_dir / "gen_periphery.py"
    gen_script.write_text(f"""import sys, os
sys.path.append(os.environ["EFXPT_HOME"] + "/bin")
from api_service.design import DesignAPI
d = DesignAPI(True)
d.create("{NAME}", "{device}", "{project_dir}", False, False)
pins = {pins}
for mode, name, pin in pins:
    creator = getattr(d, "create_" + mode + "_gpio")
    h = creator(name)
    d.assign_pkg_pin(h, pin)
d.save()
d.generate(False, "{outflow_dir}")
print("Periphery generated successfully.")
""")

    # Setup environment variables required by Efinity binaries
    env = os.environ.copy()
    env.update(
        EFINITY_HOME=str(EFINITY_HOME),
        EFXPT_HOME=str(EFINITY_HOME / "pt"),
        EFXPGM_HOME=str(EFINITY_HOME / "pgm"),
        PYTHONHOME=str(EFINITY_HOME),
        PYTHONPATH=str(EFINITY_HOME / "lib"),
        PYTHONNOUSERSITE="1",
    )

    # Step 1: Run Periphery & SDC Generation
    print(f"[*] Step 1: Generating Periphery & SDC for {device} ({len(probes)} pins)...")
    res = subprocess.run([str(EFINITY_HOME / "bin" / "efx_py"), str(gen_script)], env=env, capture_output=True, text=True)
    if res.returncode != 0:
        print(f"[-] Error generating periphery for {device}:\n", res.stderr)
        sys.exit(1)
    print(f"[+] Periphery OK for {device}.")

    # Step 2: Compile Design using efx_run
    print(f"[*] Step 2: Compiling Design with efx_run for {device}...")
    res = subprocess.run([
        str(EFINITY_HOME / "bin" / "efx_run"),
        NAME,
        "--prj",
        "--flow", "compile"
    ], cwd=str(project_dir), env=env, capture_output=True, text=True)

    if res.returncode != 0:
        print(f"[-] Compilation failed for {device}:\n", res.stderr)
        print(res.stdout[-1500:])
        sys.exit(1)
    print(f"[+] Compilation succeeded for {device}!")

    raw_bitbin = outflow_dir / f"{NAME}.bit.bin"
    if not raw_bitbin.exists():
        print(f"[-] Bitstream not found at: {raw_bitbin}")
        sys.exit(1)

    dest_bitbin = HERE / f"{NAME}_{dev_tag}.bit.bin"
    shutil.copy2(raw_bitbin, dest_bitbin)
    print(f"[+] Raw Efinity bitstream: {dest_bitbin} ({dest_bitbin.stat().st_size} bytes)")

    # Step 3: Package with Behringer 260-byte proprietary firmware header
    stock_path = REPO_ROOT / "firmware" / "ngcfpga_48k_efiT55.bin.966a0056"
    if stock_path.exists():
        header = bytearray(stock_path.read_bytes()[:260])
        bitbin_bytes = dest_bitbin.read_bytes()
        idx = bitbin_bytes.find(b"PADDED_BITS:")
        if idx != -1:
            newline_idx = bitbin_bytes.find(b"\n", idx)
            payload = bitbin_bytes[newline_idx+1:]
        else:
            payload = bitbin_bytes[256:]

        # Patch payload length in little-endian 32-bit header field
        struct.pack_into("<I", header, 0, len(payload))

        fw_pkg = bytes(header) + payload
        fw_pkg_path = HERE / f"{NAME}_{dev_tag}_firmware.bin"
        fw_pkg_path.write_bytes(fw_pkg)
        print(f"[+] Packaged Behringer Linux-uploadable firmware: {fw_pkg_path} ({len(fw_pkg)} bytes)")

        if dev_tag == "t55":
            shutil.copy2(dest_bitbin, HERE / f"{NAME}.bit.bin")
            shutil.copy2(fw_pkg_path, HERE / f"{NAME}_firmware.bin")

    return dest_bitbin


def main():
    """
    CLI entry point: Parses arguments and triggers compilation for requested targets.
    """
    if not EFINITY_HOME.exists():
        raise SystemExit(f"Efinity not found at {EFINITY_HOME}")

    parser = argparse.ArgumentParser(description="Build Efinix bitstreams for all_pin_monitor")
    parser.add_argument("--device", choices=["T55", "T85", "all"], default="all",
                        help="Target device (T55, T85, or all; default: all)")
    args = parser.parse_args()

    # Load probe pin inventory
    probes = load_probes()
    print(f"[*] Loaded {len(probes)} total non-SPI GPIO pins from inventory.")

    # Export mapping metadata JSON for tooling
    pin_table = []
    for idx, p in enumerate(probes):
        pin_table.append({
            "index": idx,
            "ball": p["package_ball"],
            "resource": p["resource"],
            "bank": p["bank"]
        })
    (HERE / "all_pins_metadata.json").write_text(json.dumps(pin_table, indent=2) + "\n")
    print(f"[+] Saved pin metadata to {HERE / 'all_pins_metadata.json'}")

    targets = ["T55F484", "T85F484"] if args.device == "all" else [f"{args.device.upper()}F484"]

    built = []
    for tgt in targets:
        img = build_target(tgt, probes)
        built.append(img)

    print("\n=================================================================")
    print("   ALL REQUESTED BITSTREAMS BUILT SUCCESSFULLY!")
    print("   (NOTE: Not uploaded to WING as requested)")
    print("=================================================================")
    for b in built:
        print(f" - {b.name}: {b.stat().st_size} bytes ({b.resolve()})")


if __name__ == "__main__":
    main()
