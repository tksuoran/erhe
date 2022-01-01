#pragma once

#include "tools/tool.hpp"
#include "windows/imgui_window.hpp"

#include "erhe/components/component.hpp"

#include <memory>

namespace erhe::geometry
{
    class Geometry;
}

namespace erhe::scene
{
    class Mesh;
}

namespace editor
{

class Scene_builder;
class Scene_root;
class Selection_tool;
class Text_renderer;

class Mesh_properties
    : public erhe::components::Component
    , public Tool
    , public Imgui_window
{
public:
    static constexpr std::string_view c_name {"Mesh_properties"};
    static constexpr std::string_view c_title{"Mesh Debug"};
    static constexpr uint32_t hash = compiletime_xxhash::xxh32(c_name.data(), c_name.size(), {});

    Mesh_properties ();
    ~Mesh_properties() override;

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return hash; }
    void connect             () override;
    void initialize_component() override;

    // Implements Tool
    [[nodiscard]] auto description() -> const char* override { return c_name.data(); }
    void tool_render(const Render_context& context) override;

    // Implements Imgui_window
    void imgui() override;

private:
    Scene_builder*  m_scene_builder {nullptr};
    Scene_root*     m_scene_root    {nullptr};
    Selection_tool* m_selection_tool{nullptr};
    Text_renderer*  m_text_renderer {nullptr};

    int  m_max_labels   {400};
    bool m_show_points  {false};
    bool m_show_polygons{false};
    bool m_show_edges   {false};
};

} // namespace editor
