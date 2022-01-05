#pragma once

#include "windows/imgui_window.hpp"

#include "erhe/components/component.hpp"
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

class Id_renderer;
class Programs;
class Shadow_renderer;

class Debug_view_window
    : public erhe::components::Component
    , public Imgui_window
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

    // Implements Imgui_window
    void imgui() override;

    void render            ();
    void update_framebuffer();

private:
    // Component dependencies
    std::shared_ptr<Id_renderer>                          m_id_renderer;
    std::shared_ptr<Shadow_renderer>                      m_shadow_renderer;
    std::shared_ptr<erhe::graphics::OpenGL_state_tracker> m_pipeline_state_tracker;
    std::shared_ptr<Programs>                             m_programs;

    erhe::scene::Viewport                               m_viewport       {0, 0, 0, 0, true};
    erhe::graphics::Vertex_attribute_mappings           m_empty_attribute_mappings;
    erhe::graphics::Vertex_format                       m_empty_vertex_format;
    std::unique_ptr<erhe::graphics::Vertex_input_state> m_empty_vertex_input;
    erhe::graphics::Pipeline                            m_pipeline;
    std::unique_ptr<erhe::graphics::Texture>            m_texture;
    std::unique_ptr<erhe::graphics::Framebuffer>        m_framebuffer;
};

} // namespace editor
