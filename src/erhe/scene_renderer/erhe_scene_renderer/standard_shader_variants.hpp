#pragma once

#include "erhe_scene_renderer/shader_variant_cache.hpp"
#include "erhe_scene_renderer/standard_shader_variant.hpp"

#include <cstddef>

namespace erhe::graphics {
    class Device;
    class Shader_stages;
}

namespace erhe::scene_renderer {

class Program_interface;

// Typed adapter over Shader_variant_cache for the editor's standard lit
// shader. Translates a Standard_variant_key (10 boolean axes + 6 light
// counts) into a generic Shader_variant_key {name="standard",
// defines=make_standard_variant_defines(key), single-view} and forwards
// to the underlying cache.
//
// The fallback parameter is a shader-stages reference (typically the
// editor's pre-built "error" shader) returned when a variant compile or
// link fails. The failed entry is still stored in the underlying cache
// so the next frame doesn't retry compile.
//
// clear() / size() forward to the underlying cache. The cache owns its
// Shader_monitor lifetime; this adapter holds no entries of its own.
class Standard_shader_variants final
{
public:
    Standard_shader_variants(
        Shader_variant_cache&                cache,
        const erhe::graphics::Shader_stages& fallback_shader_stages
    );
    ~Standard_shader_variants() noexcept;

    Standard_shader_variants(const Standard_shader_variants&)            = delete;
    Standard_shader_variants(Standard_shader_variants&&)                 = delete;
    Standard_shader_variants& operator=(const Standard_shader_variants&) = delete;
    Standard_shader_variants& operator=(Standard_shader_variants&&)      = delete;

    [[nodiscard]] auto get_or_compile(const Standard_variant_key& key) -> const erhe::graphics::Shader_stages*;

    void clear();

    [[nodiscard]] auto size() const -> std::size_t;

private:
    Shader_variant_cache&                m_cache;
    const erhe::graphics::Shader_stages& m_fallback_shader_stages;
};

} // namespace erhe::scene_renderer
