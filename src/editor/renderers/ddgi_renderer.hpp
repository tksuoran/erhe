#pragma once

#include "erhe_math/aabb.hpp"

#include <glm/glm.hpp>

#include <cstddef>
#include <memory>

namespace erhe::graphics {
    class Command_buffer;
    class Device;
    class Texture;
}
namespace erhe::scene_renderer {
    class Mesh_memory;
}

// erhe_codegen-generated config structs live in the global namespace.
struct Ddgi_config;

namespace editor {

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
// Milestone status: phase 2 - grid fit and texture allocation only. The
// trace / blend / relocation compute passes land in phases 3-5.
class Ddgi_renderer
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
        erhe::graphics::Device&            graphics_device,
        erhe::scene_renderer::Mesh_memory& mesh_memory,
        const Ddgi_config&                 config
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

    // Refits the grid to the scene's content bounds and reallocates the
    // probe textures when the grid or the texel settings changed. Must be
    // called outside a render pass. No-op unless supported and enabled.
    void tick(erhe::graphics::Command_buffer& command_buffer, Scene_root& scene_root);

private:
    // Union of the visible content meshes' world bounds, grown by
    // volume_padding_m. Invalid when the scene has no visible content.
    [[nodiscard]] auto compute_volume_bounds(Scene_root& scene_root) const -> erhe::math::Aabb;

    // Fits a grid to the given box, honouring probe_spacing_m and scaling
    // the spacing up until the probe count fits max_probes.
    [[nodiscard]] auto fit_grid(const erhe::math::Aabb& bounds) const -> Grid;

    // (Re)creates the probe textures for the current grid + texel settings.
    void allocate_textures(erhe::graphics::Command_buffer& command_buffer);

    erhe::graphics::Device&            m_graphics_device;
    erhe::scene_renderer::Mesh_memory& m_mesh_memory;
    // Live reference to the editor's Ddgi_config (editor_settings.ddgi).
    const Ddgi_config&                 m_config;
    bool                               m_supported{false};

    Grid m_grid{};
    int  m_rays_per_probe   {0};
    int  m_irradiance_texels{0};
    int  m_distance_texels  {0};
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
};

} // namespace editor
