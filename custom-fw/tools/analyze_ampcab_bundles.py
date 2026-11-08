#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Andromeda contributors
"""Read-only fingerprint and structural audit for Pocket Amp ampCab bundles.

This script does not convert records, modify firmware, or
write output unless --output is explicitly supplied. CAB tap statistics are
marked as candidate parsing only; they are not proof of the container IR encoding.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import struct
from pathlib import Path

AMP_MAGIC = (b"AM3\0", b"AM4\0")
CAB_MAGIC = b"CAB\0"
AMP_RECORD_BYTES = 0x2000
CAB_RECORD_BYTES = 0x1000
CAB_TAP_OFFSET = 0x1A
CAB_TAP_COUNT = 1024


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def c_string(data: bytes) -> str:
    return data.split(b"\0", 1)[0].decode("latin-1", "replace")


def signed24le(data: bytes) -> int:
    n = int.from_bytes(data, "little", signed=False)
    return n - 0x1000000 if n & 0x800000 else n


def floats_at(record: bytes, offset: int, count: int) -> list[float]:
    return list(struct.unpack_from("<" + "f" * count, record, offset))


def amp_record(record: bytes, ordinal: int, offset: int) -> dict:
    magic = record[:4]
    name = c_string(record[4:24])
    voicing_raw = floats_at(record, 0x18, 9)
    voicing = [
        {"band": i + 1, "frequency_hz": voicing_raw[i * 3],
         "gain_min_db": voicing_raw[i * 3 + 1], "gain_max_db": voicing_raw[i * 3 + 2]}
        for i in range(3)
    ]
    values = floats_at(record, 0x40, (len(record) - 0x40) // 4)
    finite = [x for x in values if math.isfinite(x)]
    return {
        "ordinal": ordinal,
        "file_offset": f"0x{offset:06X}",
        "record_bytes": len(record),
        "magic": magic.rstrip(b"\0").decode("ascii", "replace"),
        "name": name,
        "header_opaque_0x12_0x17_hex": record[0x12:0x18].hex(),
        "voicing": voicing,
        "float_scan": {
            "offset": "0x40", "word_count": len(values),
            "finite_count": len(finite), "nonfinite_count": len(values) - len(finite),
            "finite_min": min(finite) if finite else None,
            "finite_max": max(finite) if finite else None,
            "raw_sha256": sha256(record[0x40:]),
            "interpretation": "raw float scan; coefficient semantics not implied",
        },
        "record_sha256": sha256(record),
    }


def cab_record(record: bytes, ordinal: int, offset: int) -> dict:
    raw_taps = [
        signed24le(record[i:i + 3])
        for i in range(CAB_TAP_OFFSET, CAB_TAP_OFFSET + CAB_TAP_COUNT * 3, 3)
    ]
    peak = max((abs(v) for v in raw_taps), default=0)
    energy = sum(v * v for v in raw_taps)
    return {
        "ordinal": ordinal,
        "file_offset": f"0x{offset:06X}",
        "record_bytes": len(record),
        "magic": c_string(record[:4]),
        "name": c_string(record[4:24]),
        "record_sha256": sha256(record),
        "candidate_taps": {
            "offset": f"0x{CAB_TAP_OFFSET:X}", "count": len(raw_taps),
            "sample_format_hypothesis": "signed 24-bit little-endian, unscaled",
            "peak_abs_raw": peak, "sum_squares_raw": energy,
            "raw_tap_bytes_sha256": sha256(record[CAB_TAP_OFFSET:CAB_TAP_OFFSET + CAB_TAP_COUNT * 3]),
            "verified_as_container_ir": False,
        },
    }


def analyze(path: Path) -> dict:
    data = path.read_bytes()
    amps = []
    cabs = []
    errors = []
    cursor = 0
    while cursor + AMP_RECORD_BYTES <= len(data) and data[cursor:cursor + 4] in AMP_MAGIC:
        record = data[cursor:cursor + AMP_RECORD_BYTES]
        amps.append(amp_record(record, len(amps) + 1, cursor))
        cursor += AMP_RECORD_BYTES
    amp_end = cursor
    while cursor + CAB_RECORD_BYTES <= len(data) and data[cursor:cursor + 4] == CAB_MAGIC:
        record = data[cursor:cursor + CAB_RECORD_BYTES]
        cabs.append(cab_record(record, len(cabs) + 1, cursor))
        cursor += CAB_RECORD_BYTES
    if not amps:
        errors.append("No AM3/AM4 record at offset zero")
    if not cabs:
        errors.append(f"No CAB record found after AMP region ending at 0x{amp_end:X}")
    if cursor != len(data):
        errors.append(f"Unclassified trailing bytes: offset 0x{cursor:X}, length {len(data) - cursor}")
    magics = sorted({a["magic"] for a in amps})
    return {
        "source_file": path.name,
        "source_size_bytes": len(data),
        "source_sha256": sha256(data),
        "observed_layout": {
            "amp_region_offset": "0x000000",
            "amp_record_bytes": AMP_RECORD_BYTES,
            "amp_count": len(amps),
            "cab_region_offset": f"0x{amp_end:06X}",
            "cab_record_bytes": CAB_RECORD_BYTES,
            "cab_count": len(cabs),
            "trailing_bytes": len(data) - cursor,
            "amp_magics": magics,
            "layout_valid": not errors,
        },
        "amps": amps,
        "cabs": cabs,
        "errors": errors,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("bundles", nargs="*", type=Path,
                        help="ampCab bundle paths; defaults to all *_ampCab.bin beside this script's parent")
    parser.add_argument("--output", type=Path,
                        help="optional JSON destination; without it, print to stdout")
    args = parser.parse_args()
    base = Path(__file__).resolve().parents[1] / "factory_bundles"
    paths = args.bundles or sorted(base.glob("*_ampCab.bin"))
    if not paths:
        parser.error(f"No *_ampCab.bin bundles found in {base}")
    result = {
        "schema": "pocket-amp-ampcab-audit-v1",
        "read_only": True,
        "notes": [
            "Bundle and record hashes are exact byte fingerprints.",
            "AMP float scans report bytes only; they do not establish model topology.",
            "CAB tap decoding is a candidate signed-24-bit view and is explicitly unverified.",
        ],
        "bundles": [analyze(p) for p in paths],
    }
    serialized = json.dumps(result, indent=2, ensure_ascii=False) + "\n"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(serialized, encoding="utf-8")
    else:
        print(serialized, end="")
    return 1 if any(not b["observed_layout"]["layout_valid"] for b in result["bundles"]) else 0


if __name__ == "__main__":
    raise SystemExit(main())
