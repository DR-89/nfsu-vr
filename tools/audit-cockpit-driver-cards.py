#!/usr/bin/env python3
"""Extract private stock mesh fixtures to test driver-card removal across cars.

Run audit-cockpit-cars.py first. This reads the local disc's decoded stock
solids, including roofs, bodies and mirror geometry as negative cases. No
game assets are modified or written outside the ignored build directory.
"""
import argparse
from contextlib import ExitStack
import json
import math
import mmap
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parent.parent
# Inspected NFSU1 BASE_A cards. CELICA/0 is exterior trim, not a driver.
DRIVERS = {"240SX":6,"350Z":7,"CELICA":3,"CIVIC":5, **dict.fromkeys(
    "ECLIPSE FOCUS GOLF IMPREZA INTEGRA LANCER MIATA NEON PEUGOT RSX RX7 S2000 SENTRA SKYLINE SUPRA TIBURON".split(),0)}
# Inspected central glass batches. Focus and Tiburon have no qualified center
# glass in the decoded stock solids; keep those gaps explicit, not fabricated.
CENTER_GLASS = {"240SX":(3,6),"350Z":(4,12),"CELICA":(2,12),"CIVIC":(2,6),
    "ECLIPSE":(3,12),"GOLF":(3,12),"IMPREZA":(3,21),"INTEGRA":(3,6),
    "LANCER":(5,12),"MIATA":(3,6),"NEON":(3,12),"PEUGOT":(4,12),
    "RSX":(3,12),"RX7":(3,6),"S2000":(3,12),"SENTRA":(5,6),
    "SKYLINE":(2,12),"SUPRA":(4,6)}


def chunks(data, start, end):
    while start + 8 <= end:
        tag, size = struct.unpack_from("<II", data, start)
        if size > end - start - 8:
            raise ValueError(f"Invalid chunk at {start:x}")
        yield tag, (start + 8, size)
        start += size + 8
    if start != end:
        raise ValueError("Incomplete chunk list")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("game", type=Path)
    parser.add_argument("inventory", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    output = args.output.resolve()
    if not output.is_relative_to(ROOT / "build"):
        parser.error("Use a private output directory inside build/.")
    inventory = json.loads(args.inventory.read_text())
    output.mkdir(parents=True, exist_ok=True)
    records = []
    with ExitStack() as stack:
        files = {}
        for name in {p["archive"] for car in inventory.values() for p in car.values()}:
            f = stack.enter_context((args.game / name).open("rb"))
            files[name] = stack.enter_context(mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ))
        fixture = stack.enter_context((output / "stock-meshes.bin").open("wb"))
        mirrors = stack.enter_context((output / "stock-center-mirrors.bin").open("wb"))
        for car, parts in sorted(inventory.items()):
            drivers = 0
            for name, part in sorted(parts.items()):
                data = files[part["archive"]]
                h = part["offset"]
                solid = dict(chunks(data, h, h + struct.unpack_from("<I", data, h - 4)[0]))
                a, size = solid[0x80134100]
                mesh = dict(chunks(data, a, a + size))
                va, vs = mesh[0x134B01]
                vertex_end = va + vs
                va = (va + 127) & ~127
                ia, ins = mesh[0x134B03]
                index_end = ia + ins
                ia = (ia + 15) & ~15
                ma, ms = mesh[0x134B02]
                padding = (-ma) % 16
                ma += padding
                ms -= padding
                if ms % 64:
                    raise ValueError(f"Unexpected submesh format: {name}")
                for number in range(ms // 64):
                    header = ma + number * 64
                    low = struct.unpack_from("<3f", data, header)
                    high = struct.unpack_from("<3f", data, header + 12)
                    count, texture, flags, first = struct.unpack_from("<4I", data, header + 24)
                    if count % 3 or ia + (first + count) * 2 > index_end:
                        raise ValueError(f"Invalid triangle indices: {name}/{number}")
                    original = struct.unpack_from(f"<{count}H", data, ia + first * 2)
                    used = sorted(set(original))
                    if not used or va + (used[-1] + 1) * 36 > vertex_end:
                        raise ValueError(f"Invalid vertices: {name}/{number}")
                    vertices = [struct.unpack_from("<3f", data, va + index * 36) for index in used]
                    # Independent submesh bounds catch a wrong vertex stride,
                    # index base or alignment instead of testing garbage data.
                    if any(not math.isfinite(x) or x < low[k] - .011 or x > high[k] + .011
                           for vertex in vertices for k, x in enumerate(vertex)):
                        raise ValueError(f"Vertex outside recorded bounds: {name}/{number}")
                    mapping = {old: new for new, old in enumerate(used)}
                    indices = [mapping[index] for index in original]
                    # Inspected stock BASE solids each contain exactly one
                    # 8-vertex/4-triangle driver impostor. Labels do not call
                    # the runtime classifier under test.
                    expected = int(name == car + "_BASE_A" and number == DRIVERS[car])
                    if expected and (len(used) != 8 or count != 12):
                        raise ValueError(f"Unexpected driver topology: {car}/{number}")
                    drivers += expected
                    mirror_part, mirror_count = CENTER_GLASS.get(car, (-1, 0))
                    expected_mirror = mirror_count if name == car + "_BASE_A" and number == mirror_part else 0
                    for target, label in ((fixture, expected), (mirrors, expected_mirror)):
                        target.write(struct.pack("<4I", len(records), len(used), count, label))
                        for vertex in vertices:
                            target.write(struct.pack("<4f", *vertex, 1.0))
                        target.write(struct.pack(f"<{count}I", *indices))
                    records.append(dict(car=car, solid=name, submesh=number, vertices=len(used),
                                        indices=count, expected_remove=expected,
                                        expected_center_mirror=expected_mirror, flags=flags,
                                        low=low, high=high))
            if drivers != 1:
                raise ValueError(f"Expected one driver card in {car}, got {drivers}")
    (output / "stock-meshes.json").write_text(json.dumps(records, indent=2) + "\n")
    print(f"Prepared {len(records)} submeshes across {len(inventory)} vehicles; "
          f"{sum(r['expected_remove'] for r in records)} driver cards, remainder must be retained.")


if __name__ == "__main__":
    main()
