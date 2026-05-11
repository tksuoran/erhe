#pragma once

#include "erhe_scene_renderer/cached_shader_handle.hpp"
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
// The fallback handle is itself a lazy Cached_shader_handle (typically
// the editor's "error" shader). When a standard variant compile fails,
// the adapter returns fallback_handle.shader_stages() -- which lazy-
// compiles the fallback if it has not been compiled yet, returning
// nullptr only if the fallback also fails. The renderer's
// error_shader_stages parameter then steps in.
//
// clear() / size() forward to the underlying cache.
class Standard_shader_variants final
{
public:
    Standard_shader_variants(
        Shader_variant_cache& cache,
        Cached_shader_handle& fallback_handle
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
    Shader_variant_cache& m_cache;
    Cached_shader_handle& m_fallback_handle;
};

} // namespace erhe::scene_renderer
