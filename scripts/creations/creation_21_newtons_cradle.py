#!/usr/bin/env python3
"""Creation 21: Newton's Cradle - a working physics desk toy.

A large chrome Newton's cradle on a walnut desk. Five steel balls hang
from the two top rails in the classic V suspension (two threads per
ball), and every ball is a dynamic rigid body on a HINGE joint at the
midpoint of its two thread anchors - the axis the V suspension
physically allows. Each hinge joins a "Ball N Hinge" node under the ball
to a fixed "Ball N Pivot" node under Cradle Frame > Cradle Pivots, so
dragging a ball never moves its pivot. The left ball is built lifted; releasing
the physics clock lets it fall, and the collision momentum travels
through the row to kick out the right ball.

Physics setup that makes the momentum transfer work:
  - one shared physics material: restitution 1 (combine maximum),
    zero friction and zero damping, so a hit neither sticks nor spins
    the balls around their hinges;
  - sphere collision shapes (not hulls of the faceted render mesh), so
    contacts push along the row axis;
  - a BACKEND-DEPENDENT gap between the balls. Jolt (the default)
    solves every contact inside its 2 cm speculative contact distance in
    the same velocity solve, so touching balls share the impulse and the
    whole row swings off together (measured: gap 0.5 mm -> balls 2-5 all
    reach 0.16 m, ball 1 bounces back). A gap wider than that distance
    plus one 240 Hz step of travel makes the hits sequential: gap 2.2 cm
    -> the far ball reaches 0.408 m of the 0.410 m lift. Box3D transfers
    cleanly even with a 0.5 mm gap (0.406 m). --jolt (default) and
    --box3d pick the gap; match the flag to the editor's physics backend
    (Box3D builds come from scripts/configure_*_box3d.bat).

The threads and ball caps are motion_mode "none" children of the ball
node, so they swing with it. The script probes the swing under the
manual clock and captures the frame where the far ball is at the top of
its first swing, then leaves the cradle clicking under the wall clock.

Iteration:
  --reframe <glb>  camera/screenshot stage only on a saved scene.
  --only <object>  rebuild one object (Newton's Cradle | Books | Desk)
                   in the running editor's scene.
  --jolt | --box3d ball gap for the editor's physics backend.
  --keep-windows   leave editor window visibility and focus untouched.
  --scene-only     build the scene and nothing else: no screenshots, no
                   swing probe, no window changes, no recording pause; the
                   scene's one camera is placed once to face the cradle
                   (left alone with --only) and physics is left ON.
"""

import contextlib
import math
import os
import sys
import time

sys.path.insert(0, os.path.dirname(__file__))
from common import (  # noqa: E402
    Creation, standard_args, reframe, fail_soft,
    align_y_quaternion, axis_angle_quaternion, quat_mul, quat_rotate,
    v_add, v_sub, v_length, v_norm, sim,
)

BASE = "logs/creations/newtons_cradle"
SAVE_PATH = "res/editor/scenes/creations/newtons_cradle.glb"

# ------------------------------------------------------------- dimensions
BALL_COUNT = 5
BALL_RADIUS = 0.075
# Clearance between neighbouring balls per physics backend (see docstring).
BALL_GAPS = {"jolt": 0.022, "box3d": 0.0005}
BALL_GAP = BALL_GAPS["jolt"]     # set from --jolt / --box3d in main()
BALL_MASS = 2.0
Y_BASE_TOP = 0.08                # top of the black base
Y_RAIL = 1.00                    # pivot height (top rails)
Y_BALL = 0.36                    # ball centers at rest
RAIL_HALF_WIDTH = 0.18           # rails at z = +/- this
POST_X = 0.60                    # posts at x = +/- this
TUBE_RADIUS = 0.013
THREAD_RADIUS = 0.0018
LIFT_DEG = 40.0                  # left ball released from this angle

SHOTS = [
    ("front", [0.0, 0.95, 2.75], [0.0, 0.55, 0.0]),
    ("three_quarter", [1.75, 1.25, 2.05], [0.0, 0.52, 0.0]),
    ("row", [-2.0, 0.55, 1.0], [0.15, 0.45, 0.0]),
]


def ball_x(index):
    pitch = 2.0 * BALL_RADIUS + BALL_GAP
    return (index - 0.5 * (BALL_COUNT - 1)) * pitch


def swing_point(pivot, q, point):
    """Rotate a rest-pose world point about the ball's pivot."""
    return v_add(pivot, quat_rotate(q, v_sub(point, pivot)))


def tube(c, name, start, end, material, parent_node_id):
    """Chrome tube from start to end (a capped cone of constant radius:
    base at the node origin, +Y aligned to the segment)."""
    direction = v_sub(end, start)
    return c.shape("cone", name, start,
                   height=v_length(direction), bottom_radius=TUBE_RADIUS,
                   top_radius=TUBE_RADIUS, slice_count=20,
                   rotation_xyzw=align_y_quaternion(v_norm(direction)),
                   material_name=material, motion_mode="static",
                   parent_node_id=parent_node_id)


# ------------------------------------------------------------------ desk
def build_desk(c, m):
    desk = c.group("Desk", [0.0, 0.0, 0.0])
    c.shape("box", "Desk Top", [0.0, -0.03, 0.4], size=[6.0, 0.06, 3.0],
            material_name=m["walnut"], motion_mode="static",
            parent_node_id=desk)
    c.shape("box", "Desk Mat", [0.0, 0.002, 0.0], size=[1.65, 0.004, 0.78],
            material_name=m["felt"], motion_mode="none", parent_node_id=desk)
    c.shape("box", "Back Wall", [0.0, 2.2, -1.2], size=[12.0, 5.0, 0.1],
            material_name=m["wall"], motion_mode="none", parent_node_id=desk)
    for side in (-1.0, 1.0):
        c.shape("box", f"Side Wall {side:+.0f}", [side * 3.0, 2.2, 1.4],
                size=[0.1, 5.0, 5.2], material_name=m["wall"],
                motion_mode="none", parent_node_id=desk)
    return desk


# ----------------------------------------------------------------- books
def build_books(c, m):
    """Three stacked hardcovers: a cream page block wrapped by two cover
    boards and a spine (at local -x), so the pages show on three edges."""
    origin = [1.25, 0.0, -0.35]
    books = c.group("Books", origin)
    stack = [
        ([0.30, 0.055, 0.42], m["book_red"], 8.0),
        ([0.27, 0.045, 0.38], m["book_green"], -5.0),
        ([0.24, 0.060, 0.34], m["book_blue"], 14.0),
    ]
    board = 0.006
    y = 0.0
    for index, (size, material, yaw_deg) in enumerate(stack):
        w, h, d = size
        q = axis_angle_quaternion([0.0, 1.0, 0.0], math.radians(yaw_deg))
        center = [origin[0], y + 0.5 * h, origin[2]]

        def at(local):
            return v_add(center, quat_rotate(q, local))

        book = c.group(f"Book {index + 1}", center, parent_node_id=books)
        c.set_node_transform(book, rotation_xyzw=q)
        for side in (-1.0, 1.0):
            c.shape("box", f"Book {index + 1} Board {side:+.0f}",
                    at([0.0, side * 0.5 * (h - board), 0.0]), size=[w, board, d],
                    rotation_xyzw=q, material_name=material,
                    motion_mode="none", parent_node_id=book)
        c.shape("box", f"Book {index + 1} Spine", at([-0.5 * (w - board), 0.0, 0.0]),
                size=[board, h, d], rotation_xyzw=q, material_name=material,
                motion_mode="none", parent_node_id=book)
        c.shape("box", f"Book {index + 1} Pages", at([-0.004, 0.0, 0.0]),
                size=[w - 0.016, h - 2.0 * board, d - 0.014], rotation_xyzw=q,
                material_name=m["pages"], motion_mode="none", parent_node_id=book)
        y += h
    return books


# ---------------------------------------------------------------- cradle
def build_cradle(c, m):
    cradle = c.group("Newton's Cradle", [0.0, 0.0, 0.0])

    # Base: polished black plinth with a chrome top plate.
    c.shape("box", "Cradle Base", [0.0, 0.035, 0.0], size=[1.40, 0.07, 0.56],
            material_name=m["black"], motion_mode="static",
            parent_node_id=cradle)
    c.shape("box", "Cradle Base Plate", [0.0, 0.0755, 0.0],
            size=[1.30, 0.011, 0.48], material_name=m["chrome"],
            motion_mode="none", parent_node_id=cradle)

    # Frame: four posts + two top rails, ball joints at the corners.
    frame = c.group("Cradle Frame", [0.0, Y_BASE_TOP, 0.0], parent_node_id=cradle)
    for side_z in (-1.0, 1.0):
        z = side_z * RAIL_HALF_WIDTH
        for side_x in (-1.0, 1.0):
            x = side_x * POST_X
            tube(c, f"Post {side_x:+.0f}{side_z:+.0f}", [x, Y_BASE_TOP, z],
                 [x, Y_RAIL, z], m["chrome"], frame)
            c.shape("uv_sphere", f"Corner {side_x:+.0f}{side_z:+.0f}",
                    [x, Y_RAIL, z], radius=TUBE_RADIUS * 1.6, slice_count=16,
                    stack_count=10, material_name=m["chrome"],
                    motion_mode="none", parent_node_id=frame)
            c.shape("cone", f"Foot {side_x:+.0f}{side_z:+.0f}",
                    [x, Y_BASE_TOP, z], height=0.018,
                    bottom_radius=TUBE_RADIUS * 2.4, top_radius=TUBE_RADIUS * 1.2,
                    slice_count=20, material_name=m["chrome"],
                    motion_mode="none", parent_node_id=frame)
        tube(c, f"Rail {side_z:+.0f}", [-POST_X, Y_RAIL, z], [POST_X, Y_RAIL, z],
             m["chrome"], frame)

    # Physics: one lossless material and one hinge setting for all balls.
    c.physics_material("Cradle Steel", restitution=1.0,
                       restitution_combine="maximum", static_friction=0.0,
                       dynamic_friction=0.0, friction_combine="minimum",
                       linear_damping=0.0, angular_damping=0.0)
    # The swing axis (angular z) has no limit: nothing in a real cradle
    # stops a ball at a fixed angle, and a hard limit is what a knocked
    # ball slammed into (Jolt separated the hinge up to 6 mm there).
    c.joint_settings("Cradle Hinge", [
        {"linear_axes": [True, True, True], "min": 0.0, "max": 0.0},
        {"angular_axes": [True, True, False], "min": 0.0, "max": 0.0},
    ])

    pivots = c.group("Cradle Pivots", [0.0, Y_RAIL, 0.0], parent_node_id=frame)
    balls = c.group("Cradle Balls", [0.0, Y_RAIL, 0.0], parent_node_id=cradle)
    ball_names = []
    pins = []
    for index in range(BALL_COUNT):
        x = ball_x(index)
        pivot = [x, Y_RAIL, 0.0]
        lift = math.radians(LIFT_DEG) if index == 0 else 0.0
        # Rotation about +Z by -lift swings the ball out toward -X.
        q = axis_angle_quaternion([0.0, 0.0, 1.0], -lift)
        center = swing_point(pivot, q, [x, Y_BALL, 0.0])
        name = f"Ball {index + 1}"
        ball = c.shape("uv_sphere", name, center, radius=BALL_RADIUS,
                       slice_count=40, stack_count=24, rotation_xyzw=q,
                       material_name=m["steel"], motion_mode="none",
                       parent_node_id=balls)
        ball_id = ball["node_id"]
        ball_names.append(name)

        # Cap + eyelet on top of the ball, where the threads meet.
        cap_base = swing_point(pivot, q, [x, Y_BALL + BALL_RADIUS * 0.92, 0.0])
        c.shape("cone", f"{name} Cap", cap_base, height=0.016,
                bottom_radius=0.016, top_radius=0.011, slice_count=20,
                rotation_xyzw=q, material_name=m["chrome"],
                motion_mode="none", parent_node_id=ball_id)
        eyelet = swing_point(pivot, q, [x, Y_BALL + BALL_RADIUS + 0.018, 0.0])
        c.shape("torus", f"{name} Eyelet", eyelet, major_radius=0.008,
                minor_radius=0.0022, major_steps=16, minor_steps=6,
                rotation_xyzw=quat_mul(q, axis_angle_quaternion(
                    [1.0, 0.0, 0.0], math.pi / 2.0)),
                material_name=m["chrome"], motion_mode="none",
                parent_node_id=ball_id)

        # V suspension: one thread from the eyelet to each rail.
        for side_z in (-1.0, 1.0):
            top = [x, Y_RAIL, side_z * RAIL_HALF_WIDTH]  # on the hinge axis
            direction = v_sub(top, eyelet)
            c.shape("cone", f"{name} Thread {side_z:+.0f}", eyelet,
                    height=v_length(direction), bottom_radius=THREAD_RADIUS,
                    top_radius=THREAD_RADIUS, slice_count=6,
                    rotation_xyzw=align_y_quaternion(v_norm(direction)),
                    material_name=m["thread"], motion_mode="none",
                    parent_node_id=ball_id)

        c.body(ball_id, shape="sphere", radius=BALL_RADIUS, mass=BALL_MASS,
               material_name="Cradle Steel", motion_mode="dynamic")
        # Hinge on the line through both thread anchors: the ball's end
        # rides the ball, the fixed end lives in the cradle frame.
        pins.append((c.anchor(f"{name} Hinge", ball_id, pivot),
                     c.anchor(f"{name} Pivot", pivots, pivot)))
    c.settle()
    # Joints once the bodies exist. The connected node carries the fixed
    # side: Node_joint re-captures both frames whenever the constraint is
    # rebuilt (a viewport drag rebuilds it), and a world-anchored joint
    # without a connected node takes its world frame from the joint node
    # itself - which moves with the dragged ball, dragging the pivot along.
    # "Cradle Pivots" has no rigid body on its ancestor chain, so the
    # joint anchors to the world at each Pivot node's frame, which only
    # moves when the cradle itself is moved.
    for hinge, pivot_node in pins:
        c.joint(hinge, connected_node_id=pivot_node, settings_name="Cradle Hinge")
    c.settle()
    return cradle, ball_names


# ----------------------------------------------------------------- probe
def ball_offsets(c, ball_names):
    """Row-axis displacement of every ball from its rest x. The balls sit
    under the unrotated "Cradle Balls" group at the world origin, so the
    node-local x IS the world x. (Looked up by node type: create_shape
    also names the ball's library brush "Ball N".)"""
    by_name = {n["name"]: n for n in c.nodes()
               if n.get("type") == "Mesh" and n.get("parent") == "Cradle Balls"}
    return [by_name[name]["position"][0] - ball_x(index)
            for index, name in enumerate(ball_names)]


def capture_far_ball_peak(c, ball_names):
    """Step the manual clock until the far ball's outward swing stops
    growing; the scene is left frozen there for the action screenshot."""
    c.advance_time(mode="manual", max_step_ms=5.0)
    c.set_physics(True)
    c.wake_physics(node_name="Cradle Balls")
    best = 0.0
    t = 0.0
    step = 0.02
    print("  t[s] " + "".join(f"  ball{i + 1:d} dx" for i in range(len(ball_names))))
    while t < 2.0:
        sim(c, step, max_step_ms=5.0)
        t += step
        offsets = ball_offsets(c, ball_names)
        print(f"  {t:4.2f} " + "".join(f"  {dx:+8.4f}" for dx in offsets))
        far = offsets[-1]
        if far > best + 1e-4:
            best = far
        elif best > 0.05 and far < best - 1e-3:
            break
    return best


def add_script_arguments(parser):
    parser.add_argument("--scene-only", action="store_true",
                        help="build the scene only: no screenshots, swing "
                             "probe, window changes or recording pause; the "
                             "scene camera is placed once to face the cradle "
                             "and physics is left on")
    backend = parser.add_mutually_exclusive_group()
    backend.add_argument("--jolt", dest="backend", action="store_const",
                         const="jolt", help=f"ball gap for the Jolt backend "
                         f"({BALL_GAPS['jolt']} m, default)")
    backend.add_argument("--box3d", dest="backend", action="store_const",
                         const="box3d", help=f"ball gap for the Box3D backend "
                         f"({BALL_GAPS['box3d']} m)")
    parser.set_defaults(backend="jolt")


def main():
    global BALL_GAP
    args = standard_args("Newton's Cradle", add_script_arguments)
    BALL_GAP = BALL_GAPS[args.backend]
    print(f"physics backend: {args.backend} (ball gap {BALL_GAP} m)")
    if reframe(args, "Newton's Cradle", BASE, SHOTS):
        return
    only = args.only
    reuse = args.reuse or bool(only)
    scene_only = args.scene_only
    c = Creation("Newton's Cradle", port=args.port,
                 pause_s=0.0 if scene_only else args.pause,
                 editor_exe=args.editor_exe, reuse=reuse,
                 keep_scenes=args.keep_scenes or bool(only),
                 manage_windows=not (args.keep_windows or scene_only))
    if only:
        c.attach_scene()
        c.delete_nodes(names=[only])
    else:
        print(f"scene: {c.new_scene()}")

    # fail_soft captures a failure screenshot; --scene-only takes none.
    guard = contextlib.nullcontext() if scene_only else fail_soft(c, BASE)
    with guard:
        c.set_physics(False)
        m = {
            "chrome": c.ensure_material("chrome", base_color=[0.96, 0.96, 0.97],
                                        roughness=0.30, metallic=1.0),
            "steel": c.ensure_material("polished steel", base_color=[0.90, 0.91, 0.93],
                                       roughness=0.24, metallic=1.0),
            "black": c.ensure_material("black lacquer", base_color=[0.012, 0.012, 0.014],
                                       roughness=0.18, metallic=0.0),
            "thread": c.ensure_material("nylon thread", base_color=[0.85, 0.85, 0.82],
                                        roughness=0.6, metallic=0.0),
            "walnut": c.ensure_material("walnut", base_color=[0.13, 0.07, 0.035],
                                        roughness=0.45, metallic=0.0),
            "felt": c.ensure_material("green felt", base_color=[0.02, 0.07, 0.04],
                                      roughness=0.95, metallic=0.0),
            "wall": c.ensure_material("wall", base_color=[0.20, 0.19, 0.17],
                                      roughness=0.9, metallic=0.0),
            "book_red": c.ensure_material("book red", base_color=[0.36, 0.05, 0.05],
                                          roughness=0.7, metallic=0.0),
            "book_green": c.ensure_material("book green", base_color=[0.07, 0.20, 0.12],
                                            roughness=0.7, metallic=0.0),
            "book_blue": c.ensure_material("book blue", base_color=[0.06, 0.09, 0.26],
                                           roughness=0.7, metallic=0.0),
            "pages": c.ensure_material("pages", base_color=[0.86, 0.82, 0.70],
                                       roughness=0.9, metallic=0.0),
        }

        builders = {
            "Desk": lambda: build_desk(c, m),
            "Books": lambda: build_books(c, m),
            "Newton's Cradle": lambda: build_cradle(c, m),
        }
        if only:
            if only not in builders:
                raise SystemExit(f"--only: unknown object '{only}' "
                                 f"(known: {', '.join(builders)})")
        else:
            # Lights + shadow range first (skill rule): a warm key spot, a
            # cool fill and a rim that draws highlights along the chrome.
            c.ambience(ambient=[0.10, 0.10, 0.10],
                       clear_color=[0.05, 0.05, 0.055, 1.0], grid=False,
                       sky={"_version": 3, "enabled": True, "mode": 1})
            c.light("spot", "Key", [-1.6, 2.6, 2.2], [1.0, 0.92, 0.80], 110.0,
                    range=9.0, cast_shadow=True, inner_spot_angle=0.35,
                    outer_spot_angle=0.75)
            c.set_node_transform("Key", rotation_xyzw=quat_mul(
                axis_angle_quaternion([0.0, 1.0, 0.0], math.radians(-36.0)),
                axis_angle_quaternion([1.0, 0.0, 0.0], math.radians(-44.0))))
            c.light("point", "Fill", [2.2, 1.6, 2.0], [0.70, 0.78, 0.95], 40.0,
                    range=7.0, cast_shadow=False)
            c.light("point", "Rim", [0.4, 2.2, -0.9], [1.0, 0.95, 0.90], 35.0,
                    range=5.0, cast_shadow=False)
            # Small accents: chrome reads through its highlights, so a few
            # dim lights scattered around give the balls and tubes glints.
            for index, position in enumerate([[-2.0, 1.2, 1.4], [1.2, 0.5, 1.8],
                                              [-0.6, 2.0, 1.6]]):
                c.light("point", f"Glint {index + 1}", position,
                        [1.0, 1.0, 1.0], 6.0, range=3.5, cast_shadow=False)
            c.shadow_range(6.0, z_far=40.0)

        ball_names = [f"Ball {i + 1}" for i in range(BALL_COUNT)]
        for name, build in builders.items():
            if only and name != only:
                continue
            result = build()
            if name == "Newton's Cradle":
                ball_names = result[1]
        c.settle()

        if scene_only:
            if not only:
                c.place_camera(*SHOTS[1][1:])
                if not args.no_save:
                    c.save(SAVE_PATH)
            c.set_physics(True)
            print("Newton's Cradle scene built (physics ON).")
            return

        if only:
            eye, target = c.frame(only, azimuth=25.0, elevation=20.0)
            c.screenshot_views(f"{BASE}_only", [("close", eye, target)])
            return

        c.screenshot_views(BASE + "_still", SHOTS[:2])
        peak = capture_far_ball_peak(c, ball_names)
        print(f"far ball first-swing peak offset: {peak:.3f} m")
        c.screenshot_views(BASE + "_swing", SHOTS)
        c.advance_time(mode="wall_clock")
        if not args.no_save:
            # Save the frozen-at-peak state paused, so the file reloads
            # mid-swing; the live editor keeps clicking afterwards.
            c.set_physics(False)
            c.save(SAVE_PATH)
            c.set_physics(True)
        c.place_camera(*SHOTS[1][1:])
        print("Newton's Cradle complete (physics ON, cradle swinging).")


if __name__ == "__main__":
    main()
