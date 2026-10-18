#!/usr/bin/env python3
"""Extract private retail input routines/constants for the wheel regression test."""
import argparse
import hashlib
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parents[1]
SHA = "f4b3dc578fa38abb59961c0c1c692f28514b2ca8bfbd43854c726a1636b34b22"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--gen", type=Path, default=ROOT / "gen")
    parser.add_argument("--xbe", type=Path, default=ROOT / "game/default.xbe")
    args = parser.parse_args()
    image = args.xbe.read_bytes()
    if hashlib.sha256(image).hexdigest() != SHA:
        parser.error("Requires the supported Xbox NTSC-U executable")
    base = struct.unpack_from("<I", image, 0x104)[0]
    count, table = struct.unpack_from("<II", image, 0x11c)
    sections = [struct.unpack_from("<5I", image, table-base+i*56) for i in range(count)]
    names = ["sub_001B2344", "sub_00014940", "sub_00014330", "sub_0013EA80", "sub_00131140"]
    found = {}
    for path in sorted(args.gen.glob("recomp_*.c")):
        source = path.read_text(encoding="utf-8")
        for name in names:
            marker = "void " + name + "(void)\n{"
            if marker in source:
                if name in found:
                    raise ValueError("Duplicate generated routine " + name)
                start = source.index(marker)
                found[name] = source[start:source.index("\n}\n", start)+3]
    if set(found) != set(names):
        raise ValueError("Missing generated input routines")
    # Use executable constants, not a second implementation of the new mapping.
    constants = []
    for address in (0x1f4098, 0x1f4120, 0x1f41f8, 0x1f4320, 0x1f4324,
                    0x1f4328, 0x1f432c, 0x2195dc, 0x1f41c8, 0x1f7c58):
        for _, va, _, raw, size in sections:
            if va <= address < va+size:
                bits = struct.unpack_from("<I", image, raw+address-va)[0]
                constants.append(f"    MEM32(0x{address:x}) = 0x{bits:08x}u;")
                break
        else:
            raise ValueError("Constant outside executable")
    output = ROOT / "build/steering-audit"
    output.mkdir(parents=True, exist_ok=True)
    text = "\n".join(found[name] for name in names)
    text += "\nstatic void retail_constants(void)\n{\n" + "\n".join(constants) + "\n}\n"
    (output / "steering_generated.inc").write_text(text, encoding="utf-8", newline="\n")
    print("Private steering fixtures:", output)


if __name__ == "__main__":
    main()
