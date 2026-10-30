#include "erhe_gltf/gltf_uid.hpp"

namespace erhe::gltf {

auto generate_gltf_uid(std::mt19937_64& rng) -> std::string
{
    constexpr char alphabet[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    std::uniform_int_distribution<std::size_t> pick_char{0, sizeof(alphabet) - 2};
    std::string uid;
    uid.reserve(16);
    for (int i = 0; i < 16; ++i) {
        uid.push_back(alphabet[pick_char(rng)]);
    }
    return uid;
}

} // namespace erhe::gltf
