# erhe Editor MCP Server

The editor embeds an MCP (Model Context Protocol) server that exposes editor commands over HTTP using JSON-RPC 2.0. The server starts automatically with the editor on `127.0.0.1:8080`.

## Endpoints

| Method | Path | Description |
|--------|------|-------------|
| POST | `/mcp` | JSON-RPC 2.0 endpoint for MCP protocol |
| GET | `/health` | Health check, returns `{"status":"ok"}` |

## MCP Methods

All requests are JSON-RPC 2.0 POST to `/mcp`.

### initialize

Handshake — returns server info and capabilities.

```bash
curl -X POST http://127.0.0.1:8080/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":"1","method":"initialize"}'
```

Response:
```json
{
  "jsonrpc": "2.0",
  "id": "1",
  "result": {
    "protocolVersion": "2024-11-05",
    "capabilities": { "tools": {} },
    "serverInfo": { "name": "erhe-editor", "version": "0.1.0" }
  }
}
```

### tools/list

List all available editor commands as MCP tools.

```bash
curl -X POST http://127.0.0.1:8080/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":"2","method":"tools/list"}'
```

Response:
```json
{
  "jsonrpc": "2.0",
  "id": "2",
  "result": {
    "tools": [
      {
        "name": "some_command",
        "description": "Editor command: some_command",
        "inputSchema": { "type": "object", "properties": {} }
      }
    ]
  }
}
```

### tools/call

Invoke an editor command by name. The command is queued for execution on the main editor thread (with a 5-second timeout).

```bash
curl -X POST http://127.0.0.1:8080/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":"3","method":"tools/call","params":{"name":"some_command"}}'
```

Success response:
```json
{
  "jsonrpc": "2.0",
  "id": "3",
  "result": {
    "content": [{ "type": "text", "text": "Command executed successfully: some_command" }]
  }
}
```

Error response (command not found or failed):
```json
{
  "jsonrpc": "2.0",
  "id": "3",
  "result": {
    "content": [{ "type": "text", "text": "Command failed or not found: some_command" }],
    "isError": true
  }
}
```

## Threading Model

The HTTP server runs on a dedicated background thread. When `tools/call` is received, the command name is placed in a thread-safe queue and the HTTP handler blocks on a `std::future`. On the main editor thread, `process_queued_commands()` is called each frame, drains the queue, executes commands via `Command::try_call()`, and sets the promise to unblock the HTTP response.

## Configuration

The server listens on `127.0.0.1:8080` by default (localhost only). The port can be changed via the `Mcp_server` constructor parameter.

## Source Files

- `src/editor/mcp/mcp_server.hpp` — Server class declaration
- `src/editor/mcp/mcp_server.cpp` — Implementation
- `src/editor/editor.cpp` — Startup/shutdown/tick integration

## Dependencies

- [cpp-httplib](https://github.com/yhirose/cpp-httplib) v0.18.7 — Single-header HTTP server (fetched via CPM)
- [nlohmann/json](https://github.com/nlohmann/json) — JSON serialization (already in project)
