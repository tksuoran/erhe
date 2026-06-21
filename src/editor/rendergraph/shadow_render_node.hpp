#pragma once

#include "erhe_rendergraph/rendergraph_node.hpp"
#include "erhe_scene_renderer/light_buffer.hpp"

#include <memory>
#include <span>
#include <string>

namespace erhe::graphics       { class Command_buffer; class Device; class Gpu_timer; }
namespace erhe::scene_renderer { class Light_projections; }

namespace editor {

class App_context;
class Scene_view;
class Viewport_scene_view;

// Helper rendergraph node calling Shadow_renderer

// TODO Think about Shadow_render_node / Shadow_renderer relationship,
//      should Shadow_render_node have more state?
class Shadow_render_node : public erhe::rendergraph::Rendergraph_node
{
public:
    Shadow_render_node(
        erhe::graphics::Device&         graphics_device,
        erhe::graphics::Command_buffer& init_command_buffer,
        erhe::rendergraph::Rendergraph& rendergraph,
        App_context&                    context,
        Scene_view&                     scene_view,
        int                             resolution,
        int                             light_count,
        int                             depth_bits,
        int                             point_resolution,
        int                             point_light_count
    );
    ~Shadow_render_node() noexcept override;

    // Implements Rendergraph_node
    auto get_type_name() const -> std::string_view override { return "Shadow_render_node"; }
    void execute_rendergraph_node(erhe::graphics::Command_buffer& command_buffer) override;

    auto get_producer_output_texture (int key, int depth = 0) const -> std::shared_ptr<erhe::graphics::Texture> override;
    auto inputs_allowed() const -> bool override;

    // Public API
    void reconfigure(erhe::graphics::Device& graphics_device, erhe::graphics::Command_buffer& command_buffer, int resolution, int light_count, int depth_bits, bool distance_technique, int point_resolution, int point_light_count);

    [[nodiscard]] auto get_scene_view       () -> Scene_view&;
    [[nodiscard]] auto get_scene_view       () const -> const Scene_view&;
    [[nodiscard]] auto get_light_projections() -> erhe::scene_renderer::Light_projections&;
    [[nodiscard]] auto get_texture          () const -> std::shared_ptr<erhe::graphics::Texture>;
    [[nodiscard]] auto get_viewport         () const -> erhe::math::Viewport;

    // Read-only access to the per-light shadow Render_pass list and the
    // active reverse-depth setting. Used by the init-time pipeline
    // prewarm (renderers/prewarm.cpp) to drive Shadow_renderer::
    // prewarm_pipelines so vkCreateGraphicsPipelines for each shadow
    // pass runs at init rather than first frame.
    [[nodiscard]] auto get_render_passes    () const -> std::span<const std::unique_ptr<erhe::graphics::Render_pass>>;
    [[nodiscard]] auto get_reverse_depth    () const -> bool;

private:
    App_context&                                              m_context;
    Scene_view&                                               m_scene_view;
    std::shared_ptr<erhe::graphics::Texture>                  m_texture;
    // Shadow_technique_mode::distance R32F distance map (color attachment of the
    // render passes). Allocated only while the distance technique is active;
    // null otherwise (a full-resolution R32F array is large). m_distance_technique
    // tracks the last-configured technique so execute_rendergraph_node can
    // reconfigure lazily when the preset flips; the cached resolution / light
    // count / depth bits let it re-call reconfigure with the same dimensions.
    std::shared_ptr<erhe::graphics::Texture>                  m_distance_texture;
    bool                                                      m_distance_technique{false};
    int                                                       m_resolution {0};
    int                                                       m_light_count{0};
    int                                                       m_depth_bits {0};
    std::vector<std::unique_ptr<erhe::graphics::Render_pass>> m_render_passes;

    // Omnidirectional point-light shadows: an R32F texture_cube_map_array (one
    // cube / 6 faces per shadow-casting point light) of radial distances, a
    // shared 2D depth scratch reused as the rasterization depth for every face,
    // and 6 * m_point_light_count render passes (face f of cube p ->
    // m_point_render_passes[6*p + f]). Sized by the preset's
    // point_shadow_resolution / point_shadow_light_count.
    std::shared_ptr<erhe::graphics::Texture>                  m_point_cube_texture;
    std::shared_ptr<erhe::graphics::Texture>                  m_point_cube_depth;
    std::vector<std::unique_ptr<erhe::graphics::Render_pass>> m_point_render_passes;
    int                                                       m_point_resolution {0};
    int                                                       m_point_light_count{0};
    erhe::math::Viewport                                      m_point_viewport{0, 0, 0, 0};
    std::vector<std::string>                                  m_gpu_timer_labels;
    std::vector<std::unique_ptr<erhe::graphics::Gpu_timer>>   m_gpu_timers;
    erhe::math::Viewport                                      m_viewport{0, 0, 0, 0};
    erhe::scene_renderer::Light_projections                   m_light_projections;
    erhe::scene::Shadow_frustum_fit_settings                  m_fit_settings; // refreshed from editor settings each frame

    // Diagnostics: log the fit view camera / projection / viewport only when
    // they change, so a run reveals which camera the shadow fit is fitted to
    // (the headset drives m_root_camera as perspective_xr from the combined
    // stereo eye frustum) and its fov, without per-frame log spam.
    const void* m_dbg_last_camera{nullptr};
    int         m_dbg_last_viewport_width {-1};
    int         m_dbg_last_viewport_height{-1};
    int         m_dbg_last_projection_type{-1};
};

}
