# erhe_hash -- Code Review

## Summary
A small hashing utility library providing two separate hash implementations: a custom FNV-1a hash for glm vectors (in `hash.hpp`) and a compile-time XXH32 implementation (in `xxhash.hpp`). Both are header-only. The code is simple and functional but has a few style issues.

## Strengths
- FNV-1a implementation is simple and correct
- Compile-time XXH32 enables `constexpr` string hashing, useful for compile-time dispatch
- `[[nodiscard]]` used on all hash functions
- `inline` functions keep the header-only design clean
- Good separation: FNV-1a for runtime glm hashing, XXH32 for compile-time string hashing

## Issues
- **[minor]** `static const` variables `c_prime` and `c_seed` in `hash.hpp:10-11` should be `constexpr` or `inline constexpr`. As `static const` in a header, they create separate copies in each translation unit.
- **[minor]** Missing return type on the raw `hash(const void*, std::size_t, uint64_t)` overload (hash.hpp:13-26). The `auto` return type is deduced to `uint64_t` but an explicit return type would improve readability and match the other overloads.
- **[minor]** `compiletime_strlen` in `xxhash.hpp:143` is a free function not in any namespace, which could conflict. In C++20, `std::char_traits<char>::length()` is `constexpr` and could replace it.
- **[minor]** The `ERHE_HASH` template at `xxhash.hpp:152` is also not in any namespace, polluting the global scope.
- **[minor]** Missing closing namespace comment in `hash.hpp:65` (just `}` instead of `} // namespace erhe::hash`).

## Suggestions
- Change `static const` to `inline constexpr` for `c_prime` and `c_seed` to avoid ODR issues.
- Place `ERHE_HASH` and `compiletime_strlen` into the `erhe::hash` or `compiletime_xxhash` namespace.
- Consider removing the `hash.cpp` file since it only includes the header and adds nothing; the library could be header-only or `INTERFACE` in CMake.
