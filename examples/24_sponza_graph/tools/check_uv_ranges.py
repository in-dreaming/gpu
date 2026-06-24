#!/usr/bin/env python3
"""Compare UV ranges and frac() impact for arch vs column materials."""
import os
import sys
from collections import defaultdict

try:
    from PIL import Image
except ImportError:
    Image = None

ROOT = sys.argv[1] if len(sys.argv) > 1 else r"G:\work\tech\infra\gpu\build\examples\Debug\Sponza"
OBJ = os.path.join(ROOT, "sponza.obj")
MTL = os.path.join(ROOT, "sponza.mtl")


def resolve_texture_path(root, material_path):
    token = material_path.strip().split()[-1].replace("\\", "/")
    if not token:
        return None
    for c in [os.path.join(root, token), os.path.join(root, "textures", os.path.basename(token))]:
        if os.path.isfile(c):
            return c
    return None


def parse_mtl():
    mats = []
    mtl_map = {}
    bc = {}
    with open(MTL, encoding="utf-8", errors="ignore") as f:
        cur = None
        for line in f:
            line = line.strip()
            if line.startswith("newmtl "):
                cur = line.split()[1]
                mats.append(cur)
                mtl_map[cur] = len(mats) - 1
                bc[cur] = ""
            elif line.startswith("map_Kd ") and cur:
                bc[cur] = line[7:].strip()
    if "default" not in mtl_map:
        mtl_map["default"] = len(mats)
        mats.append("default")
    return mtl_map, bc


def load_img(path):
    if not Image or not path:
        return None
    return Image.open(path).convert("RGBA")


def sample_lum(img, u, v):
    w, h = img.size
    x = min(max(int(u * w) % w, 0), w - 1)
    y = min(max(int((1.0 - v) * h) % h, 0), h - 1)  # OBJ v often flipped; test both
    r, g, b, _ = img.getpixel((x, y))
    return (r + g + b) / 3.0


def sample_lum_vflip(img, u, v):
    w, h = img.size
    x = min(max(int(u * w), 0), w - 1)
    y = min(max(int(v * h), 0), h - 1)
    r, g, b, _ = img.getpixel((x, y))
    return (r + g + b) / 3.0


def main():
    mtl_map, bc = parse_mtl()
    uvs = []
    active_mtl = "default"
    face_uvs = defaultdict(list)

    def resolve_idx(idx, n):
        if idx > 0:
            return idx - 1
        if idx < 0:
            return n + idx
        return -1

    with open(OBJ, encoding="utf-8", errors="ignore") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            parts = line.split()
            tag = parts[0]
            if tag == "vt":
                uvs.append(tuple(map(float, parts[1:3])))
            elif tag == "usemtl":
                active_mtl = parts[1]
            elif tag == "f":
                for tok in parts[1:]:
                    vals = [0, 0, 0]
                    vi = 0
                    cur = ""
                    for ch in tok + "/":
                        if ch == "/":
                            if cur:
                                vals[vi] = int(cur)
                                cur = ""
                                vi += 1
                        else:
                            cur += ch
                    ti = resolve_idx(vals[1], len(uvs))
                    if ti >= 0:
                        face_uvs[active_mtl].append(uvs[ti])

    targets = ["arch", "column_a", "column_b", "ceiling", "bricks"]
    print("=== UV RANGES (raw OBJ vt) ===")
    for name in targets:
        if name not in face_uvs:
            continue
        us = [p[0] for p in face_uvs[name]]
        vs = [p[1] for p in face_uvs[name]]
        print(f"{name:12s} layer={mtl_map[name]:2d}  U=[{min(us):.4f},{max(us):.4f}]  V=[{min(vs):.4f},{max(vs):.4f}]  "
              f"outside01={sum(1 for u,v in face_uvs[name] if u<0 or u>1 or v<0 or v>1)}/{len(us)}")

    if not Image:
        print("\nPIL not installed, skipping texture sampling")
        return

    print("\n=== BRIGHTNESS: raw UV vs frac(UV) on full-res texture ===")
    for name in targets:
        path = resolve_texture_path(ROOT, bc.get(name, ""))
        if not path or name not in face_uvs:
            continue
        img = load_img(path)
        raw = []
        fracd = []
        for u, v in face_uvs[name]:
            u2 = u - int(u) if u >= 0 else u - int(u) + 1  # frac positive
            v2 = v - int(v) if v >= 0 else v - int(v) + 1
            # try no vflip first (stb/direct)
            raw.append(sample_lum_vflip(img, u, v))
            fracd.append(sample_lum_vflip(img, u2, v2))
        raw.sort()
        fracd.sort()
        print(f"{name:12s}  raw mean={sum(raw)/len(raw):.1f} min={raw[0]:.1f} p50={raw[len(raw)//2]:.1f}")
        print(f"{'':12s}  frac mean={sum(fracd)/len(fracd):.1f} min={fracd[0]:.1f} p50={fracd[len(fracd)//2]:.1f}")
        print(f"{'':12s}  texture={os.path.basename(path)} size={img.size}")


if __name__ == "__main__":
    main()
