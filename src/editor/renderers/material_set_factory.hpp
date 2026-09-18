#pragma once

#include "erhe_scene_renderer/material_set.hpp"

#include <cstddef>
#include <memory>

namespace erhe::graphics {
    class Command_buffer;
    class Device;
    class Sampler;
    class Texture;
}
namespace erhe::scene_renderer {
    class Program_interface;
}

namespace editor {

// The application-wide pieces every Material_set is built from
// (doc/draw_list_material_set.md D3, D8): the one fallback texture /
// sampler pair that replaces the pair each renderer used to create for
// itself, and the shared empty set.
//
// Owned by Editor and published into App_context before the construction
// taskflow runs, because the sets themselves are created by objects that
// taskflow builds - the scene roots, the previews - and each of them needs a
// GPU-backed set from its constructor. An owner built inside the taskflow
// would be visible to some of them and not others depending on scheduling.
class Material_set_factory
{
public:
    Material_set_factory(
        erhe::graphics::Device&                  graphics_device,
        erhe::graphics::Command_buffer&          init_command_buffer,
        erhe::scene_renderer::Program_interface& program_interface
    );
    ~Material_set_factory() noexcept;

    Material_set_factory (const Material_set_factory&) = delete;
    void operator=       (const Material_set_factory&) = delete;

    // max_textures is a HARD limit on Vulkan, where it sizes the variable
    // descriptor count, so it is the sizing that matters: the full 4096 for
    // the sets of roots that render content, a small value for the previews,
    // the BRDF slice and the empty set. initial_material_count only decides
    // how much the buffer reserves up front; it grows past it on demand.
    [[nodiscard]] auto make_create_info(
        const char* debug_label,
        std::size_t max_textures           = 4096,
        std::size_t initial_material_count = 256
    ) const -> erhe::scene_renderer::Material_set_create_info;

    // One Material_set holding no materials, bound by the passes that carry
    // no materials of their own - the grid / sky branch of Composition_pass
    // and the depth visualization window's fullscreen triangle. It exists for
    // the texture heap rather than the buffer: giving each such pass its own
    // would add descriptor-set pools for permanently empty content.
    [[nodiscard]] auto get_empty_material_set() -> erhe::scene_renderer::Material_set&;
    // Step 3 of the per-frame material schedule (D6). Its membership never
    // changes, so this writes once and does nothing from then on.
    void update_empty_material_set(erhe::graphics::Command_buffer& command_buffer);

private:
    erhe::graphics::Device&                             m_graphics_device;
    erhe::scene_renderer::Program_interface&            m_program_interface;
    std::shared_ptr<erhe::graphics::Texture>            m_fallback_texture;
    std::unique_ptr<erhe::graphics::Sampler>            m_fallback_sampler;
    std::unique_ptr<erhe::scene_renderer::Material_set> m_empty_material_set;
};

} // namespace editor
