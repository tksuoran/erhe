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

namespace editor {

class App_context;
class Render_context;

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
// Storage-image compute (used to generate the LUTs) is wired on the Vulkan and
// OpenGL backends; OpenGL additionally requires GL 4.3 (use_compute_shader). On
// backends/versions without it, is_atmosphere_supported() returns false and the
// Sky_composition_pass falls back to the gradient sky.
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

    // GPU resources. Constructed on backends with storage-image compute
    // (Vulkan, or OpenGL 4.3+); null elsewhere -> is_atmosphere_supported() == false.
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
