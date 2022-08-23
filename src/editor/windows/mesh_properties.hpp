#pragma once

#include "tools/tool.hpp"

#include "erhe/application/imgui/imgui_window.hpp"
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

class Editor_scenes;
class Selection_tool;

class Mesh_properties
    : public erhe::components::Component
    , public Tool
    , public erhe::application::Imgui_window
{
public:
    static constexpr std::string_view c_type_name{"Mesh_properties"};
    static constexpr std::string_view c_title{"Mesh"};
    static constexpr uint32_t c_type_hash = compiletime_xxhash::xxh32(c_type_name.data(), c_type_name.size(), {});

    Mesh_properties ();
    ~Mesh_properties() noexcept override;

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return c_type_hash; }
    void declare_required_components() override;
    void initialize_component       () override;
    void post_initialize            () override;

    // Implements Tool
    [[nodiscard]] auto description() -> const char* override { return c_title.data(); }
    void tool_render(const Render_context& context) override;

    // Implements Imgui_window
    void imgui() override;

private:
    // Component dependencies
    std::shared_ptr<Selection_tool>                   m_selection_tool;
    std::shared_ptr<erhe::application::Text_renderer> m_text_renderer;

    int  m_max_labels   {400};
    bool m_show_points  {false};
    bool m_show_polygons{false};
    bool m_show_edges   {false};
};

} // namespace editor
