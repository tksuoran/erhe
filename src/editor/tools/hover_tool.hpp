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

class Editor_scenes;
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
    void tool_hover (Scene_view* scene_view) override;
    void tool_render(const Render_context& context) override;

    // Implements Imgui_window
    void imgui() override;

    // Public API
    //[[nodiscard]] auto viewport_window() const -> std::shared_ptr<Viewport_window>;

    // Command
    //void on_inactive  ();
    //void on_scene_view(Scene_view* scene_view);

private:
    //void deselect();
    //void select  ();

    // Component dependencies
    std::shared_ptr<erhe::application::Line_renderer_set> m_line_renderer_set;
    std::shared_ptr<Editor_scenes>                        m_editor_scenes;
    std::shared_ptr<erhe::application::Text_renderer>     m_text_renderer;
    std::shared_ptr<Viewport_windows>                     m_viewport_windows;

    std::array<Hover_entry, Hover_entry::slot_count> m_hover_entries;
    std::optional<glm::dvec3> m_origin;
    std::optional<glm::dvec3> m_direction;

    //std::shared_ptr<erhe::scene::Mesh> m_hover_mesh           {nullptr};
    //std::size_t                        m_hover_primitive_index{0};
    //std::optional<glm::vec3>           m_hover_position_world;
    //std::optional<glm::vec3>           m_hover_normal;
    //bool                               m_hover_content        {false};
    //bool                               m_hover_tool           {false};

    //glm::vec4                                  m_hover_primitive_emissive{0.00f, 0.00f, 0.00f, 0.0f};
    //glm::vec4                                  m_hover_emissive          {0.05f, 0.05f, 0.10f, 0.0f};
    //std::shared_ptr<erhe::primitive::Material> m_original_primitive_material;
    //std::shared_ptr<erhe::primitive::Material> m_hover_material;
    //std::size_t                                m_hover_material_index{0};

    //bool m_enable_color_highlight{false};
};

} // namespace editor
