#include "erhe_asset_store/key.hpp"
#include "erhe_asset_store/item_kind.hpp"
#include "erhe_verify/verify.hpp"

#include <xxhash.h>

#include <bit>
#include <cstring>
#include <vector>

namespace erhe::asset_store {

namespace {

// Field type tags of the Key_builder byte stream (doc/erhe/asset_store.md "Key_builder").
constexpr std::uint8_t field_kind          = 1;
constexpr std::uint8_t field_code_version  = 2;
constexpr std::uint8_t field_format_stamp  = 3;
constexpr std::uint8_t field_int_setting   = 4;
constexpr std::uint8_t field_uint_setting  = 5;
constexpr std::uint8_t field_float_setting = 6;
constexpr std::uint8_t field_text_setting  = 7;
constexpr std::uint8_t field_key           = 8;
constexpr std::uint8_t field_content_hash  = 9;
constexpr std::uint8_t field_bytes         = 10;

auto to_little_endian(const std::uint64_t value) -> std::array<std::byte, 8>
{
    std::array<std::byte, 8> result{};
    for (std::size_t i = 0; i < 8; ++i) {
        result[i] = static_cast<std::byte>((value >> (8 * i)) & 0xffu);
    }
    return result;
}

auto as_bytes(const std::string_view text) -> std::span<const std::byte>
{
    return std::span<const std::byte>{reinterpret_cast<const std::byte*>(text.data()), text.size()};
}

auto hex_digit_value(const char c) -> int
{
    if ((c >= '0') && (c <= '9')) {
        return c - '0';
    }
    if ((c >= 'a') && (c <= 'f')) {
        return 10 + (c - 'a');
    }
    if ((c >= 'A') && (c <= 'F')) {
        return 10 + (c - 'A');
    }
    return -1;
}

}

auto Key::to_hex() const -> std::string
{
    static constexpr const char* c_digits = "0123456789abcdef";
    std::string result(2 * byte_count, '0');
    for (std::size_t i = 0; i < byte_count; ++i) {
        result[(2 * i) + 0] = c_digits[(bytes[i] >> 4u) & 0xfu];
        result[(2 * i) + 1] = c_digits[bytes[i] & 0xfu];
    }
    return result;
}

auto Key::from_hex(const std::string_view text) -> std::optional<Key>
{
    if (text.size() != (2 * byte_count)) {
        return {};
    }
    Key key{};
    for (std::size_t i = 0; i < byte_count; ++i) {
        const int high = hex_digit_value(text[(2 * i) + 0]);
        const int low  = hex_digit_value(text[(2 * i) + 1]);
        if ((high < 0) || (low < 0)) {
            return {};
        }
        key.bytes[i] = static_cast<std::uint8_t>((high << 4) | low);
    }
    return key;
}

auto Key_hash::operator()(const Key& key) const noexcept -> std::size_t
{
    std::uint64_t value = 0;
    std::memcpy(&value, key.bytes.data(), sizeof(value));
    return static_cast<std::size_t>(value);
}

class Hash_state
{
public:
    Hash_state()
        : state{XXH3_createState()}
    {
        ERHE_VERIFY(state != nullptr);
        XXH3_128bits_reset(state);
    }
    ~Hash_state() noexcept
    {
        XXH3_freeState(state);
    }
    Hash_state(const Hash_state&) = delete;
    auto operator=(const Hash_state&) -> Hash_state& = delete;

    XXH3_state_t* state{nullptr};
};

Content_hasher::Content_hasher()
    : m_state{std::make_unique<Hash_state>()}
{
}

Content_hasher::~Content_hasher() noexcept = default;

Content_hasher::Content_hasher(Content_hasher&& other) noexcept = default;

auto Content_hasher::operator=(Content_hasher&& other) noexcept -> Content_hasher& = default;

void Content_hasher::reset()
{
    XXH3_128bits_reset(m_state->state);
}

void Content_hasher::update(const std::span<const std::byte> bytes)
{
    if (bytes.empty()) {
        return;
    }
    const XXH_errorcode result = XXH3_128bits_update(m_state->state, bytes.data(), bytes.size());
    ERHE_VERIFY(result == XXH_OK);
}

auto Content_hasher::finish() const -> Key
{
    const XXH128_hash_t digest = XXH3_128bits_digest(m_state->state);
    XXH128_canonical_t canonical{};
    XXH128_canonicalFromHash(&canonical, digest);
    Key key{};
    static_assert(sizeof(canonical.digest) == Key::byte_count);
    std::memcpy(key.bytes.data(), canonical.digest, Key::byte_count);
    return key;
}

auto Content_hasher::hash(const std::span<const std::byte> bytes) -> Key
{
    const XXH128_hash_t digest = XXH3_128bits(bytes.data(), bytes.size());
    XXH128_canonical_t canonical{};
    XXH128_canonicalFromHash(&canonical, digest);
    Key key{};
    std::memcpy(key.bytes.data(), canonical.digest, Key::byte_count);
    return key;
}

Key_builder::Key_builder(const Item_kind kind)
{
    const std::array<std::byte, 8> value = to_little_endian(static_cast<std::uint64_t>(kind));
    add_field(field_kind, value, {});
}

void Key_builder::add_field(const std::uint8_t field_type, const std::span<const std::byte> head, const std::span<const std::byte> body)
{
    const std::byte                tag    = static_cast<std::byte>(field_type);
    const std::array<std::byte, 8> length = to_little_endian(static_cast<std::uint64_t>(head.size() + body.size()));
    m_hasher.update(std::span<const std::byte>{&tag, 1});
    m_hasher.update(length);
    m_hasher.update(head);
    m_hasher.update(body);
}

auto Key_builder::add_code_version(const std::uint32_t version) -> Key_builder&
{
    add_field(field_code_version, to_little_endian(version), {});
    return *this;
}

auto Key_builder::add_format_stamp(const std::string_view stamp) -> Key_builder&
{
    add_field(field_format_stamp, as_bytes(stamp), {});
    return *this;
}

namespace {

// A setting field body: name length, name, then the value bytes.
class Setting_head
{
public:
    explicit Setting_head(const std::string_view name)
    {
        const std::array<std::byte, 8> length = to_little_endian(name.size());
        bytes.insert(bytes.end(), length.begin(), length.end());
        const std::span<const std::byte> name_bytes = as_bytes(name);
        bytes.insert(bytes.end(), name_bytes.begin(), name_bytes.end());
    }

    std::vector<std::byte> bytes;
};

}

auto Key_builder::add_int_setting(const std::string_view name, const std::int64_t value) -> Key_builder&
{
    const Setting_head head{name};
    add_field(field_int_setting, head.bytes, to_little_endian(static_cast<std::uint64_t>(value)));
    return *this;
}

auto Key_builder::add_uint_setting(const std::string_view name, const std::uint64_t value) -> Key_builder&
{
    const Setting_head head{name};
    add_field(field_uint_setting, head.bytes, to_little_endian(value));
    return *this;
}

auto Key_builder::add_float_setting(const std::string_view name, const double value) -> Key_builder&
{
    const Setting_head head{name};
    add_field(field_float_setting, head.bytes, to_little_endian(std::bit_cast<std::uint64_t>(value)));
    return *this;
}

auto Key_builder::add_text_setting(const std::string_view name, const std::string_view value) -> Key_builder&
{
    const Setting_head head{name};
    add_field(field_text_setting, head.bytes, as_bytes(value));
    return *this;
}

auto Key_builder::add_key(const Key& key) -> Key_builder&
{
    add_field(field_key, std::as_bytes(std::span{key.bytes}), {});
    return *this;
}

auto Key_builder::add_content_hash(const Key& content_hash) -> Key_builder&
{
    add_field(field_content_hash, std::as_bytes(std::span{content_hash.bytes}), {});
    return *this;
}

auto Key_builder::add_bytes(const std::span<const std::byte> bytes) -> Key_builder&
{
    add_field(field_bytes, bytes, {});
    return *this;
}

auto Key_builder::finish() const -> Key
{
    return m_hasher.finish();
}

}
