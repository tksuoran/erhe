#pragma once

#include "tools/tool.hpp"

#include "erhe_commands/command.hpp"
#include "erhe_imgui/imgui_window.hpp"
#include "erhe_geometry/types.hpp"

#include <geogram/mesh/mesh.h>

#include <glm/glm.hpp>

#include <memory>
#include <optional>
#include <vector>

namespace erhe::geometry { class Geometry; }
namespace erhe::imgui    { class Imgui_windows; }
namespace erhe::scene    { class Mesh; }

namespace editor {

class Paint_tool;

class Paint_vertex_command : public erhe::commands::Command
{
public:
    Paint_vertex_command(erhe::commands::Commands& commands, App_context& context);
    void try_ready() override;
    auto try_call () -> bool override;

private:
    App_context& m_context;
};

enum class Paint_mode {
    Point = 0,
    Corner,
    Polygon
};

static constexpr const char* c_paint_mode_strings[] = {
    "Point",
    "Corner",
    "Polygon"
};

class App_message_bus;
class App_scenes;
class Icon_set;
class Mesh_memory;
class Selection_tool;
class Headset_view;

class Paint_tool
    : public erhe::imgui::Imgui_window
    , public Tool
{
public:
    static constexpr int c_priority{4};

    Paint_tool(
        erhe::commands::Commands&    commands,
        erhe::imgui::Imgui_renderer& imgui_renderer,
        erhe::imgui::Imgui_windows&  imgui_windows,
        App_context&                 context,
        App_message_bus&             app_message_bus,
        Headset_view&                headset_view,
        Icon_set&                    icon_set,
        Tools&                       tools
    );

    // Implements Imgui_window
    void imgui() override;

    // Implements Tool
    void handle_priority_update(int old_priority, int new_priority) override;
    void tool_render           (const Render_context& context)      override;
    void tool_properties       (erhe::imgui::Imgui_window&)         override;

    auto try_ready() -> bool;
    void paint();

private:
    static auto vertex_buffer_index_from_scnene_mesh_primitive_corner(
        const erhe::scene::Mesh& scene_mesh,
        std::size_t              scene_mesh_primitive_index,
        GEO::index_t             geo_mesh_corner
    ) -> std::optional<uint32_t>;

    void paint_corner(
        erhe::scene::Mesh& scene_mesh,
        std::size_t        scene_mesh_primitive_index,
        GEO::index_t       corner,
        glm::vec4          color
    );
    void paint_vertex(
        erhe::scene::Mesh& scene_mesh_mesh,
        std::size_t        scene_mesh_primitive_index,
        GEO::index_t       vertex,
        glm::vec4          color
    );

    Paint_vertex_command                m_paint_vertex_command;
    erhe::commands::Redirect_command    m_drag_redirect_update_command;
    erhe::commands::Drag_enable_command m_drag_enable_command;

    Paint_mode              m_paint_mode{Paint_mode::Point};
    size_t                  m_selected_palette_slot{0};

    GEO::index_t            m_vertex;
    GEO::index_t            m_corner;
    std::vector<glm::vec4>  m_ngon_colors;
    bool                    m_edit_palette{false};
    std::vector<glm::vec4>  m_palette;
};

}
