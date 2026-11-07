# erhe_hash

Stability: stable

## Purpose
Provides lightweight hashing utilities for runtime FNV-1a hashing of floats and
glm vectors, plus a compile-time constexpr XXH32 implementation used to hash
string literals at compile time (e.g., for fast string-to-ID mapping), and
runtime XXH3-128 for content hashes.

## Key Types
- `erhe::hash::hash()` -- FNV-1a hash function (overloaded for raw bytes, float, vec2/3/4)
- `ERHE_HASH(string_literal)` -- constexpr macro producing a uint32_t XXH32 hash at compile time
- `erhe::hash::Content_hash` -- a 128-bit XXH3-128 digest in canonical (big-endian)
  byte order, with 32-digit lowercase hex text (`to_hex()` / `from_hex()`)
- `erhe::hash::Xxh3_128_hasher` -- streaming XXH3-128 (`update()`, `update_u64_le()`,
  `finish()`, one-shot `hash()`)

## Public API
```cpp
uint64_t h = erhe::hash::hash(glm::vec3{1,2,3});      // runtime FNV-1a
constexpr uint32_t id = ERHE_HASH("some_identifier");  // compile-time XXH32
```

## Dependencies
- `glm` -- vector types for hash overloads
- xxHash (`xxhash.c` of the download-only `xxhash` package, compiled in with
  `XXH_NAMESPACE=erhe_hash_`) -- XXH3-128
- No erhe library dependencies (leaf library)

## Notes
- `hash.hpp` uses FNV-1a (64-bit) with overloads for `float`, `glm::vec2/3/4`.
- `xxhash.hpp` is a standalone C++20 constexpr XXH32 implementation adapted from
  a public snippet. The `ERHE_HASH` template deduces string length from array size.
- `hash.hpp` and `xxhash.hpp` are inline/header-only.
- `Content_hash` is the content hash a mesh primitive and an image carry as loaded
  state (`doc/erhe/primitive.md` "Content hash", `doc/erhe/gltf.md` "Content
  hashes"); its bytes equal an `erhe::asset_store::Key` of the same digest.
