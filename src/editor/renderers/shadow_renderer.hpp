#pragma once

#include "renderers/base_renderer.hpp"
#include "erhe/graphics/pipeline.hpp"
#include "erhe/scene/viewport.hpp"
#include "erhe/components/component.hpp"

namespace erhe::graphics
{
    class Framebuffer;
    class OpenGL_state_tracker;
    class Vertex_input_state;
    class Texture;
}

namespace erhe::scene
{
    class Layer;
    class Camera;
    class Light;
    class Mesh;
}

namespace editor
{

class Programs;
class Scene_manager;

class Shadow_renderer
    : public erhe::components::Component,
      public Base_renderer
{
public:
    Shadow_renderer();

    virtual ~Shadow_renderer() = default;

    void connect(std::shared_ptr<erhe::graphics::OpenGL_state_tracker> pipeline_state_tracker,
                 std::shared_ptr<Scene_manager>                        scene_manager,
                 std::shared_ptr<Programs>                             programs);

    void initialize_component() override;

    void render(Layer_collection&           layers,
                const erhe::scene::ICamera& camera);

    auto texture() const -> erhe::graphics::Texture*
    {
        return m_texture.get();
    }

    auto viewport() const -> erhe::scene::Viewport
    {
        return m_viewport;
    }

    static constexpr size_t s_max_light_count    = 8;
    static constexpr size_t s_texture_resolution = 4 * 1024;
    static constexpr bool   s_enable             = true;

private:
    erhe::scene::Viewport                                     m_viewport;
    std::shared_ptr<erhe::graphics::OpenGL_state_tracker>     m_pipeline_state_tracker;
    std::shared_ptr<Scene_manager>                            m_scene_manager;
    erhe::graphics::Pipeline                                  m_pipeline;
    std::unique_ptr<erhe::graphics::Vertex_input_state>       m_vertex_input;
    std::unique_ptr<erhe::graphics::Texture>                  m_texture;
    std::vector<std::unique_ptr<erhe::graphics::Framebuffer>> m_framebuffers;
};

} // namespace editor
