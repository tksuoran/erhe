#include "erhe_scene_renderer/standard_shader_variants.hpp"

#include "erhe_scene_renderer/program_interface.hpp"
#include "erhe_scene_renderer/scene_renderer_log.hpp"

#include "erhe_graphics/device.hpp"
#include "erhe_graphics/shader_monitor.hpp"
#include "erhe_graphics/shader_stages.hpp"
#include "erhe_profile/profile.hpp"

namespace erhe::scene_renderer {

Standard_shader_variants::Standard_shader_variants(
    erhe::graphics::Device&              graphics_device,
    Program_interface&                   program_interface,
    const erhe::graphics::Shader_stages& fallback_shader_stages
)
    : m_graphics_device      {graphics_device}
    , m_program_interface    {program_interface}
    , m_fallback_shader_stages{fallback_shader_stages}
{
}

Standard_shader_variants::~Standard_shader_variants() noexcept
{
    // Detach every variant from the Shader_monitor before the unique_ptrs
    // free them; otherwise the monitor's polling thread would dereference
    // freed Shader_stages* on the next file edit.
    erhe::graphics::Shader_monitor& monitor = m_graphics_device.get_shader_monitor();
    for (auto& [key, entry] : m_entries) {
        monitor.remove(*entry);
    }
}

auto Standard_shader_variants::get_or_compile(const Standard_variant_key& key) -> const erhe::graphics::Shader_stages*
{
    ERHE_PROFILE_FUNCTION();

    std::lock_guard<std::mutex> lock{m_mutex};

    const auto it = m_entries.find(key);
    if (it != m_entries.end()) {
        if (it->second->shader_stages.is_valid()) {
            return &it->second->shader_stages;
        }
        return &m_fallback_shader_stages;
    }

    // Cache miss: build, compile, link a fresh variant. The variant
    // shares the standard shader source ("standard.vert" / ".frag") and
    // adds the variant-specific defines on top of the program-interface
    // defaults that make_prototype() injects (struct_types,
    // interface_blocks, fragment_outputs, vertex_format,
    // bind_group_layout, ERHE_SHADOW_MAPS).
    erhe::graphics::Shader_stages_create_info create_info{
        .name    = "standard",
        .defines = make_standard_variant_defines(key)
    };

    erhe::graphics::Shader_stages_prototype prototype = m_program_interface.make_prototype(std::move(create_info));
    prototype.compile_shaders();
    const bool linked = prototype.link_program();

    auto entry = std::make_unique<erhe::graphics::Reloadable_shader_stages>(m_graphics_device, std::move(prototype));
    erhe::graphics::Shader_stages& stages = entry->shader_stages;
    const bool initially_valid = linked && stages.is_valid();

    if (!initially_valid) {
        log_program_interface->error("Standard shader variant compile/link failed for key {{ mask=0x{:08x} }}; using fallback", key.boolean_mask);
    }

    // Register for hot reload unconditionally so a future edit to the
    // .vert / .frag source can recover a variant whose initial compile
    // failed. The Shader_monitor's stored shader_stages pointer refers
    // to entry->shader_stages, which is stable as long as the unique_ptr
    // in m_entries lives -- clear() removes the monitor entry first.
    m_graphics_device.get_shader_monitor().add(*entry);

    m_entries.emplace(key, std::move(entry));

    if (initially_valid) {
        return &stages;
    }
    return &m_fallback_shader_stages;
}

void Standard_shader_variants::clear()
{
    std::lock_guard<std::mutex> lock{m_mutex};
    erhe::graphics::Shader_monitor& monitor = m_graphics_device.get_shader_monitor();
    for (auto& [key, entry] : m_entries) {
        monitor.remove(*entry);
    }
    m_entries.clear();
}

auto Standard_shader_variants::size() const -> std::size_t
{
    std::lock_guard<std::mutex> lock{m_mutex};
    return m_entries.size();
}

} // namespace erhe::scene_renderer
