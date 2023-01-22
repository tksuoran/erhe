#pragma once

#include "renderers/light_buffer.hpp"
#include "renderers/draw_indirect_buffer.hpp"
#include "renderers/primitive_buffer.hpp"

#include "erhe/application/rendergraph/rendergraph_node.hpp"
#include "erhe/components/components.hpp"
#include "erhe/graphics/pipeline.hpp"
#include "erhe/scene/viewport.hpp"

#include <gsl/gsl>

#include <initializer_list>

namespace erhe::graphics
{
    class Framebuffer;
    class Gpu_timer;
    class Texture;
    class Vertex_input_state;
}

namespace erhe::scene
{
    class Camera;
    class Light;
    class Mesh;
}

namespace editor
{

class Editor_message;
class Scene_root;
class Scene_view;
class Shadow_render_node;

class Shadow_renderer
    : public erhe::components::Component
{
public:
    static constexpr std::string_view c_type_name{"Shadow_renderer"};
    static constexpr uint32_t c_type_hash = compiletime_xxhash::xxh32(c_type_name.data(), c_type_name.size(), {});

    Shadow_renderer ();
    ~Shadow_renderer() noexcept override;

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return c_type_hash; }
    void declare_required_components() override;
    void initialize_component       () override;
    void deinitialize_component     () override;

    // Public API
    class Render_parameters
    {
    public:
        Scene_root*                                                scene_root;
        const erhe::scene::Camera*                                 view_camera;
        const erhe::scene::Viewport                                view_camera_viewport;
        const erhe::scene::Viewport                                light_camera_viewport;
        erhe::graphics::Texture&                                   texture;
        const std::vector<
            std::unique_ptr<erhe::graphics::Framebuffer>
        >&                                                         framebuffers;
        const std::initializer_list<
            const gsl::span<
                const std::shared_ptr<erhe::scene::Mesh>
            >
        >&                                                         mesh_spans;
        const gsl::span<const std::shared_ptr<erhe::scene::Light>> lights;
        Light_projections&                                         light_projections;
    };

    auto create_node_for_viewport(
        const std::shared_ptr<Scene_view>& scene_view
    ) -> std::shared_ptr<Shadow_render_node>;
    auto get_node_for_view(
        const Scene_view* scene_view
    ) -> std::shared_ptr<Shadow_render_node>;
    auto get_nodes() const -> const std::vector<std::shared_ptr<Shadow_render_node>>&;

    auto render    (const Render_parameters& parameters) -> bool;
    void next_frame();

private:
    void on_message                      (Editor_message& message);
    void handle_graphics_settings_changed();

    erhe::graphics::Pipeline                            m_pipeline;
    std::unique_ptr<erhe::graphics::Vertex_input_state> m_vertex_input;
    std::unique_ptr<erhe::graphics::Gpu_timer>          m_gpu_timer;
    std::vector<std::shared_ptr<Shadow_render_node>>    m_nodes;
    std::unique_ptr<Light_buffer        >               m_light_buffers;
    std::unique_ptr<Draw_indirect_buffer>               m_draw_indirect_buffers;
    std::unique_ptr<Primitive_buffer    >               m_primitive_buffers;
};

extern Shadow_renderer* g_shadow_renderer;

} // namespace editor
