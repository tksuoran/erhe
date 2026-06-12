# Follow-up: print the receiver hull / clipped / re-hulled point sets of the
# last shadow-fit dump and validate each clipped point against the view
# frustum planes, to see where the near (low-x) region is lost.
import json
import sys

LOG_PATH = "logs/log.txt"
with open(LOG_PATH, "r", encoding="utf-8", errors="replace") as f:
    lines = f.readlines()

start = None
for i, line in enumerate(lines):
    if "Shadow fit debug dump" in line:
        start = i

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

light = next(l for l in dump["lights"] if l.get("valid", False))
planes = light["view_frustum_planes"]

def dist(plane, p):
    return plane[0] * p[0] + plane[1] * p[1] + plane[2] * p[2] + plane[3]

def show(name, pts):
    print(f"\n{name} ({len(pts)} points), sorted by x:")
    for p in sorted(pts, key=lambda q: q[0]):
        ds = [dist(pl, p) for pl in planes]
        worst = min(ds)
        worst_i = ds.index(worst)
        print(f"  ({p[0]:8.3f}, {p[1]:8.3f}, {p[2]:8.3f})  min-frustum-dist={worst:8.4f} (plane {worst_i})")

show("receiver_hull_points (unclipped)", light["receiver_hull_points"])
show("receiver_clipped_points (clip output)", light["receiver_clipped_points"])
show("receiver_clipped_hull_points (after re-hull)", light["receiver_clipped_hull_points"])

print("\nview frustum corners:")
for c in light["view_frustum_corners"]:
    print(f"  ({c[0]:8.3f}, {c[1]:8.3f}, {c[2]:8.3f})")
