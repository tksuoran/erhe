#pragma once

#include "erhe_scene_renderer/standard_shader_variant.hpp"

#include <cstddef>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace erhe::graphics {
    class Device;
    class Reloadable_shader_stages;
    class Shader_stages;
}

namespace erhe::scene_renderer {

class Program_interface;

// Cache of compiled "standard" shader variants keyed by Standard_variant_key.
//
// On a render-thread cache miss the cache compiles the variant synchronously,
// registers the resulting Reloadable_shader_stages with the device's
// Shader_monitor (so .vert / .frag edits hot-reload every active variant),
// and returns it. If the freshly compiled variant fails validation the
// cache returns the supplied fallback shader (typically the editor's
// pre-built non-variant "standard" stages); the failed entry is still
// stored so we don't retry compile every frame.
//
// clear() drops every cached variant; the next render reissues compiles
// for the entries it still needs. The plan's invalidation triggers
// (material added/removed/edited; light count change in any of the six
// count axes) are all routed through this single call.
//
// This initial implementation compiles synchronously. The plan's "async on
// miss with error-shader fallback" path can be added later by moving the
// compile dispatch behind a worker queue without changing the public API.
class Standard_shader_variants final
{
public:
    Standard_shader_variants(
        erhe::graphics::Device&              graphics_device,
        Program_interface&                   program_interface,
        const erhe::graphics::Shader_stages& fallback_shader_stages
    );
    ~Standard_shader_variants() noexcept;

    Standard_shader_variants(const Standard_shader_variants&)            = delete;
    Standard_shader_variants(Standard_shader_variants&&)                 = delete;
    Standard_shader_variants& operator=(const Standard_shader_variants&) = delete;
    Standard_shader_variants& operator=(Standard_shader_variants&&)      = delete;

    // Look up the compiled shader stages for `key`, compiling on miss.
    // Caller must hold the GL context (the render thread). Returns the
    // fallback shader stages if compile or link fails for the variant.
    // Pointer convention matches Forward_renderer's override_shader_stages.
    [[nodiscard]] auto get_or_compile(const Standard_variant_key& key) -> const erhe::graphics::Shader_stages*;

    // Drop every cached variant. Pre-existing pointers returned by
    // get_or_compile() become dangling -- the caller must not retain
    // them across a clear() call. The Shader_monitor entries pointing
    // at the cached variants are removed first so the polling thread
    // does not call reload() on freed memory.
    void clear();

    [[nodiscard]] auto size() const -> std::size_t;

private:
    erhe::graphics::Device&              m_graphics_device;
    Program_interface&                   m_program_interface;
    const erhe::graphics::Shader_stages& m_fallback_shader_stages;

    // unique_ptr so map rehash / erase doesn't move the Reloadable_shader_stages
    // (the Shader_monitor keeps a stable pointer to the .shader_stages member).
    std::unordered_map<
        Standard_variant_key,
        std::unique_ptr<erhe::graphics::Reloadable_shader_stages>,
        Standard_variant_key_hash
    > m_entries;
    mutable std::mutex m_mutex;
};

} // namespace erhe::scene_renderer
