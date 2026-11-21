#!/usr/bin/env python3
"""Run the wrapped wing-t55 binary in a disposable root Linux container."""

import fcntl
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import zlib

from stage_wing_dsp import digest


def run(*args, error=None, **env):
    result = subprocess.run([sys.argv[1], *args], env={**os.environ, **env}, capture_output=True, text=True)
    assert result.returncode == int(error is not None) and (error is None or error in result.stderr), result


def main():
    bundle = Path("/usr/share/wing-t55")
    manifest = json.loads((bundle / "manifest.json").read_text())
    os.environ["TEST_BUILD_ID"] = str(manifest["build_id"])
    # Regular dummy glob entries; all open/ioctl calls for these are wrapped.
    for core in (50, 51):
        Path(f"/dev/gpiochip{core}").touch(exist_ok=False)
    Path("/etc/wing-t55-bringup").touch(exist_ok=False)
    receipt = Path("/run/omc-wing-fpga.json")
    load = ("load", "--bench-muted", "--clocks-verified")
    boot1 = ("boot-dsp", "1", "--bench-muted")

    def expect_mask(mask):
        file = receipt.stat()
        assert (file.st_uid, file.st_mode & 0o777) == (0, 0o600)
        assert json.loads(receipt.read_text()) == {
            "manifest": str(bundle / "manifest.json"), "manifest_sha256": digest(bundle / "manifest.json"),
            "build_id": manifest["build_id"], "chip_select": 1, "bitstream_sha256": manifest["bitstream"]["sha256"],
            "spi_device": "/dev/spibone0", "dsp_runtime_mask": mask,
        }

    def frames(core, nonce=0x12345678, flags=1, reply_core=None, reserved=0):
        request = struct.pack(">4sI3xB24x", b"WDSP", 0x12345678, core)
        check = zlib.crc32(request)
        reply = struct.pack(">4sIIBBBBI", b"WDSR", 1, nonce, reply_core or core, 0, flags, reserved, check)
        return dict(WDSP_REQUEST=(request + struct.pack(">I", check)).hex(),
                    WDSP_REPLY=(reply + struct.pack(">I", zlib.crc32(reply))).hex())

    run(*boot1, error="no matching boot-local")
    run("load", "--bench-muted", error="clocks-verified")
    run(*load, FAULT="short-fpga", error="short or failed passive SPI")
    assert not receipt.exists()
    run(*load)
    expect_mask(0)
    pristine = receipt.read_bytes()
    run("boot-dsp", "3", "--bench-muted", error="DSP3 blocked")
    with open("/run/wing-t55.lock", "rb") as lock:
        fcntl.flock(lock, fcntl.LOCK_SH | fcntl.LOCK_NB)
        run(*boot1, error="bring-up lock unavailable")
    expect_mask(0)
    for core, mask in ((1, 0), (4, 1), (2, 9)):
        run("boot-dsp", str(core), "--bench-muted", PRELOADED="1", PRIOR_RESET=str(mask), **frames(core))
        expect_mask(mask | (1 << (core - 1)))

    def reject(error, fault="", prior="0", **reply):
        receipt.write_bytes(pristine)
        receipt.chmod(0o600)
        run(*boot1, error=error, PRELOADED="1", PRIOR_RESET=prior, FAULT=fault, **frames(1, **reply))
        assert not receipt.exists()

    reject("live DSP reset state", prior="2")
    reject("reset release readback", "bad-reset-readback")
    reject("FS and VALID", "audio-active")
    reject("FPGA identity mismatch", "wrong-build")
    reject("one continuous SS0 message", "bufsiz")
    reject("short or failed DSP transfer", "short-dsp")
    reject("short or failed WDSP", "short-reply")
    reject("no matching armed WDSP", "bad-crc")
    reject("no matching armed WDSP", nonce=0x12345679)
    reject("no matching armed WDSP", reply_core=4)
    reject("no matching armed WDSP", flags=0)
    reject("no matching armed WDSP", reserved=1)
    loader = bundle / "dsp1.ldr"
    wrong = (bundle / "dsp2.ldr").read_bytes()
    loader.write_bytes(wrong.ljust(loader.stat().st_size, b"\0"))  # Exercise hash, not stat.
    reject("SHA-256 mismatch: dsp1.ldr")
    print("Mocked FPGA load and DSP1/4/2 qualification passed; all negative paths failed closed.")


if __name__ == "__main__":
    main()
