# Extracts the most recent shadow-fit debug dump JSON from logs/log.txt and
# summarizes the receiver-cull diagnostics: per-receiver in-frustum verdicts,
# presence of each receiver's corners along the hull -> clip -> re-hull chain,
# and per-caster cull verdicts with the rejecting plane.
import json
import sys

LOG_PATH = sys.argv[1] if len(sys.argv) > 1 else "logs/log.txt"

with open(LOG_PATH, "r", encoding="utf-8", errors="replace") as f:
    lines = f.readlines()

# Find the last dump marker, then the JSON block that follows it.
start = None
for i, line in enumerate(lines):
    if "Shadow fit debug dump" in line:
        start = i
if start is None:
    sys.exit("no dump found")

# JSON starts at the first line that is exactly "{" after the marker line
# (the marker line itself ends with "dump:"). Collect until braces balance.
json_lines = []
depth = 0
started = False
for line in lines[start:]:
    idx = line.find("{")
    if not started:
        if idx < 0:
            continue
        line = line[idx:]
        started = True
    json_lines.append(line)
    depth += line.count("{") - line.count("}")
    if started and depth == 0:
        break

dump = json.loads("".join(json_lines))

def fmt(v, nd=3):
    return "(" + ", ".join(f"{x:.{nd}f}" for x in v) + ")"

def aabb_corners(mn, mx):
    return [
        [mx[0] if (i & 1) else mn[0],
         mx[1] if (i & 2) else mn[1],
         mx[2] if (i & 4) else mn[2]]
        for i in range(8)
    ]

def point_near_set(p, point_set, tol=1e-3):
    return any(
        abs(p[0] - q[0]) <= tol and abs(p[1] - q[1]) <= tol and abs(p[2] - q[2]) <= tol
        for q in point_set
    )

def plane_distance(plane, p):
    return plane[0] * p[0] + plane[1] * p[1] + plane[2] * p[2] + plane[3]

print(f"view_camera: {dump['view_camera']}  shadow_range: {dump['shadow_range']}")
for light in dump["lights"]:
    if not light.get("valid", False):
        continue
    print(f"\n=== light: {light['name']} ===")
    print(f"receiver_hull_path_used: {light['receiver_hull_path_used']}  "
          f"receiver_box_path_used: {light['receiver_box_path_used']}")
    print(f"counts: receiver_boxes={light['receiver_box_count']} "
          f"hull_pts={light['receiver_hull_point_count']} "
          f"clipped_pts={light['receiver_clipped_point_count']} "
          f"clipped_hull_pts={light['receiver_clipped_hull_point_count']} "
          f"far_plane_hull={light['receiver_far_plane_hull_count']} "
          f"caster_boxes={light['caster_box_count']}")

    hull_pts         = light["receiver_hull_points"]
    clipped_pts      = light["receiver_clipped_points"]
    clipped_hull_pts = light["receiver_clipped_hull_points"]

    print("\nreceivers (per AABB: corner presence in hull / clipped / re-hulled sets):")
    for r in light["receiver_boxes"]:
        mn, mx = r["min"], r["max"]
        corners = aabb_corners(mn, mx)
        n_hull    = sum(point_near_set(c, hull_pts)         for c in corners)
        n_clip    = sum(point_near_set(c, clipped_pts)      for c in corners)
        n_rehull  = sum(point_near_set(c, clipped_hull_pts) for c in corners)
        print(f"  aabb {fmt(mn)}..{fmt(mx)} in_frustum={str(r['in_frustum']):5s} "
              f"corners-in: hull={n_hull}/8 clipped={n_clip}/8 rehull={n_rehull}/8")

    def show_points(name, pts):
        print(f"\n{name} ({len(pts)} points), sorted by x, with distance to the nearest view frustum plane:")
        for p in sorted(pts, key=lambda q: q[0]):
            ds = [plane_distance(pl, p) for pl in light["view_frustum_planes"]]
            worst = min(ds)
            print(f"  ({p[0]:8.3f}, {p[1]:8.3f}, {p[2]:8.3f})  min-frustum-dist={worst:8.4f} (plane {ds.index(worst)})")

    show_points("receiver_hull_points (unclipped)", hull_pts)
    show_points("receiver_clipped_points (clip output)", clipped_pts)
    show_points("receiver_clipped_hull_points (after re-hull)", clipped_hull_pts)

    print("\nview_frustum_corners:")
    for c in light["view_frustum_corners"]:
        print(f"  ({c[0]:8.3f}, {c[1]:8.3f}, {c[2]:8.3f})")

    print("\nview_frustum_planes (l, r, b, t, near, far):")
    for i, pl in enumerate(light["view_frustum_planes"]):
        print(f"  [{i}] {fmt(pl, 4)}")

    print("\nreceiver_filter_planes (0 = far cap, i>=1 = silhouette edge i-1):")
    for i, pl in enumerate(light["receiver_filter_planes"]):
        print(f"  [{i}] {fmt(pl, 4)}")

    print("\nfar_plane_hull:")
    for p in light["receiver_far_plane_hull"]:
        print(f"  {fmt(p)}")

    print("\ncasters:")
    for c in light["caster_boxes"]:
        flags = []
        if c["culled_by_shadow_volume"]:
            flags.append("CULLED-BY-SHADOW-VOLUME")
        if c["culled_by_receiver_volume"]:
            flags.append("CULLED-BY-RECEIVER-VOLUME")
        verdict = " ".join(flags) if flags else "kept"
        line = f"  aabb {fmt(c['min'])}..{fmt(c['max'])} {verdict}"
        if c["rejecting_plane"] >= 0:
            line += f" rejecting_plane={c['rejecting_plane']}"
        print(line)
        # For receiver-volume culls, show per-corner distances to the rejecting plane.
        if c["culled_by_receiver_volume"] and c["rejecting_plane"] >= 0:
            plane = light["receiver_filter_planes"][c["rejecting_plane"]]
            ds = [plane_distance(plane, p) for p in aabb_corners(c["min"], c["max"])]
            print(f"    distances to rejecting plane: {', '.join(f'{d:.3f}' for d in ds)}")
