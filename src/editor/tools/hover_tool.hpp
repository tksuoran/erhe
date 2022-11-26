#pragma once

#include "tools/tool.hpp"
#include "scene/scene_view.hpp"

#include "erhe/application/imgui/imgui_window.hpp"
#include "erhe/components/components.hpp"

#include <glm/glm.hpp>

#include <memory>
#include <optional>

namespace erhe::scene
{
    class Mesh;
}

namespace erhe::primitive
{
    class Material;
}

namespace erhe::application
{
    class Line_renderer_set;
    class Text_renderer;
}

namespace editor
{

class Editor_message;
class Hover_tool;
class Scene_view;
class Viewport_window;
class Viewport_windows;

class Hover_tool
    : public erhe::application::Imgui_window
    , public erhe::components::Component
    , public Tool
{
public:
    static constexpr std::string_view c_type_name{"Hover_tool"};
    static constexpr std::string_view c_title{"Hover tool"};
    static constexpr uint32_t c_type_hash{
        compiletime_xxhash::xxh32(
            c_type_name.data(),
            c_type_name.size(),
            {}
        )
    };

    Hover_tool ();
    ~Hover_tool() noexcept override;

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return c_type_hash; }
    void declare_required_components() override;
    void initialize_component       () override;
    void post_initialize            () override;

    // Implements Tool
    [[nodiscard]] auto description() -> const char* override;
    void tool_render(const Render_context& context) override;

    // Implements Imgui_window
    void imgui() override;

private:
    void on_message(Editor_message& message);
    void tool_hover(Scene_view* scene_view);

    // Component dependencies
    std::shared_ptr<erhe::application::Line_renderer_set> m_line_renderer_set;
    std::shared_ptr<erhe::application::Text_renderer>     m_text_renderer;
    std::shared_ptr<Viewport_windows>                     m_viewport_windows;

    Scene_view* m_scene_view                {nullptr};
    bool        m_show_snapped_grid_position{false};
};

} // namespace editor
