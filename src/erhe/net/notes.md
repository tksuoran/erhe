# erhe::net Review

## What it is

A cross-platform (Windows/Linux/macOS) TCP networking layer built on raw BSD sockets with `select()`.

- **`Socket`** - non-blocking TCP socket with ring-buffered send/receive and a custom packet framing protocol (8-byte header: 4-byte magic `"Erhe"` + 4-byte length)
- **`Server`** - accepts multiple clients, broadcasts, polls via `select()`
- **`Client`** - connects to a server, sends/receives
- **`Ring_buffer`** - lock-free (single-producer/single-consumer) ring buffer for buffering I/O
- **`Select_sockets`** - thin wrapper around `select()`/`fd_set`
- Platform-specific error handling for Windows (Winsock) and Unix (errno)

## Strengths

- **Well-structured**: Clean separation between socket, client, server, platform layer
- **Non-blocking I/O**: All sockets are non-blocking with `select()`-based polling - fits well into a game loop via `poll(0)`
- **Packet framing**: Built-in message framing with magic + length header - no partial-message headaches
- **Cross-platform**: Proper Windows/Unix abstraction with a shared API surface
- **4 MB ring buffers**: Generous buffer sizes for message queuing
- **Already integrated**: `Network_window` in the editor provides a working test harness

## Issues & Gaps

1. **No threading** - everything is poll-based on the caller's thread. For an MCP server you'd need to either poll from the main loop (which is already done in `Network_window::update_network()`) or run a dedicated thread. Polling at 0ms timeout is fine for the editor's frame loop.

2. **No TLS/SSL** - plain TCP only. MCP typically runs over `stdio` or `SSE/HTTP`, not raw TCP. Using this for an MCP-like protocol means building a custom transport.

3. **No HTTP support** - MCP's standard transports are stdio and HTTP+SSE (streamable HTTP). This library is raw TCP with a custom binary framing protocol. Options:
   - Build HTTP parsing on top (significant effort)
   - Define a custom MCP transport over raw TCP (non-standard but workable)
   - Use this as the substrate and add an HTTP layer (e.g., integrate a lightweight HTTP library)

4. **IPv4 only** - hardcoded to `AF_INET` / `sockaddr_in`. No IPv6 support.

5. **`select()` scalability** - `FD_SETSIZE` limits (typically 64 on Windows, 1024 on Linux). Fine for a few editor clients, not for many concurrent connections.

6. **Linux `net_unix.cpp` bugs**:
   - `get_net_hints()` ignores all parameters and hardcodes values (line 293-309)
   - `set_socket_option` for `NonBlocking` has inverted logic: `value != 0` clears `O_NONBLOCK` instead of setting it (line 216)
   - `ReceiveTimeout` incorrectly uses `SO_REUSEADDR`, `SendTimeout` uses `SO_RCVBUF` (lines 233, 239)

7. **No per-client send** on `Server` - only `broadcast()`. For MCP you'd need to send responses to specific clients.

8. **No message routing/dispatch** - the receive handler gives raw bytes. JSON-RPC parsing/dispatch would need to be built on top for MCP.

## Verdict for MCP Server

Usable as a starting point, but not a direct fit. The core TCP plumbing (non-blocking sockets, ring buffers, packet framing, select-based polling) is solid and well-written. However, MCP expects either stdio or HTTP transports with JSON-RPC 2.0 message framing.

### Practical options

- **Option A: Custom TCP transport for MCP** - Use `erhe::net` as-is with the existing binary framing, implement JSON-RPC message serialization on top. Works for a custom editor-to-Claude-Code bridge but wouldn't be a standard MCP server that other MCP clients can connect to.

- **Option B: Stdio-based MCP** - Bypass `erhe::net` entirely. MCP over stdio is simplest: the editor spawns a child process and communicates via stdin/stdout with newline-delimited JSON-RPC. No networking needed.

- **Option C: HTTP transport** - Add a lightweight HTTP server (e.g., cpp-httplib, Boost.Beast) for proper MCP SSE/streamable HTTP support. `erhe::net` isn't the right layer for this.

The library is a good low-level networking foundation. For "expose editor commands via MCP protocol," Option B (stdio) is the simplest path, Option C (HTTP library) is the most standards-compliant. Using `erhe::net` directly works for a proprietary TCP protocol between editor instances, but needs the Linux bugs fixed first.
