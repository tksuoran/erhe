#include "erhe_gltf/gltf_uid.hpp"

#include <cstdint>

namespace erhe::gltf {

namespace {

[[nodiscard]] auto hash_seed(const std::string_view seed, const uint64_t basis) -> uint64_t
{
    // FNV-1a, then the splitmix64 finalizer to spread short seeds over all bits.
    uint64_t h = basis;
    for (const char c : seed) {
        h = (h ^ static_cast<uint64_t>(static_cast<unsigned char>(c))) * uint64_t{0x100000001b3};
    }
    h = (h ^ (h >> 30)) * uint64_t{0xbf58476d1ce4e5b9};
    h = (h ^ (h >> 27)) * uint64_t{0x94d049bb133111eb};
    return h ^ (h >> 31);
}

} // anonymous namespace

auto make_derived_gltf_uid(const std::string_view seed) -> std::string
{
    constexpr char alphabet[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    constexpr uint64_t alphabet_size = sizeof(alphabet) - 1;
    const uint64_t words[2] = {
        hash_seed(seed, uint64_t{0xcbf29ce484222325}),
        hash_seed(seed, uint64_t{0x84222325cbf29ce4})
    };
    std::string uid(16, '0');
    for (std::size_t i = 0; i < 16; ++i) {
        const uint64_t byte = (words[i / 8] >> ((i % 8) * 8)) & uint64_t{0xff};
        uid[i] = alphabet[byte % alphabet_size];
    }
    return uid;
}

} // namespace erhe::gltf
