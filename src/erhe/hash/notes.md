# erhe_hash

## Purpose
Provides lightweight hashing utilities for runtime FNV-1a hashing of floats and
glm vectors, plus a compile-time constexpr XXH32 implementation used to hash
string literals at compile time (e.g., for fast string-to-ID mapping).

## Key Types
- `erhe::hash::hash()` -- FNV-1a hash function (overloaded for raw bytes, float, vec2/3/4)
- `ERHE_HASH(string_literal)` -- constexpr macro producing a uint32_t XXH32 hash at compile time

## Public API
```cpp
uint64_t h = erhe::hash::hash(glm::vec3{1,2,3});      // runtime FNV-1a
constexpr uint32_t id = ERHE_HASH("some_identifier");  // compile-time XXH32
```

## Dependencies
- `glm` -- vector types for hash overloads
- No erhe library dependencies (leaf library)

## Notes
- `hash.hpp` uses FNV-1a (64-bit) with overloads for `float`, `glm::vec2/3/4`.
- `xxhash.hpp` is a standalone C++20 constexpr XXH32 implementation adapted from
  a public snippet. The `ERHE_HASH` template deduces string length from array size.
- Both headers are inline/header-only.
