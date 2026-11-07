#include "erhe_hash/content_hash.hpp"

#include <xxhash.h>

#include <cstdlib>
#include <cstring>

namespace erhe::hash {

namespace {

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

auto Content_hash::to_hex() const -> std::string
{
    static constexpr const char* c_digits = "0123456789abcdef";
    std::string result(2 * byte_count, '0');
    for (std::size_t i = 0; i < byte_count; ++i) {
        result[(2 * i) + 0] = c_digits[(bytes[i] >> 4u) & 0xfu];
        result[(2 * i) + 1] = c_digits[bytes[i] & 0xfu];
    }
    return result;
}

auto Content_hash::from_hex(const std::string_view text) -> std::optional<Content_hash>
{
    if (text.size() != (2 * byte_count)) {
        return {};
    }
    Content_hash hash{};
    for (std::size_t i = 0; i < byte_count; ++i) {
        const int high = hex_digit_value(text[(2 * i) + 0]);
        const int low  = hex_digit_value(text[(2 * i) + 1]);
        if ((high < 0) || (low < 0)) {
            return {};
        }
        hash.bytes[i] = static_cast<std::uint8_t>((high << 4) | low);
    }
    return hash;
}

class Xxh3_128_state
{
public:
    Xxh3_128_state()
        : state{XXH3_createState()}
    {
        if (state == nullptr) {
            std::abort();
        }
        XXH3_128bits_reset(state);
    }
    ~Xxh3_128_state() noexcept
    {
        XXH3_freeState(state);
    }
    Xxh3_128_state(const Xxh3_128_state&) = delete;
    auto operator=(const Xxh3_128_state&) -> Xxh3_128_state& = delete;

    XXH3_state_t* state{nullptr};
};

Xxh3_128_hasher::Xxh3_128_hasher()
    : m_state{std::make_unique<Xxh3_128_state>()}
{
}

Xxh3_128_hasher::~Xxh3_128_hasher() noexcept = default;

Xxh3_128_hasher::Xxh3_128_hasher(Xxh3_128_hasher&& other) noexcept = default;

auto Xxh3_128_hasher::operator=(Xxh3_128_hasher&& other) noexcept -> Xxh3_128_hasher& = default;

void Xxh3_128_hasher::update(const std::span<const std::byte> bytes)
{
    if (bytes.empty()) {
        return;
    }
    if (XXH3_128bits_update(m_state->state, bytes.data(), bytes.size()) != XXH_OK) {
        std::abort();
    }
}

void Xxh3_128_hasher::update_u64_le(const std::uint64_t value)
{
    std::array<std::byte, 8> bytes{};
    for (std::size_t i = 0; i < 8; ++i) {
        bytes[i] = static_cast<std::byte>((value >> (8u * i)) & 0xffu);
    }
    update(bytes);
}

auto Xxh3_128_hasher::finish() const -> Content_hash
{
    const XXH128_hash_t digest = XXH3_128bits_digest(m_state->state);
    XXH128_canonical_t canonical{};
    XXH128_canonicalFromHash(&canonical, digest);
    Content_hash hash{};
    static_assert(sizeof(canonical.digest) == Content_hash::byte_count);
    std::memcpy(hash.bytes.data(), canonical.digest, Content_hash::byte_count);
    return hash;
}

auto Xxh3_128_hasher::hash(const std::span<const std::byte> bytes) -> Content_hash
{
    Xxh3_128_hasher hasher;
    hasher.update(bytes);
    return hasher.finish();
}

}
