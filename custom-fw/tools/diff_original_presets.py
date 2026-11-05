#!/usr/bin/env python3
"""Lossless byte diff for two saved 80 x 92-byte preset banks.

Use with controlled captures where exactly one editor/device setting changed.
This reports changed bytes only; it does not assign semantic field names or
convert presets. By default results go to stdout and inputs are read-only.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

RECORD_BYTES = 92
RECORD_COUNT = 80
TRAILER = b"PATCHEND\0\0"
EXPECTED_BYTES = RECORD_BYTES * RECORD_COUNT + len(TRAILER)


def read_bank(path: Path) -> bytes:
    data = path.read_bytes()
    if len(data) != EXPECTED_BYTES:
        raise ValueError(f"{path}: expected {EXPECTED_BYTES} bytes, found {len(data)}")
    if data[-len(TRAILER):] != TRAILER:
        raise ValueError(f"{path}: missing expected PATCHEND trailer")
    return data


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("baseline", type=Path)
    parser.add_argument("changed", type=Path)
    parser.add_argument("--output", type=Path, help="optional JSON output; default is stdout")
    args = parser.parse_args()
    before = read_bank(args.baseline)
    after = read_bank(args.changed)
    records = []
    for index in range(RECORD_COUNT):
        start = index * RECORD_BYTES
        left = before[start:start + RECORD_BYTES]
        right = after[start:start + RECORD_BYTES]
        changes = [
            {"byte_offset_in_record": offset, "file_offset": start + offset,
             "before": left[offset], "after": right[offset]}
            for offset in range(RECORD_BYTES) if left[offset] != right[offset]
        ]
        if changes:
            records.append({"preset_index_1_based": index + 1, "change_count": len(changes),
                            "before_raw_hex": left.hex(), "after_raw_hex": right.hex(),
                            "changes": changes})
    result = {
        "schema": "preset-byte-diff-v1",
        "semantic_interpretation": "none; byte offsets only",
        "baseline": {"file": args.baseline.name, "sha256": hashlib.sha256(before).hexdigest()},
        "changed": {"file": args.changed.name, "sha256": hashlib.sha256(after).hexdigest()},
        "record_bytes": RECORD_BYTES,
        "record_count": RECORD_COUNT,
        "changed_record_count": len(records),
        "records": records,
    }
    text = json.dumps(result, indent=2) + "\n"
    if args.output:
        args.output.write_text(text, encoding="utf-8")
    else:
        print(text, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
