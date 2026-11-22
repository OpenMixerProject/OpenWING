#!/usr/bin/env python3
"""Add UI and touch userspace to the pinned T55 newc, retaining device nodes."""
import gzip
import hashlib
import json
import re
import sys
from pathlib import Path

from stage_wing_t55 import verify_sums


def padded(size):
    return (size + 3) & ~3


def read_newc(archive):
    pos = 0
    records = {}
    while pos < len(archive):
        if archive[pos:pos + 6] != b"070701":
            raise ValueError(f"invalid newc magic at {pos}")
        fields = [int(archive[pos + n:pos + n + 8], 16) for n in range(6, 110, 8)]
        name_end = pos + 110 + fields[11]
        name = archive[pos + 110:name_end - 1].decode()
        start = padded(name_end)
        end = start + fields[6]
        nxt = padded(end)
        if name in records or end > len(archive) or archive[name_end - 1] != 0:
            raise ValueError(f"invalid/duplicate newc entry: {name}")
        records[name] = (fields, archive[start:end], archive[pos:nxt])
        if name == "TRAILER!!!":
            if any(archive[nxt:]):
                raise ValueError("unexpected data after newc trailer")
            return records
        pos = nxt
    raise ValueError("missing newc trailer")


def record(name, fields, payload):
    raw_name = name.encode() + b"\0"
    fields = fields.copy()
    fields[6], fields[11] = len(payload), len(raw_name)
    header = b"070701" + b"".join(f"{field:08x}".encode() for field in fields) + raw_name
    return header.ljust(padded(len(header)), b"\0") + payload.ljust(padded(len(payload)), b"\0")


def stage(base, init, ui, omc, runtime, touch, output):
    entries = read_newc(gzip.decompress(base.read_bytes()))
    prefix = "usr/share/wing-t55/"
    manifest = json.loads(entries[prefix + "manifest.json"][1])
    if manifest["build_id"] != 0x2BCF0718 or "etc/wing-t55-recovery" not in entries:
        raise ValueError("not the reviewed T55 build ID")
    # The historical package predates qualified DSP boot. Keep its payloads
    # for comparison, but do not republish its transfer-only loader or SSH key.
    for name in ("usr/bin/wing-t55", "etc/wing-t55-bringup", "etc/dropbear/dropbear_ed25519_host_key"):
        entries.pop(name, None)
    verify_sums(omc / "SHA256SUMS", omc, ("omc",))
    provenance = (omc / "provenance.txt").read_bytes()
    uapi = next(line.split()[0] for line in entries[prefix + "spibone.provenance.txt"][1].decode().splitlines()
                if line.endswith("  spibone_uapi.h"))
    omc_bytes = (omc / "omc").read_bytes()
    for key, expected in {"target": "TARGET_WING", "transport": "spibone", "spibone_uapi_sha256": uapi,
                          "dsp_board_profile_sha256": manifest["dsp_board_profile_sha256"],
                          "omc_sha256": hashlib.sha256(omc_bytes).hexdigest()}.items():
        lines = [line for line in provenance.decode().splitlines() if line.startswith(key + "=")]
        if lines != [f"{key}={expected}"]:
            raise ValueError(f"OMC provenance must declare exactly one {key}={expected}")
    updates = {"init": init.read_bytes(), prefix + "omc": omc_bytes,
               prefix + "omc.SHA256SUMS": (omc / "SHA256SUMS").read_bytes(),
               prefix + "omc.provenance.txt": provenance}
    updates[prefix + "SHA256SUMS"] = re.sub(
        rb"^[0-9a-f]{64}(?=  (omc(?:\.SHA256SUMS|\.provenance\.txt)?)$)",
        lambda m: hashlib.sha256(updates[prefix + m[1].decode()]).hexdigest().encode(),
        entries[prefix + "SHA256SUMS"][1], flags=re.MULTILINE)

    additions = {"usr/bin/openwing-ui": (0o100555, ui.read_bytes()),
                 "usr/bin/wing-touch-uinput": (0o100555, touch.read_bytes())}
    for path in sorted(runtime.rglob("*")):
        name = path.relative_to(runtime).as_posix()
        if name in additions:
            raise ValueError(f"runtime would overwrite a UI executable: {name}")
        if path.is_symlink():
            additions[name] = (0o120777, path.readlink().as_posix().encode())
        elif path.is_dir():
            if name not in entries:
                additions[name] = (0o40755, b"")
        elif path.is_file():
            additions[name] = (0o100755 if path.stat().st_mode & 0o111 else 0o100644, path.read_bytes())
        else:
            raise ValueError(f"unexpected runtime input: {name}")
    if entries.keys() & additions.keys():
        raise ValueError(f"runtime would overwrite base entries: {entries.keys() & additions.keys()}")
    next_inode = max(item[0][0] for item in entries.values()) + 1
    raw = bytearray()
    for name, (attrs, _, block) in entries.items():
        if name == "TRAILER!!!":
            for add_name, (mode, payload) in additions.items():
                raw.extend(record(add_name, [next_inode, mode, 0, 0, 1, 1767225600, 0, 0, 0, 0, 0, 0, 0], payload))
                next_inode += 1
        raw.extend(record(name, attrs, updates[name]) if name in updates else block)
    verified = read_newc(raw)
    for line in verified[prefix + "SHA256SUMS"][1].decode().splitlines():
        sha, name = line.split("  ", 1)
        if hashlib.sha256(verified[prefix + name][1]).hexdigest() != sha:
            raise ValueError(f"staged bundle checksum mismatch: {name}")
    output.write_bytes(gzip.compress(raw, compresslevel=9, mtime=0))


if __name__ == "__main__":
    stage(*map(Path, sys.argv[1:]))
