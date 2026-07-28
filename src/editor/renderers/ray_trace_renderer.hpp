#pragma once

#include "erhe_graphics/acceleration_structure.hpp"
#include "erhe_graphics/ring_buffer_client.hpp"
#include "erhe_graphics/sampler.hpp"
#include "erhe_graphics/shader_resource.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <unordered_map>
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
// Requires Device_info::use_ray_query and use_ray_tracing_position_fetch;
// is_supported() is false otherwise and render() does nothing.
//
// Bottom level acceleration structures are built lazily, once per unique
// Buffer_mesh (non-skinned meshes only), reading the Mesh_memory vertex/index
// pools in place. The top level structure is rebuilt every frame from the
// visible content meshes, with one structure per frame-in-flight slot.
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
    class Blas_entry
    {
    public:
        // Pins the GPU vertex/index ranges the structure references.
        std::shared_ptr<erhe::primitive::Primitive>            primitive;
        std::unique_ptr<erhe::graphics::Acceleration_structure> acceleration_structure;
        bool                                                   built{false};
    };

    // CPU mirror of the std430 Instance_record the compute shader reads,
    // indexed by instance_custom_index. Layout is verified against the
    // generated Shader_resource offsets at construction.
    class Instance_record_data
    {
    public:
        uint64_t index_address;      // device address of the first triangle index
        uint64_t vertex_address;     // device address of the stream-1 vertex range start
        uint32_t vertex_stride_uints;
        uint32_t material_index;
        uint32_t flags;
        uint32_t reserved0;
    };
    static_assert(sizeof(Instance_record_data) == 32);

    [[nodiscard]] auto get_or_create_blas(
        erhe::graphics::Command_buffer&                    command_buffer,
        const std::shared_ptr<erhe::primitive::Primitive>& primitive,
        const erhe::primitive::Buffer_mesh&                buffer_mesh
    ) -> erhe::graphics::Acceleration_structure*;

    erhe::graphics::Device&            m_graphics_device;
    App_context&                       m_context;
    erhe::scene_renderer::Mesh_memory& m_mesh_memory;
    // Live reference to the editor's Ray_trace_config
    // (editor_settings.ray_trace): resolution scale, ray budget, bounce cap.
    const Ray_trace_config&            m_config;
    bool                               m_enabled{false};

    std::shared_ptr<erhe::graphics::Texture>                   m_output_texture;
    erhe::graphics::Shader_resource                            m_instance_struct;
    erhe::graphics::Shader_resource                            m_instance_block;
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
    std::unique_ptr<erhe::graphics::Ring_buffer_client>        m_instance_record_buffer;
    uint32_t                                                   m_tlas_binding_point  {0};
    uint32_t                                                   m_output_binding_point{0};

    // Bottom level structure per unique Buffer_mesh. Entries are never
    // evicted (milestone limitation; stale entries for edited/deleted
    // geometry only cost memory, they drop out of the top level rebuild).
    std::unordered_map<const erhe::primitive::Buffer_mesh*, Blas_entry> m_blas_cache;

    // One top level structure per frame-in-flight slot: rebuilding a
    // structure a still-in-flight frame reads is a data race. Slot count
    // must be >= the backend's frames in flight (Vulkan: 2).
    static constexpr std::size_t s_tlas_slot_count = 4;
    class Tlas_slot
    {
    public:
        std::unique_ptr<erhe::graphics::Acceleration_structure> acceleration_structure;
        uint32_t                                                capacity{0};
    };
    std::array<Tlas_slot, s_tlas_slot_count> m_tlas_slots;

    // Per-frame scratch, cleared at point of use (capacity kept).
    std::vector<erhe::graphics::Acceleration_structure_instance> m_instances;
    std::vector<Instance_record_data>                             m_instance_records;
};

} // namespace editor
