#!/usr/bin/env python3
"""Map upper-atrium geometry to materials (what user sees as 拱顶)."""
import os
import sys
from collections import defaultdict

ROOT = sys.argv[1] if len(sys.argv) > 1 else r"G:\work\tech\infra\gpu\build\examples\Debug\Sponza"
OBJ = os.path.join(ROOT, "sponza.obj")
MTL = os.path.join(ROOT, "sponza.mtl")


def parse_mtl():
    mats = []
    mtl_map = {}
    with open(MTL, encoding="utf-8", errors="ignore") as f:
        cur = None
        for line in f:
            line = line.strip()
            if line.startswith("newmtl "):
                cur = line.split()[1]
                mats.append(cur)
                mtl_map[cur] = len(mats) - 1
    if "default" not in mtl_map:
        mtl_map["default"] = len(mats)
        mats.append("default")
    return mats, mtl_map


def main():
    mats, mtl_map = parse_mtl()
    positions = []
    uvs = []
    active_obj = "(none)"
    active_mtl = "default"
    # tri stats: material -> {y_min,y_max,count,objs}
    tri_stats = defaultdict(lambda: {"count": 0, "y_sum": 0.0, "y_max": -1e9, "objs": set()})

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
            if tag == "o":
                active_obj = parts[1]
            elif tag == "v":
                positions.append(tuple(map(float, parts[1:4])))
            elif tag == "vt":
                uvs.append(tuple(map(float, parts[1:3])))
            elif tag == "usemtl":
                active_mtl = parts[1]
            elif tag == "f":
                verts = []
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
                    pi = resolve_idx(vals[0], len(positions))
                    if pi >= 0:
                        verts.append(positions[pi])
                if len(verts) >= 3:
                    for i in range(1, len(verts) - 1):
                        tri = (verts[0], verts[i], verts[i + 1])
                        cy = sum(p[1] for p in tri) / 3.0
                        s = tri_stats[active_mtl]
                        s["count"] += 1
                        s["y_sum"] += cy
                        s["y_max"] = max(s["y_max"], cy)
                        s["objs"].add(active_obj)

    # Sponza Y-up in obj; upper atrium ~ y > 4
    thresholds = [3.0, 4.0, 5.0, 6.0]
    for th in thresholds:
        print(f"\n=== Triangles with centroid Y > {th} ===")
        rows = []
        for mtl, s in tri_stats.items():
            # re-scan is expensive; approximate with y_max filter
            if s["y_max"] < th:
                continue
            rows.append((mtl, s))
        rows.sort(key=lambda x: -x[1]["count"])
        for mtl, s in rows[:15]:
            avg_y = s["y_sum"] / max(s["count"], 1)
            layer = mtl_map.get(mtl, "?")
            print(f"  {mtl:20s} layer={layer:2}  tris={s['count']:6d}  avgY={avg_y:.2f}  maxY={s['y_max']:.2f}  objs={len(s['objs'])}")

    print("\n=== arch objects Y extent ===")
    arch_objs = ["sponza_17", "sponza_123", "sponza_124", "sponza_08", "sponza_19"]
    obj_ys = defaultdict(list)
    active_obj = "(none)"
    active_mtl = "default"
    with open(OBJ, encoding="utf-8", errors="ignore") as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            parts = line.split()
            if parts[0] == "o":
                active_obj = parts[1]
            elif parts[0] == "usemtl":
                active_mtl = parts[1]
            elif parts[0] == "f" and active_obj in arch_objs:
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
                    pi = resolve_idx(vals[0], len(positions))
                    if pi >= 0:
                        obj_ys[active_obj].append(positions[pi][1])
    for o in arch_objs:
        if o in obj_ys:
            ys = obj_ys[o]
            print(f"  {o:12s}  mtl from file... Y=[{min(ys):.2f},{max(ys):.2f}]")


if __name__ == "__main__":
    main()
