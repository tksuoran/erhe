#pragma once

#include "renderers/base_renderer.hpp"
#include "erhe/graphics/pipeline.hpp"
#include "erhe/scene/viewport.hpp"
#include "erhe/components/component.hpp"

namespace erhe::graphics
{
    class Framebuffer;
    class OpenGL_state_tracker;
    class Texture;
    class Vertex_input_state;
}

namespace erhe::scene
{
    class Camera;
    class Layer;
    class Light;
    class Mesh;
}

namespace editor
{

class Mesh_memory;

class Shadow_renderer
    : public erhe::components::Component,
      public Base_renderer
{
public:
    static constexpr std::string_view c_name{"Shadow_renderer"};
    Shadow_renderer ();
    ~Shadow_renderer() override;

    // Implements Component
    void connect             () override;
    void initialize_component() override;

    void render  (Layer_collection& layers, const erhe::scene::ICamera& camera);
    auto texture () const -> erhe::graphics::Texture*;
    auto viewport() const -> erhe::scene::Viewport;

    static constexpr size_t s_max_light_count    = 8;
    static constexpr size_t s_texture_resolution = 4 * 1024;
    static constexpr bool   s_enable             = true;

private:
    erhe::scene::Viewport                                     m_viewport;
    std::shared_ptr<erhe::graphics::OpenGL_state_tracker>     m_pipeline_state_tracker;
    std::shared_ptr<Mesh_memory>                              m_mesh_memory;
    erhe::graphics::Pipeline                                  m_pipeline;
    std::unique_ptr<erhe::graphics::Vertex_input_state>       m_vertex_input;
    std::unique_ptr<erhe::graphics::Texture>                  m_texture;
    std::vector<std::unique_ptr<erhe::graphics::Framebuffer>> m_framebuffers;
};

} // namespace editor
