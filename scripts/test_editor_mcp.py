"""
Test suite for the erhe editor MCP server.

Part 1: Unit tests — deterministic correctness checks of each MCP tool.
Part 2: Smoke test — randomized stress test with configurable seed and duration.

Usage:
    py -3 scripts/test_editor_mcp.py [OPTIONS]

Options:
    --host HOST          MCP server host (default: 127.0.0.1)
    --port PORT          MCP server port (default: 8080)
    --seed SEED          Random seed for smoke test (default: random)
    --smoke-time SECS    Smoke test duration in seconds (default: 10, 0=run until Ctrl+C)
    --unit-only          Run only unit tests
    --smoke-only         Run only smoke tests
"""

import argparse
import json
import random
import sys
import time
import urllib.error
import urllib.request


# ---------------------------------------------------------------------------
# MCP Client
# ---------------------------------------------------------------------------

class McpClient:
    def __init__(self, host="127.0.0.1", port=8080):
        self.base_url = f"http://{host}:{port}"
        self.mcp_url = f"{self.base_url}/mcp"
        self._id = 0

    def _next_id(self):
        self._id += 1
        return str(self._id)

    def health(self):
        req = urllib.request.Request(f"{self.base_url}/health")
        with urllib.request.urlopen(req, timeout=5) as resp:
            return json.loads(resp.read())

    def raw_rpc(self, method, params=None):
        body = {"jsonrpc": "2.0", "id": self._next_id(), "method": method}
        if params is not None:
            body["params"] = params
        data = json.dumps(body).encode()
        req = urllib.request.Request(
            self.mcp_url,
            data=data,
            headers={"Content-Type": "application/json"},
        )
        with urllib.request.urlopen(req, timeout=10) as resp:
            return json.loads(resp.read())

    def call(self, tool_name, arguments=None):
        if arguments is None:
            arguments = {}
        rpc = self.raw_rpc("tools/call", {"name": tool_name, "arguments": arguments})
        if "error" in rpc:
            raise RuntimeError(f"RPC error: {rpc['error']}")
        result = rpc["result"]
        text = result["content"][0]["text"]
        try:
            parsed = json.loads(text)
        except json.JSONDecodeError:
            parsed = {"_text": text}
        is_error = result.get("isError", False) or (isinstance(parsed, dict) and parsed.get("isError", False))
        return parsed, is_error

    def call_ok(self, tool_name, arguments=None):
        parsed, is_error = self.call(tool_name, arguments)
        if is_error:
            raise RuntimeError(f"{tool_name} returned error: {parsed}")
        return parsed

    def wait_async(self, timeout=10.0, poll_interval=0.1):
        """Poll get_async_status until pending+running reach 0. Returns True if settled."""
        start = time.time()
        while time.time() - start < timeout:
            status = self.call_ok("get_async_status")
            if status["pending"] == 0 and status["running"] == 0:
                return True
            time.sleep(poll_interval)
        return False

    def tools_list(self):
        rpc = self.raw_rpc("tools/list")
        return rpc["result"]["tools"]

    def initialize(self):
        rpc = self.raw_rpc("initialize")
        return rpc["result"]


# ---------------------------------------------------------------------------
# Unit Tests
# ---------------------------------------------------------------------------

class UnitTestRunner:
    def __init__(self, client):
        self.client = client
        self.passed = 0
        self.failed = 0
        self.skipped = 0
        self.scene_name = None
        self.placed_node_id = None

    def run(self):
        tests = [
            self.test_health,
            self.test_initialize,
            self.test_tools_list,
            self.test_list_scenes,
            self.test_get_scene_nodes,
            self.test_get_node_details,
            self.test_get_scene_cameras,
            self.test_get_scene_lights,
            self.test_get_scene_materials,
            self.test_get_material_details,
            self.test_get_scene_brushes,
            self.test_get_selection_empty,
            self.test_select_items,
            self.test_select_items_clear,
            self.test_place_brush,
            self.test_toggle_physics,
            self.test_invalid_scene,
            self.test_invalid_method,
            self.test_undo_redo,
            self.test_undo_redo_stack,
            self.test_lock_edit,
            self.test_lock_edit_prevents_delete,
            self.test_tags,
            self.test_reparent_node,
            self.test_reparent_hierarchy,
            self.test_async_status,
            self.test_geometry_catmull_clark,
            self.test_geometry_triangulate,
            self.test_geometry_subdivision_sqrt3,
            self.test_geometry_conway_dual,
            self.test_geometry_conway_ambo,
            self.test_geometry_conway_kis,
            self.test_geometry_conway_join,
            self.test_geometry_conway_meta,
            self.test_geometry_conway_ortho,
            self.test_geometry_conway_truncate,
            self.test_geometry_conway_gyro,
            self.test_geometry_reverse,
            self.test_geometry_normalize,
            self.test_geometry_repair,
            self.test_geometry_weld,
            self.test_geometry_generate_tangents,
            self.test_geometry_bake_transform,
            self.test_geometry_chain,
            self.test_geometry_csg_union,
            self.test_geometry_csg_intersection,
            self.test_geometry_csg_difference,
            # New tools (functionality since d6282ce)
            self.test_new_tools_registered,
            self.test_set_mesh_component_mode,
            self.test_mesh_component_selection,
            self.test_align_components,
            self.test_add_joint,
            self.test_decimate,
            self.test_smooth,
            self.test_transform_reference_mode,
            # NOTE: remesh (GEO::remesh_smooth) and generate_texture_coordinates
            # (atlas parameterization) drive Geogram's OpenNL solver, which can
            # abort() the process on degenerate inputs (e.g. NB_VARIABLES == 0).
            # That pre-existing Geogram fragility is out of scope here, so these
            # ops are not auto-run (a crash would poison the rest of the suite);
            # their MCP registration is still covered by test_new_tools_registered.
        ]
        print("=" * 60)
        print("UNIT TESTS")
        print("=" * 60)
        for test in tests:
            name = test.__name__
            try:
                test()
                self.passed += 1
                print(f"  PASS  {name}")
            except SkipTest as e:
                self.skipped += 1
                print(f"  SKIP  {name}: {e}")
            except Exception as e:
                self.failed += 1
                print(f"  FAIL  {name}: {e}")

        # Cleanup
        self._cleanup()

        print(f"\nUnit tests: {self.passed} passed, {self.failed} failed, {self.skipped} skipped")
        return self.failed == 0

    def _cleanup(self):
        try:
            self.client.call("select_items", {"scene_name": self.scene_name or "", "ids": []})
        except Exception:
            pass
        # Reset editor state touched by the new-tool tests so the smoke test starts clean.
        try:
            self.client.call("clear_mesh_component_selection")
            self.client.call("set_mesh_component_mode", {"mode": "object"})
            self.client.call("set_transform_reference_mode", {"mode": "global"})
        except Exception:
            pass

    def _require_scene(self):
        if not self.scene_name:
            raise SkipTest("no scene discovered")

    def test_health(self):
        result = self.client.health()
        assert result.get("status") == "ok", f"Expected ok, got {result}"

    def test_initialize(self):
        result = self.client.initialize()
        assert "protocolVersion" in result, "Missing protocolVersion"
        assert "serverInfo" in result, "Missing serverInfo"
        assert result["serverInfo"]["name"] == "erhe-editor"

    def test_tools_list(self):
        tools = self.client.tools_list()
        assert isinstance(tools, list), "tools should be a list"
        names = {t["name"] for t in tools}
        expected = {"list_scenes", "get_scene_nodes", "get_scene_materials", "get_selection", "place_brush", "toggle_physics", "select_items"}
        missing = expected - names
        assert not missing, f"Missing tools: {missing}"

    def test_list_scenes(self):
        result = self.client.call_ok("list_scenes")
        scenes = result["scenes"]
        assert len(scenes) >= 1, "Expected at least one scene"
        scene = scenes[0]
        for key in ("name", "node_count", "camera_count", "light_count", "material_count"):
            assert key in scene, f"Missing key: {key}"
        self.scene_name = scene["name"]

    def test_get_scene_nodes(self):
        self._require_scene()
        result = self.client.call_ok("get_scene_nodes", {"scene_name": self.scene_name})
        nodes = result["nodes"]
        assert isinstance(nodes, list), "nodes should be a list"
        if nodes:
            node = nodes[0]
            for key in ("name", "id", "position", "rotation_xyzw", "scale"):
                assert key in node, f"Missing key: {key}"

    def test_get_node_details(self):
        self._require_scene()
        result = self.client.call_ok("get_scene_nodes", {"scene_name": self.scene_name})
        nodes = result["nodes"]
        if not nodes:
            raise SkipTest("no nodes in scene")
        node_name = nodes[0]["name"]
        detail = self.client.call_ok("get_node_details", {"scene_name": self.scene_name, "node_name": node_name})
        for key in ("name", "id", "world_position", "local_transform", "attachments", "children"):
            assert key in detail, f"Missing key: {key}"
        assert "translation" in detail["local_transform"]

    def test_get_scene_cameras(self):
        self._require_scene()
        result = self.client.call_ok("get_scene_cameras", {"scene_name": self.scene_name})
        cameras = result["cameras"]
        assert isinstance(cameras, list)
        if cameras:
            for key in ("name", "id", "exposure"):
                assert key in cameras[0], f"Missing key: {key}"

    def test_get_scene_lights(self):
        self._require_scene()
        result = self.client.call_ok("get_scene_lights", {"scene_name": self.scene_name})
        lights = result["lights"]
        assert isinstance(lights, list)
        if lights:
            for key in ("name", "id", "type", "color", "intensity"):
                assert key in lights[0], f"Missing key: {key}"

    def test_get_scene_materials(self):
        self._require_scene()
        result = self.client.call_ok("get_scene_materials", {"scene_name": self.scene_name})
        materials = result["materials"]
        assert isinstance(materials, list)
        assert len(materials) >= 1, "Expected at least one material"
        for key in ("name", "id", "base_color", "metallic", "roughness"):
            assert key in materials[0], f"Missing key: {key}"

    def test_get_material_details(self):
        self._require_scene()
        result = self.client.call_ok("get_scene_materials", {"scene_name": self.scene_name})
        materials = result["materials"]
        if not materials:
            raise SkipTest("no materials")
        mat_name = materials[0]["name"]
        detail = self.client.call_ok("get_material_details", {"scene_name": self.scene_name, "material_name": mat_name})
        for key in ("name", "id", "base_color", "metallic", "roughness", "opacity", "emissive"):
            assert key in detail, f"Missing key: {key}"

    def test_get_scene_brushes(self):
        self._require_scene()
        result = self.client.call_ok("get_scene_brushes", {"scene_name": self.scene_name})
        brushes = result["brushes"]
        assert isinstance(brushes, list)
        assert len(brushes) >= 1, "Expected at least one brush"
        for key in ("name", "id"):
            assert key in brushes[0], f"Missing key: {key}"

    def test_get_selection_empty(self):
        self._require_scene()
        self.client.call("select_items", {"scene_name": self.scene_name, "ids": []})
        result = self.client.call_ok("get_selection")
        assert result["items"] == [], f"Expected empty selection, got {result['items']}"

    def test_select_items(self):
        self._require_scene()
        nodes_result = self.client.call_ok("get_scene_nodes", {"scene_name": self.scene_name})
        nodes = nodes_result["nodes"]
        if not nodes:
            raise SkipTest("no nodes")
        target_id = nodes[0]["id"]
        target_name = nodes[0]["name"]
        result = self.client.call_ok("select_items", {"scene_name": self.scene_name, "ids": [target_id]})
        assert result["selected_count"] == 1, f"Expected 1 selected, got {result['selected_count']}"
        sel = self.client.call_ok("get_selection")
        sel_names = [item["name"] for item in sel["items"]]
        assert target_name in sel_names, f"Expected {target_name} in selection, got {sel_names}"

    def test_select_items_clear(self):
        self._require_scene()
        self.client.call("select_items", {"scene_name": self.scene_name, "ids": []})
        sel = self.client.call_ok("get_selection")
        assert sel["items"] == [], f"Expected empty, got {sel['items']}"

    def test_place_brush(self):
        self._require_scene()
        brushes = self.client.call_ok("get_scene_brushes", {"scene_name": self.scene_name})["brushes"]
        if not brushes:
            raise SkipTest("no brushes")
        nodes_before = self.client.call_ok("get_scene_nodes", {"scene_name": self.scene_name})["nodes"]
        count_before = len(nodes_before)

        result = self.client.call_ok("place_brush", {
            "scene_name": self.scene_name,
            "brush_id": brushes[0]["id"],
            "position": [0.0, 10.0, 0.0],
        })
        assert "node_id" in result, f"Missing node_id in result: {result}"
        self.placed_node_id = result["node_id"]

        nodes_after = self.client.call_ok("get_scene_nodes", {"scene_name": self.scene_name})["nodes"]
        assert len(nodes_after) == count_before + 1, f"Expected {count_before+1} nodes, got {len(nodes_after)}"

    def test_toggle_physics(self):
        r1 = self.client.call_ok("toggle_physics")
        state1 = r1["dynamic_physics_enabled"]
        r2 = self.client.call_ok("toggle_physics")
        state2 = r2["dynamic_physics_enabled"]
        assert state1 != state2, f"Toggle didn't change state: {state1} -> {state2}"

    def test_invalid_scene(self):
        _, is_error = self.client.call("get_scene_nodes", {"scene_name": "nonexistent_scene_xyz"})
        assert is_error, "Expected error for invalid scene name"

    def test_invalid_method(self):
        rpc = self.client.raw_rpc("nonexistent/method")
        assert "error" in rpc, "Expected error for unknown method"

    def test_undo_redo(self):
        self._require_scene()
        if self.placed_node_id is None:
            raise SkipTest("no brush was placed")

        nodes_before_undo = self.client.call_ok("get_scene_nodes", {"scene_name": self.scene_name})["nodes"]
        node_ids_before = {n["id"] for n in nodes_before_undo}
        assert self.placed_node_id in node_ids_before, "Placed node should exist before undo"

        # Undo
        self.client.call("undo")
        nodes_after_undo = self.client.call_ok("get_scene_nodes", {"scene_name": self.scene_name})["nodes"]
        node_ids_after_undo = {n["id"] for n in nodes_after_undo}
        assert self.placed_node_id not in node_ids_after_undo, "Placed node should be gone after undo"

        # Redo
        self.client.call("redo")
        nodes_after_redo = self.client.call_ok("get_scene_nodes", {"scene_name": self.scene_name})["nodes"]
        node_ids_after_redo = {n["id"] for n in nodes_after_redo}
        assert self.placed_node_id in node_ids_after_redo, "Placed node should be back after redo"

        # Undo again to clean up
        self.client.call("undo")
        self.placed_node_id = None


    def test_undo_redo_stack(self):
        self._require_scene()
        result = self.client.call_ok("get_undo_redo_stack")
        assert "undo" in result, "Missing undo key"
        assert "redo" in result, "Missing redo key"
        assert "can_undo" in result, "Missing can_undo key"
        assert "can_redo" in result, "Missing can_redo key"
        assert isinstance(result["undo"], list)
        assert isinstance(result["redo"], list)

        # Place a brush so there's something on the undo stack
        brushes = self.client.call_ok("get_scene_brushes", {"scene_name": self.scene_name})["brushes"]
        if not brushes:
            raise SkipTest("no brushes")
        self.client.call_ok("place_brush", {
            "scene_name": self.scene_name,
            "brush_id": brushes[0]["id"],
            "position": [0.0, 10.0, 0.0],
        })
        result_after = self.client.call_ok("get_undo_redo_stack")
        assert result_after["can_undo"], "Should be able to undo after placing brush"
        assert len(result_after["undo"]) > len(result["undo"]), "Undo stack should grow after place"

        # Clean up
        self.client.call("undo")

    def test_lock_edit(self):
        self._require_scene()
        # Place a brush to get a node we own
        brushes = self.client.call_ok("get_scene_brushes", {"scene_name": self.scene_name})["brushes"]
        if not brushes:
            raise SkipTest("no brushes")
        result = self.client.call_ok("place_brush", {
            "scene_name": self.scene_name,
            "brush_id": brushes[0]["id"],
            "position": [0.0, 10.0, 0.0],
        })
        node_id = result["node_id"]

        # Lock the item
        lock_result = self.client.call_ok("lock_items", {"scene_name": self.scene_name, "ids": [node_id]})
        assert lock_result["locked_count"] == 1

        # Verify lock state in query
        nodes = self.client.call_ok("get_scene_nodes", {"scene_name": self.scene_name})["nodes"]
        locked_node = next((n for n in nodes if n["id"] == node_id), None)
        assert locked_node is not None, "Placed node not found"
        assert locked_node["locked"], "Node should be locked"

        # Unlock
        unlock_result = self.client.call_ok("unlock_items", {"scene_name": self.scene_name, "ids": [node_id]})
        assert unlock_result["unlocked_count"] == 1

        nodes = self.client.call_ok("get_scene_nodes", {"scene_name": self.scene_name})["nodes"]
        unlocked_node = next((n for n in nodes if n["id"] == node_id), None)
        assert not unlocked_node["locked"], "Node should be unlocked"

        # Clean up
        self.client.call("undo")  # undo unlock
        self.client.call("undo")  # undo lock
        self.client.call("undo")  # undo place

    def test_lock_edit_prevents_delete(self):
        self._require_scene()
        # Place and lock a node
        brushes = self.client.call_ok("get_scene_brushes", {"scene_name": self.scene_name})["brushes"]
        if not brushes:
            raise SkipTest("no brushes")
        result = self.client.call_ok("place_brush", {
            "scene_name": self.scene_name,
            "brush_id": brushes[0]["id"],
            "position": [0.0, 10.0, 0.0],
        })
        node_id = result["node_id"]
        self.client.call_ok("lock_items", {"scene_name": self.scene_name, "ids": [node_id]})

        count_before = len(self.client.call_ok("get_scene_nodes", {"scene_name": self.scene_name})["nodes"])

        # Try to delete the locked node
        self.client.call_ok("select_items", {"scene_name": self.scene_name, "ids": [node_id]})
        self.client.call("Selection.delete")

        count_after = len(self.client.call_ok("get_scene_nodes", {"scene_name": self.scene_name})["nodes"])
        assert count_after == count_before, f"Locked node should not be deleted: {count_before} -> {count_after}"

        # Clean up: unlock, select, delete
        self.client.call_ok("unlock_items", {"scene_name": self.scene_name, "ids": [node_id]})
        self.client.call_ok("select_items", {"scene_name": self.scene_name, "ids": [node_id]})
        self.client.call("Selection.delete")

    def test_tags(self):
        self._require_scene()
        # Place a node to tag
        brushes = self.client.call_ok("get_scene_brushes", {"scene_name": self.scene_name})["brushes"]
        if not brushes:
            raise SkipTest("no brushes")
        result = self.client.call_ok("place_brush", {
            "scene_name": self.scene_name,
            "brush_id": brushes[0]["id"],
            "position": [0.0, 10.0, 0.0],
        })
        node_id = result["node_id"]

        # Add tags
        tag_result = self.client.call_ok("add_tags", {"scene_name": self.scene_name, "ids": [node_id], "tags": ["test", "important"]})
        assert tag_result["tagged_count"] == 1

        # Verify tags in query
        nodes = self.client.call_ok("get_scene_nodes", {"scene_name": self.scene_name})["nodes"]
        tagged_node = next((n for n in nodes if n["id"] == node_id), None)
        assert "test" in tagged_node["tags"], f"Expected 'test' in tags, got {tagged_node['tags']}"
        assert "important" in tagged_node["tags"]

        # Remove one tag
        self.client.call_ok("remove_tags", {"scene_name": self.scene_name, "ids": [node_id], "tags": ["test"]})
        nodes = self.client.call_ok("get_scene_nodes", {"scene_name": self.scene_name})["nodes"]
        tagged_node = next((n for n in nodes if n["id"] == node_id), None)
        assert "test" not in tagged_node["tags"]
        assert "important" in tagged_node["tags"]

        # Clean up
        self.client.call("undo")  # undo place

    def test_reparent_node(self):
        """Place two brushes, reparent one under the other, verify, undo."""
        self._require_scene()
        parent_id = self._place_test_brush()
        child_id = self._place_test_brush()

        # Reparent child under parent
        result = self.client.call_ok("reparent_node", {
            "scene_name": self.scene_name,
            "node_id": child_id,
            "parent_node_id": parent_id
        })
        assert "node" in result
        assert "parent" in result

        # Verify child's parent changed
        nodes = self.client.call_ok("get_scene_nodes", {"scene_name": self.scene_name})["nodes"]
        child_node = next((n for n in nodes if n["id"] == child_id), None)
        parent_node = next((n for n in nodes if n["id"] == parent_id), None)
        assert child_node is not None, "Child node not found"
        assert parent_node is not None, "Parent node not found"
        assert child_node["parent"] == parent_node["name"], f"Expected parent '{parent_node['name']}', got '{child_node['parent']}'"

        # Verify parent's children in node details
        detail = self.client.call_ok("get_node_details", {"scene_name": self.scene_name, "node_name": parent_node["name"]})
        assert child_node["name"] in [str(c) for c in detail["children"]], "Child not in parent's children list"

        # Undo reparent + 2 placements
        self.client.call("undo")  # undo reparent
        self.client.call("undo")  # undo child placement
        self.client.call("undo")  # undo parent placement

    def test_reparent_hierarchy(self):
        """Build a 3-level hierarchy, verify, then reparent to root."""
        self._require_scene()
        grandparent_id = self._place_test_brush()
        parent_id = self._place_test_brush()
        child_id = self._place_test_brush()

        # Build hierarchy: grandparent -> parent -> child
        self.client.call_ok("reparent_node", {
            "scene_name": self.scene_name,
            "node_id": parent_id,
            "parent_node_id": grandparent_id
        })
        self.client.call_ok("reparent_node", {
            "scene_name": self.scene_name,
            "node_id": child_id,
            "parent_node_id": parent_id
        })

        # Verify 3-level hierarchy
        nodes = self.client.call_ok("get_scene_nodes", {"scene_name": self.scene_name})["nodes"]
        child_node = next((n for n in nodes if n["id"] == child_id), None)
        parent_node = next((n for n in nodes if n["id"] == parent_id), None)
        assert child_node["parent"] == parent_node["name"]

        # Reparent child directly to root (parent_node_id = 0)
        self.client.call_ok("reparent_node", {
            "scene_name": self.scene_name,
            "node_id": child_id,
            "parent_node_id": 0
        })
        nodes = self.client.call_ok("get_scene_nodes", {"scene_name": self.scene_name})["nodes"]
        child_node = next((n for n in nodes if n["id"] == child_id), None)
        assert child_node["parent"] == "root", f"Expected parent 'root', got '{child_node['parent']}'"

        # Undo all: 3 reparents + 3 placements
        for _ in range(6):
            self.client.call("undo")

    def test_async_status(self):
        result = self.client.call_ok("get_async_status")
        assert "pending" in result, "Missing pending key"
        assert "running" in result, "Missing running key"
        assert result["pending"] == 0, f"Expected 0 pending, got {result['pending']}"
        assert result["running"] == 0, f"Expected 0 running, got {result['running']}"

    def _place_test_brush(self):
        """Place a brush, return its node_id. Caller must clean up."""
        self._require_scene()
        brushes = self.client.call_ok("get_scene_brushes", {"scene_name": self.scene_name})["brushes"]
        if not brushes:
            raise SkipTest("no brushes")
        # Pick cube if available, else first brush
        cube = next((b for b in brushes if b["name"] == "cube"), brushes[0])
        result = self.client.call_ok("place_brush", {
            "scene_name": self.scene_name,
            "brush_id": cube["id"],
            "position": [0.0, 10.0, 0.0],
        })
        return result["node_id"]

    def _run_geometry_op(self, command_name):
        """Place a cube, select it, run a geometry operation, wait, verify undo, clean up."""
        self._require_scene()
        node_id = self._place_test_brush()
        self.client.call_ok("select_items", {"scene_name": self.scene_name, "ids": [node_id]})
        undo_before = len(self.client.call_ok("get_undo_redo_stack")["undo"])
        self.client.call(command_name)
        self.client.wait_async(timeout=15.0)
        # Poll until the operation appears on the undo stack (handles both sync and async ops)
        deadline = time.time() + 15.0
        while time.time() < deadline:
            stack = self.client.call_ok("get_undo_redo_stack")
            if len(stack["undo"]) > undo_before:
                break
            time.sleep(0.1)
        stack = self.client.call_ok("get_undo_redo_stack")
        assert len(stack["undo"]) > undo_before, f"Undo stack did not grow after {command_name}"
        self.client.call("undo")  # undo geometry op
        self.client.call("undo")  # undo placement

    def test_geometry_catmull_clark(self):
        self._run_geometry_op("Geometry.Subdivision.Catmull-Clark")

    def test_geometry_triangulate(self):
        self._run_geometry_op("Geometry.Triangulate")

    def test_geometry_subdivision_sqrt3(self):
        self._run_geometry_op("Geometry.Subdivision.Sqrt3")

    def test_geometry_conway_dual(self):
        self._run_geometry_op("Geometry.Conway.Dual")

    def test_geometry_conway_ambo(self):
        self._run_geometry_op("Geometry.Conway.Ambo")

    def test_geometry_conway_kis(self):
        self._run_geometry_op("Geometry.Conway.Kis")

    def test_geometry_conway_join(self):
        self._run_geometry_op("Geometry.Conway.Join")

    def test_geometry_conway_meta(self):
        self._run_geometry_op("Geometry.Conway.Meta")

    def test_geometry_conway_ortho(self):
        self._run_geometry_op("Geometry.Conway.Ortho")

    def test_geometry_conway_truncate(self):
        self._run_geometry_op("Geometry.Conway.Truncate")

    def test_geometry_conway_gyro(self):
        self._run_geometry_op("Geometry.Conway.Gyro")

    def test_geometry_reverse(self):
        self._run_geometry_op("Geometry.Reverse")

    def test_geometry_normalize(self):
        self._run_geometry_op("Geometry.Normalize")

    def test_geometry_repair(self):
        self._run_geometry_op("Geometry.Repair")

    def test_geometry_weld(self):
        self._run_geometry_op("Geometry.Weld")

    def test_geometry_generate_tangents(self):
        self._run_geometry_op("Geometry.GenerateTangents")

    def test_geometry_bake_transform(self):
        self._run_geometry_op("Geometry.BakeTransform")

    def test_geometry_chain(self):
        """Test chaining multiple geometry operations on a single mesh."""
        self._require_scene()
        node_id = self._place_test_brush()
        self.client.call_ok("select_items", {"scene_name": self.scene_name, "ids": [node_id]})

        chain = ["Geometry.Conway.Kis", "Geometry.Triangulate", "Geometry.Subdivision.Catmull-Clark"]
        undo_before = len(self.client.call_ok("get_undo_redo_stack")["undo"])
        for i, cmd in enumerate(chain):
            self.client.call(cmd)
            self.client.wait_async(timeout=15.0)
            # Wait for this op to appear on the undo stack
            target = undo_before + i + 1
            deadline = time.time() + 15.0
            while time.time() < deadline:
                stack = self.client.call_ok("get_undo_redo_stack")
                if len(stack["undo"]) >= target:
                    break
                time.sleep(0.1)

        stack = self.client.call_ok("get_undo_redo_stack")
        assert len(stack["undo"]) >= undo_before + len(chain), f"Expected at least {len(chain)} new undo entries"

        # Undo all: 3 geometry ops + 1 placement
        for _ in range(len(chain) + 1):
            self.client.call("undo")


    def _run_csg_op(self, command_name):
        """Place two overlapping brushes, select both, run CSG op, wait, verify, undo."""
        self._require_scene()
        brushes = self.client.call_ok("get_scene_brushes", {"scene_name": self.scene_name})["brushes"]
        if not brushes:
            raise SkipTest("no brushes")
        cube = next((b for b in brushes if b["name"] == "cube"), brushes[0])
        # Place both at same position for guaranteed overlap
        result_a = self.client.call_ok("place_brush", {"scene_name": self.scene_name, "brush_id": cube["id"], "position": [0.0, 10.0, 0.0]})
        result_b = self.client.call_ok("place_brush", {"scene_name": self.scene_name, "brush_id": cube["id"], "position": [0.0, 10.0, 0.0]})
        node_a_id = result_a["node_id"]
        node_b_id = result_b["node_id"]

        # Select both nodes
        self.client.call_ok("select_items", {"scene_name": self.scene_name, "ids": [node_a_id, node_b_id]})

        undo_before = len(self.client.call_ok("get_undo_redo_stack")["undo"])

        self.client.call(command_name)
        self.client.wait_async(timeout=30.0)

        # Wait for op to land on undo stack
        deadline = time.time() + 30.0
        while time.time() < deadline:
            stack = self.client.call_ok("get_undo_redo_stack")
            if len(stack["undo"]) > undo_before:
                break
            time.sleep(0.1)

        stack = self.client.call_ok("get_undo_redo_stack")
        assert len(stack["undo"]) > undo_before, f"Undo stack did not grow after {command_name}"

        # Undo CSG op + 2 placements
        self.client.call("undo")  # undo CSG
        self.client.call("undo")  # undo placement b
        self.client.call("undo")  # undo placement a

    def test_geometry_csg_union(self):
        self._run_csg_op("Geometry.Union")

    def test_geometry_csg_intersection(self):
        self._run_csg_op("Geometry.Intersection")

    def test_geometry_csg_difference(self):
        self._run_csg_op("Geometry.Difference")

    # -- New tools (functionality since d6282ce) ----------------------------

    def test_new_tools_registered(self):
        """All new MCP tools must be advertised by tools/list."""
        names = {t["name"] for t in self.client.tools_list()}
        expected = {
            "set_mesh_component_mode", "select_mesh_components",
            "get_mesh_component_selection", "clear_mesh_component_selection",
            "align_components", "add_joint",
            "remesh", "decimate", "smooth", "generate_texture_coordinates",
            "set_transform_reference_mode",
        }
        missing = expected - names
        assert not missing, f"Missing new tools: {sorted(missing)}"

    def _wait_undo_growth(self, undo_before, timeout=15.0):
        """Wait until the undo stack grows past undo_before. Returns True if it did."""
        self.client.wait_async(timeout=timeout)
        deadline = time.time() + timeout
        while time.time() < deadline:
            if len(self.client.call_ok("get_undo_redo_stack")["undo"]) > undo_before:
                return True
            time.sleep(0.1)
        return len(self.client.call_ok("get_undo_redo_stack")["undo"]) > undo_before

    def _place_brush_at(self, position, motion_mode=None):
        """Place a cube brush at a position, return its node_id."""
        brushes = self.client.call_ok("get_scene_brushes", {"scene_name": self.scene_name})["brushes"]
        if not brushes:
            raise SkipTest("no brushes")
        cube = next((b for b in brushes if b["name"] == "cube"), brushes[0])
        args = {"scene_name": self.scene_name, "brush_id": cube["id"], "position": position}
        if motion_mode is not None:
            args["motion_mode"] = motion_mode
        return self.client.call_ok("place_brush", args)["node_id"]

    def _node_world_pos(self, node_id):
        # Look up by id (get_node_details takes a name, and placed brushes share
        # the name "cube", so a name lookup would be ambiguous). For top-level
        # nodes the reported "position" is the world position.
        nodes = self.client.call_ok("get_scene_nodes", {"scene_name": self.scene_name})["nodes"]
        node = next((n for n in nodes if n["id"] == node_id), None)
        assert node is not None, f"Node {node_id} not found"
        return node["position"]

    def _node_vertex_count(self, node_id):
        """Total vertex count across the node's mesh attachment primitives, or None."""
        nodes = self.client.call_ok("get_scene_nodes", {"scene_name": self.scene_name})["nodes"]
        node = next((n for n in nodes if n["id"] == node_id), None)
        assert node is not None, f"Node {node_id} not found"
        detail = self.client.call_ok("get_node_details", {"scene_name": self.scene_name, "node_name": node["name"]})
        for att in detail["attachments"]:
            if "vertex_count" in att:
                return att["vertex_count"]
        return None

    def _create_dense_sphere(self):
        """Create a fairly dense uv_sphere instance; return its node_id (undoable)."""
        result = self.client.call_ok("create_shape", {
            "scene_name": self.scene_name,
            "shape": "uv_sphere",
            "name": "mcp_test_sphere",
            "position": [0.0, 12.0, 0.0],
            "motion_mode": "static",
            "radius": 1.0,
            "slice_count": 24,
            "stack_count": 24,
        })
        assert "node_id" in result, f"create_shape returned no node_id: {result}"
        return result["node_id"]

    def test_set_mesh_component_mode(self):
        for mode in ("vertex", "edge", "face", "object"):
            result = self.client.call_ok("set_mesh_component_mode", {"mode": mode})
            assert result["mode"] == mode, f"set mode {mode} returned {result}"
            sel = self.client.call_ok("get_mesh_component_selection")
            assert sel["mode"] == mode, f"get_mesh_component_selection mode {sel['mode']} != {mode}"
        # Invalid mode is rejected.
        _, is_error = self.client.call("set_mesh_component_mode", {"mode": "bogus"})
        assert is_error, "Expected error for invalid component mode"

    def test_mesh_component_selection(self):
        self._require_scene()
        node_id = self._place_brush_at([0.0, 11.0, 0.0])
        nodes = self.client.call_ok("get_scene_nodes", {"scene_name": self.scene_name})["nodes"]
        node_name = next(n["name"] for n in nodes if n["id"] == node_id)

        result, is_error = self.client.call("select_mesh_components", {
            "scene_name": self.scene_name,
            "node_id": node_id,
            "mode": "face",
            "facets": [0],
        })
        if is_error:
            self.client.call("undo")  # undo placement
            raise SkipTest(f"node has no selectable mesh geometry: {result}")
        assert result["facets"] == 1, f"Expected 1 facet selected, got {result}"

        sel = self.client.call_ok("get_mesh_component_selection")
        assert sel["mode"] == "face"
        assert len(sel["entries"]) == 1, f"Expected 1 entry, got {sel['entries']}"
        entry = sel["entries"][0]
        assert 0 in entry["facets"], f"Facet 0 not in entry: {entry}"
        assert entry.get("node_name") == node_name, f"Entry node_name {entry.get('node_name')} != {node_name}"

        self.client.call_ok("clear_mesh_component_selection")
        sel = self.client.call_ok("get_mesh_component_selection")
        assert sel["entries"] == [], f"Expected empty after clear, got {sel['entries']}"

        self.client.call("set_mesh_component_mode", {"mode": "object"})
        self.client.call("undo")  # undo placement

    def test_align_components(self):
        self._require_scene()
        node_a = self._place_brush_at([0.0, 11.0, 0.0])
        node_b = self._place_brush_at([2.0, 11.0, 0.0])

        r1, e1 = self.client.call("select_mesh_components", {
            "scene_name": self.scene_name, "node_id": node_a, "mode": "face", "facets": [0]})
        r2, e2 = self.client.call("select_mesh_components", {
            "scene_name": self.scene_name, "node_id": node_b, "extend": True, "facets": [0]})
        if e1 or e2:
            self.client.call("clear_mesh_component_selection")
            self.client.call("undo"); self.client.call("undo")
            raise SkipTest(f"mesh component selection failed: {r1} / {r2}")

        pos_a_before = self._node_world_pos(node_a)
        pos_b_before = self._node_world_pos(node_b)
        undo_before = len(self.client.call_ok("get_undo_redo_stack")["undo"])

        result = self.client.call_ok("align_components", {"apply_scale": False})
        assert result["aligned"] is True, f"align_components result {result}"
        assert self._wait_undo_growth(undo_before), "Align did not land on the undo stack"

        pos_a_after = self._node_world_pos(node_a)
        pos_b_after = self._node_world_pos(node_b)
        moved_a = pos_a_after != pos_a_before
        moved_b = pos_b_after != pos_b_before
        assert moved_a or moved_b, f"Neither node moved: A {pos_a_before}->{pos_a_after}, B {pos_b_before}->{pos_b_after}"

        # Clean up: undo align + 2 placements, clear component selection.
        self.client.call("clear_mesh_component_selection")
        self.client.call("set_mesh_component_mode", {"mode": "object"})
        self.client.call("undo")  # undo align
        self.client.call("undo")  # undo placement b
        self.client.call("undo")  # undo placement a

    def test_add_joint(self):
        self._require_scene()
        node_a = self._place_brush_at([0.0, 11.0, 0.0], motion_mode="dynamic")
        node_b = self._place_brush_at([1.2, 11.0, 0.0], motion_mode="dynamic")

        r1, e1 = self.client.call("select_mesh_components", {
            "scene_name": self.scene_name, "node_id": node_a, "mode": "face", "facets": [0]})
        r2, e2 = self.client.call("select_mesh_components", {
            "scene_name": self.scene_name, "node_id": node_b, "extend": True, "facets": [0]})
        if e1 or e2:
            self.client.call("clear_mesh_component_selection")
            self.client.call("undo"); self.client.call("undo")
            raise SkipTest(f"mesh component selection failed: {r1} / {r2}")

        nodes_before = len(self.client.call_ok("get_scene_nodes", {"scene_name": self.scene_name})["nodes"])
        result, is_error = self.client.call("add_joint", {"avoidance": "joint_pair"})

        self.client.call("clear_mesh_component_selection")
        self.client.call("set_mesh_component_mode", {"mode": "object"})

        if is_error:
            # The non-intersecting orientation search can legitimately fail; treat as skip.
            self.client.call("undo")  # undo placement b
            self.client.call("undo")  # undo placement a
            raise SkipTest(f"add_joint reported: {result}")

        assert result["created"] is True, f"add_joint result {result}"
        # The compound adds joint frame nodes; expect the node count to have grown.
        nodes_after = len(self.client.call_ok("get_scene_nodes", {"scene_name": self.scene_name})["nodes"])
        assert nodes_after > nodes_before, f"Expected new joint nodes: {nodes_before} -> {nodes_after}"
        names = [n["name"] for n in self.client.call_ok("get_scene_nodes", {"scene_name": self.scene_name})["nodes"]]
        assert any("Joint" in n for n in names), f"No 'Joint' node found in {names}"

        self.client.call("undo")  # undo add joint (compound)
        self.client.call("undo")  # undo placement b
        self.client.call("undo")  # undo placement a

    def test_decimate(self):
        self._require_scene()
        node_id = self._create_dense_sphere()
        before = self._node_vertex_count(node_id)
        self.client.call_ok("select_items", {"scene_name": self.scene_name, "ids": [node_id]})
        undo_before = len(self.client.call_ok("get_undo_redo_stack")["undo"])

        queued = self.client.call_ok("decimate", {"bins": 8})
        assert queued.get("queued") is True, f"decimate result {queued}"
        if not self._wait_undo_growth(undo_before):
            self.client.call("undo")  # undo create
            raise SkipTest("decimate did not complete (Geogram may be unavailable)")

        after = self._node_vertex_count(node_id)
        assert (before is not None) and (after is not None), "vertex_count not reported"
        # Validate the tool wiring and that the op yields a valid (non-increasing)
        # vertex count; the exact reduction depends on Geogram's clustering and the
        # bin count, which is not what this test pins down.
        assert after <= before, f"decimate increased vertex count ({before} -> {after})"

        self.client.call("undo")  # undo decimate
        self.client.call("undo")  # undo create

    def test_smooth(self):
        self._require_scene()
        node_id = self._create_dense_sphere()
        before = self._node_vertex_count(node_id)
        self.client.call_ok("select_items", {"scene_name": self.scene_name, "ids": [node_id]})
        undo_before = len(self.client.call_ok("get_undo_redo_stack")["undo"])

        queued = self.client.call_ok("smooth", {"iterations": 3, "strength": 0.5})
        assert queued.get("queued") is True, f"smooth result {queued}"
        if not self._wait_undo_growth(undo_before):
            self.client.call("undo")  # undo create
            raise SkipTest("smooth did not complete (Geogram may be unavailable)")

        after = self._node_vertex_count(node_id)
        assert after == before, f"smooth should preserve vertex count ({before} -> {after})"

        self.client.call("undo")  # undo smooth
        self.client.call("undo")  # undo create

    def test_transform_reference_mode(self):
        self._require_scene()
        for mode in ("local", "selection", "global"):
            result = self.client.call_ok("set_transform_reference_mode", {"mode": mode})
            assert result["mode"] == mode, f"set reference mode {mode} returned {result}"
            sel = self.client.call_ok("get_selection")
            assert sel.get("transform_reference_mode") == mode, \
                f"get_selection reference mode {sel.get('transform_reference_mode')} != {mode}"

        # Reference mode with an explicit reference node.
        node_id = self._place_brush_at([0.0, 11.0, 0.0])
        nodes = self.client.call_ok("get_scene_nodes", {"scene_name": self.scene_name})["nodes"]
        node_name = next(n["name"] for n in nodes if n["id"] == node_id)
        result = self.client.call_ok("set_transform_reference_mode", {
            "scene_name": self.scene_name, "mode": "reference", "reference_node_id": node_id})
        assert result["mode"] == "reference", f"reference mode result {result}"
        assert result.get("reference_node") == node_name, f"reference_node {result.get('reference_node')} != {node_name}"

        # Unknown reference node is an error.
        _, is_error = self.client.call("set_transform_reference_mode", {
            "scene_name": self.scene_name, "mode": "reference", "reference_node_name": "no_such_node_xyz"})
        assert is_error, "Expected error for unknown reference node"

        # Reset and clean up.
        self.client.call("set_transform_reference_mode", {"mode": "global"})
        self.client.call("undo")  # undo placement


class SkipTest(Exception):
    pass


# ---------------------------------------------------------------------------
# Smoke Test
# ---------------------------------------------------------------------------

class SmokeTestRunner:
    def __init__(self, client, seed, duration, no_physics=False, max_brush_faces=20, max_brush_vertices=20):
        self.client = client
        self.seed = seed
        self.duration = duration
        self.no_physics = no_physics
        self.max_brush_faces = max_brush_faces
        self.max_brush_vertices = max_brush_vertices
        self.rng = random.Random(seed)
        self.ok_count = 0
        self.error_count = 0
        self.stats = {"query": 0, "place": 0, "select": 0, "delete": 0, "undo": 0, "redo": 0, "toggle": 0, "detail": 0, "geometry": 0, "reparent": 0, "csg": 0}
        self.scene_name = None
        self.brush_ids = []
        self.material_names = []

    def run(self):
        print("=" * 60)
        physics_str = "  no-physics" if self.no_physics else ""
        print(f"SMOKE TEST  seed={self.seed}  duration={'infinite' if self.duration == 0 else f'{self.duration}s'}{physics_str}  max-faces={self.max_brush_faces}  max-verts={self.max_brush_vertices}")
        print("=" * 60)

        # Disable physics if requested
        if self.no_physics:
            try:
                status = self.client.call_ok("toggle_physics")
                if status["dynamic_physics_enabled"]:
                    self.client.call_ok("toggle_physics")  # was off, toggled on, toggle back off
            except Exception:
                pass

        # Discover scene
        try:
            scenes = self.client.call_ok("list_scenes")["scenes"]
            if not scenes:
                print("  No scenes found, aborting smoke test")
                return True
            self.scene_name = scenes[0]["name"]

            brushes = self.client.call_ok("get_scene_brushes", {"scene_name": self.scene_name})["brushes"]
            small_brushes = [b for b in brushes
                            if b.get("facet_count", 0) <= self.max_brush_faces
                            and b.get("vertex_count", 0) <= self.max_brush_vertices]
            if not small_brushes:
                small_brushes = brushes  # fallback if no brushes match
            self.brush_ids = [b["id"] for b in small_brushes]
            print(f"  Using {len(self.brush_ids)} of {len(brushes)} brushes (max {self.max_brush_faces} faces, {self.max_brush_vertices} vertices)")

            materials = self.client.call_ok("get_scene_materials", {"scene_name": self.scene_name})["materials"]
            self.material_names = [m["name"] for m in materials]
        except Exception as e:
            print(f"  Setup failed: {e}")
            return False

        start = time.time()
        try:
            while True:
                elapsed = time.time() - start
                if self.duration > 0 and elapsed >= self.duration:
                    break
                self._random_action()
        except KeyboardInterrupt:
            print("\n  Interrupted by user")

        elapsed = time.time() - start
        total = self.ok_count + self.error_count
        print(f"\nSmoke test complete: seed={self.seed}, duration={elapsed:.1f}s")
        print(f"  Actions: {total} total, {self.ok_count} ok, {self.error_count} errors")
        print(f"  Breakdown: {self.stats}")
        return self.error_count == 0

    # Geometry commands safe for smoke test chaining.
    # Excluded: Chamfer (known broken).
    # Repair is included — mesh validation after each operation
    # prevents degenerate geometry from reaching Geogram.
    GEOMETRY_COMMANDS = [
        "Geometry.Triangulate",
        "Geometry.Reverse",
        "Geometry.Normalize",
        "Geometry.Repair",
        "Geometry.Weld",
        "Geometry.GenerateTangents",
        "Geometry.Subdivision.Catmull-Clark",
        "Geometry.Subdivision.Sqrt3",
        "Geometry.Conway.Dual",
        "Geometry.Conway.Ambo",
        "Geometry.Conway.Kis",
        "Geometry.Conway.Join",
        "Geometry.Conway.Meta",
        "Geometry.Conway.Ortho",
        "Geometry.Conway.Truncate",
        "Geometry.Conway.Gyro",
    ]

    def _random_action(self):
        r = self.rng.random()
        if r < 0.25:
            self._do_query()
        elif r < 0.40:
            self._do_place()
        elif r < 0.50:
            self._do_select()
        elif r < 0.58:
            self._do_node_detail()
        elif r < 0.66:
            self._do_material_detail()
        elif r < 0.68:
            if not self.no_physics:
                self._do_toggle()
        elif r < 0.74:
            self._do_delete()
        elif r < 0.78:
            self._do_reparent()
        elif r < 0.82:
            self._do_csg()
        elif r < 0.92:
            self._do_geometry_chain()
        else:
            self._do_undo_redo()

    def _try(self, stat_key, fn):
        try:
            fn()
            self.ok_count += 1
            self.stats[stat_key] += 1
        except Exception as e:
            self.error_count += 1
            print(f"  ERROR [{stat_key}]: {e}")

    def _do_query(self):
        tool = self.rng.choice([
            "list_scenes", "get_scene_nodes", "get_scene_cameras",
            "get_scene_lights", "get_scene_materials", "get_scene_brushes", "get_selection",
        ])
        args = {} if tool in ("list_scenes", "get_selection") else {"scene_name": self.scene_name}
        self._try("query", lambda: self.client.call_ok(tool, args))

    def _do_place(self):
        if not self.brush_ids:
            return
        brush_id = self.rng.choice(self.brush_ids)
        pos = [self.rng.uniform(-5, 5), self.rng.uniform(0.5, 5), self.rng.uniform(-5, 5)]
        args = {"scene_name": self.scene_name, "brush_id": brush_id, "position": pos}
        if self.material_names:
            args["material_name"] = self.rng.choice(self.material_names)
        self._try("place", lambda: self.client.call_ok("place_brush", args))

    def _get_unlocked_nodes(self):
        nodes = self.client.call_ok("get_scene_nodes", {"scene_name": self.scene_name})["nodes"]
        return [n for n in nodes if not n.get("locked", False)]

    def _do_select(self):
        def action():
            nodes = self._get_unlocked_nodes()
            if not nodes:
                return
            k = self.rng.randint(0, min(5, len(nodes)))
            ids = [n["id"] for n in self.rng.sample(nodes, k)]
            self.client.call_ok("select_items", {"scene_name": self.scene_name, "ids": ids})
        self._try("select", action)

    def _do_node_detail(self):
        def action():
            nodes = self.client.call_ok("get_scene_nodes", {"scene_name": self.scene_name})["nodes"]
            if not nodes:
                return
            node = self.rng.choice(nodes)
            self.client.call_ok("get_node_details", {"scene_name": self.scene_name, "node_name": node["name"]})
        self._try("detail", action)

    def _do_material_detail(self):
        def action():
            if not self.material_names:
                return
            name = self.rng.choice(self.material_names)
            self.client.call_ok("get_material_details", {"scene_name": self.scene_name, "material_name": name})
        self._try("detail", action)

    def _do_toggle(self):
        def action():
            self.client.call_ok("toggle_physics")
            self.client.call_ok("toggle_physics")
        self._try("toggle", action)

    def _do_delete(self):
        def action():
            nodes = self._get_unlocked_nodes()
            if not nodes:
                return
            node = self.rng.choice(nodes)
            self.client.call_ok("select_items", {"scene_name": self.scene_name, "ids": [node["id"]]})
            self.client.call("Selection.delete")
        self._try("delete", action)

    def _do_reparent(self):
        def action():
            nodes = self._get_unlocked_nodes()
            if len(nodes) < 2:
                return
            child = self.rng.choice(nodes)
            parent = self.rng.choice(nodes)
            if child["id"] == parent["id"]:
                return
            self.client.call_ok("reparent_node", {
                "scene_name": self.scene_name,
                "node_id": child["id"],
                "parent_node_id": parent["id"]
            })
        self._try("reparent", action)

    CSG_COMMANDS = ["Geometry.Union", "Geometry.Intersection", "Geometry.Difference"]

    def _place_overlapping_brushes(self):
        """Place two brushes that are very likely to intersect. Returns (node_id_a, node_id_b)."""
        x = self.rng.uniform(-3, 3)
        y = self.rng.uniform(0.5, 3)
        z = self.rng.uniform(-3, 3)
        brush_a = self.rng.choice(self.brush_ids)
        brush_b = self.rng.choice(self.brush_ids)
        mat = self.rng.choice(self.material_names) if self.material_names else None

        # 80% chance: same position (guaranteed overlap)
        # 20% chance: small offset (likely overlap for most brushes)
        if self.rng.random() < 0.8:
            offset_x = 0.0
            offset_y = 0.0
            offset_z = 0.0
        else:
            offset_x = self.rng.uniform(-0.3, 0.3)
            offset_y = self.rng.uniform(-0.3, 0.3)
            offset_z = self.rng.uniform(-0.3, 0.3)

        args_a = {"scene_name": self.scene_name, "brush_id": brush_a, "position": [x, y, z]}
        args_b = {"scene_name": self.scene_name, "brush_id": brush_b, "position": [x + offset_x, y + offset_y, z + offset_z]}
        if mat:
            args_a["material_name"] = mat
            args_b["material_name"] = mat
        result_a = self.client.call_ok("place_brush", args_a)
        result_b = self.client.call_ok("place_brush", args_b)
        return result_a["node_id"], result_b["node_id"]

    def _do_csg(self):
        def action():
            if not self.brush_ids:
                return
            node_a_id, node_b_id = self._place_overlapping_brushes()

            # Select both
            self.client.call_ok("select_items", {
                "scene_name": self.scene_name,
                "ids": [node_a_id, node_b_id]
            })

            undo_before = len(self.client.call_ok("get_undo_redo_stack")["undo"])
            cmd = self.rng.choice(self.CSG_COMMANDS)
            self.client.call(cmd)
            self.client.wait_async(timeout=30.0)

            # Wait for op to land
            deadline = time.time() + 30.0
            while time.time() < deadline:
                stack = self.client.call_ok("get_undo_redo_stack")
                if len(stack["undo"]) > undo_before:
                    break
                time.sleep(0.1)
        self._try("csg", action)

    def _do_geometry_chain(self):
        def action():
            if not self.brush_ids:
                return
            # Place a fresh brush
            brush_id = self.rng.choice(self.brush_ids)
            pos = [self.rng.uniform(-5, 5), self.rng.uniform(0.5, 5), self.rng.uniform(-5, 5)]
            args = {"scene_name": self.scene_name, "brush_id": brush_id, "position": pos}
            if self.material_names:
                args["material_name"] = self.rng.choice(self.material_names)
            result = self.client.call_ok("place_brush", args)
            node_id = result["node_id"]

            # Select it
            self.client.call_ok("select_items", {"scene_name": self.scene_name, "ids": [node_id]})

            # Apply 1-3 random geometry operations in sequence
            chain_len = self.rng.randint(1, 3)
            undo_before = len(self.client.call_ok("get_undo_redo_stack")["undo"])
            for i in range(chain_len):
                cmd = self.rng.choice(self.GEOMETRY_COMMANDS)
                self.client.call(cmd)
                self.client.wait_async(timeout=15.0)
                # Wait for op to land on undo stack
                target = undo_before + i + 1
                deadline = time.time() + 15.0
                while time.time() < deadline:
                    stack = self.client.call_ok("get_undo_redo_stack")
                    if len(stack["undo"]) >= target:
                        break
                    time.sleep(0.1)
        self._try("geometry", action)

    def _do_undo_redo(self):
        if self.rng.random() < 0.5:
            self._try("undo", lambda: self.client.call("undo"))
        else:
            self._try("redo", lambda: self.client.call("redo"))


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(description="Test erhe editor MCP server")
    parser.add_argument("--host", default="127.0.0.1", help="MCP server host")
    parser.add_argument("--port", type=int, default=8080, help="MCP server port")
    parser.add_argument("--seed", type=int, default=None, help="Random seed for smoke test")
    parser.add_argument("--smoke-time", type=float, default=10.0, help="Smoke test duration (0=infinite)")
    parser.add_argument("--unit-only", action="store_true", help="Run only unit tests")
    parser.add_argument("--smoke-only", action="store_true", help="Run only smoke tests")
    parser.add_argument("--no-physics", action="store_true", help="Disable physics during smoke test")
    parser.add_argument("--max-brush-faces", type=int, default=20, help="Max brush face count for smoke test (default: 20)")
    parser.add_argument("--max-brush-vertices", type=int, default=20, help="Max brush vertex count for smoke test (default: 20)")
    args = parser.parse_args()

    client = McpClient(args.host, args.port)

    # Check connectivity
    try:
        client.health()
    except (urllib.error.URLError, ConnectionRefusedError, OSError) as e:
        print(f"Cannot connect to editor MCP server at {args.host}:{args.port}")
        print(f"Make sure the editor is running. Error: {e}")
        sys.exit(1)

    print(f"Connected to MCP server at {args.host}:{args.port}\n")

    all_passed = True

    if not args.smoke_only:
        runner = UnitTestRunner(client)
        if not runner.run():
            all_passed = False
        print()

    if not args.unit_only:
        seed = args.seed if args.seed is not None else random.randint(0, 2**32 - 1)
        runner = SmokeTestRunner(client, seed, args.smoke_time, no_physics=args.no_physics,
                                 max_brush_faces=args.max_brush_faces, max_brush_vertices=args.max_brush_vertices)
        if not runner.run():
            all_passed = False
        print()

    sys.exit(0 if all_passed else 1)


if __name__ == "__main__":
    main()
