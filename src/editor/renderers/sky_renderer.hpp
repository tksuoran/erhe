#pragma once

#include "erhe_graphics/shader_stages.hpp"
#include "erhe_scene_renderer/camera_buffer.hpp"

#include <memory>
#include <vector>

namespace erhe::graphics {
    class Base_render_pipeline;
    class Bind_group_layout;
    class Command_buffer;
    class Compute_pipeline;
    class Device;
    class Reloadable_shader_stages;
    class Sampler;
    class Texture;
}
namespace erhe::scene_renderer {
    class Camera_buffer;
    class Program_interface;
}

struct Sky_config;

namespace editor {

class App_context;
class Render_context;
class Scene_render_settings;
class Scene_root;

// Physically-based procedural sky (Sebastien Hillaire, EGSR 2020) rendered as
// a fullscreen pass into the viewport render pass, selected via the sky mode
// config. Owns the transmittance + multi-scattering LUT textures, the two
// compute pipelines that generate them once at startup (using the storage-image
// compute support in erhe::graphics), and the atmosphere graphics pipeline.
//
// Reuses the scene camera UBO (via an owned Camera_buffer) for correct,
// multiview-aware view matrices and the sun direction / atmosphere parameters
// in Sky_parameters; only the two LUT samplers are dedicated to this pass.
//
// On backends without a graphics API (the null backend),
// is_atmosphere_supported() returns false and the Sky_composition_pass falls
// back to the gradient sky.
class Sky_renderer
{
public:
    Sky_renderer(
        erhe::graphics::Device&                  graphics_device,
        erhe::graphics::Command_buffer&          init_command_buffer,
        App_context&                             context,
        erhe::scene_renderer::Program_interface& program_interface,
        int                                      view_count
    );
    ~Sky_renderer() noexcept;
    Sky_renderer (const Sky_renderer&) = delete;
    void operator=(const Sky_renderer&) = delete;

    [[nodiscard]] auto is_atmosphere_supported() const -> bool;

    // Generates the transmittance + multi-scattering LUTs once (no-op after the
    // first successful call). Must be called outside a render pass (it issues
    // compute dispatches + image barriers), before render_atmosphere().
    void ensure_luts(erhe::graphics::Device& graphics_device, erhe::graphics::Command_buffer& command_buffer);

    // Draws the atmosphere fullscreen pass into the current viewport render
    // pass (context.encoder). No-op when atmosphere is unsupported.
    void render_atmosphere(const Render_context& context);

    // LUT access for the lightmap baker's sky term (Lightmap_baker::
    // set_sky_lighting): valid only when atmosphere is supported and the
    // LUTs were generated. The textures are created by the constructor and
    // never replaced; ensure_luts() (render phase) records their contents.
    [[nodiscard]] auto get_transmittance_lut() const -> erhe::graphics::Texture* { return m_transmittance_lut.get(); }
    [[nodiscard]] auto get_multiscatter_lut () const -> erhe::graphics::Texture* { return m_multiscatter_lut.get(); }
    // Render phase only: ensure_luts() ran. The main thread reads the value
    // the last rendered frame reported (Frame_result::sky_luts_ready).
    [[nodiscard]] auto are_luts_ready       () const -> bool;

    // Toward-sun direction for a scene: the scene's last directional light
    // when present (scene_render_settings, the frame's copy from
    // Scene_root::get_render_settings(); null without a scene), else the
    // config's elevation / azimuth. The same resolution render_atmosphere()
    // uses, shared with the lightmap baker so the baked sky and the rendered
    // sky always agree on the sun.
    [[nodiscard]] static auto resolve_sun_direction(const Sky_config& sky_config, const Scene_render_settings* scene_render_settings) -> glm::vec3;

private:
    App_context&                             m_context;
    erhe::graphics::Device&                  m_graphics_device;
    erhe::scene_renderer::Program_interface& m_program_interface;
    int                                      m_view_count;
    bool                                     m_luts_ready{false};
    // True when the device rejects storage-image reads in compute (KosmicKrisp):
    // the multi-scatter pass then samples the transmittance LUT instead of
    // reading it as a storage image. Set from Device_info at construction so the
    // bind group layout and the LUT generation agree. See
    // WORKAROUND_NO_COMPUTE_STORAGE_IMAGE_READ.
    bool                                     m_sample_transmittance_lut{false};

    // GPU resources. Constructed on backends with a graphics API; null on the
    // null backend -> is_atmosphere_supported() == false.
    std::unique_ptr<erhe::graphics::Texture>                  m_transmittance_lut;
    std::unique_ptr<erhe::graphics::Texture>                  m_multiscatter_lut;
    std::unique_ptr<erhe::graphics::Sampler>                  m_lut_sampler;
    std::unique_ptr<erhe::graphics::Bind_group_layout>        m_transmittance_layout;
    std::unique_ptr<erhe::graphics::Bind_group_layout>        m_multiscatter_layout;
    std::unique_ptr<erhe::graphics::Bind_group_layout>        m_atmosphere_layout;
    std::unique_ptr<erhe::graphics::Reloadable_shader_stages> m_transmittance_compute;
    std::unique_ptr<erhe::graphics::Reloadable_shader_stages> m_multiscatter_compute;
    std::unique_ptr<erhe::graphics::Reloadable_shader_stages> m_atmosphere_shader;
    std::unique_ptr<erhe::graphics::Compute_pipeline>         m_transmittance_pipeline;
    std::unique_ptr<erhe::graphics::Compute_pipeline>         m_multiscatter_pipeline;
    std::unique_ptr<erhe::graphics::Base_render_pipeline>     m_atmosphere_pipeline;
    std::unique_ptr<erhe::scene_renderer::Camera_buffer>      m_camera_buffer;

    // Per-frame scratch padded to exactly view_count entries (Camera_buffer::
    // update_views requires that). Capacity is retained across frames so steady
    // state does no allocation.
    std::vector<erhe::scene_renderer::Camera_view_input>     m_view_scratch;
};

} // namespace editor
