#include "erhe_scene_renderer/standard_shader_variant.hpp"

namespace erhe::scene_renderer {

namespace {

constexpr std::size_t s_count_axis_count =
#define ERHE_X(NAME, FIELD) +1
    0 ERHE_STANDARD_VARIANT_COUNT_AXES(ERHE_X)
#undef ERHE_X
    ;

[[nodiscard]] auto fnv1a64(const void* data, std::size_t size, std::uint64_t seed) noexcept -> std::uint64_t
{
    constexpr std::uint64_t prime = 1099511628211ull;
    std::uint64_t hash = seed;
    const auto* bytes = static_cast<const unsigned char*>(data);
    for (std::size_t i = 0; i < size; ++i) {
        hash ^= bytes[i];
        hash *= prime;
    }
    return hash;
}

} // anonymous namespace

auto Standard_variant_key_hash::operator()(const Standard_variant_key& key) const noexcept -> std::size_t
{
    constexpr std::uint64_t fnv_offset_basis = 14695981039346656037ull;

    std::uint64_t hash = fnv1a64(&key.boolean_mask, sizeof(key.boolean_mask), fnv_offset_basis);

    // Pack the count axes into a small contiguous array so the hash is
    // stable regardless of struct padding. uint16_t fields are read from
    // the key directly via the same X-macro list.
    std::uint16_t count_buffer[s_count_axis_count];
    std::size_t   index = 0;
#define ERHE_X(NAME, FIELD) count_buffer[index++] = key.FIELD;
    ERHE_STANDARD_VARIANT_COUNT_AXES(ERHE_X)
#undef ERHE_X

    hash = fnv1a64(count_buffer, sizeof(count_buffer), hash);
    return static_cast<std::size_t>(hash);
}

auto make_standard_variant_defines(const Standard_variant_key& key)
    -> std::vector<std::pair<std::string, std::string>>
{
    std::vector<std::pair<std::string, std::string>> defines;

    // Boolean axes -- emit only when set, with value "1" so the GLSL
    // preprocessor recognizes both `#ifdef ERHE_X` and `#if ERHE_X` style
    // checks.
#define ERHE_X(NAME, FIELD)                                                            \
    if (key.get_boolean(Standard_variant_key::Boolean_axis::FIELD)) {                  \
        defines.emplace_back(std::string{"ERHE_" #NAME}, std::string{"1"});            \
    }
    ERHE_STANDARD_VARIANT_BOOL_AXES(ERHE_X)
#undef ERHE_X

    // Count axes -- always emit so the shader can rely on them as
    // compile-time loop bounds.
#define ERHE_X(NAME, FIELD)                                                            \
    defines.emplace_back(std::string{"ERHE_" #NAME}, std::to_string(key.FIELD));
    ERHE_STANDARD_VARIANT_COUNT_AXES(ERHE_X)
#undef ERHE_X

    return defines;
}

} // namespace erhe::scene_renderer
