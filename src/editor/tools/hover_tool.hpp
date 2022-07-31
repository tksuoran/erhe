#pragma once

#include "tools/tool.hpp"

#include "erhe/application/commands/command.hpp"
#include "erhe/application/windows/imgui_window.hpp"
#include "erhe/components/components.hpp"
#include "erhe/toolkit/optional.hpp"

#include <glm/glm.hpp>

#include <memory>

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

class Editor_scenes;
class Hover_tool;
class Viewport_windows;

class Hover_tool_hover_command
    : public erhe::application::Command
{
public:
    explicit Hover_tool_hover_command(Hover_tool& hover_tool)
        : Command     {"Hover_tool.hover"}
        , m_hover_tool{hover_tool}
    {
    }

    auto try_call   (erhe::application::Command_context& context) -> bool override;
    void on_inactive(erhe::application::Command_context& context) override;

private:
    Hover_tool& m_hover_tool;
};

class Hover_tool
    : public erhe::components::Component
    , public Tool
{
public:
    static constexpr std::string_view c_label{"Hover_tool"};
    static constexpr std::string_view c_title{"Hover tool"};
    static constexpr uint32_t hash{
        compiletime_xxhash::xxh32(
            c_label.data(),
            c_label.size(),
            {}
        )
    };

    Hover_tool ();
    ~Hover_tool() noexcept override;

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return hash; }
    void declare_required_components() override;
    void initialize_component       () override;
    void post_initialize            () override;

    // Implements Tool
    [[nodiscard]] auto description() -> const char* override;
    void tool_render(const Render_context& context) override;

    // Public API

    // Command
    void on_inactive();
    auto try_call() -> bool;

private:
    void deselect();
    void select  ();

    // Component dependencies
    std::shared_ptr<erhe::application::Line_renderer_set> m_line_renderer_set;
    std::shared_ptr<Editor_scenes>                        m_editor_scenes;
    std::shared_ptr<erhe::application::Text_renderer>     m_text_renderer;
    std::shared_ptr<Viewport_windows>                     m_viewport_windows;

    Hover_tool_hover_command           m_hover_command;

    std::shared_ptr<erhe::scene::Mesh> m_hover_mesh           {nullptr};
    std::size_t                        m_hover_primitive_index{0};
    nonstd::optional<glm::vec3>        m_hover_position_world;
    nonstd::optional<glm::vec3>        m_hover_normal;
    bool                               m_hover_content        {false};
    bool                               m_hover_tool           {false};

    glm::vec4                                  m_hover_primitive_emissive{0.00f, 0.00f, 0.00f, 0.0f};
    glm::vec4                                  m_hover_emissive          {0.05f, 0.05f, 0.10f, 0.0f};
    std::shared_ptr<erhe::primitive::Material> m_original_primitive_material;
    std::shared_ptr<erhe::primitive::Material> m_hover_material;
    std::size_t                                m_hover_material_index{0};

    bool m_enable_color_highlight{false};
};

} // namespace editor
