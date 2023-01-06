#pragma once

#include <cstdint>
#include <cstddef>

#include <glm/glm.hpp>

namespace erhe::toolkit
{

static const uint64_t c_prime = 0x100000001b3;
static const uint64_t c_seed  = 0xcbf29ce484222325;

[[nodiscard]] inline const auto hash(
    const void*       data,
    const std::size_t byte_count,
    uint64_t          seed = c_seed
)
{
    const uint8_t* u8_data = reinterpret_cast<const uint8_t*>(data);

    for (std::size_t i = 0; i < byte_count; ++i)
    {
        seed = (seed ^ u8_data[i]) * c_prime;
    }

    return seed;
}

[[nodiscard]] inline auto hash(const float value, const uint64_t seed = c_seed) -> uint64_t
{
    return hash(&value, sizeof(float), seed);
}

[[nodiscard]] inline auto hash(const float x, const float y, const float z, uint64_t seed = c_seed) -> uint64_t
{
    seed = hash(x, seed);
    seed = hash(y, seed);
    seed = hash(z, seed);
    return seed;
}

[[nodiscard]] inline auto hash(const glm::vec2& value, uint64_t seed = c_seed) -> uint64_t
{
    seed = hash(value.x, seed);
    seed = hash(value.y, seed);
    return seed;
}

[[nodiscard]] inline auto hash(const glm::vec3& value, uint64_t seed = c_seed) -> uint64_t
{
    seed = hash(value.x, seed);
    seed = hash(value.y, seed);
    seed = hash(value.z, seed);
    return seed;
}

[[nodiscard]] inline auto hash(const glm::vec4& value, uint64_t seed = c_seed) -> uint64_t
{
    seed = hash(value.x, seed);
    seed = hash(value.y, seed);
    seed = hash(value.z, seed);
    seed = hash(value.w, seed);
    return seed;
}

} // namespace erhe::toolkit
