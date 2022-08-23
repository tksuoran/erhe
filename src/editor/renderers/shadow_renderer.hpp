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

namespace erhe::application
{
    class Configuration;
    class Rendergraph;
}

namespace erhe::graphics
{
    class Framebuffer;
    class Gpu_timer;
    class OpenGL_state_tracker;
    class Texture;
    class Vertex_input_state;
}

namespace erhe::scene
{
    class Camera;
    class Light;
    class Mesh;
    class Mesh_layer;
}

namespace editor
{

class Mesh_memory;
class Scene_root;
class Shadow_renderer;
class Viewport_window;

/// <summary>
/// Helper rendergraph node calling Shadow_renderer
/// </summary>
/// TODO Think about Shadow_render_node / Shadow_renderer relationship,
///      should Shadow_render_node have more state?
class Shadow_render_node
    : public erhe::application::Rendergraph_node
{
public:
    static constexpr std::string_view c_type_name{"Shadow_render_node"};
    static constexpr uint32_t c_type_hash = compiletime_xxhash::xxh32(c_type_name.data(), c_type_name.size(), {});

    Shadow_render_node(
        Shadow_renderer&                        shadow_renderer,
        const std::shared_ptr<Viewport_window>& viewport_window,
        int                                     resolution,
        int                                     light_count,
        bool                                    reverse_depth
    );

    // Implements Rendergraph_node
    [[nodiscard]] auto type_name() const -> std::string_view override { return c_type_name; }
    [[nodiscard]] auto type_hash() const -> uint32_t         override { return c_type_hash; }

    void execute_rendergraph_node() override;

    [[nodiscard]] auto get_producer_output_texture(
        erhe::application::Resource_routing resource_routing,
        int                                 key,
        int                                 depth = 0
    ) const -> std::shared_ptr<erhe::graphics::Texture> override;

    [[nodiscard]] auto get_producer_output_viewport(
        erhe::application::Resource_routing resource_routing,
        int                                 key,
        int                                 depth = 0
    ) const -> erhe::scene::Viewport override;

    [[nodiscard]] auto inputs_allowed() const -> bool override;

    // Public API
    auto viewport_window  () const -> std::shared_ptr<Viewport_window>;
    auto light_projections() -> Light_projections&;
    [[nodiscard]] auto texture () const -> std::shared_ptr<erhe::graphics::Texture>;
    [[nodiscard]] auto viewport() const -> erhe::scene::Viewport;

private:
    Shadow_renderer&                 m_shadow_renderer;
    std::shared_ptr<Viewport_window> m_viewport_window;

    std::shared_ptr<erhe::graphics::Texture>                  m_texture;
    std::vector<std::unique_ptr<erhe::graphics::Framebuffer>> m_framebuffers;
    erhe::scene::Viewport                                     m_viewport{0, 0, 0, 0, true};

    Scene_root*       m_shadow_scene{nullptr};
    Light_projections m_light_projections;
};

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
    void post_initialize            () override;

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
        const std::shared_ptr<Viewport_window>& viewport_window
    ) -> std::shared_ptr<Shadow_render_node>;
    auto get_node_for_viewport(
        const Viewport_window* viewport_window
    ) -> std::shared_ptr<Shadow_render_node>;
    auto get_nodes() const -> const std::vector<std::shared_ptr<Shadow_render_node>>&;

    auto render    (const Render_parameters& parameters) -> bool;
    void next_frame();

private:
    // Component dependencies
    std::shared_ptr<erhe::application::Configuration>     m_configuration;
    std::shared_ptr<erhe::graphics::OpenGL_state_tracker> m_pipeline_state_tracker;
    std::shared_ptr<erhe::application::Rendergraph>       m_render_graph;
    std::shared_ptr<Mesh_memory>                          m_mesh_memory;

    erhe::graphics::Pipeline                              m_pipeline;
    std::unique_ptr<erhe::graphics::Vertex_input_state>   m_vertex_input;
    std::unique_ptr<erhe::graphics::Gpu_timer>            m_gpu_timer;

    std::vector<std::shared_ptr<Shadow_render_node>> m_nodes;

    std::unique_ptr<Light_buffer        > m_light_buffers;
    std::unique_ptr<Draw_indirect_buffer> m_draw_indirect_buffers;
    std::unique_ptr<Primitive_buffer    > m_primitive_buffers;
};

} // namespace editor
