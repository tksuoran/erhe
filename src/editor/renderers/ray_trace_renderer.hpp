#pragma once

#include "renderers/scene_tlas.hpp"

#include "erhe_graphics/ring_buffer_client.hpp"
#include "erhe_graphics/sampler.hpp"
#include "erhe_graphics/shader_resource.hpp"

#include <cstdint>
#include <memory>
#include <vector>

namespace erhe::graphics {
    class Bind_group_layout;
    class Command_buffer;
    class Compute_pipeline;
    class Device;
    class Reloadable_shader_stages;
    class Texture;
    class Texture_heap;
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
// lights with ray traced shadows; transmissive materials (Material_data::
// transmission > 0) refract with Fresnel-weighted traced reflections.
// Requires Device_info::use_ray_query; is_supported() is false otherwise and
// render() does nothing. When use_ray_tracing_position_fetch is additionally
// available the shader reads the committed triangle's positions from the
// acceleration structure; otherwise it falls back to fetching them from the
// mesh memory stream-0 pool via the per-instance device addresses (the same
// mechanism the attribute fetch already uses), so backends without the
// extension (Metal) still work.
//
// The acceleration structures (per-Buffer_mesh bottom level cache, per-frame
// top level rebuild, per-instance record SSBO) live in the shared Scene_tlas
// helper - see renderers/scene_tlas.hpp.
class Ray_trace_renderer
{
public:
    Ray_trace_renderer(
        erhe::graphics::Device&                  graphics_device,
        erhe::graphics::Command_buffer&          init_command_buffer,
        App_context&                             context,
        erhe::scene_renderer::Program_interface& program_interface,
        erhe::scene_renderer::Mesh_memory&       mesh_memory,
        const Ray_trace_config&                  config
    );
    ~Ray_trace_renderer() noexcept;

    [[nodiscard]] auto is_supported      () const -> bool;
    [[nodiscard]] auto is_enabled        () const -> bool;
    void               set_enabled       (bool enabled);
    [[nodiscard]] auto get_output_texture() const -> std::shared_ptr<erhe::graphics::Texture>;
    [[nodiscard]] auto get_instance_count() const -> std::size_t;

    // Reads the output texture back as tightly packed RGBA8 rows (top row
    // first). Self-contained submit + wait; intended for the MCP
    // set_ray_trace save_path diagnostic, not per-frame use.
    [[nodiscard]] auto read_output_rgba8(std::vector<uint8_t>& out_pixels) -> bool;

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
        const erhe::scene::Camera&                       camera,
        const erhe::math::Viewport&                      viewport,
        const erhe::scene_renderer::Light_projections*   light_projections
    );

private:
    erhe::graphics::Device&            m_graphics_device;
    App_context&                       m_context;
    // Live reference to the editor's Ray_trace_config
    // (editor_settings.ray_trace): resolution scale, ray budget, bounce cap.
    const Ray_trace_config&            m_config;
    bool                               m_enabled{false};

    std::shared_ptr<erhe::graphics::Texture>                   m_output_texture;
    std::unique_ptr<Scene_tlas>                                m_scene_tlas;
    erhe::graphics::Shader_resource                            m_control_block;
    std::size_t                                                m_control_max_rays_offset   {0};
    std::size_t                                                m_control_max_bounces_offset{0};
    std::unique_ptr<erhe::graphics::Ring_buffer_client>        m_control_buffer;
    std::unique_ptr<erhe::graphics::Bind_group_layout>         m_bind_group_layout;
    std::unique_ptr<erhe::graphics::Reloadable_shader_stages>  m_shader_stages;
    std::unique_ptr<erhe::graphics::Compute_pipeline>          m_pipeline;
    std::unique_ptr<erhe::scene_renderer::Camera_buffer>       m_camera_buffer;
    std::unique_ptr<erhe::scene_renderer::Material_buffer>     m_material_buffer;
    std::unique_ptr<erhe::scene_renderer::Light_buffer>        m_light_buffer;
    erhe::graphics::Sampler                                    m_fallback_sampler;
    std::shared_ptr<erhe::graphics::Texture>                   m_dummy_texture;
    std::unique_ptr<erhe::graphics::Texture_heap>              m_texture_heap;
    uint32_t                                                   m_tlas_binding_point  {0};
    uint32_t                                                   m_output_binding_point{0};
};

} // namespace editor
