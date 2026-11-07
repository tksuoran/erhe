#pragma once

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace erhe::hash {

// A 128-bit XXH3-128 digest in canonical (big-endian) byte order (doc/erhe/hash.md).
class Content_hash
{
public:
    static constexpr std::size_t byte_count = 16;

    // 32 lowercase hexadecimal digits.
    [[nodiscard]] auto to_hex() const -> std::string;
    [[nodiscard]] static auto from_hex(std::string_view text) -> std::optional<Content_hash>;

    auto operator<=>(const Content_hash& other) const = default;
    auto operator== (const Content_hash& other) const -> bool = default;

    std::array<std::uint8_t, byte_count> bytes{};
};

class Xxh3_128_state;

// Streaming XXH3-128 over byte spans.
class Xxh3_128_hasher
{
public:
    Xxh3_128_hasher();
    ~Xxh3_128_hasher() noexcept;
    Xxh3_128_hasher(const Xxh3_128_hasher&) = delete;
    auto operator=(const Xxh3_128_hasher&) -> Xxh3_128_hasher& = delete;
    Xxh3_128_hasher(Xxh3_128_hasher&& other) noexcept;
    auto operator=(Xxh3_128_hasher&& other) noexcept -> Xxh3_128_hasher&;

    void update(std::span<const std::byte> bytes);
    // The value as 8 bytes, least significant first.
    void update_u64_le(std::uint64_t value);
    [[nodiscard]] auto finish() const -> Content_hash;

    [[nodiscard]] static auto hash(std::span<const std::byte> bytes) -> Content_hash;

private:
    std::unique_ptr<Xxh3_128_state> m_state;
};

}
