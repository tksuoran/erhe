#!/usr/bin/env python3
"""Call a tool on the in-editor MCP server (see AGENTS.md "In-editor MCP server").

Usage:
    py -3 scripts/mcp_call.py <tool> [json-arguments] [--port N]
    py -3 scripts/mcp_call.py --list [pattern] [--port N]

The JSON arguments may be given as:
    - a JSON object literal   (shell quoting permitting)
    - base64 of a JSON object, prefixed "b64:" - avoids shell quoting entirely
    - "-" to read the JSON object from stdin

PowerShell example (base64 sidesteps PowerShell 5.1 quote mangling):
    $b64 = "b64:" + [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes('{"shape":"box"}'))
    py -3 scripts/mcp_call.py create_shape $b64

The editor binds 127.0.0.1:8080 by default and scans [8080, 8100) when the
port is taken; grep logs/log.txt for "MCP server: listening" to find the
actual port and pass it with --port. If ~/.agents/erhe_mcp_token exists its
contents are sent as a bearer token (matches the server's auth behavior).
"""

import argparse
import base64
import json
import pathlib
import sys
import urllib.error
import urllib.request


def rpc(port, method, params):
    body = json.dumps({"jsonrpc": "2.0", "id": 1, "method": method, "params": params}).encode("utf-8")
    headers = {"Content-Type": "application/json"}
    token_path = pathlib.Path.home() / ".agents" / "erhe_mcp_token"
    if token_path.is_file():
        headers["Authorization"] = "Bearer " + token_path.read_text(encoding="utf-8").strip()
    request = urllib.request.Request(f"http://127.0.0.1:{port}/mcp", data=body, headers=headers)
    with urllib.request.urlopen(request, timeout=30) as response:
        return json.loads(response.read().decode("utf-8"))


def parse_arguments_value(raw):
    if raw == "-":
        raw = sys.stdin.read()
    elif raw.startswith("b64:"):
        raw = base64.b64decode(raw[4:]).decode("utf-8")
    return json.loads(raw)


def main():
    parser = argparse.ArgumentParser(description="Call a tool on the in-editor MCP server")
    parser.add_argument("tool", nargs="?", help="tool name (see --list)")
    parser.add_argument("arguments", nargs="?", default=None, help="JSON object / b64:<base64 JSON> / '-' for stdin")
    parser.add_argument("--port", type=int, default=8080, help="MCP port (default 8080)")
    parser.add_argument("--list", nargs="?", const="", default=None, metavar="PATTERN", help="list tool names (optionally filtered by substring)")
    args = parser.parse_args()

    try:
        if args.list is not None:
            payload = rpc(args.port, "tools/list", {})
            for tool in payload.get("result", {}).get("tools", []):
                if args.list.lower() in tool["name"].lower():
                    print(tool["name"])
            return 0

        if not args.tool:
            parser.error("tool name required (or use --list)")
        arguments = parse_arguments_value(args.arguments) if args.arguments else {}
        payload = rpc(args.port, "tools/call", {"name": args.tool, "arguments": arguments})
    except urllib.error.URLError as error:
        print(f"error: cannot reach MCP server on 127.0.0.1:{args.port} ({error.reason}).", file=sys.stderr)
        print("Is the editor running? Check logs/log.txt for 'MCP server: listening on 127.0.0.1:<port>'.", file=sys.stderr)
        return 1

    if "error" in payload:
        print(json.dumps(payload["error"], indent=2), file=sys.stderr)
        return 1
    result = payload.get("result", {})
    content = result.get("content")
    if isinstance(content, list) and content and "text" in content[0]:
        print(content[0]["text"])
    else:
        print(json.dumps(result, indent=2))
    return 0 if not result.get("isError", False) else 1


if __name__ == "__main__":
    sys.exit(main())
