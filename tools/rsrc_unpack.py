#!/usr/bin/env python3
import argparse
import csv
import hashlib
import json
import re
import struct
from pathlib import Path


ENTRY_SIZE = 0x80
DEFAULT_DATA_OFFSET = 0x2800


def safe_output_name(name):
    name = name.strip("/")
    name = re.sub(r"[^A-Za-z0-9._/-]+", "_", name)
    parts = [p for p in name.split("/") if p not in ("", ".", "..")]
    return Path(*parts)


def parse_resource(blob, data_offset=DEFAULT_DATA_OFFSET):
    entries = []
    data_cursor = data_offset

    for entry_offset in range(0, data_offset, ENTRY_SIZE):
        entry = blob[entry_offset : entry_offset + ENTRY_SIZE]
        raw_name = entry.split(b"\0", 1)[0]
        if not raw_name:
            if entries:
                break
            continue

        name = raw_name.decode("ascii", errors="replace")
        size = struct.unpack_from("<I", entry, ENTRY_SIZE - 4)[0]
        unknown = entry[ENTRY_SIZE - 8 : ENTRY_SIZE - 4].hex()

        if data_cursor + size > len(blob):
            raise ValueError(
                f"{name}: payload 0x{data_cursor:x}+0x{size:x} exceeds blob size"
            )

        payload = blob[data_cursor : data_cursor + size]
        entries.append(
            {
                "index": len(entries),
                "entry_offset": entry_offset,
                "data_offset": data_cursor,
                "size": size,
                "unknown_tail": unknown,
                "sha256": hashlib.sha256(payload).hexdigest(),
                "name": name,
            }
        )
        data_cursor += size

    return entries


def main():
    parser = argparse.ArgumentParser(
        description="Extract the Behringer WING decompressed resource archive."
    )
    parser.add_argument("resource", type=Path, help="decompressed resource blob")
    parser.add_argument("-o", "--output-dir", type=Path, required=True)
    parser.add_argument(
        "--data-offset",
        type=lambda value: int(value, 0),
        default=DEFAULT_DATA_OFFSET,
        help="payload area start, default: 0x2800",
    )
    args = parser.parse_args()

    blob = args.resource.read_bytes()
    entries = parse_resource(blob, args.data_offset)

    files_dir = args.output_dir / "files"
    files_dir.mkdir(parents=True, exist_ok=True)

    for entry in entries:
        out_path = files_dir / safe_output_name(entry["name"])
        out_path.parent.mkdir(parents=True, exist_ok=True)
        out_path.write_bytes(
            blob[entry["data_offset"] : entry["data_offset"] + entry["size"]]
        )
        entry["output"] = str(out_path)

    args.output_dir.mkdir(parents=True, exist_ok=True)
    (args.output_dir / "manifest.json").write_text(
        json.dumps(entries, indent=2), encoding="utf-8"
    )

    with (args.output_dir / "manifest.csv").open("w", newline="", encoding="utf-8") as f:
        fieldnames = [
            "index",
            "entry_offset",
            "data_offset",
            "size",
            "unknown_tail",
            "sha256",
            "name",
            "output",
        ]
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        for entry in entries:
            writer.writerow(entry)

    print(f"Extracted {len(entries)} entries to {files_dir}")
    print(f"Manifest: {args.output_dir / 'manifest.json'}")


if __name__ == "__main__":
    main()
