#pragma once

#include "app_message.hpp"
#include "tools/tool.hpp"

#include "erhe_commands/command.hpp"
#include "erhe_message_bus/message_bus.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace erhe::commands { class Commands; }

namespace editor {

class App_message_bus;
class Point_set_tool;
class Render_context;
class Scene_root;
class Scene_view;
class Tools;
class Viewport_scene_view;

// How a click on a point changes the point selection.
enum class Point_select : unsigned int
{
    replace = 0,
    toggle
};

// How a pointer drag of the hovered point's gizmo ended.
enum class Point_drag_result : unsigned int
{
    moved = 0,
    unmoved // a click
};

// Which points the shared drawing draws as handles.
enum class Point_drawing : unsigned int
{
    all = 0,          // every point, the selected and hovered ones highlighted
    highlighted_only, // only the selected and hovered points (another drawing shows the rest)
    gizmos            // as all, and the pointer brings the transform gizmo to the nearest point, in place of its handle
};

// The editable points of one target, in world space, with a selection and a
// write-back of moved points (doc/editor/point_set_editing.md).
class IPoint_set
{
public:
    virtual ~IPoint_set() noexcept;

    // Re-resolves the target when a change it depends on was reported; true when there is one.
    virtual auto update_point_set() -> bool = 0;
    [[nodiscard]] virtual auto has_point_set_target       () const -> bool = 0;
    [[nodiscard]] virtual auto get_point_set_scene_root   () const -> std::shared_ptr<Scene_root> = 0;
    // Clears and fills one entry per point index; nullopt for an index that is no editable point now (the handle of a corner).
    virtual void get_point_world_positions(std::vector<std::optional<glm::vec3>>& out_positions) const = 0;
    [[nodiscard]] virtual auto is_point_selected          (std::size_t index) const -> bool = 0;
    [[nodiscard]] virtual auto has_point_selection        () const -> bool = 0;
    virtual void select_point                             (std::size_t index, Point_select select) = 0;
    // False when the set keeps its selection (a lattice always has a selected point).
    virtual auto clear_point_selection                    () -> bool = 0;
    // The gizmo's frame orientation: the target's world rotation.
    [[nodiscard]] virtual auto get_point_frame_rotation   () const -> glm::quat = 0;
    [[nodiscard]] virtual auto get_point_drawing          () const -> Point_drawing = 0;

    // The point the pointer brought the gizmo to (Point_drawing::gizmos), re-decided each idle frame by update_hot_point().
    virtual void update_hot_point() = 0;
    [[nodiscard]] virtual auto get_hot_point() const -> std::optional<std::size_t> = 0;
    virtual void clear_hot_point() = 0;
    // A pointer drag of the gizmo, around the gesture below: the press selects the hovered point,
    // an unmoved release is a click on it (doc/editor/point_set_editing.md "Hover gizmo").
    virtual void begin_pointer_drag() = 0;
    virtual void end_pointer_drag  (Point_drag_result result) = 0;

    // One gesture of the selected points: begin snapshots, apply writes the
    // snapshot moved by world_delta (nullopt: no motion, the snapshot exactly),
    // commit records one undoable operation, cancel restores the snapshot.
    virtual auto begin_point_edit () -> bool = 0;
    virtual void apply_point_edit (const std::optional<glm::mat4>& world_delta) = 0;
    virtual void commit_point_edit() = 0;
    virtual void cancel_point_edit() = 0;
};

// Left click in a viewport: picks a point of the tool's set (Point_set_tool::try_ready / on_select).
class Point_set_select_command : public erhe::commands::Command
{
public:
    Point_set_select_command(erhe::commands::Commands& commands, const char* name, Point_set_tool& tool);
    void try_ready() override;
    auto try_call () -> bool override;

private:
    Point_set_tool& m_tool;
};

// Escape during a gizmo gesture of the tool's points: restores the start state.
class Point_set_cancel_command : public erhe::commands::Command
{
public:
    Point_set_cancel_command(erhe::commands::Commands& commands, const char* name, Point_set_tool& tool);
    void try_ready() override;
    auto try_call () -> bool override;

private:
    Point_set_tool& m_tool;
};

// The shared part of a viewport point editing tool: the click pick and the
// selection rules, the cancel key and the handle drawing.
class Point_set_tool : public Tool, public IPoint_set
{
public:
    static constexpr int   c_priority    {4};
    static constexpr float c_pick_radius {12.0f}; // pixels
    static constexpr float c_hover_radius{24.0f}; // pixels: the pointer this near a point brings the gizmo to it
    static constexpr float c_gizmo_bounds_margin{4.0f}; // pixels around the hovered gizmo's handles that still keep it

    Point_set_tool(
        erhe::commands::Commands& commands,
        App_context&              context,
        App_message_bus&          app_message_bus,
        Tools&                    tools,
        const char*               select_command_name,
        const char*               cancel_command_name
    );

    // Implements Tool
    void tool_render(const Render_context& context) override;

    // Implements IPoint_set
    void update_hot_point() override;
    [[nodiscard]] auto get_hot_point() const -> std::optional<std::size_t> override;
    void clear_hot_point() override;
    void begin_pointer_drag() override;
    void end_pointer_drag  (Point_drag_result result) override;

    // The point within the pick radius of pointer nearest to the camera.
    [[nodiscard]] static auto pick_point(
        Viewport_scene_view&                         viewport_scene_view,
        const glm::vec2&                             pointer,
        std::span<const std::optional<glm::vec3>>    positions
    ) -> std::optional<std::size_t>;

    // Commands
    [[nodiscard]] auto try_ready_select() -> bool;
    auto on_select       () -> bool;
    [[nodiscard]] auto is_own_edit_active() const -> bool;
    auto on_cancel_edit  () -> bool;

protected:
    // A click that hits no point: true arms on_select_miss() (the click is consumed).
    [[nodiscard]] virtual auto try_ready_miss(Viewport_scene_view& viewport_scene_view, const glm::vec2& pointer) -> bool;
    virtual auto on_select_miss() -> bool;
    // Drawing of the tool's own beyond the point handles (the lattice cage); hovered is the point under the pointer.
    virtual void render_extras(const Render_context& context, std::optional<std::size_t> hovered);
    [[nodiscard]] auto is_view_of_target_scene(const Scene_view& scene_view) const -> bool;

private:

    erhe::message_bus::Subscription<Hover_scene_view_message> m_hover_scene_view_subscription;
    Point_set_select_command  m_select_command;
    Point_set_cancel_command  m_cancel_command;
    std::optional<std::size_t> m_pick_point; // armed by try_ready_select(), consumed by on_select()
    bool                      m_pick_miss{false};
    std::optional<std::size_t> m_hot_point;

    // The hovered point a pointer drag was pressed on.
    class Press
    {
    public:
        std::size_t point       {0};
        bool        toggle      {false}; // Ctrl or Shift held, the pointer on the point itself
        bool        was_selected{false};
    };
    std::optional<Press> m_press;

    // Scratch of the point positions (cleared per use, capacity kept)
    std::vector<std::optional<glm::vec3>> m_scratch_world;
};

}
