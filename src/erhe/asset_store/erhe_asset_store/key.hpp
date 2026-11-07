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

namespace erhe::asset_store {

enum class Item_kind : std::uint32_t;

// A 128-bit digest: an item key or a content hash, in XXH3-128 canonical (big-endian) byte order.
class Key
{
public:
    static constexpr std::size_t byte_count = 16;

    [[nodiscard]] auto to_hex() const -> std::string;
    [[nodiscard]] static auto from_hex(std::string_view text) -> std::optional<Key>;

    auto operator<=>(const Key& other) const = default;
    auto operator== (const Key& other) const -> bool = default;

    std::array<std::uint8_t, byte_count> bytes{};
};

class Key_hash
{
public:
    [[nodiscard]] auto operator()(const Key& key) const noexcept -> std::size_t;
};

class Hash_state;

class Content_hasher
{
public:
    Content_hasher();
    ~Content_hasher() noexcept;
    Content_hasher(const Content_hasher&) = delete;
    auto operator=(const Content_hasher&) -> Content_hasher& = delete;
    Content_hasher(Content_hasher&& other) noexcept;
    auto operator=(Content_hasher&& other) noexcept -> Content_hasher&;

    void reset();
    void update(std::span<const std::byte> bytes);
    [[nodiscard]] auto finish() const -> Key;

    [[nodiscard]] static auto hash(std::span<const std::byte> bytes) -> Key;

private:
    std::unique_ptr<Hash_state> m_state;
};

class Key_builder
{
public:
    explicit Key_builder(Item_kind kind);

    auto add_code_version (std::uint32_t version)                         -> Key_builder&;
    auto add_format_stamp (std::string_view stamp)                        -> Key_builder&;
    auto add_int_setting  (std::string_view name, std::int64_t value)     -> Key_builder&;
    auto add_uint_setting (std::string_view name, std::uint64_t value)    -> Key_builder&;
    auto add_float_setting(std::string_view name, double value)           -> Key_builder&;
    auto add_text_setting (std::string_view name, std::string_view value) -> Key_builder&;
    auto add_key          (const Key& key)                                -> Key_builder&;
    auto add_content_hash (const Key& content_hash)                       -> Key_builder&;
    auto add_bytes        (std::span<const std::byte> bytes)              -> Key_builder&;

    [[nodiscard]] auto finish() const -> Key;

private:
    void add_field(std::uint8_t field_type, std::span<const std::byte> head, std::span<const std::byte> body);

    Content_hasher m_hasher;
};

}
