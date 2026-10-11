#pragma once

#include "renderers/render_frame_results.hpp"
#include "renderers/scene_tlas.hpp"

#include "erhe_graphics/ring_buffer_client.hpp"
#include "erhe_graphics/sampler.hpp"
#include "erhe_graphics/shader_resource.hpp"

#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

namespace erhe::graphics {
    class Bind_group_layout;
    class Buffer;
    class Command_buffer;
    class Compute_pipeline;
    class Device;
    class Reloadable_shader_stages;
    class Texture;
    class Texture_heap;
    class Texture_reference;
}
namespace erhe::math {
    class Viewport;
}
namespace erhe::primitive {
    class Buffer_mesh;
    class Material;
    class Primitive;
}
namespace erhe::scene {
    class Camera;
    class Camera_snapshot;
}
namespace erhe::scene_renderer {
    class Camera_buffer;
    class Light_buffer;
    class Light_projections;
    class Material_buffer;
    class Mesh_memory;
    class Program_interface;
}

// erhe_codegen-generated config structs live in the global namespace.
struct Ray_trace_config;

namespace editor {

class App_context;
class Scene_root;

// GPU ray tracing renderer (issue #233): a ray query compute shader renders
// the scene into a storage texture that the Ray_trace_window displays. Hits
// are shaded with real materials (base color / metallic-roughness textures
// via the texture heap, interpolated smooth normals from the mesh memory
// stream-1 pool reached through buffer device addresses) against the scene
// lights with ray traced shadows; transmissive materials (the Material property
// transmission > 0) refract with Fresnel-weighted traced reflections.
// Requires Device_info::use_ray_query; is_supported() is false otherwise and
// render() does nothing. When use_ray_tracing_position_fetch is additionally
// available the shader reads the committed triangle's positions from the
// acceleration structure; otherwise it falls back to fetching them from the
// mesh memory stream-0 pool via the per-instance device addresses (the same
// mechanism the attribute fetch already uses), so backends without the
// extension (Metal) still work.
//
// What it traces is the scene root's Ray_trace_scene slot for the frame
// (prepared in M2; the bottom level structures are shared with the DDGI and
// radiance cascades consumers); the per-frame top level structure and the
// per-instance record SSBO live in this renderer's Scene_tlas - see
// renderers/ray_trace_scene.hpp and renderers/scene_tlas.hpp.
class Ray_trace_renderer
{
public:
    Ray_trace_renderer(
        erhe::graphics::Device&                  graphics_device,
        erhe::graphics::Command_buffer&          init_command_buffer,
        App_context&                             context,
        erhe::scene_renderer::Program_interface& program_interface,
        erhe::scene_renderer::Mesh_memory&       mesh_memory
    );
    ~Ray_trace_renderer() noexcept;

    [[nodiscard]] auto is_supported      () const -> bool;
    // Main thread: the live switch (Ray Trace window, MCP set_ray_trace).
    // The prepare phase hands it to the render phase of each frame in the
    // Frame_packet (set_render_enabled()).
    [[nodiscard]] auto is_enabled        () const -> bool;
    void               set_enabled       (bool enabled);
    // Render phase, at its start: the frame's copy of the switch, which
    // render() reads.
    void               set_render_enabled(bool enabled);
    // Render phase: the output texture as render() left it. The main thread
    // reaches it only through get_output_texture_reference().
    [[nodiscard]] auto get_output_texture() const -> std::shared_ptr<erhe::graphics::Texture>;
    // A reference to the output texture that the render thread resolves when
    // it draws the ImGui window showing it (render() replaces the texture on
    // a resize).
    [[nodiscard]] auto get_output_texture_reference() const -> const std::shared_ptr<erhe::graphics::Texture_reference>&;

    // What render() made of a frame, for the main thread.
    class Rendered_output
    {
    public:
        int         width         {0};
        int         height        {0};
        std::size_t instance_count{0};
    };
    // The output of the last rendered frame (pass App_context::
    // last_rendered_frame_index), or null when that frame traced nothing.
    [[nodiscard]] auto get_rendered_output(std::optional<uint64_t> frame_index) const -> const Rendered_output*;

    // The output texture readback for the MCP set_ray_trace save_path
    // diagnostic (not per-frame use), handed to the render phase: the main
    // thread requests it, the render phase records the copy into its frame
    // command buffer after the rendergraph (record_output_readback()), and
    // the main thread reads the pixels once that frame retired on the GPU.
    // The state is shared under m_readback_mutex.
    enum class Readback_state : unsigned int {
        idle = 0,  // nothing requested (or the last result was taken)
        requested, // waiting for a render phase to record the copy
        in_flight, // copy recorded, waiting for its frame to retire
        complete,  // pixels read back (take_output_readback())
        failed     // no output texture when the render phase looked
    };
    // Main thread: asks the next render phase for a copy. False while an
    // earlier copy is requested or in flight.
    [[nodiscard]] auto request_output_readback() -> bool;
    // Main thread: the state; reads the pixels back once the frame that
    // recorded the copy has retired.
    [[nodiscard]] auto poll_output_readback() -> Readback_state;
    // Main thread, after poll_output_readback() reported complete: the
    // output texture as tightly packed RGBA8 rows (top row first; the
    // texture format is format_8_vec4_unorm) and its size; the state goes
    // back to idle.
    void take_output_readback(std::vector<uint8_t>& out_pixels, int& out_width, int& out_height);
    // Main thread: drops a requested copy no render phase recorded yet
    // (its MCP request expired); a recorded one completes and is dropped by
    // the next request.
    void cancel_output_readback();
    // Render phase, after the rendergraph: records a requested copy of the
    // output texture (as this frame's render() left it) into a host-visible
    // buffer.
    void record_output_readback(erhe::graphics::Command_buffer& command_buffer);

    // Records acceleration structure builds and the ray trace compute
    // dispatch into the command buffer. Must be called outside a render pass
    // (compute dispatches + image layout transitions). viewport is the
    // viewport the camera renders the raster view with - it defines both
    // the projection (aspect / fov) and the output texture's resolution,
    // so the traced image matches the raster view 1:1 (the texture is
    // recreated on viewport resize). light_projections may be null (e.g.
    // no shadow render node); the scene lights are then skipped and only
    // ambient light applies. No-op unless supported and enabled.
    void render(
        erhe::graphics::Command_buffer&                  command_buffer,
        Scene_root&                                      scene_root,
        const erhe::scene::Camera_snapshot&              camera, // the render phase's copy of the view camera
        const erhe::math::Viewport&                      viewport,
        const erhe::scene_renderer::Light_projections*   light_projections
    );

private:
    // The calling role's copy of the ray trace settings (resolution scale,
    // ray budget, bounce cap; App_context::render_settings->ray_trace): the
    // render phase reads the copy of the frame it renders, so the config is
    // fetched at each use, never kept.
    [[nodiscard]] auto get_config() const -> const Ray_trace_config&;

    // Render phase: writes this frame's Rendered_output.
    void publish_rendered_output();

    erhe::graphics::Device&            m_graphics_device;
    App_context&                       m_context;
    bool                               m_enabled{false};        // live, main thread
    bool                               m_render_enabled{false}; // the rendered frame's copy, render phase
    std::shared_ptr<erhe::graphics::Texture_reference>         m_output_texture_reference;
    Render_frame_results<Rendered_output>                      m_rendered_outputs;

    std::shared_ptr<erhe::graphics::Texture>                   m_output_texture;

    // The output readback (request_output_readback()), under
    // m_readback_mutex: the main thread requests and polls, the render phase
    // records.
    std::mutex                                                 m_readback_mutex;
    Readback_state                                             m_readback_state{Readback_state::idle};
    uint64_t                                                   m_readback_frame {0};
    int                                                        m_readback_width {0};
    int                                                        m_readback_height{0};
    std::unique_ptr<erhe::graphics::Buffer>                    m_readback_buffer;
    std::vector<uint8_t>                                       m_readback_pixels;
    std::unique_ptr<Scene_tlas>                                m_scene_tlas;
    erhe::graphics::Shader_resource                            m_control_block;
    std::size_t                                                m_control_max_rays_offset   {0};
    std::size_t                                                m_control_max_bounces_offset{0};
    std::unique_ptr<erhe::graphics::Ring_buffer_client>        m_control_buffer;
    std::unique_ptr<erhe::graphics::Bind_group_layout>         m_bind_group_layout;
    std::unique_ptr<erhe::graphics::Reloadable_shader_stages>  m_shader_stages;
    std::unique_ptr<erhe::graphics::Compute_pipeline>          m_pipeline;
    std::unique_ptr<erhe::scene_renderer::Camera_buffer>       m_camera_buffer;
    // No material buffer, texture heap or fallback pair: the tracing dispatch
    // binds the scene root's forward Material_set, which owns all three
    // (doc/erhe/draw_list_material_set.md D5).
    std::unique_ptr<erhe::scene_renderer::Light_buffer>        m_light_buffer;
    uint32_t                                                   m_tlas_binding_point  {0};
    uint32_t                                                   m_output_binding_point{0};
};

} // namespace editor
