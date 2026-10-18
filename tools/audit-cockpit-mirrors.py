#!/usr/bin/env python3
"""Extract private NFSU1 stock side-mirror batches for classifier validation.

Xbox NFSU1 truncates these solid names, often before the LOD suffix. Scanning
only names ending in _A silently omits them. Keep assets in ignored build/.
"""
import argparse
import importlib.util
import json
import math
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parent.parent
spec = importlib.util.spec_from_file_location("driver_audit", ROOT / "tools/audit-cockpit-driver-cards.py")
audit = importlib.util.module_from_spec(spec)
spec.loader.exec_module(audit)
# Material batches inspected independently from the runtime classifier.
# Four cars combine glass and housing; separate-glass cars use batch 1/2.
GLASS_PART = {car: 1 for car in audit.DRIVERS}
GLASS_PART.update(dict.fromkeys(("CIVIC", "LANCER", "RSX", "SENTRA", "SKYLINE"), 0))
GLASS_PART.update({"350Z": 2, "CELICA": 2})


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("game", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    if not args.output.resolve().is_relative_to(ROOT / "build"):
        parser.error("Keep game-derived fixtures inside build/.")
    records, found = [], {}
    for car in sorted(audit.DRIVERS):
        data = (args.game / "CARS" / car / "GEOMETRY.BIN").read_bytes()
        offset = 0
        while (header := data.find(b"\x11\x40\x13\x00", offset)) >= 0:
            offset = header + 4
            size = struct.unpack_from("<I", data, header + 4)[0]
            if not 0xb0 <= size <= 0x200 or header + 8 + size > len(data):
                continue
            name = data[header + 0x9c:header + 8 + size].split(b"\0")[0].decode("ascii", errors="replace")
            side = 1 if name.startswith(car + "_KIT00_LEFT_SIDE_") else 2 if name.startswith(car + "_KIT00_RIGHT_SIDE_") else 0
            if not side:
                continue
            solid = dict(audit.chunks(data, header, header + struct.unpack_from("<I", data, header - 4)[0]))
            start, size = solid[0x80134100]
            chunks = dict(audit.chunks(data, start, start + size))
            va, vs = chunks[0x134b01]; vend = va + vs; va = (va + 127) & ~127
            ia, ins = chunks[0x134b03]; iend = ia + ins; ia = (ia + 15) & ~15
            ma, ms = chunks[0x134b02]; ms -= (-ma) % 16; ma = (ma + 15) & ~15
            if ms % 64:
                raise ValueError("Unexpected submesh records")
            parts = []
            for part in range(ms // 64):
                low = struct.unpack_from("<3f", data, ma + part * 64)
                high = struct.unpack_from("<3f", data, ma + part * 64 + 12)
                n, texture, flags, first = struct.unpack_from("<4I", data, ma + part * 64 + 24)
                if n % 3 or ia + (first + n) * 2 > iend:
                    raise ValueError("Invalid mirror indices")
                original = struct.unpack_from(f"<{n}H", data, ia + first * 2)
                used = sorted(set(original))
                if not used or va + (used[-1] + 1) * 36 > vend:
                    raise ValueError("Invalid mirror vertex range")
                vertices = [struct.unpack_from("<3f", data, va + index * 36) for index in used]
                if any(not math.isfinite(v) or v < low[k] - .011 or v > high[k] + .011 for p in vertices for k, v in enumerate(p)):
                    raise ValueError("Mirror vertices do not match independent submesh bounds")
                remap = {v: i for i, v in enumerate(used)}
                parts.append((vertices, [remap[v] for v in original]))
            # Truncated names can identify multiple LODs. Keep most detailed.
            key = car, side
            if key not in found or sum(len(p) for p, ix in parts) > sum(len(p) for p, ix in found[key][1]):
                found[key] = name, parts
    args.output.mkdir(parents=True, exist_ok=True)
    with (args.output / "side-geometry.bin").open("wb") as file:
        for car_id, car in enumerate(sorted(audit.DRIVERS), 1):
            for side in (1, 2):
                name, parts = found[car, side]
                for number, (vertices, indices) in enumerate(parts):
                    expected = number == GLASS_PART[car]
                    file.write(struct.pack("<5I", car_id, side, expected, len(vertices), len(indices)))
                    for vertex in vertices:
                        file.write(struct.pack("<3f", *vertex))
                    file.write(struct.pack(f"<{len(indices)}I", *indices))
                    records.append(dict(car=car, side=side, solid=name, submesh=number, expected_glass=expected, vertices=len(vertices), indices=len(indices)))
    (args.output / "side-geometry.json").write_text(json.dumps(records, indent=2) + "\n")
    print(f"Extracted {len(records)} mirror batches, both sides of {len(found) // 2} stock cars.")


if __name__ == "__main__":
    main()
