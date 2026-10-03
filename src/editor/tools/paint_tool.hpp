#pragma once

#include "tools/tool.hpp"
#include "tools/tool_window.hpp"

#include "erhe_commands/command.hpp"
#include "erhe_geometry/types.hpp"
#include "app_message.hpp"
#include "erhe_message_bus/message_bus.hpp"

#include <geogram/mesh/mesh.h>

#include <glm/glm.hpp>

#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

namespace erhe::geometry       { class Geometry; }
namespace erhe::imgui          { class Imgui_windows; }
namespace erhe::primitive      { class Primitive; }
namespace erhe::scene          { class Mesh; }
namespace erhe::scene_renderer { class Mesh_memory; }

namespace editor {

class Paint_tool;

class Paint_vertex_command : public erhe::commands::Command
{
public:
    Paint_vertex_command(erhe::commands::Commands& commands, App_context& context);
    void try_ready  () override;
    auto try_call   () -> bool override;
    void on_inactive() override;

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
class Selection_tool;
class Headset_view;

class Paint_tool : public Tool
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

    // Implements Tool
    void handle_priority_update(int old_priority, int new_priority) override;
    void tool_render           (const Render_context& context)      override;
    void tool_properties       (erhe::imgui::Imgui_window&)         override;

    auto try_ready() -> bool;
    void paint();
    // Stroke end (Paint_vertex_command deactivation): releases every
    // optimization hold the stroke took. See m_stroke_holds.
    void end_stroke();

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

    void window_imgui();

    Tool_window                         m_window;
    erhe::message_bus::Subscription<Hover_scene_view_message> m_hover_scene_view_subscription;
    Paint_vertex_command                m_paint_vertex_command;
    erhe::commands::Redirect_command    m_drag_redirect_update_command;
    erhe::commands::Drag_enable_command m_drag_enable_command;

    // The Primitives the active stroke has bracketed with an optimization
    // hold (requirement 11: invalidate at edit start, block re-optimization
    // until edit end). Taken lazily in paint_vertex() - a hover-driven stroke
    // can cross meshes - and released via release_optimization_hold() on
    // exactly these objects by end_stroke(). Keyed on the Primitive itself so
    // a mid-stroke primitive swap at the same (mesh, index) gets its own
    // hold and the old object still gets its release.
    std::vector<std::shared_ptr<erhe::primitive::Primitive>> m_stroke_holds;

    // Durable-paint accumulation, one record per Geometry the stroke touched:
    // paint_corner() writes the color into the geometry's corner_color_0
    // attribute live (mirroring Weight_paint_tool) and records each corner's
    // before/after values here; end_stroke() turns every record into one
    // undoable Paint_colors_operation, whose primitive rebuild also brings
    // the optimized variant back. Geometry-less primitives (triangle-soup
    // builds) get the GPU preview only, as before.
    class Stroke_paint
    {
    public:
        std::weak_ptr<erhe::scene::Mesh>              mesh{};
        std::size_t                                   primitive_index{0};
        std::shared_ptr<erhe::geometry::Geometry>     geometry{};
        std::vector<GEO::index_t>                     corners{};
        std::vector<glm::vec4>                        before_colors{}; // parallel to corners
        std::vector<glm::vec4>                        after_colors{};  // parallel to corners
        std::unordered_map<GEO::index_t, std::size_t> corner_to_slot{};
    };
    std::vector<Stroke_paint> m_stroke_paints;

    Paint_mode              m_paint_mode{Paint_mode::Point};
    size_t                  m_selected_palette_slot{0};

    GEO::index_t            m_vertex;
    GEO::index_t            m_corner;
    std::vector<glm::vec4>  m_ngon_colors;
    bool                    m_edit_palette{false};
    std::vector<glm::vec4>  m_palette;
};

}
