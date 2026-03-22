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


class SkipTest(Exception):
    pass


# ---------------------------------------------------------------------------
# Smoke Test
# ---------------------------------------------------------------------------

class SmokeTestRunner:
    def __init__(self, client, seed, duration):
        self.client = client
        self.seed = seed
        self.duration = duration
        self.rng = random.Random(seed)
        self.ok_count = 0
        self.error_count = 0
        self.stats = {"query": 0, "place": 0, "select": 0, "delete": 0, "undo": 0, "redo": 0, "toggle": 0, "detail": 0}
        self.scene_name = None
        self.brush_ids = []
        self.material_names = []

    def run(self):
        print("=" * 60)
        print(f"SMOKE TEST  seed={self.seed}  duration={'infinite' if self.duration == 0 else f'{self.duration}s'}")
        print("=" * 60)

        # Discover scene
        try:
            scenes = self.client.call_ok("list_scenes")["scenes"]
            if not scenes:
                print("  No scenes found, aborting smoke test")
                return True
            self.scene_name = scenes[0]["name"]

            brushes = self.client.call_ok("get_scene_brushes", {"scene_name": self.scene_name})["brushes"]
            self.brush_ids = [b["id"] for b in brushes]

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

    def _random_action(self):
        r = self.rng.random()
        if r < 0.30:
            self._do_query()
        elif r < 0.50:
            self._do_place()
        elif r < 0.65:
            self._do_select()
        elif r < 0.75:
            self._do_node_detail()
        elif r < 0.85:
            self._do_material_detail()
        elif r < 0.90:
            self._do_toggle()
        elif r < 0.95:
            self._do_delete()
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

    def _do_select(self):
        def action():
            nodes = self.client.call_ok("get_scene_nodes", {"scene_name": self.scene_name})["nodes"]
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
            nodes = self.client.call_ok("get_scene_nodes", {"scene_name": self.scene_name})["nodes"]
            if not nodes:
                return
            node = self.rng.choice(nodes)
            self.client.call_ok("select_items", {"scene_name": self.scene_name, "ids": [node["id"]]})
            self.client.call("Selection.delete")
        self._try("delete", action)

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
        runner = SmokeTestRunner(client, seed, args.smoke_time)
        if not runner.run():
            all_passed = False
        print()

    sys.exit(0 if all_passed else 1)


if __name__ == "__main__":
    main()
