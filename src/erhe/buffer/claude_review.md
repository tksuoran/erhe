# erhe_buffer -- Code Review

## Summary
A small, clean library providing a CPU-side buffer abstraction with a simple bump allocator. The code is well-structured with proper RAII, thread safety via mutex, and good use of modern C++ (`std::span`, `std::optional`, `noexcept`). Few issues exist.

## Strengths
- Clean virtual interface (`IBuffer`) with `[[nodiscard]]` on all pure virtual methods
- Thread-safe allocation via `std::lock_guard`
- Move semantics properly implemented; copy is deleted
- Good use of `std::span` and `std::optional` for safe return values
- `ERHE_VERIFY` guards on capacity and alignment preconditions

## Issues
- **[minor]** `get_span()` (ibuffer.hpp:21) is not thread-safe -- it returns a mutable span while `allocate_bytes` can be called concurrently, potentially leading to data races on the buffer contents. The allocator is protected, but the returned span is not.
- **[minor]** `get_available_byte_count()` and `get_used_byte_count()` (ibuffer.hpp:24-25) are not `noexcept` in the concrete class despite having no throwing code paths.
- **[minor]** Move assignment operator does not check for self-assignment (`this != &other`), though this is extremely unlikely to cause problems in practice.
- **[minor]** The `m_span` member is redundant; `get_span()` could simply construct `std::span<std::byte>(m_buffer.data(), m_buffer.size())` on demand, avoiding the need to keep it synchronized after moves.
- **[minor]** The TODO on line 49 of ibuffer.cpp ("TODO log warning or error") indicates a missing diagnostic for allocation failures.

## Suggestions
- Remove `m_span` member and compute the span on the fly from `m_buffer` to eliminate the synchronization requirement during moves.
- Add a log warning when `allocate_bytes` fails (the TODO on line 49).
- Consider making `get_available_byte_count` and `get_used_byte_count` `noexcept` consistently.
- If concurrent access to the buffer contents is expected, document the threading model (allocator is thread-safe, but the returned span is not).
