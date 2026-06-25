#!/usr/bin/env python3
"""CPU replay CSM for simple scene default camera."""
import math

cam_pos = (5.8, 4.2, -5.2)
target = (0.0, 0.85, 0.2)
delta = tuple(target[i] - cam_pos[i] for i in range(3))
horiz = math.sqrt(delta[0] ** 2 + delta[2] ** 2)
yaw = math.atan2(delta[0], delta[2])
pitch = math.atan2(delta[1], horiz)
sy, cy = math.sin(yaw), math.cos(yaw)
sp, cp = math.sin(pitch), math.cos(pitch)
fwd = (sy * cp, sp, cy * cp)
L = math.sqrt(sum(x * x for x in fwd))
fwd = tuple(x / L for x in fwd)
rgt = (cy, 0.0, -sy)
up = (
    fwd[1] * rgt[2] - fwd[2] * rgt[1],
    fwd[2] * rgt[0] - fwd[0] * rgt[2],
    fwd[0] * rgt[1] - fwd[1] * rgt[0],
)
L = math.sqrt(sum(x * x for x in up))
up = tuple(x / L for x in up)

aspect = 1280 / 720
fov = 60.0
near_z, far_z = 0.4, 60.0
f = 1.0 / math.tan(fov * 0.5 * math.pi / 180.0)
proj = (f / aspect, f)

dir_light = (0.45, -0.88, 0.15)
L = math.sqrt(sum(x * x for x in dir_light))
light_fwd = tuple(x / L for x in dir_light)

bounds_min = (-4, 0, -4)
bounds_max = (4, 2.2, 4)


def dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def sub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def add(a, b):
    return (a[0] + b[0], a[1] + b[1], a[2] + b[2])


def mul(v, s):
    return (v[0] * s, v[1] * s, v[2] * s)


def mat4_mul(a, b):
    out = [0.0] * 16
    for c in range(4):
        for r in range(4):
            s = 0.0
            for k in range(4):
                s += a[k * 4 + r] * b[c * 4 + k]
            out[c * 4 + r] = s
    return out


def mat4_transform(m, p):
    x = m[0] * p[0] + m[4] * p[1] + m[8] * p[2] + m[12]
    y = m[1] * p[0] + m[5] * p[1] + m[9] * p[2] + m[13]
    z = m[2] * p[0] + m[6] * p[1] + m[10] * p[2] + m[14]
    w = m[3] * p[0] + m[7] * p[1] + m[11] * p[2] + m[15]
    return (x, y, z, w)


def build_light_basis(light_forward):
    forward = light_forward
    world_up = (0.0, 1.0, 0.0)
    if abs(dot(forward, world_up)) > 0.95:
        world_up = (0.0, 0.0, 1.0)
    right = normalize(cross(world_up, forward))
    up = normalize(cross(forward, right))
    return right, up, forward


def cross(a, b):
    return (
        a[1] * b[2] - a[2] * b[1],
        a[2] * b[0] - a[0] * b[2],
        a[0] * b[1] - a[1] * b[0],
    )


def normalize(v):
    L = math.sqrt(dot(v, v))
    return (v[0] / L, v[1] / L, v[2] / L) if L > 1e-12 else (0, 1, 0)


def build_frustum_corners(near, far):
    half_wn = near / proj[0]
    half_hn = near / proj[1]
    half_wf = far / proj[0]
    half_hf = far / proj[1]
    nc = add(cam_pos, mul(fwd, near))
    fc = add(cam_pos, mul(fwd, far))
    corners = []
    for ho, center in [
        ((half_wn, half_hn), nc),
        ((-half_wn, half_hn), nc),
        ((-half_wn, -half_hn), nc),
        ((half_wn, -half_hn), nc),
        ((half_wf, half_hf), fc),
        ((-half_wf, half_hf), fc),
        ((-half_wf, -half_hf), fc),
        ((half_wf, -half_hf), fc),
    ]:
        off = add(mul(rgt, ho[0]), mul(up, ho[1]))
        corners.append(add(center, off))
    return corners


def splits(near, far, count):
    out = [near]
    lam = 0.5
    for i in range(1, count + 1):
        p = i / count
        log_s = near * (far / near) ** p
        uni_s = near + (far - near) * p
        out.append(lam * log_s + (1 - lam) * uni_s)
    return out


def expand_bounds(center, right, up, forward, bmin, bmax, min_x, max_x, min_y, max_y, min_z, max_z):
    corners = []
    for x in (bmin[0], bmax[0]):
        for y in (bmin[1], bmax[1]):
            for z in (bmin[2], bmax[2]):
                corners.append((x, y, z))
    for c in corners:
        d = sub(c, center)
        lx, ly, lz = dot(d, right), dot(d, up), dot(d, forward)
        min_x = min(min_x, lx)
        max_x = max(max_x, lx)
        min_y = min(min_y, ly)
        max_y = max(max_y, ly)
        min_z = min(min_z, lz)
        max_z = max(max_z, lz)
    return min_x, max_x, min_y, max_y, min_z, max_z


right, up, forward = build_light_basis(light_fwd)
sp = splits(near_z, far_z, 4)
print("camera fwd", tuple(round(x, 3) for x in fwd))
print("splits", [round(x, 1) for x in sp])

test_pts = [
    (0, 0.01, 0),
    (0, 2, 0),
    (-2.0, 0.35, 0.8),
    (1.9, 0.45, -0.7),
    cam_pos,
    target,
]

for ci in range(4):
    corners = build_frustum_corners(sp[ci], sp[ci + 1])
    center = mul(add(add(add(add(add(add(add(corners[0], corners[1]), corners[2]), corners[3]), corners[4]), corners[5]), corners[6]), corners[7]), 1 / 8)
    min_x = min_y = min_z = 1e9
    max_x = max_y = max_z = -1e9
    for c in corners:
        d = sub(c, center)
        lx, ly, lz = dot(d, right), dot(d, up), dot(d, forward)
        min_x = min(min_x, lx)
        max_x = max(max_x, lx)
        min_y = min(min_y, ly)
        max_y = max(max_y, ly)
        min_z = min(min_z, lz)
        max_z = max(max_z, lz)
    min_x, max_x, min_y, max_y, min_z, max_z = expand_bounds(
        center, right, up, forward, bounds_min, bounds_max, min_x, max_x, min_y, max_y, min_z, max_z
    )
    ext = max((max_x - min_x) * 0.5, (max_y - min_y) * 0.5)
    max_extent = ext
    min_x, max_x = -max_extent, max_extent
    min_y, max_y = -max_extent, max_extent
    min_z -= 150
    max_z += 400
    pad = max_extent * 0.08
    min_x -= pad
    max_x += pad
    min_y -= pad
    max_y += pad
    view = [0] * 16
    view[0], view[1], view[2] = right
    view[4], view[5], view[6] = up
    view[8], view[9], view[10] = forward
    view[12] = -dot(right, center)
    view[13] = -dot(up, center)
    view[14] = -dot(forward, center)
    view[15] = 1.0
    proj_m = [0] * 16
    proj_m[0] = 2 / (max_x - min_x)
    proj_m[5] = 2 / (max_y - min_y)
    proj_m[10] = 1 / (max_z - min_z)
    proj_m[14] = -min_z / (max_z - min_z)
    proj_m[15] = 1.0
    vp = mat4_mul(proj_m, view)
    print(f"cascade {ci} splitFar={sp[ci+1]:.1f}")
    for p in test_pts:
        clip = mat4_transform(vp, (*p, 1.0))
        ndc = (clip[0] / clip[3], clip[1] / clip[3], clip[2] / clip[3])
        inside = -1 <= ndc[0] <= 1 and -1 <= ndc[1] <= 1 and 0 <= ndc[2] <= 1
        print(f"  p={p} ndc=({ndc[0]:.3f},{ndc[1]:.3f},{ndc[2]:.3f}) in={inside}")
