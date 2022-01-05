#pragma once

#include "command.hpp"
#include "tools/tool.hpp"
#include "windows/imgui_window.hpp"

#include "erhe/components/component.hpp"

#include <glm/glm.hpp>

#include <memory>
#include <optional>

namespace erhe::scene {
    class Mesh;
}

namespace erhe::primitive {
    class Material;
}

namespace editor
{

class Hover_tool;
class Line_renderer;
class Pointer_context;
class Scene_root;
class Text_renderer;

class Hover_tool_hover_command
    : public Command
{
public:
    explicit Hover_tool_hover_command(Hover_tool& hover_tool)
        : Command     {"Hover_tool.hover"}
        , m_hover_tool{hover_tool}
    {
    }

    auto try_call   (Command_context& context) -> bool override;
    void on_inactive(Command_context& context) override;

private:
    Hover_tool& m_hover_tool;
};

class Hover_tool
    : public erhe::components::Component
    , public Tool
{
public:
    static constexpr std::string_view c_name       {"Hover_tool"};
    static constexpr std::string_view c_description{"Hover tool"};
    static constexpr uint32_t hash{
        compiletime_xxhash::xxh32(
            c_name.data(),
            c_name.size(),
            {}
        )
    };

    Hover_tool ();
    ~Hover_tool() override;

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return hash; }
    void connect             () override;
    void initialize_component() override;

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
    std::shared_ptr<Line_renderer>     m_line_renderer;
    std::shared_ptr<Pointer_context>   m_pointer_context;
    std::shared_ptr<Scene_root>        m_scene_root;
    std::shared_ptr<Text_renderer>     m_text_renderer;

    std::shared_ptr<erhe::scene::Mesh> m_hover_mesh           {nullptr};
    size_t                             m_hover_primitive_index{0};
    std::optional<glm::vec3>           m_hover_position_world;
    std::optional<glm::vec3>           m_hover_normal;
    bool                               m_hover_content        {false};
    bool                               m_hover_tool           {false};

    glm::vec4                                  m_hover_primitive_emissive{0.00f, 0.00f, 0.00f, 0.0f};
    glm::vec4                                  m_hover_emissive          {0.05f, 0.05f, 0.10f, 0.0f};
    std::shared_ptr<erhe::primitive::Material> m_original_primitive_material;
    std::shared_ptr<erhe::primitive::Material> m_hover_material;
    size_t                                     m_hover_material_index{0};

    bool m_enable_color_highlight{false};
};

} // namespace editor
