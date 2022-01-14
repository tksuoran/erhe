#pragma once

#include "erhe/components/component.hpp"
#include "erhe/graphics/pipeline.hpp"
#include "erhe/graphics/vertex_format.hpp"
#include "erhe/graphics/vertex_attribute_mappings.hpp"
#include "erhe/scene/viewport.hpp"

#include <imgui.h>

#include <gsl/gsl>

namespace erhe::graphics {
    class Framebuffer;
    class OpenGL_state_tracker;
    class Texture;
}

namespace erhe::scene {
    class Mesh;
}

namespace editor {

class Editor_rendering;
class Imgui_window;

class Editor_imgui_windows
    : public erhe::components::Component
{
public:
    static constexpr std::string_view c_name       {"Editor_imgui_windows"};
    static constexpr std::string_view c_description{"Editor ImGui Windows"};
    static constexpr uint32_t         hash{
        compiletime_xxhash::xxh32(
            c_name.data(),
            c_name.size(),
            {}
        )
    };

    Editor_imgui_windows ();
    ~Editor_imgui_windows() override;

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return hash; }
    void connect             () override;
    void initialize_component() override;

    // Public API
    [[nodiscard]] auto texture() const -> std::shared_ptr<erhe::graphics::Texture>;
    void imgui_windows             ();
    void register_imgui_window     (Imgui_window* window);
    void menu                      ();
    void begin_imgui_frame         ();
    void end_and_render_imgui_frame();

private:
    void window_menu();
    void add_scene_node();

    // Component dependencies
    std::shared_ptr<Editor_rendering>                     m_editor_rendering;
    std::shared_ptr<erhe::graphics::OpenGL_state_tracker> m_pipeline_state_tracker;
    std::shared_ptr<erhe::scene::Mesh>                    m_gui_mesh;
    std::shared_ptr<erhe::graphics::Texture>              m_texture;
    std::unique_ptr<erhe::graphics::Framebuffer>          m_framebuffer;

    std::mutex                                m_mutex;
    std::vector<gsl::not_null<Imgui_window*>> m_imgui_windows;
    ImGuiContext*                             m_imgui_context{nullptr};
    ImVector<ImWchar>                         m_glyph_ranges;
    bool                                      m_show_tool_properties{true};
    bool                                      m_show_style_editor   {false};
};

}
