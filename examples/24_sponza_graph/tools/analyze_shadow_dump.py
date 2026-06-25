#!/usr/bin/env python3
"""Analyze --dump-shadow output: histograms, red-pixel ratio in shadowuv, A/B direct delta."""
import argparse
import os
import struct
import sys


def read_ppm(path):
    with open(path, "rb") as f:
        magic = f.readline().strip()
        if magic not in (b"P5", b"P6"):
            raise ValueError(f"{path}: not PPM/PGM ({magic!r})")
        dims = b""
        while len(dims.split()) < 2:
            line = f.readline()
            if not line:
                raise ValueError(f"{path}: truncated header")
            if line.startswith(b"#"):
                continue
            dims += line
        w, h = map(int, dims.split()[:2])
        maxval = int(f.readline().strip())
        if magic == b"P5":
            if maxval <= 255:
                data = f.read(w * h)
                px = list(data)
            else:
                raw = f.read(w * h * 2)
                px = [struct.unpack(">H", raw[i : i + 2])[0] / 65535.0 for i in range(0, len(raw), 2)]
            return w, h, "gray", px
        data = f.read(w * h * 3)
        px = [(data[i], data[i + 1], data[i + 2]) for i in range(0, len(data), 3)]
        return w, h, "rgb", px


def luma_rgb(px):
    r, g, b = (c / 255.0 for c in px)
    return 0.2126 * r + 0.7152 * g + 0.0722 * b


def stats_gray(px):
    if not px:
        return {}
    return {
        "min": min(px),
        "max": max(px),
        "mean": sum(px) / len(px),
        "near0": sum(1 for v in px if v < 0.05) / len(px),
        "near1": sum(1 for v in px if v > 0.95) / len(px),
    }


def stats_luma(px_rgb):
    lum = [luma_rgb(p) for p in px_rgb]
    return stats_gray(lum)


def red_ratio(px_rgb, thresh=200):
    if not px_rgb:
        return 0.0
    red = sum(1 for r, g, b in px_rgb if r >= thresh and g < 64 and b < 64)
    return red / len(px_rgb)


def main():
    ap = argparse.ArgumentParser(description="Analyze shadow dump directory")
    ap.add_argument("dir", help="Output dir passed to --dump-shadow")
    args = ap.parse_args()
    d = args.dir
    if not os.path.isdir(d):
        print(f"not a directory: {d}", file=sys.stderr)
        return 1

    report = os.path.join(d, "report.txt")
    if os.path.isfile(report):
        print("--- report.txt ---")
        with open(report, encoding="utf-8", errors="replace") as f:
            print(f.read().rstrip())

    views = ["final", "shadow", "shadowmap", "shadowatlas", "shadowuv", "direct"]
    print("\n--- view statistics ---")
    lumas = {}
    for name in views:
        path = os.path.join(d, f"view_{name}.ppm")
        if not os.path.isfile(path):
            print(f"{name}: MISSING")
            continue
        w, h, kind, px = read_ppm(path)
        if kind == "rgb":
            st = stats_luma(px)
            lumas[name] = st["mean"]
            extra = ""
            if name == "shadowuv":
                extra = f" red_oob={red_ratio(px):.3f}"
            print(
                f"{name}: {w}x{h} luma min={st['min']:.4f} max={st['max']:.4f} "
                f"mean={st['mean']:.4f} nearBlack={st['near0']:.3f} nearWhite={st['near1']:.3f}{extra}"
            )
        else:
            st = stats_gray(px)
            print(f"{name}: {w}x{h} gray min={st['min']:.4f} max={st['max']:.4f} mean={st['mean']:.4f}")

    if "direct" in lumas:
        baseline = lumas.get("final")
        if baseline is not None:
            print(f"\ndirect vs final mean luma delta = {baseline - lumas['direct']:.4f}")

    print("\n--- cascade depth (PGM) ---")
    for ci in range(4):
        path = os.path.join(d, f"cascade_{ci}_depth.pgm")
        if not os.path.isfile(path):
            print(f"cascade {ci}: MISSING")
            continue
        w, h, kind, px = read_ppm(path)
        st = stats_gray(px)
        print(
            f"cascade {ci}: {w}x{h} normDepth min={st['min']:.4f} max={st['max']:.4f} "
            f"mean={st['mean']:.4f} near0={st['near0']:.3f} near1={st['near1']:.3f}"
        )

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
