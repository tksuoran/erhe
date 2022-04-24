#pragma once

#include "tools/tool.hpp"

#include "erhe/application/windows/imgui_window.hpp"
#include "erhe/components/components.hpp"

#include <memory>

namespace erhe::geometry
{
    class Geometry;
}

namespace erhe::scene
{
    class Mesh;
}

namespace erhe::application
{
    class Text_renderer;
}

namespace editor
{

class Scene_root;
class Selection_tool;

class Mesh_properties
    : public erhe::components::Component
    , public Tool
    , public erhe::application::Imgui_window
{
public:
    static constexpr std::string_view c_label{"Mesh_properties"};
    static constexpr std::string_view c_title{"Mesh"};
    static constexpr uint32_t hash = compiletime_xxhash::xxh32(c_label.data(), c_label.size(), {});

    Mesh_properties ();
    ~Mesh_properties() noexcept override;

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return hash; }
    void connect             () override;
    void initialize_component() override;

    // Implements Tool
    [[nodiscard]] auto description() -> const char* override { return c_title.data(); }
    void tool_render(const Render_context& context) override;

    // Implements Imgui_window
    void imgui() override;

private:
    // Component dependencies
    std::shared_ptr<Scene_root>                       m_scene_root;
    std::shared_ptr<Selection_tool>                   m_selection_tool;
    std::shared_ptr<erhe::application::Text_renderer> m_text_renderer;

    int  m_max_labels   {400};
    bool m_show_points  {false};
    bool m_show_polygons{false};
    bool m_show_edges   {false};
};

} // namespace editor
