# Deterministic editor clock for headless, cloud and CI runs

Status: in progress

Extends `doc/editor/time.md` (the editor clock, its `wall_clock` / `fixed_dt`
sources, `ERHE_FIXED_DT_MS`, MCP `get_time` / `advance_frames`, MCP request
expiry measured from the last step) and `doc/testing.md`
(`mcp_server_tests` runs against the fixed-dt clock).

## Goal

Every scripted editor run - tests, verification scripts, creations - lets
frames pass with `advance_frames` and runs the editor with the fixed-dt clock,
so its result does not depend on how fast frames render.

## Remaining work

| Step | Content | Size |
|---|---|---|
| R1 | The verification scripts that step frames with `advance_time seconds=0.016` call `advance_frames` instead: `geometry_edit_node_order_verify.py`, `geometry_spreadsheet_verify.py`, `ik_effector_orientation_verify.py`, `ik_interactive_pass_verify.py`, `ik_pole_verify.py`, `joint_constraint_assets_verify.py`, `joint_visualization_verify.py`, `reloadable_asset_loads_smoke_test.py`, `rotation_inspector_smoke_test.py`, `undo_reference_clearing_smoke_test.py` (all under `scripts/`). A script that queues simulation time on purpose keeps `advance_time` | S |
| R2 | Self-launching scripts (`scripts/creations/common.py` and the `*_verify.py` scripts that start their own editor) set `ERHE_FIXED_DT_MS=16.667` unless the environment sets it, as `scripts/mesh_modeling_verify.py` does | S |
| R3 | An `mcp_server_tests` case for ImGui key repeat at slow frames under the fixed-dt clock (Backspace held with `key_press hold_frames` in a focused text field deletes one character plus one per `io.KeyRepeatRate` after `io.KeyRepeatDelay` of editor time, under the simulated frame workload) | S |

## Risks

- Behavior differs between the wall and fixed-dt clocks; interactive Windows
  / macOS runs keep exercising the wall clock, CI and headless runs the
  fixed-dt clock.
- A test that measures real elapsed time says so and reads the wall clock
  explicitly (`Mcp_test.gesture_longer_than_the_request_expiry_completes_while_it_steps`).
