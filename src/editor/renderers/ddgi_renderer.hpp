#pragma once

#include "renderable.hpp"
#include "renderers/scene_tlas.hpp"

#include "erhe_graphics/sampler.hpp"
#include "erhe_graphics/shader_resource.hpp"
#include "erhe_math/aabb.hpp"

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <random>

namespace erhe::graphics {
    class Bind_group_layout;
    class Buffer;
    class Command_buffer;
    class Compute_pipeline;
    class Device;
    class Reloadable_shader_stages;
    class Ring_buffer_client;
    class Texture;
    class Texture_heap;
}
namespace erhe::scene_renderer {
    class Light_buffer;
    class Light_projections;
    class Material_buffer;
    class Mesh_memory;
    class Program_interface;
}

// erhe_codegen-generated config structs live in the global namespace.
struct Ddgi_config;

namespace editor {

class App_context;
class Render_context;
class Scene_root;

// Dynamic diffuse global illumination (doc/ddgi-plan.md).
//
// One scene-wide probe volume, auto-fitted to the padded content bounding
// box. Probes are traced with ray queries into a ray data texture, blended
// into octahedral irradiance and distance atlases with temporal hysteresis,
// and sampled by the forward shader in place of the flat ambient term.
//
// Requires Device_info::use_ray_query; is_supported() is false otherwise and
// tick() does nothing. All probe state is held in 2D textures because the
// graphics abstraction only exposes image2D storage images.
//
// Milestone status: phase 3 - grid fit, texture allocation and the probe
// trace pass. The blend / relocation passes land in phases 4-5.
class Ddgi_renderer : public Renderable
{
public:
    // The fitted probe grid. spacing is per axis: the padded box extent
    // divided by (counts - 1), so the first and last probe planes sit
    // exactly on the box faces.
    class Grid
    {
    public:
        glm::vec3  origin {0.0f};              // world position of probe (0,0,0)
        glm::vec3  spacing{1.0f};              // world distance between adjacent probes, per axis
        glm::ivec3 counts {0};                 // probes per axis

        [[nodiscard]] auto get_probe_count() const -> int { return counts.x * counts.y * counts.z; }
        [[nodiscard]] auto is_valid       () const -> bool { return (counts.x > 1) && (counts.y > 1) && (counts.z > 1); }
        [[nodiscard]] auto operator==(const Grid& other) const -> bool;
        [[nodiscard]] auto operator!=(const Grid& other) const -> bool { return !(*this == other); }
    };

    Ddgi_renderer(
        erhe::graphics::Device&                  graphics_device,
        erhe::graphics::Command_buffer&          init_command_buffer,
        App_context&                             context,
        erhe::scene_renderer::Program_interface& program_interface,
        erhe::scene_renderer::Mesh_memory&       mesh_memory,
        const Ddgi_config&                       config
    );
    ~Ddgi_renderer() noexcept;

    [[nodiscard]] auto is_supported() const -> bool;
    // Configured on AND supported AND a usable grid was fitted.
    [[nodiscard]] auto is_active   () const -> bool;

    [[nodiscard]] auto get_grid                    () const -> const Grid&;
    [[nodiscard]] auto get_irradiance_texture      () const -> const std::shared_ptr<erhe::graphics::Texture>&;
    [[nodiscard]] auto get_distance_texture        () const -> const std::shared_ptr<erhe::graphics::Texture>&;
    [[nodiscard]] auto get_probe_data_texture      () const -> const std::shared_ptr<erhe::graphics::Texture>&;
    [[nodiscard]] auto get_ray_data_texture        () const -> const std::shared_ptr<erhe::graphics::Texture>&;
    [[nodiscard]] auto get_texture_byte_count      () const -> std::size_t;
    // Rays per probe actually used (the configured value rounded up to the
    // trace workgroup size), and the interior octahedral resolutions.
    [[nodiscard]] auto get_rays_per_probe          () const -> int;
    [[nodiscard]] auto get_irradiance_texels       () const -> int;
    [[nodiscard]] auto get_distance_texels         () const -> int;
    // Probes updated per tick (config budget clamped to the grid), and how
    // many ticks one full sweep of the grid therefore takes.
    [[nodiscard]] auto get_probes_per_update       () const -> int;
    [[nodiscard]] auto get_instance_count          () const -> std::size_t;

    // Refits the grid, reallocates the probe textures when needed, and
    // records this tick's probe trace into the command buffer. Must be
    // called outside a render pass. No-op unless supported and enabled.
    void tick(erhe::graphics::Command_buffer& command_buffer, Scene_root& scene_root);

    // Implements Renderable: the probe overlay (volume box + one wire
    // sphere per probe, coloured by classification state and offset by the
    // relocation the GPU applied). Drawn only when debug_draw_probes is on.
    void render(const Render_context& context) override;

private:
    // The two blend passes share one source (ddgi_blend.comp, switched by
    // ERHE_DDGI_BLEND_DISTANCE) but need separate layouts: the irradiance
    // atlas is rgba16f and the distance atlas rg16f.
    class Blend_pass
    {
    public:
        std::unique_ptr<erhe::graphics::Bind_group_layout>        bind_group_layout;
        std::unique_ptr<erhe::graphics::Reloadable_shader_stages> shader_stages;
        std::unique_ptr<erhe::graphics::Compute_pipeline>         pipeline;
    };

    // Union of the visible content meshes' world bounds, grown by
    // volume_padding_m. Invalid when the scene has no visible content.
    [[nodiscard]] auto compute_volume_bounds(Scene_root& scene_root) const -> erhe::math::Aabb;

    // Fits a grid to the given box, honouring probe_spacing_m and scaling
    // the spacing up until the probe count fits max_probes.
    [[nodiscard]] auto fit_grid(const erhe::math::Aabb& bounds) const -> Grid;

    // (Re)creates the probe textures for the current grid + texel settings.
    void allocate_textures(erhe::graphics::Command_buffer& command_buffer);

    // Refits + reallocates when needed. Returns false when there is no
    // usable volume this tick.
    [[nodiscard]] auto update_volume(erhe::graphics::Command_buffer& command_buffer, Scene_root& scene_root) -> bool;

    // Writes this tick's control UBO (grid, dispatch window, rotation).
    [[nodiscard]] auto update_control_buffer() -> erhe::graphics::Ring_buffer_range;

    // Builds one of the two blend passes from the shared ddgi_blend.comp.
    void create_blend_pass(
        erhe::graphics::Device& graphics_device,
        Blend_pass&             pass,
        bool                    distance,
        const char*             image_name,
        const char*             image_format,
        const char*             debug_label
    );

    // Copies the probe data texture into the host-visible mirror the debug
    // overlay reads. Recorded into the frame's command buffer, so the
    // overlay sees the previous frame's probes - fine for a debug aid, and
    // it costs no stall.
    void copy_probe_data_for_debug(erhe::graphics::Command_buffer& command_buffer);

    // A uniformly distributed random rotation for this tick's ray set.
    [[nodiscard]] auto next_random_rotation() -> glm::vec4;

    erhe::graphics::Device& m_graphics_device;
    App_context&            m_context;
    // Live reference to the editor's Ddgi_config (editor_settings.ddgi).
    const Ddgi_config&      m_config;
    bool                    m_supported{false};

    Grid m_grid{};
    int  m_rays_per_probe   {0};
    int  m_irradiance_texels{0};
    int  m_distance_texels  {0};
    int  m_probes_per_update{0};
    // Round-robin cursor: the first probe this tick's budget updates.
    uint32_t m_probe_cursor{0};
    // Config values the current grid was fitted with. Changing any of them
    // changes the fit itself, so they force a refit even when the scene's
    // content bounds did not move.
    float m_fit_spacing_m {0.0f};
    float m_fit_padding_m {-1.0f};
    int   m_fit_max_probes{0};

    std::shared_ptr<erhe::graphics::Texture> m_irradiance_texture;
    std::shared_ptr<erhe::graphics::Texture> m_distance_texture;
    std::shared_ptr<erhe::graphics::Texture> m_probe_data_texture;
    std::shared_ptr<erhe::graphics::Texture> m_ray_data_texture;
    std::size_t                              m_texture_byte_count{0};

    // The padded box the current grid was fitted to. The grid is only
    // refitted when the scene's content leaves this box or shrinks well
    // inside it - refitting on every content transform would reallocate the
    // probe textures (and throw away their converged contents) every frame.
    erhe::math::Aabb m_volume_bounds{};

    // Radiance a probe ray gets when it escapes the scene. Scene ambient for
    // now; the atmosphere LUTs are a later refinement.
    glm::vec3 m_sky_radiance{0.0f};

    // GPU side. The acceleration structures are the shared Scene_tlas; the
    // material / light buffers and the texture heap mirror
    // Ray_trace_renderer's, because the probe trace shades hits with the
    // same erhe_ray_hit.glsl path.
    std::unique_ptr<Scene_tlas>                               m_scene_tlas;
    erhe::graphics::Shader_resource                           m_control_block;
    std::unique_ptr<erhe::graphics::Ring_buffer_client>       m_control_buffer;
    std::unique_ptr<erhe::graphics::Bind_group_layout>        m_trace_bind_group_layout;
    std::unique_ptr<erhe::graphics::Reloadable_shader_stages> m_trace_shader_stages;
    std::unique_ptr<erhe::graphics::Compute_pipeline>         m_trace_pipeline;

    Blend_pass m_blend_irradiance;
    Blend_pass m_blend_distance;

    // Probe relocation + classification (phase 5). Shares the blend passes'
    // shape: control UBO + ray data + the probe data texture it writes.
    std::unique_ptr<erhe::graphics::Bind_group_layout>        m_relocate_bind_group_layout;
    std::unique_ptr<erhe::graphics::Reloadable_shader_stages> m_relocate_shader_stages;
    std::unique_ptr<erhe::graphics::Compute_pipeline>         m_relocate_pipeline;
    std::unique_ptr<erhe::scene_renderer::Material_buffer>    m_material_buffer;
    std::unique_ptr<erhe::scene_renderer::Light_buffer>       m_light_buffer;
    std::unique_ptr<erhe::scene_renderer::Light_projections>  m_light_projections;
    erhe::graphics::Sampler                                   m_fallback_sampler;
    std::shared_ptr<erhe::graphics::Texture>                  m_dummy_texture;
    std::unique_ptr<erhe::graphics::Texture_heap>             m_texture_heap;
    uint32_t                                                  m_tlas_binding_point      {0};
    uint32_t                                                  m_ray_data_binding_point  {0};
    uint32_t                                                  m_probe_data_binding_point{0};

    // Control block field offsets, resolved once at construction.
    class Control_offsets
    {
    public:
        std::size_t grid_origin    {0};
        std::size_t grid_spacing   {0};
        std::size_t grid_counts    {0};
        std::size_t dispatch       {0};
        std::size_t random_rotation{0};
        std::size_t params         {0};
        std::size_t sky_radiance   {0};
        std::size_t flags          {0};
    };
    Control_offsets m_control_offsets{};

    std::mt19937 m_random_engine{0x0DD91u};

    // Host-visible mirror of the probe data texture (xyz relocation offset,
    // w state), refreshed while the probe overlay is enabled.
    std::unique_ptr<erhe::graphics::Buffer> m_probe_readback_buffer;
    bool                                    m_probe_readback_valid{false};
};

} // namespace editor
