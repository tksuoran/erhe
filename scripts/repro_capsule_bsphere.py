# Faithful Python port of erhe::math::optimal_enclosing_sphere (sphere.cpp,
# adapted from MathGeoLib) run on the capsule brush vertex set, to measure how
# far off the Y axis the computed bounding sphere center lands.
#
# Capsule brush (scene_builder.cpp make_capsule_brushes): radius=1, length=2,
# slice_count = 8*detail, hemisphere_stack_count = 3*detail.
# Vertex order (capsule.cpp Capsule_builder::build): rings for stacks
# 1..stack_count-1 bottom-up (slice-major), then bottom pole, then top pole.

import math
import numpy as np

S_EPSILON = 5e-3

def v(x, y, z):
    return np.array([x, y, z], dtype=np.float64)

def dist2(a, b):
    d = a - b
    return float(np.dot(d, d))

def contains(center, radius, p, eps=0.0):
    return dist2(center, p) <= radius * radius + eps

def fit_sphere_2(ab, ac):
    BB = float(np.dot(ab, ab)); CC = float(np.dot(ac, ac)); BC = float(np.dot(ab, ac))
    denom = BB * CC - BC * BC
    if abs(denom) <= 5e-3:
        return None
    denom = 0.5 / denom
    s = (CC * BB - BC * CC) * denom
    t = (CC * BB - BC * BB) * denom
    return s, t

def fit_sphere_3(ab, ac, ad):
    BB = float(np.dot(ab, ab)); BC = float(np.dot(ab, ac)); BD = float(np.dot(ab, ad))
    CC = float(np.dot(ac, ac)); CD = float(np.dot(ac, ad)); DD = float(np.dot(ad, ad))
    m = np.array([[BB, BC, BD], [BC, CC, CD], [BD, CD, DD]], dtype=np.float64)
    det = np.linalg.det(m)
    if abs(det) <= 1e-6:
        return None
    sol = np.linalg.solve(m, np.array([BB * 0.5, CC * 0.5, DD * 0.5]))
    return float(sol[0]), float(sol[1]), float(sol[2])

def oes2(a, b):
    center = (a + b) * 0.5
    radius = math.sqrt(dist2(b, center)) + S_EPSILON
    return center, radius

def oes3(a, b, c):
    ab = b - a; ac = c - a
    are_collinear = float(np.dot(np.cross(ab, ac), np.cross(ab, ac))) < 1e-4
    fit = None if are_collinear else fit_sphere_2(ab, ac)
    if fit is None or abs(fit[0]) > 10000.0 or abs(fit[1]) > 10000.0:
        min_pt = np.minimum(np.minimum(a, b), c)
        max_pt = np.maximum(np.maximum(a, b), c)
        center = (min_pt + max_pt) * 0.5
        radius = math.sqrt(dist2(center, min_pt))
    else:
        s, t = fit
        if s < 0.0:
            center = (a + c) * 0.5
            radius = max(math.sqrt(dist2(a, c)) * 0.5, math.sqrt(dist2(b, center)))
        elif t < 0.0:
            center = (a + b) * 0.5
            radius = max(math.sqrt(dist2(a, b)) * 0.5, math.sqrt(dist2(c, center)))
        elif s + t > 1.0:
            center = (b + c) * 0.5
            radius = max(math.sqrt(dist2(b, c)) * 0.5, math.sqrt(dist2(a, center)))
        else:
            center = a + s * ab + t * ac
            radius = math.sqrt(max(dist2(center, a), dist2(center, b), dist2(center, c)))
    return center, radius + 2.0 * S_EPSILON

def oes4(a, b, c, d):
    ab = b - a; ac = c - a; ad = d - a
    fit = fit_sphere_3(ab, ac, ad)
    if fit is None or fit[0] < 0.0 or fit[1] < 0.0 or fit[2] < 0.0 or fit[0] + fit[1] + fit[2] > 1.0:
        center, radius = oes3(a, b, c)
        if not contains(center, radius, d):
            center, radius = oes3(a, b, d)
            if not contains(center, radius, c):
                center, radius = oes3(a, c, d)
                if not contains(center, radius, b):
                    center, radius = oes3(b, c, d)
                    radius = max(radius, math.sqrt(dist2(a, center)) + 1e-3)
    else:
        s, t, u = fit
        center = a + s * ab + t * ac + u * ad
        radius = math.sqrt(max(dist2(center, a), dist2(center, b), dist2(center, c), dist2(center, d)))
    return center, radius + 2.0 * S_EPSILON

def oes5(a, b, c, d, e):
    center, radius = oes4(b, c, d, e)
    if contains(center, radius, a, S_EPSILON):
        return center, radius, 0
    center, radius = oes4(a, c, d, e)
    if contains(center, radius, b, S_EPSILON):
        return center, radius, 1
    center, radius = oes4(a, b, d, e)
    if contains(center, radius, c, S_EPSILON):
        return center, radius, 2
    center, radius = oes4(a, b, c, e)
    if contains(center, radius, d, S_EPSILON):
        return center, radius, 3
    center, radius = oes4(a, b, c, d)
    return center, radius, 4

def optimal_enclosing_sphere(pts):
    n = len(pts)
    assert n > 4
    sp = [0, 1, 2, 3]
    expendable = [True, True, True, True]
    center, radius = oes4(pts[sp[0]], pts[sp[1]], pts[sp[2]], pts[sp[3]])
    r2 = radius * radius + S_EPSILON
    i = 4
    iterations = 0
    while i < n:
        iterations += 1
        if iterations > 200000:
            raise RuntimeError("no convergence")
        if i in sp:
            i += 1
            continue
        if dist2(pts[i], center) > r2:
            center, radius, redundant = oes5(pts[sp[0]], pts[sp[1]], pts[sp[2]], pts[sp[3]], pts[i])
            r2 = radius * radius + S_EPSILON
            if redundant != 4 and (sp[redundant] < i or expendable[redundant]):
                sp[redundant] = i
                expendable[redundant] = False
                for k in range(4):
                    if sp[k] < i:
                        expendable[k] = True
                i = 0
                continue
        i += 1
    return center, radius, sp

def make_capsule_points(radius, length, slice_count, hemisphere_stack_count):
    half_length = 0.5 * length
    m = float(hemisphere_stack_count)
    stack_count = 2 * hemisphere_stack_count + 1
    pts = []
    for stack in range(1, stack_count):
        for slice_ in range(slice_count):
            rel_slice = slice_ / slice_count
            phi = 2.0 * math.pi * rel_slice
            st = float(stack)
            if st <= m:
                theta = -0.5 * math.pi + 0.5 * math.pi * (st / m)
                center_y = -half_length
            elif st >= m + 1.0:
                theta = 0.5 * math.pi * ((st - (m + 1.0)) / m)
                center_y = half_length
            else:
                theta = 0.0
                center_y = -half_length + (st - m) * length
            x = radius * math.cos(theta) * math.cos(phi)
            y = center_y + radius * math.sin(theta)
            z = radius * math.cos(theta) * math.sin(phi)
            pts.append(np.array([np.float32(x), np.float32(y), np.float32(z)], dtype=np.float64))
    pts.append(v(0.0, -half_length - radius, 0.0))  # bottom pole
    pts.append(v(0.0, half_length + radius, 0.0))   # top pole
    return pts

def post_process(pts, center, radius):
    # Mirror of the post-processing added to erhe::math::calculate_bounding_volume:
    # try the bounding box center; keep it when the enclosing sphere centered
    # there is smaller than the algorithm's result.
    box_min = np.min(pts, axis=0)
    box_max = np.max(pts, axis=0)
    box_center = (box_min + box_max) * 0.5
    max_d2 = max(dist2(p, box_center) for p in pts)
    box_radius = math.sqrt(max_d2) + 1e-4
    if box_radius < radius:
        return box_center, box_radius, True
    return center, radius, False

def report(name, pts):
    center, radius, sp = optimal_enclosing_sphere(pts)
    off_axis = math.hypot(center[0], center[2])
    true_r = 2.0  # half_length + radius for radius=1, length=2
    print(f"{name}: {len(pts)} points")
    print(f"  center        = ({center[0]:+.6f}, {center[1]:+.6f}, {center[2]:+.6f})")
    print(f"  radius        = {radius:.6f}  (true minimal radius = {true_r})")
    print(f"  off-axis dist = {off_axis:.6f}  ({100.0 * off_axis / 1.0:.2f}% of capsule radius)")
    print(f"  support idx   = {sp}  (poles are the last two indices: {len(pts)-2}, {len(pts)-1})")
    pp_center, pp_radius, replaced = post_process(pts, center, radius)
    pp_off_axis = math.hypot(pp_center[0], pp_center[2])
    print(f"  post-processed: center = ({pp_center[0]:+.6f}, {pp_center[1]:+.6f}, {pp_center[2]:+.6f}),"
          f" radius = {pp_radius:.6f}, off-axis = {pp_off_axis:.6f}, box center used = {replaced}")
    print()

if __name__ == "__main__":
    for detail in (1, 2, 3, 4):
        pts = make_capsule_points(1.0, 2.0, 8 * detail, 3 * detail)
        report(f"capsule detail={detail} (slices={8*detail}, hemi_stacks={3*detail})", pts)
    # Contrast: UV-sphere-like point set (all points on the minimal sphere)
    sphere_pts = []
    for stack in range(1, 12):
        theta = -0.5 * math.pi + math.pi * stack / 12.0
        for slice_ in range(16):
            phi = 2.0 * math.pi * slice_ / 16.0
            sphere_pts.append(v(2.0 * math.cos(theta) * math.cos(phi), 2.0 * math.sin(theta), 2.0 * math.cos(theta) * math.sin(phi)))
    sphere_pts.append(v(0.0, -2.0, 0.0))
    sphere_pts.append(v(0.0, 2.0, 0.0))
    center, radius, sp = optimal_enclosing_sphere(sphere_pts)
    print(f"uv sphere contrast: center=({center[0]:+.6f},{center[1]:+.6f},{center[2]:+.6f}) radius={radius:.6f} off-axis={math.hypot(center[0],center[2]):.6f}")

    # Sanity: an asymmetric shape where the box center is a poor sphere center.
    # The minimal sphere of a diagonal rod plus an off-corner blob is centered
    # near the rod; the box center candidate must lose and be rejected.
    rng = [(-3.0, -3.0, -3.0), (3.0, 3.0, 3.0), (2.9, 3.0, 2.8), (-2.95, -3.0, -2.9),
           (0.0, 0.0, 0.0), (1.0, 1.1, 0.9), (-1.0, -0.9, -1.05), (2.0, 2.0, 2.0),
           (3.0, -0.5, 0.1), (0.2, 2.5, -0.3)]
    rod_pts = [v(*p) for p in rng]
    # pad to more than 4 points spread along the diagonal
    for k in range(20):
        t = -3.0 + 6.0 * k / 19.0
        rod_pts.append(v(t, t, t))
    center, radius, _ = optimal_enclosing_sphere(rod_pts)
    pp_center, pp_radius, replaced = post_process(rod_pts, center, radius)
    print(f"diagonal rod: algo radius={radius:.6f}, post radius={pp_radius:.6f}, box center used = {replaced}")
    # Verify containment of all points by the post-processed sphere
    worst = max(math.sqrt(dist2(p, pp_center)) for p in rod_pts)
    print(f"diagonal rod: farthest point distance = {worst:.6f} <= radius {pp_radius:.6f}: {worst <= pp_radius}")
