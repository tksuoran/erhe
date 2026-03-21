# erhe_net -- Code Review

## Summary
A cross-platform networking library providing non-blocking TCP client/server communication with a custom packet framing protocol (magic header + length). It includes a ring buffer for send/receive buffering and `select()`-based I/O multiplexing. The implementation handles platform differences between Windows (Winsock) and Unix sockets. The code is functional and handles many edge cases, but has some thread safety gaps and platform abstraction concerns.

## Strengths
- Clean packet framing protocol with magic number validation prevents protocol desync
- `Ring_buffer` is a well-implemented circular buffer with correct wrap-around handling for both produce/consume patterns
- Proper non-blocking socket handling with correct error classification (fatal vs. busy/in-progress)
- Move semantics correctly implemented for `Socket` with proper cleanup of the moved-from object
- Good error reporting: all socket operations log errors with platform-specific error messages
- Socket RAII: destructor calls `close()`, preventing socket leaks

## Issues
- **[notable]** `Socket::recv()` (socket.cpp:339-417) has a potential issue: if the ring buffer wraps during packet reassembly, it calls `rotate()` and then re-calls `begin_consume()`. However, the `rotate()` implementation (ring_buffer.cpp:68-94) uses a juggling algorithm that modifies the buffer in-place, which could corrupt data if `read_offset` is not reset. After `rotate()`, `m_read_offset` should be 0 and `m_write_offset` should be `size()`, but the rotate function does not update these offsets.
- **[moderate]** No thread safety: `Socket`, `Client`, and `Server` have no mutex protection. The `poll()` methods are presumably called from a single thread, but if `send()` is called from another thread while `poll()` is running, the ring buffer will be corrupted.
- **[moderate]** `receive_packet_length()` (socket.cpp:322-336) does `ERHE_VERIFY(header.magic == erhe_header_magic_u32)` which is a fatal assertion. If the stream becomes desynchronized (e.g., partial write), this crashes the application instead of gracefully closing the connection.
- **[moderate]** `Ring_buffer::begin_produce()` returns `0` (null pointer) when the buffer is full (ring_buffer.cpp:116). This is correct but the return type is `uint8_t*` -- returning `nullptr` would be clearer.
- **[minor]** `net_os.hpp` defines `TRUE`, `INVALID_SOCKET`, `SOCKET_ERROR`, etc. as `static constexpr` globals for Unix. The macro-like naming and global scope are concerning; these should be in a namespace.
- **[minor]** Buffer sizes are hardcoded to `4 * 1024 * 1024` bytes (4 MB) in multiple places (socket.cpp:231-232, 481-482). These should be configurable.
- **[minor]** `Client::disconnect()` (client.cpp:33-37) logs "Socket move assignment" which is a copy-paste error from the move assignment operator.
- **[minor]** `Server::broadcast()` continues sending to remaining clients even after errors. Whether this is desired behavior should be documented.

## Suggestions
- Fix `Ring_buffer::rotate()` to also update `m_read_offset` and `m_write_offset` after rotation. The `rotate()` method should set `m_read_offset = 0` and `m_write_offset = size()`.
- Replace the fatal `ERHE_VERIFY` in `receive_packet_length()` with a graceful error that closes the connection and logs the problem.
- Return `nullptr` instead of `0` from `Ring_buffer::begin_produce()`.
- Fix the wrong log message in `Client::disconnect()`.
- Consider adding a mutex to `Socket` for thread-safe `send()`, or document that all operations must occur on the same thread.
- Make buffer sizes configurable via constructor parameters or constants.
