#pragma once

#include "windows/framebuffer_window.hpp"
#include "renderers/renderpass.hpp"

#include "erhe/components/components.hpp"
#include "erhe/graphics/pipeline.hpp"
#include "erhe/graphics/vertex_format.hpp"
#include "erhe/graphics/vertex_attribute_mappings.hpp"
#include "erhe/scene/viewport.hpp"

#include <memory>

namespace erhe::graphics
{
    class Framebuffer;
    class OpenGL_state_tracker;
    class Texture;
}

namespace editor
{

class Forward_renderer;
class Programs;
class Scene_root;
class Shadow_renderer;

class Debug_view_window
    : public erhe::components::Component
    , public Framebuffer_window
{
public:
    static constexpr std::string_view c_name {"Debug_view_window"};
    static constexpr std::string_view c_title{"Debug View"};
    static constexpr uint32_t hash{
        compiletime_xxhash::xxh32(
            c_name.data(),
            c_name.size(),
            {}
        )
    };

    Debug_view_window();
    ~Debug_view_window() override;

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return hash; }
    void connect             () override;
    void initialize_component() override;

    // Implements Framebuffer_window
    auto get_size() const -> glm::vec2 override;

    // Public API
    void render();

private:
    // Component dependencies
    std::shared_ptr<Forward_renderer>                     m_forward_renderer;
    std::shared_ptr<erhe::graphics::OpenGL_state_tracker> m_pipeline_state_tracker;
    std::shared_ptr<Programs>                             m_programs;
    std::shared_ptr<Scene_root>                           m_scene_root;
    std::shared_ptr<Shadow_renderer>                      m_shadow_renderer;

    std::unique_ptr<erhe::graphics::Vertex_input_state>   m_empty_vertex_input;
    Renderpass                                            m_renderpass;

};

} // namespace editor
