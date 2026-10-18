#!/usr/bin/env python3
"""Summarize the renderer's complete, unpaused, tracked 120-frame windows.

These rounded log values cannot establish an overall per-frame percentile or
a controlled speedup between different routes, views or resolutions.
"""
import argparse
import json
from pathlib import Path
import re
import statistics


def summarize(path):
    text = path.read_text(encoding="utf-8", errors="replace")
    windows = [dict(zip(("fps", "p50_ms", "p95_ms", "max_ms"), map(float, m)))
               for m in re.findall(
                   r"XR tracked race: ([\d.]+) fps, frame p50 ([\d.]+) ms, "
                   r"p95 ([\d.]+) ms, max ([\d.]+) ms \(120 frames\)", text)]
    result = {
        "log": str(path),
        "eye_buffers": re.findall(r"\[XR\] eye \d: \d+x\d+, \d+ images", text),
        "window_count": len(windows),
        "frames": len(windows) * 120,
        "windows": windows,
        "decoder_checks": re.findall(r"\[VR texture decode\].*", text),
        "bulk_checks": re.findall(r"\[PB\] bulk upload (?:verified|mismatch).*", text),
    }
    if windows:
        rates = [w["fps"] for w in windows]
        result.update(
            minimum_window_fps=min(rates),
            maximum_window_fps=max(rates),
            median_window_fps=statistics.median(rates),
            elapsed_weighted_fps=round(statistics.harmonic_mean(rates), 2),
            approximate_measured_seconds=round(sum(120 / r for r in rates), 2),
            worst_frame_ms=max(w["max_ms"] for w in windows),
            windows_at_least_60_fps=sum(r >= 60 for r in rates),
            windows_at_least_72_fps=sum(r >= 72 for r in rates),
        )
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    result = summarize(args.log)
    if args.output:
        args.output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({k: v for k, v in result.items() if k != "windows"}, indent=2))


if __name__ == "__main__":
    main()
