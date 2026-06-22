#!/usr/bin/env python3
"""
capture_renderdoc_tools.py -- capture the RenderDoc fork MCP server's tools/list
into scripts/renderdoc_tools.json, which renderdoc_mcp_proxy.py serves as its
baked-in tool schema (so the fork's tools are advertised to Claude Code at
session start without qrenderdoc having to be running).

Run this ONCE after building or upgrading the fork. If qrenderdoc is not already
reachable, this script launches it on demand using the same configuration env
vars as the proxy (ERHE_RENDERDOC_QRENDERDOC / _MCP_HOST / _MCP_PORT). Re-run
whenever the fork's tool set changes.

  py -3 scripts/capture_renderdoc_tools.py

The qrenderdoc instance is left running afterwards (same policy as the proxy).
"""
import json
import sys

import renderdoc_mcp_proxy as rd


def main():
    ready, msg = rd.ensure_server()
    rd.log(msg)
    if not ready:
        rd.log("cannot reach the RenderDoc MCP server; aborting capture")
        return 1

    resp = rd.post_jsonrpc("tools/list", {}, timeout=30.0)
    result = resp.get("result") if isinstance(resp, dict) else None
    if not isinstance(result, dict) or "tools" not in result:
        rd.log(f"unexpected tools/list response: {resp}")
        return 1

    tools = result["tools"]
    with open(rd.SCHEMA_PATH, "w", encoding="utf-8") as f:
        json.dump({"tools": tools}, f, indent=2)
        f.write("\n")
    rd.log(f"wrote {len(tools)} fork tool definitions to {rd.SCHEMA_PATH}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
