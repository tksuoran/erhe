#pragma once

#include "tools/channel_paint.hpp"
#include "tools/tool.hpp"
#include "tools/tool_window.hpp"

#include "erhe_commands/command.hpp"
#include "erhe_geometry/types.hpp"
#include "erhe_scene_renderer/shader_key.hpp"
#include "app_message.hpp"
#include "erhe_message_bus/message_bus.hpp"

#include <geogram/mesh/mesh.h>

#include <glm/glm.hpp>

#include <array>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

struct Paint_brush_config;
enum class Paint_brush_target  : unsigned int;
enum class Paint_brush_blend   : unsigned int;
enum class Paint_brush_channel : unsigned int;

namespace erhe                 { class Item_host; }
namespace erhe::geometry       { class Geometry; }
namespace erhe::imgui          { class Imgui_windows; }
namespace erhe::primitive      { class Material; class Primitive; }
namespace erhe::scene          { class Mesh; }
namespace erhe::scene_renderer { class Mesh_memory; }

namespace editor {

class Paint_tool;
class Scene_root;
class Viewport_scene_view;

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

// Shift + wheel: the Brush mode radius.
class Paint_brush_radius_command : public erhe::commands::Command
{
public:
    Paint_brush_radius_command(erhe::commands::Commands& commands, Paint_tool& tool);
    auto try_call_with_input(erhe::commands::Input_arguments& input) -> bool override;

private:
    Paint_tool& m_tool;
};

// Escape: cancels the Brush mode stroke.
class Paint_brush_cancel_command : public erhe::commands::Command
{
public:
    Paint_brush_cancel_command(erhe::commands::Commands& commands, Paint_tool& tool);
    auto try_call() -> bool override;

private:
    Paint_tool& m_tool;
};

enum class Paint_mode {
    Point = 0,
    Corner,
    Polygon,
    Brush
};

static constexpr const char* c_paint_mode_strings[] = {
    "Point",
    "Corner",
    "Polygon",
    "Brush"
};

inline constexpr float       c_paint_brush_min_radius        = 0.01f;
inline constexpr float       c_paint_brush_max_radius        = 100.0f;
inline constexpr float       c_paint_brush_radius_wheel_step = 1.1f;
inline constexpr std::size_t c_paint_brush_circle_segments   = 48;

// Where the channel a Weight stroke paints comes from (B7.4).
enum class Paint_channel_source : unsigned int {
    material = 0, // the material has detail blend on: its weight channel
    tool,         // the material has detail blend off: the tool's channel
    no_mesh       // nothing hovered yet: the tool's channel
};

class Paint_channel_choice
{
public:
    unsigned int                               channel{0};
    Paint_channel_source                       source {Paint_channel_source::no_mesh};
    std::shared_ptr<erhe::primitive::Material> material{};
};

enum class Paint_debug_view : unsigned int {
    off = 0,
    on
};

// How the tool's last Brush stroke ended.
enum class Paint_stroke_outcome : unsigned int {
    none = 0,  // no stroke yet
    ended,     // the release: queued as one operation when it changed a value
    cancelled, // Escape, a priority loss, its mesh leaving the editor
    refused    // begin() refused the primitive
};

[[nodiscard]] auto c_str(Paint_stroke_outcome outcome) -> const char*;

class Paint_last_stroke
{
public:
    Paint_stroke_outcome outcome{Paint_stroke_outcome::none};
    std::string          mesh_name;
    std::size_t          primitive_index{0};
    unsigned int         channel_mask{0};
    std::size_t          changed_vertices{0};
    bool                 recorded{false};
    std::string          refusal;
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
        Paint_brush_config&          config,
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

    // Brush mode (doc/editor/tools.md "Paint tool brush"); MCP set_paint_brush_settings writes through these.
    [[nodiscard]] auto cancel_brush_stroke () -> bool;
    [[nodiscard]] auto adjust_brush_radius (float wheel_steps) -> bool;
    [[nodiscard]] auto get_config          () const -> const Paint_brush_config&;
    [[nodiscard]] auto get_paint_mode      () const -> Paint_mode;
    void               set_paint_mode      (Paint_mode mode);
    void               set_brush_target    (Paint_brush_target target);
    void               set_brush_radius    (float radius);
    void               set_brush_strength  (float strength);
    void               set_brush_value     (float value);
    void               set_brush_blend     (Paint_brush_blend blend);
    void               set_brush_channel   (Paint_brush_channel channel);
    [[nodiscard]] auto is_brush_stroke_active() const -> bool;
    [[nodiscard]] auto get_brush_changed_vertex_count() const -> std::size_t;
    [[nodiscard]] auto get_brush_center    () const -> const std::optional<glm::vec3>&;
    [[nodiscard]] auto get_last_stroke     () const -> const Paint_last_stroke&;
    // The channel a Weight stroke paints on the primitive (B7.4); a null mesh gives the tool's channel.
    [[nodiscard]] auto get_weight_channel  (const erhe::scene::Mesh* mesh, std::size_t primitive_index) const -> Paint_channel_choice;
    // The mesh primitive the pointer was last over in a viewport, else null.
    [[nodiscard]] auto get_last_hovered_mesh(std::size_t& out_primitive_index) const -> std::shared_ptr<erhe::scene::Mesh>;
    // The viewport the Detail Weight switch set to Shader_debug::detail_blend_weight, while it still shows it.
    [[nodiscard]] auto get_debug_view      () const -> std::shared_ptr<Viewport_scene_view>;

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
    void brush_properties();
    void on_hover_mesh      ();
    void on_close_scene     (const erhe::Item_host* closing_host);
    void on_items_removed   (const Removed_items& removed);
    void update_brush       ();
    void update_brush_circles();
    void paint_brush        ();
    void dab_at_hover       ();
    void finish_brush_stroke(Paint_stroke_outcome outcome);
    void fill_selection     (Channel_fill_mode mode);
    // On: the last hovered viewport shows the detail weight; off: it gets back the mode it had.
    void set_debug_view     (Paint_debug_view state);
    void restore_debug_view ();
    void touch_settings     ();

    // The press state of a Brush drag: a refused or cancelled press paints nothing until the release.
    enum class Brush_press : unsigned int {
        idle = 0,
        painting,
        stopped
    };

    class Render_vertex
    {
    public:
        GEO::index_t vertex;
        glm::vec3    position;
    };

    Paint_brush_config&                 m_config;
    Tool_window                         m_window;
    erhe::message_bus::Subscription<Hover_scene_view_message> m_hover_scene_view_subscription;
    erhe::message_bus::Subscription<Hover_mesh_message>       m_hover_mesh_subscription;
    erhe::message_bus::Subscription<Close_scene_message>      m_close_scene_subscription;
    erhe::message_bus::Subscription<Items_removed_message>    m_items_removed_subscription;
    Paint_vertex_command                m_paint_vertex_command;
    erhe::commands::Redirect_command    m_drag_redirect_update_command;
    erhe::commands::Drag_enable_command m_drag_enable_command;
    Paint_brush_radius_command          m_brush_radius_command;
    Paint_brush_cancel_command          m_brush_cancel_command;

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
    std::vector<Render_vertex> m_render_vertices; // tool_render() scratch

    // Brush mode
    Channel_paint_stroke                m_channel_stroke;
    Brush_press                         m_brush_press{Brush_press::idle};
    Paint_last_stroke                   m_last_stroke;
    std::optional<glm::vec3>            m_brush_center;
    std::weak_ptr<Scene_root>           m_brush_scene;
    std::array<glm::vec3, 3 * c_paint_brush_circle_segments> m_brush_circle_points{};
    std::weak_ptr<erhe::scene::Mesh>    m_last_hovered_mesh;
    std::size_t                         m_last_hovered_primitive_index{0};
    std::weak_ptr<Viewport_scene_view>  m_debug_view;
    erhe::scene_renderer::Shader_debug  m_debug_view_previous{};
    std::string                         m_debug_view_message;
    std::string                         m_fill_message;
};

}
