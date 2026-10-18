#!/usr/bin/env python3
"""Apply narrow, repeatable VR timing hooks to privately generated NFSU1 C.

The retail maximum-step, tick quantization and replay code stay intact.
At XR rates, carry uses emitted ticks instead of a second 4000-Hz rounding.
Non-OpenXR builds compile the unmodified expressions. No retail bytes are
stored here or patched on disk. Unknown executable revisions are rejected.
"""
import argparse
import hashlib
from pathlib import Path

TITLE_SHA = "f4b3dc578fa38abb59961c0c1c692f28514b2ca8bfbd43854c726a1636b34b22"


def function(source, name):
    start = source.index("void " + name + "(void)\n{")
    return source[start:source.index("\n}\n", start) + 3]


def patch(directory, xbe, fixtures=None):
    if hashlib.sha256(xbe.read_bytes()).hexdigest() != TITLE_SHA:
        raise ValueError("VR timing hooks require the supported Xbox NTSC-U executable")
    bodies = {}
    pending = []
    for path in sorted(directory.glob("recomp_*.c")):
        source = original = path.read_text()
        for name in ("sub_001410D0", "sub_001D9EB0"):
            if "void " + name + "(void)\n{" not in source:
                continue
            body = function(source, name)
            # Normalize only our precise substitutions, permitting idempotent builds.
            body = body.replace("NFSU1_VR_MINIMUM(MEMF(", "MEMF(")
            for address in ("1F84A8", "1F8190"):
                body = body.replace(f"MEMF(0x{address})))", f"MEMF(0x{address}))")
            body = body.replace("NFSU1_VR_PRESENT(MEM32(esp + 0x28))", "MEM32(esp + 0x28)")
            body = body.replace("NFSU1_VR_CARRY(MEMF(esp + 0x24), fp_top(), edi)", "MEMF(esp + 0x24) - fp_top()")
            updated = body
            if name == "sub_001410D0":
                for address in ("1F84A8", "1F8190"):
                    for expression in (f"RECOMP_FCMP(fp_top(), MEMF(0x{address}))",
                                       f"fp_push(MEMF(0x{address}))"):
                        if updated.count(expression) != 1:
                            raise ValueError(f"Unexpected simulation translation: {expression}")
                        updated = updated.replace(expression,
                            expression.replace(f"MEMF(0x{address})", f"NFSU1_VR_MINIMUM(MEMF(0x{address}))"))
                    # The same constants also define the MAXIMUM step. Never zero them.
                    if updated.count(f"fp_top() * MEMF(0x{address})") != 1:
                        raise ValueError("Maximum simulation-step expression changed")
                carry = "fp_top() = MEMF(esp + 0x24) - fp_top();"
                if updated.count(carry) != 1:
                    raise ValueError("Unexpected simulation carry expression")
                updated = updated.replace(carry, "fp_top() = NFSU1_VR_CARRY(MEMF(esp + 0x24), fp_top(), edi);")
            else:
                expression = "edi = MEM32(esp + 0x28);"
                if updated.count(expression) != 1:
                    raise ValueError("Unexpected D3D command translation")
                updated = updated.replace(expression, "edi = NFSU1_VR_PRESENT(MEM32(esp + 0x28));")
            bodies[name] = (body, updated)
            source = source.replace(function(source, name), updated)
        if source != original or "/* NFSU1 VR timing hooks */" in source:
            if "/* NFSU1 VR timing hooks */" in source:
                source = source[source.index("#endif\n") + len("#endif\n"):]
            source = """/* NFSU1 VR timing hooks */
#ifdef NFSU2_OPENXR
extern float nfsu1_vr_minimum(float);
extern float nfsu1_vr_carry(float, double, unsigned int);
extern unsigned int nfsu1_vr_present(unsigned int);
#define NFSU1_VR_MINIMUM(v) nfsu1_vr_minimum(v)
#define NFSU1_VR_CARRY(d,r,t) nfsu1_vr_carry(d,r,t)
#define NFSU1_VR_PRESENT(v) nfsu1_vr_present(v)
#else
#define NFSU1_VR_MINIMUM(v) (v)
#define NFSU1_VR_CARRY(d,r,t) ((d)-(r))
#define NFSU1_VR_PRESENT(v) (v)
#endif
""" + source
        if source != original:
            pending.append((path, source))
    if len(bodies) != 2:
        raise ValueError("Expected exactly both title timing functions")
    for path, source in pending:
        path.write_text(source, newline="\n")
    if fixtures:
        fixtures.mkdir(parents=True, exist_ok=True)
        sources = "\n".join(p.read_text() for p in directory.glob("recomp_*.c"))
        helpers = []
        for address in ("001408A0", "001408E0", "001408F0"):
            helpers.append(function(sources, "sub_" + address))
        baseline, updated = bodies["sub_001410D0"]
        baseline = baseline.replace("void sub_001410D0(void)", "void simulation_original(void)")
        updated = updated.replace("void sub_001410D0(void)", "void simulation_updated(void)")
        (fixtures / "timing_generated.inc").write_text("\n".join(helpers + [baseline, updated]), newline="\n")
    print(f"NFSU1 timing hooks verified; {len(pending)} generated files updated.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("gen", type=Path)
    parser.add_argument("xbe", type=Path)
    parser.add_argument("--fixtures", type=Path)
    args = parser.parse_args()
    if args.fixtures and not args.fixtures.resolve().is_relative_to(Path(__file__).resolve().parents[1] / "build"):
        parser.error("Private generated fixtures must be inside build/.")
    patch(args.gen, args.xbe, args.fixtures)
