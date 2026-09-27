#pragma once

#include "renderers/indirect_diffuse.hpp"
#include "renderers/probe_grid.hpp"
#include "renderers/radiance_cascades_layout.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace erhe::graphics {
    class Command_buffer;
    class Device;
    class Texture;
}

// erhe_codegen-generated config structs live in the global namespace.
struct Radiance_cascades_config;

namespace editor {

class Scene_root;

// World-space radiance cascades (doc/editor/radiance_cascades.md,
// doc/plans/radiance_cascades.md): the second producer of the indirect
// diffuse probe field, next to Ddgi_renderer.
//
// Fits the cascades to the padded content bounding box
// (fit_radiance_cascades()) and allocates each cascade's raw and merged
// radiance atlases. The trace, merge and reduce passes are later phases of
// the plan: until the reduce pass exists the renderer produces no probe
// field (has_field() is false) and the forward pass keeps the flat ambient
// term while this source is selected.
//
// Requires Device_info::use_ray_query; is_supported() is false otherwise and
// tick() does nothing.
class Radiance_cascades_renderer
{
public:
    // The two atlases of one cascade (RGBA16F, rgb radiance, a transparency
    // beta; storage + sampled). raw holds the traced intervals, merged the
    // raw intervals merged with everything beyond them.
    class Cascade_textures
    {
    public:
        std::shared_ptr<erhe::graphics::Texture> raw;
        std::shared_ptr<erhe::graphics::Texture> merged;
    };

    Radiance_cascades_renderer(
        erhe::graphics::Device&         graphics_device,
        const Radiance_cascades_config& config,
        Producer_selection              selection
    );
    ~Radiance_cascades_renderer() noexcept;

    [[nodiscard]] auto is_supported() const -> bool;
    // Radiance cascades is the selected indirect diffuse source.
    [[nodiscard]] auto is_selected () const -> bool;
    // Called by set_indirect_diffuse_source() only. Deselecting releases the
    // atlases and the layout; selecting leaves the fit to the next tick.
    void               set_selection(Producer_selection selection);
    // Selected AND supported AND a layout was fitted and allocated.
    [[nodiscard]] auto is_active   () const -> bool;
    // The renderer writes the probe field the forward pass samples. False
    // until the reduce pass exists (plan phase 4).
    [[nodiscard]] auto has_field   () const -> bool;

    [[nodiscard]] auto get_layout                   () const -> const Radiance_cascades_layout&;
    [[nodiscard]] auto get_cascade_textures         (int cascade) const -> const Cascade_textures&;
    // Bytes of one cascade's two atlases, and of all cascades.
    [[nodiscard]] auto get_cascade_texture_byte_count(int cascade) const -> std::size_t;
    [[nodiscard]] auto get_texture_byte_count       () const -> std::size_t;
    // Layout refits since construction (each one reallocates the atlases).
    [[nodiscard]] auto get_fit_count                () const -> uint64_t;

    // Refits the cascades and reallocates the atlases when the content
    // bounds or a fit setting changed. Editor::tick() calls it only while
    // radiance cascades is the selected source. Records the atlas clears
    // into the command buffer; must be called outside a render pass.
    void tick(erhe::graphics::Command_buffer& command_buffer, Scene_root& scene_root);

private:
    // Refits + reallocates when needed. Returns false when there is no
    // usable volume this tick.
    [[nodiscard]] auto update_layout    (erhe::graphics::Command_buffer& command_buffer, Scene_root& scene_root) -> bool;
    void               allocate_textures(erhe::graphics::Command_buffer& command_buffer);
    void               release_textures ();

    erhe::graphics::Device&         m_graphics_device;
    // Live reference to the editor's Radiance_cascades_config
    // (editor_settings.radiance_cascades).
    const Radiance_cascades_config& m_config;
    Producer_selection              m_selection{Producer_selection::deselected};
    bool                            m_supported{false};

    // The settings the current layout was fitted with; any change refits.
    Radiance_cascades_layout_settings m_fit_settings{};
    float                             m_fit_padding_m{-1.0f};

    Radiance_cascades_layout                                 m_layout{};
    Probe_volume_bounds                                      m_volume_bounds{};
    std::array<Cascade_textures, c_max_radiance_cascades>    m_cascade_textures{};
    std::array<std::size_t,      c_max_radiance_cascades>    m_cascade_byte_counts{};
    std::size_t                                              m_texture_byte_count{0};
    uint64_t                                                 m_fit_count{0};
};

} // namespace editor
