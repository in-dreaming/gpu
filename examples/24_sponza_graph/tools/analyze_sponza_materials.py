#!/usr/bin/env python3
"""Precise Sponza material / object / texture analysis for 24_sponza_graph."""
import os
import sys
from collections import defaultdict

ROOT = sys.argv[1] if len(sys.argv) > 1 else r"G:\work\tech\infra\gpu\build\examples\Debug\Sponza"
MTL = os.path.join(ROOT, "sponza.mtl")
OBJ = os.path.join(ROOT, "sponza.obj")


def resolve_texture_path(root, material_path):
    token = material_path.strip().split()[-1].replace("\\", "/")
    if not token:
        return None
    candidates = [
        os.path.join(root, token),
        os.path.join(root, "textures", os.path.basename(token)),
    ]
    for c in candidates:
        if os.path.isfile(c):
            return c
    return None


def main():
    # --- MTL: material table (parseMtl order) ---
    materials = []
    mtl_map = {}
    with open(MTL, encoding="utf-8", errors="ignore") as f:
        cur = None
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            if line.startswith("newmtl "):
                cur = line.split()[1]
                materials.append({"name": cur, "baseColor": "", "index": len(materials)})
                mtl_map[cur] = materials[-1]
            elif line.startswith("map_Kd ") and cur:
                mtl_map[cur]["baseColor"] = line[7:].strip()

    # parseObj adds "default" at start if not in mtl
    scene_mats = [m["name"] for m in materials]
    if "default" not in scene_mats:
        pass  # parseObj adds default via addMaterial at line 48 before mtl names... 
    # Actually: parseMtl runs first filling scene.materials, then parseObj does activeMat = addMaterial("default")
    # If default not in mtl, it gets appended. Mtl has 25 materials, default appended = 26 total.

    print("=== MATERIAL TABLE (layer index = vertex.material = texture array layer) ===")
    loaded = 0
    for i, m in enumerate(materials):
        name = m["name"]
        bc = mtl_map[name]["baseColor"]
        path = resolve_texture_path(ROOT, bc) if bc else None
        if path:
            status = "LOADED"
            loaded += 1
        elif not bc:
            status = "FALLBACK_COLOR"
        else:
            status = "FILE_MISSING"
        print(f"  [{i:2d}] {name:22s}  map_Kd={bc or '-':42s}  {status}")
        if path:
            print(f"       -> {path}")

    # default material from parseObj
    default_idx = None
    print("\n  Note: parseObj also calls addMaterial('default') at load time.")
    print("  Runtime scene has 26 materials; 'default' is index 25 if not in mtl.")

    # --- OBJ: per-object per-material triangle counts ---
    active_obj = "(none)"
    active_mtl = "default"
    obj_mtl_tris = defaultdict(lambda: defaultdict(int))
    mtl_tris = defaultdict(int)
    mtl_objects = defaultdict(set)
    draw_segments = []  # (obj, mtl, tri_count) per usemtl segment

    with open(OBJ, encoding="utf-8", errors="ignore") as f:
        seg_tris = 0
        seg_obj = active_obj
        seg_mtl = active_mtl
        for line in f:
            if line.startswith("o "):
                if seg_tris > 0:
                    draw_segments.append((seg_obj, seg_mtl, seg_tris))
                active_obj = line[2:].strip()
                seg_obj = active_obj
                seg_tris = 0
            elif line.startswith("usemtl "):
                if seg_tris > 0:
                    draw_segments.append((seg_obj, seg_mtl, seg_tris))
                active_mtl = line.split()[1]
                seg_mtl = active_mtl
                seg_tris = 0
            elif line.startswith("f "):
                nv = len(line.split()) - 1
                tris = max(0, nv - 2)
                seg_tris += tris
                obj_mtl_tris[active_obj][active_mtl] += tris
                mtl_tris[active_mtl] += tris
                mtl_objects[active_mtl].add(active_obj)
        if seg_tris > 0:
            draw_segments.append((seg_obj, seg_mtl, seg_tris))

    # Build runtime material index (parseMtl order + default at end)
    runtime_mats = [m["name"] for m in materials]
    runtime_mats.append("default")  # parseObj always adds at start but if exists... 
    # parseObj: activeMat = addMaterial("default") FIRST - so default is index 0 if not in mtl!
    # parseMtl runs FIRST in main.cpp, THEN parseObj.
    # Order: parseMtl adds 25 materials (indices 0-24), parseObj starts with addMaterial("default")
    # addMaterial checks if name exists - default NOT in mtl, so appended as index 25.

    def layer_index(mtl_name):
        for i, m in enumerate(materials):
            if m["name"] == mtl_name:
                return i
        if mtl_name == "default":
            return len(materials)  # 25
        return -1

    print("\n=== MATERIALS WITH NO TEXTURE (FALLBACK only, NOT black in albedo) ===")
    for m in materials:
        if not m["baseColor"]:
            idx = m["index"]
            print(f"  layer [{idx}] {m['name']}: {mtl_tris[m['name']]} tris")

    print("\n=== ARCH MATERIAL (layer 4) ===")
    arch_idx = layer_index("arch")
    bc = mtl_map["arch"]["baseColor"]
    path = resolve_texture_path(ROOT, bc)
    print(f"  index={arch_idx}, map_Kd={bc}")
    print(f"  file={path}, exists={bool(path)}")
    print(f"  triangles={mtl_tris['arch']}, object groups={len(mtl_objects['arch'])}")

    print("\n=== CEILING MATERIAL (often confused with arch soffit) ===")
    ceil_idx = layer_index("ceiling")
    bc = mtl_map["ceiling"]["baseColor"]
    path = resolve_texture_path(ROOT, bc)
    print(f"  index={ceil_idx}, map_Kd={bc}")
    print(f"  file={path}, triangles={mtl_tris['ceiling']}")

    print("\n=== BACKGROUND MATERIAL (window openings / far plane) ===")
    bg_idx = layer_index("Material__298")
    bc = mtl_map.get("Material__298", {}).get("baseColor", "")
    path = resolve_texture_path(ROOT, bc) if bc else None
    print(f"  index={bg_idx}, map_Kd={bc}")
    print(f"  file={path}, triangles={mtl_tris.get('Material__298', 0)}")

    print("\n=== Material__47 (NO map_Kd) ===")
    print(f"  index={layer_index('Material__47')}, triangles={mtl_tris.get('Material__47', 0)}")

    print("\n=== OBJECTS using material 'arch' (all groups) ===")
    arch_objs = sorted(mtl_objects["arch"])
    for obj in arch_objs:
        print(f"  {obj}: {obj_mtl_tris[obj]['arch']} tris")

    print("\n=== OBJECTS using material 'ceiling' near upper balcony ===")
    for obj in sorted(mtl_objects["ceiling"]):
        if "arch" in obj.lower() or "sponza" in obj.lower():
            print(f"  {obj}: {obj_mtl_tris[obj]['ceiling']} tris")

    print("\n=== BLACK REGION CANDIDATES: materials on curved upper structures ===")
    suspects = ["arch", "ceiling", "Material__298", "Material__47", "roof", "background"]
    for mtl in suspects:
        if mtl not in mtl_tris:
            continue
        idx = layer_index(mtl)
        bc = mtl_map.get(mtl, {}).get("baseColor", "")
        path = resolve_texture_path(ROOT, bc) if bc else None
        tex = "LOADED" if path else ("NO_MAP_KD" if not bc else "MISSING")
        print(f"  [{idx:2d}] {mtl:20s} tris={mtl_tris[mtl]:6d} texture={tex}")

    print("\n=== DRAW BATCHES (usemtl segments) for arch-only objects ===")
    arch_segs = [(o, m, t) for o, m, t in draw_segments if m == "arch"]
    print(f"  Total arch draw segments: {len(arch_segs)}")
    for o, m, t in sorted(arch_segs, key=lambda x: -x[2])[:20]:
        print(f"    {o}: {t} tris")

    print("\n=== SIMULATE RUNTIME: which layers fail texture load ===")
    # Match createSponzaMaterialTextures logic
    lc = len(materials) + 1  # + default
    runtime_materials = [m["name"] for m in materials] + ["default"]
    load_ok = 0
    for i, name in enumerate(runtime_materials):
        m = mtl_map.get(name, {"baseColor": ""})
        bc = m["baseColor"] if name != "default" else ""
        path = resolve_texture_path(ROOT, bc) if bc else None
        if path:
            load_ok += 1
            st = "LOADED"
        elif not bc:
            st = "FALLBACK"
        else:
            st = "RESOLVE_FAIL"
        if st != "LOADED":
            print(f"  layer [{i}] {name}: {st} (tris={mtl_tris.get(name, 0)})")
    print(f"  Runtime: {load_ok} loaded / {lc} layers (log says 24 loaded / 26)")

    # Analyze arch texture pixel brightness at mesh UVs
    try:
        from PIL import Image
        import statistics

        img = Image.open(os.path.join(ROOT, "textures", "sponza_arch_diff.tga")).convert("RGB")
        w, h = img.size
        px = img.load()
        texcoords = []
        with open(OBJ, encoding="utf-8", errors="ignore") as f:
            for line in f:
                if line.startswith("vt "):
                    _, u, v = line.split()[:3]
                    texcoords.append((float(u), float(v)))

        def sample(u, v):
            u, v = u % 1.0, v % 1.0
            x = min(int(u * w), w - 1)
            y = min(int(v * h), h - 1)
            r, g, b = px[x, y]
            return (r + g + b) / 3.0

        samples = []
        with open(OBJ, encoding="utf-8", errors="ignore") as f:
            active = None
            for line in f:
                if line.startswith("usemtl "):
                    active = line.split()[1]
                elif line.startswith("f ") and active == "arch":
                    for p in line.split()[1:]:
                        ti = int(p.split("/")[1]) - 1
                        u, v = texcoords[ti]
                        samples.append(sample(u, v))

        dark = sum(1 for s in samples if s < 30)
        print("\n=== ARCH TEXTURE UV SAMPLING (CPU, layer 4 file) ===")
        print(f"  arch face UV samples: {len(samples)}")
        print(f"  mean brightness: {statistics.mean(samples):.1f} / 255")
        print(f"  samples with brightness < 30: {dark} ({100*dark/len(samples):.1f}%)")
        print(f"  -> If shader samples correctly, {100*dark/len(samples):.1f}% of arch pixels are NEAR BLACK from texture data")
    except ImportError:
        pass

    # --- Vertex material index integrity on arch / Material__298 ---
    print("\n=== VERTEX MATERIAL INDEX INTEGRITY ===")
    name_to_idx = {m["name"]: m["index"] for m in materials}
    if "default" not in name_to_idx:
        name_to_idx["default"] = len(materials)

    positions, texcoords, normals = [], [], []
    with open(OBJ, encoding="utf-8", errors="ignore") as f:
        for line in f:
            if line.startswith("v "):
                p = line.split()
                positions.append((float(p[1]), float(p[2]), float(p[3])))
            elif line.startswith("vt "):
                p = line.split()
                texcoords.append((float(p[1]), float(p[2])))
            elif line.startswith("vn "):
                p = line.split()
                normals.append((float(p[1]), float(p[2]), float(p[3])))

    def resolve_idx(idx, count):
        if idx > 0:
            return idx - 1
        if idx < 0:
            return count + idx
        return -1

    vmap = {}
    arch_idx = name_to_idx["arch"]
    m298_idx = name_to_idx["Material__298"]
    wrong_arch = 0
    arch_refs = 0

    with open(OBJ, encoding="utf-8", errors="ignore") as f:
        active_mtl = "default"
        active_mat = name_to_idx.get("default", 25)
        for line in f:
            line = line.strip()
            if line.startswith("usemtl "):
                active_mtl = line.split()[1]
                active_mat = name_to_idx[active_mtl]
            elif line.startswith("f "):
                for tok in line.split()[1:]:
                    parts = tok.split("/")
                    vi = resolve_idx(int(parts[0]), len(positions))
                    ti = resolve_idx(int(parts[1]), len(texcoords)) if len(parts) > 1 and parts[1] else -1
                    ni = resolve_idx(int(parts[2]), len(normals)) if len(parts) > 2 and parts[2] else -1
                    key = (vi, ti, ni, active_mat)
                    if key not in vmap:
                        vmap[key] = active_mat
                    stored = vmap[key]
                    if active_mtl == "arch":
                        arch_refs += 1
                        if stored != arch_idx:
                            wrong_arch += 1

    print(f"  arch material index = {arch_idx}")
    print(f"  Material__298 index = {m298_idx}")
    print(f"  arch face vertex refs = {arch_refs}, wrong material stored = {wrong_arch}")

    try:
        from PIL import Image
        import statistics

        def uv_black_rate(mtl_name, tex_file):
            img = Image.open(os.path.join(ROOT, "textures", tex_file)).convert("RGB")
            w, h = img.size
            px = img.load()
            samples = []
            zeros = 0
            with open(OBJ, encoding="utf-8", errors="ignore") as f:
                active = None
                for line in f:
                    if line.startswith("usemtl "):
                        active = line.split()[1]
                    elif line.startswith("f ") and active == mtl_name:
                        for p in line.split()[1:]:
                            ti = int(p.split("/")[1]) - 1
                            u, v = texcoords[ti]
                            u, v = u % 1.0, v % 1.0
                            x, y = int(u * w) % w, int(v * h) % h
                            r, g, b = px[x, y]
                            lum = r + g + b
                            samples.append(lum)
                            if lum == 0:
                                zeros += 1
            if not samples:
                return
            print(f"\n=== {mtl_name} ({tex_file}) UV -> TEXEL ===")
            print(f"  samples={len(samples)} mean_lum={statistics.mean(samples)/3:.1f}/255")
            print(f"  min_lum={min(samples)/3:.1f}/255  pure_black_samples={zeros} ({100*zeros/len(samples):.2f}%)")

        uv_black_rate("arch", "sponza_arch_diff.tga")
        uv_black_rate("Material__298", "background.tga")
        uv_black_rate("ceiling", "sponza_ceiling_a_diff.tga")
    except ImportError:
        pass

    print("\n=== CONCLUSION ===")
    print("  Missing textures: ONLY Material__47 (layer 2, 18 tris) and default (layer 25, 0 tris)")
    print("  Arch layer 4: sponza_arch_diff.tga LOADED - NOT a missing texture")
    print("  Window/far quads: Material__298 layer 1, background.tga LOADED")
    print("  Black window openings = Material__298 geometry + dark background.tga texels")
    print("  Arch soffit darkness = UV hits dark padding in sponza_arch_diff.tga (~74% pixels < 30/255)")


if __name__ == "__main__":
    main()
