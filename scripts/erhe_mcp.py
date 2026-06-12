"""Shared helpers for erhe editor MCP integration test scripts.

The editor's MCP server listens on http://127.0.0.1:<port>/mcp (JSON-RPC 2.0).
Tool results arrive as a text content block whose text is a JSON document;
McpClient.call() unwraps and parses it.
"""

import json
import math
import os
import time
import urllib.error
import urllib.request


class McpClient:
    def __init__(self, port: int) -> None:
        self.url = f"http://127.0.0.1:{port}/mcp"
        self.next_id = 0
        self.headers = {"Content-Type": "application/json"}
        token_path = os.path.join(os.path.expanduser("~"), ".claude", "erhe_mcp_token")
        if os.path.isfile(token_path):
            with open(token_path, "r", encoding="utf-8") as f:
                token = f.read().strip()
            if token:
                self.headers["Authorization"] = f"Bearer {token}"

    def rpc(self, method: str, params: dict) -> dict:
        self.next_id += 1
        request = {
            "jsonrpc": "2.0",
            "id": str(self.next_id),
            "method": method,
            "params": params,
        }
        data = json.dumps(request).encode("utf-8")
        req = urllib.request.Request(self.url, data=data, headers=self.headers)
        with urllib.request.urlopen(req, timeout=10) as response:
            reply = json.loads(response.read().decode("utf-8"))
        if "error" in reply:
            raise RuntimeError(f"JSON-RPC error from {method}: {reply['error']}")
        return reply["result"]

    def call(self, tool: str, arguments: dict | None = None) -> dict:
        """Call a tool and return its JSON payload (parsed from the text content)."""
        result = self.rpc("tools/call", {"name": tool, "arguments": arguments or {}})
        text = result["content"][0]["text"]
        if result.get("isError", False):
            raise RuntimeError(f"Tool {tool} failed: {text}")
        try:
            return json.loads(text)
        except json.JSONDecodeError:
            return {"text": text}

    def tool_names(self) -> set[str]:
        result = self.rpc("tools/list", {})
        return {tool["name"] for tool in result.get("tools", [])}


FAILURES: list[str] = []


def check_close(label: str, actual, expected, tolerance: float = 1.0e-4) -> None:
    ok = (len(actual) == len(expected)) and all(math.isclose(a, e, abs_tol=tolerance) for a, e in zip(actual, expected))
    status = "PASS" if ok else "FAIL"
    print(f"  [{status}] {label}: actual={actual} expected={expected}")
    if not ok:
        FAILURES.append(label)


def check_true(label: str, condition: bool, detail: str = "") -> None:
    status = "PASS" if condition else "FAIL"
    suffix = f" ({detail})" if detail else ""
    print(f"  [{status}] {label}{suffix}")
    if not condition:
        FAILURES.append(label)


def report() -> int:
    """Print a summary and return the process exit code."""
    if FAILURES:
        print(f"\n{len(FAILURES)} check(s) FAILED:")
        for failure in FAILURES:
            print(f"  - {failure}")
        return 1
    print("\nAll checks passed.")
    return 0


def wait_for_server(client: McpClient, timeout_s: float) -> None:
    deadline = time.monotonic() + timeout_s
    while True:
        try:
            client.rpc("initialize", {})
            return
        except (urllib.error.URLError, ConnectionError, OSError):
            if time.monotonic() >= deadline:
                raise RuntimeError(f"MCP server did not respond within {timeout_s} s")
            time.sleep(1.0)


def pick_scene(client: McpClient, scene_name: str) -> str:
    if scene_name:
        return scene_name
    scenes = client.call("list_scenes")["scenes"]
    if not scenes:
        raise RuntimeError("no scenes available")
    return scenes[0]["name"]
