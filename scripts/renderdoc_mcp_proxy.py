#!/usr/bin/env python3
"""
renderdoc_mcp_proxy.py -- stdio MCP proxy in front of the RenderDoc fork's
embedded HTTP MCP server (qrenderdoc --mcp-server).

Why this exists
---------------
Claude Code connects every MCP server at session start and fetches tools/list
then; there is no lazy-server config flag and the model itself cannot trigger a
reconnect (only the user, via the /mcp command). An http registration that
points straight at qrenderdoc therefore only yields callable tools if
qrenderdoc happened to be running when Claude Code started.

A *stdio* server, by contrast, is always spawned by Claude Code at startup, so
its tools are always registered. This proxy exploits that:

  * It advertises a baked-in copy of the fork's tool schema
    (scripts/renderdoc_tools.json, captured once with
    capture_renderdoc_tools.py) so the fork's tools are visible immediately,
    without qrenderdoc running.
  * It launches `qrenderdoc --mcp-server` on the first tool call that needs the
    backend (or eagerly via the explicit renderdoc_launch tool) and proxies
    JSON-RPC to it.
  * It leaves qrenderdoc running when this proxy / Claude Code exits. Only the
    explicit renderdoc_shutdown tool tears down a qrenderdoc that THIS proxy
    launched; an externally started qrenderdoc is never touched.

Transport: MCP stdio is newline-delimited JSON-RPC 2.0 -- one message per line
on stdin/stdout. All diagnostics go to stderr only (stdout carries protocol).

Config (environment overrides):
  ERHE_RENDERDOC_QRENDERDOC       path to the fork's qrenderdoc.exe
  ERHE_RENDERDOC_MCP_HOST         backend host, default 127.0.0.1
  ERHE_RENDERDOC_MCP_PORT         backend port, default 7398
  ERHE_RENDERDOC_LAUNCH_TIMEOUT   seconds to wait for the backend, default 60
"""
import json
import os
import socket
import subprocess
import sys
import time
import urllib.error
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
SCHEMA_PATH = os.path.join(HERE, "renderdoc_tools.json")

QRENDERDOC_PATH = os.environ.get(
    "ERHE_RENDERDOC_QRENDERDOC",
    r"D:\renderdoc\x64\Development\qrenderdoc.exe",
)
HOST = os.environ.get("ERHE_RENDERDOC_MCP_HOST", "127.0.0.1")
PORT = int(os.environ.get("ERHE_RENDERDOC_MCP_PORT", "7398"))
LAUNCH_TIMEOUT = float(os.environ.get("ERHE_RENDERDOC_LAUNCH_TIMEOUT", "60"))
URL = f"http://{HOST}:{PORT}/"

PROTOCOL_VERSION_DEFAULT = "2024-11-05"

# pid of a qrenderdoc that THIS proxy launched. Stays None when qrenderdoc was
# already running (started externally) or never launched. renderdoc_shutdown
# only terminates a pid recorded here, so externally started instances survive.
_launched_pid = None


def log(msg):
    sys.stderr.write(f"[renderdoc-proxy] {msg}\n")
    sys.stderr.flush()


# ---------------------------------------------------------------------------
# Tools owned by the proxy itself (always available, even with no backend / no
# baked schema). The fork's own tools are merged in from renderdoc_tools.json.
# ---------------------------------------------------------------------------
MANAGEMENT_TOOLS = [
    {
        "name": "renderdoc_launch",
        "description": (
            "Ensure the RenderDoc fork MCP server (qrenderdoc --mcp-server) is "
            "running, launching it on demand if needed. Use to pre-warm before "
            "a capture so the first capture tool call does not pay the "
            "qrenderdoc cold-start delay."
        ),
        "inputSchema": {"type": "object", "properties": {}, "additionalProperties": False},
    },
    {
        "name": "renderdoc_status",
        "description": (
            "Report whether the RenderDoc fork MCP server is reachable, the "
            "host/port and qrenderdoc path in use, and whether this proxy "
            "launched the running qrenderdoc."
        ),
        "inputSchema": {"type": "object", "properties": {}, "additionalProperties": False},
    },
    {
        "name": "renderdoc_shutdown",
        "description": (
            "Terminate the qrenderdoc instance that THIS proxy launched. If "
            "qrenderdoc was started externally (or is not running), it is left "
            "untouched."
        ),
        "inputSchema": {"type": "object", "properties": {}, "additionalProperties": False},
    },
]
MANAGEMENT_TOOL_NAMES = {t["name"] for t in MANAGEMENT_TOOLS}


# ---------------------------------------------------------------------------
# Backend HTTP transport (the fork server is stateless JSON-RPC over POST).
# ---------------------------------------------------------------------------
def post_jsonrpc(method, params, timeout):
    """POST a single JSON-RPC request to the fork server and return the parsed
    response dict. Raises urllib/socket errors if the server is unreachable and
    ValueError if the body is not parseable JSON-RPC."""
    payload = json.dumps(
        {"jsonrpc": "2.0", "id": 1, "method": method, "params": params}
    ).encode("utf-8")
    req = urllib.request.Request(
        URL,
        data=payload,
        method="POST",
        headers={
            "Content-Type": "application/json",
            "Accept": "application/json, text/event-stream",
        },
    )
    with urllib.request.urlopen(req, timeout=timeout) as resp:
        content_type = resp.headers.get("Content-Type", "")
        body = resp.read().decode("utf-8", errors="replace")
    if "text/event-stream" in content_type:
        return _parse_sse(body)
    return json.loads(body)


def _parse_sse(body):
    """Extract the JSON-RPC object from a text/event-stream body (data: lines)."""
    data_parts = []
    for line in body.splitlines():
        if line.startswith("data:"):
            data_parts.append(line[len("data:"):].lstrip())
    if not data_parts:
        raise ValueError("SSE response contained no data: lines")
    return json.loads("".join(data_parts))


def is_server_ready(timeout=1.0):
    """True if the backend port is open AND answers an MCP initialize probe."""
    try:
        with socket.create_connection((HOST, PORT), timeout=timeout):
            pass
    except OSError:
        return False  # fast negative: nothing is listening
    try:
        resp = post_jsonrpc(
            "initialize",
            {
                "protocolVersion": PROTOCOL_VERSION_DEFAULT,
                "capabilities": {},
                "clientInfo": {"name": "renderdoc-mcp-proxy", "version": "1"},
            },
            timeout=timeout + 2.0,
        )
        return isinstance(resp, dict) and (("result" in resp) or ("error" in resp))
    except (urllib.error.URLError, OSError, ValueError):
        return False


def launch_qrenderdoc():
    """Start qrenderdoc detached so it outlives this proxy. Returns its pid."""
    global _launched_pid
    if not os.path.isfile(QRENDERDOC_PATH):
        raise FileNotFoundError(
            f"qrenderdoc.exe not found at '{QRENDERDOC_PATH}'. Set "
            f"ERHE_RENDERDOC_QRENDERDOC to the fork build's qrenderdoc.exe."
        )
    creationflags = 0
    # Detach from this proxy's console / process group so it keeps running after
    # Claude Code (and this proxy) exit.
    creationflags |= getattr(subprocess, "DETACHED_PROCESS", 0)
    creationflags |= getattr(subprocess, "CREATE_NEW_PROCESS_GROUP", 0)
    log(f"launching {QRENDERDOC_PATH} --mcp-server --mcp-port {PORT}")
    proc = subprocess.Popen(
        [QRENDERDOC_PATH, "--mcp-server", "--mcp-port", str(PORT)],
        creationflags=creationflags,
        close_fds=True,
        stdin=subprocess.DEVNULL,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    _launched_pid = proc.pid
    return proc.pid


def ensure_server(timeout=LAUNCH_TIMEOUT):
    """Return (ready: bool, message: str). If the backend is not already up,
    launch qrenderdoc and poll until its MCP server answers, up to timeout."""
    if is_server_ready():
        return True, "RenderDoc MCP server already running"
    pid = launch_qrenderdoc()
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if is_server_ready():
            return True, f"launched qrenderdoc (pid {pid}); MCP server ready"
        time.sleep(0.3)
    return False, (
        f"launched qrenderdoc (pid {pid}) but its MCP server did not become "
        f"ready within {timeout:.0f}s on {URL}"
    )


# ---------------------------------------------------------------------------
# Baked tool schema
# ---------------------------------------------------------------------------
def load_baked_tools():
    """Load the fork's tool definitions captured by capture_renderdoc_tools.py.
    Tolerates absence (advertise only management tools) and a bare-list or
    {"tools": [...]} shape."""
    try:
        with open(SCHEMA_PATH, "r", encoding="utf-8") as f:
            data = json.load(f)
    except FileNotFoundError:
        log(
            f"baked schema '{SCHEMA_PATH}' not found; advertising management "
            f"tools only. Run 'py -3 scripts/capture_renderdoc_tools.py' to "
            f"generate it."
        )
        return []
    except (OSError, ValueError) as e:
        log(f"failed to read baked schema '{SCHEMA_PATH}': {e}")
        return []
    tools = data.get("tools", []) if isinstance(data, dict) else data
    if not isinstance(tools, list):
        log(f"baked schema '{SCHEMA_PATH}' has an unexpected shape; ignoring")
        return []
    return tools


# ---------------------------------------------------------------------------
# JSON-RPC helpers
# ---------------------------------------------------------------------------
def ok_response(req_id, result):
    return {"jsonrpc": "2.0", "id": req_id, "result": result}


def err_response(req_id, code, message):
    return {"jsonrpc": "2.0", "id": req_id, "error": {"code": code, "message": message}}


def err_response_obj(req_id, error):
    return {"jsonrpc": "2.0", "id": req_id, "error": error}


def text_result(text, is_error=False):
    return {"content": [{"type": "text", "text": text}], "isError": is_error}


# ---------------------------------------------------------------------------
# Tool dispatch
# ---------------------------------------------------------------------------
def handle_management_call(name, arguments):
    global _launched_pid
    if name == "renderdoc_status":
        status = {
            "ready": is_server_ready(),
            "url": URL,
            "qrenderdoc_path": QRENDERDOC_PATH,
            "launched_by_proxy_pid": _launched_pid,
        }
        return text_result(json.dumps(status, indent=2))
    if name == "renderdoc_launch":
        try:
            ready, msg = ensure_server()
        except FileNotFoundError as e:
            return text_result(str(e), is_error=True)
        return text_result(msg, is_error=not ready)
    if name == "renderdoc_shutdown":
        if _launched_pid is None:
            return text_result(
                "qrenderdoc was not launched by this proxy (started externally "
                "or not running); leaving it untouched."
            )
        pid = _launched_pid
        try:
            subprocess.run(
                ["taskkill", "/PID", str(pid), "/T", "/F"],
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
                check=False,
            )
        except OSError as e:
            return text_result(
                f"failed to terminate qrenderdoc pid {pid}: {e}", is_error=True
            )
        _launched_pid = None
        return text_result(f"terminated qrenderdoc pid {pid}")
    return text_result(f"unknown management tool '{name}'", is_error=True)


def handle_tools_call(req_id, name, arguments):
    if name in MANAGEMENT_TOOL_NAMES:
        return ok_response(req_id, handle_management_call(name, arguments))

    # A fork tool: make sure the backend is up, then proxy the call through.
    try:
        ready, msg = ensure_server()
    except FileNotFoundError as e:
        return ok_response(req_id, text_result(str(e), is_error=True))
    if not ready:
        return ok_response(req_id, text_result(msg, is_error=True))

    try:
        fork_resp = post_jsonrpc(
            "tools/call", {"name": name, "arguments": arguments}, timeout=120.0
        )
    except (urllib.error.URLError, OSError, ValueError) as e:
        return ok_response(
            req_id,
            text_result(
                f"failed to reach the RenderDoc MCP server at {URL} while "
                f"calling '{name}': {e}",
                is_error=True,
            ),
        )

    if "result" in fork_resp:
        return ok_response(req_id, fork_resp["result"])
    if "error" in fork_resp:
        return err_response_obj(req_id, fork_resp["error"])
    return ok_response(
        req_id,
        text_result(
            f"RenderDoc MCP server returned an unexpected response for "
            f"'{name}': {fork_resp}",
            is_error=True,
        ),
    )


def handle_request(req):
    """Return a response dict for a request, or None for a notification."""
    method = req.get("method")
    req_id = req.get("id")
    is_notification = "id" not in req

    if method == "initialize":
        params = req.get("params") or {}
        protocol_version = params.get("protocolVersion", PROTOCOL_VERSION_DEFAULT)
        return ok_response(
            req_id,
            {
                "protocolVersion": protocol_version,
                "capabilities": {"tools": {"listChanged": False}},
                "serverInfo": {"name": "renderdoc-mcp-proxy", "version": "1.0.0"},
            },
        )

    if method in ("notifications/initialized", "initialized"):
        return None

    if method == "ping":
        return ok_response(req_id, {})

    if method == "tools/list":
        baked = [
            t for t in load_baked_tools() if t.get("name") not in MANAGEMENT_TOOL_NAMES
        ]
        return ok_response(req_id, {"tools": list(MANAGEMENT_TOOLS) + baked})

    if method == "tools/call":
        params = req.get("params") or {}
        name = params.get("name")
        arguments = params.get("arguments") or {}
        return handle_tools_call(req_id, name, arguments)

    if is_notification:
        return None
    return err_response(req_id, -32601, f"Method not found: {method}")


def main():
    try:
        sys.stdin.reconfigure(encoding="utf-8")
        sys.stdout.reconfigure(encoding="utf-8", newline="\n")
    except AttributeError:
        pass  # Python without TextIOWrapper.reconfigure (< 3.7)
    log(
        f"started; backend {URL}, qrenderdoc '{QRENDERDOC_PATH}', schema "
        f"{'present' if os.path.isfile(SCHEMA_PATH) else 'MISSING'}"
    )
    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue
        try:
            req = json.loads(line)
        except ValueError as e:
            log(f"ignoring non-JSON line: {e}")
            continue
        try:
            resp = handle_request(req)
        except Exception as e:  # one bad request must not kill the server loop
            req_id = req.get("id") if isinstance(req, dict) else None
            log(f"handler error: {e}")
            resp = err_response(req_id, -32603, f"Internal proxy error: {e}")
        if resp is not None:
            sys.stdout.write(json.dumps(resp) + "\n")
            sys.stdout.flush()
    log("stdin closed; exiting (qrenderdoc left running)")


if __name__ == "__main__":
    main()
