#include "erhe_scene_renderer/shader_variant_cache.hpp"

#include "erhe_scene_renderer/program_interface.hpp"
#include "erhe_scene_renderer/scene_renderer_log.hpp"
#include "erhe_scene_renderer/variant_handle.hpp"

#include "erhe_graphics/device.hpp"
#include "erhe_graphics/shader_monitor.hpp"
#include "erhe_graphics/shader_stages.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_verify/verify.hpp"

#include <algorithm>

namespace erhe::scene_renderer {

Shader_variant_cache::Shader_variant_cache(
    erhe::graphics::Device& graphics_device,
    Program_interface&      program_interface
)
    : m_graphics_device  {graphics_device}
    , m_program_interface{program_interface}
{
}

Shader_variant_cache::~Shader_variant_cache() noexcept
{
    erhe::graphics::Shader_monitor& monitor = m_graphics_device.get_shader_monitor();
    for (auto& [key, entry] : m_entries) {
        monitor.remove(*entry);
    }
}

auto Shader_variant_cache::get(
    const Shader_key&                      shader_key,
    const erhe::dataformat::Vertex_format* vertex_format
) -> erhe::graphics::Reloadable_shader_stages*
{
    const auto it = m_entries.find(shader_key);
    if (it != m_entries.end()) {
        erhe::graphics::Reloadable_shader_stages* entry = it->second.get();
        ERHE_VERIFY(entry->shader_stages.is_valid());
        return entry;
    }

    erhe::graphics::Shader_stages_create_info create_info{
        .name          = "standard",
        .defines       = shader_key.get_defines(),
        .vertex_format = vertex_format,
        .view_count    = shader_key.get(Shader_int::SHADER_MULTIVIEW_COUNT)
    };

    erhe::graphics::Shader_stages_prototype prototype = m_program_interface.make_prototype(std::move(create_info));
    prototype.compile_shaders();
    const bool linked = prototype.link_program();
    if (!linked) {
        return nullptr;
    }

    auto entry = std::make_unique<erhe::graphics::Reloadable_shader_stages>(m_graphics_device, std::move(prototype));
    erhe::graphics::Reloadable_shader_stages* entry_ptr = entry.get();
    const bool valid = linked && entry_ptr->shader_stages.is_valid();

    if (!valid) {
        return nullptr;
    }

    // Register for hot reload unconditionally so a future edit to the
    // .vert / .frag source can recover a variant whose initial compile
    // failed. Stable pointer guaranteed by the unique_ptr; entries are
    // only ever freed in the cache's destructor (clear() invalidates in
    // place), so the monitor's Shader_stages* stays valid for the
    // cache's whole lifetime.
    m_graphics_device.get_shader_monitor().add(*entry_ptr);

    m_entries[shader_key] = std::move(entry);
    return entry_ptr;
}


} // namespace erhe::scene_renderer
