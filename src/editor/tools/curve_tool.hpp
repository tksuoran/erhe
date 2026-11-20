#pragma once

#include "app_message.hpp"
#include "tools/point_set_tool.hpp"

#include "erhe_commands/command.hpp"
#include "erhe_math/curve_path.hpp"
#include "erhe_message_bus/message_bus.hpp"

#include <glm/glm.hpp>

#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace erhe::commands { class Commands; }
namespace erhe::scene    { class Curve; }

namespace editor {

class App_message_bus;
class Curve_set_operation;
class Curve_tool;
class Scene_root;
class Tools;

// The editable parts of one control point: the point itself and its two handle ends.
enum class Curve_point_part : unsigned int
{
    position = 0,
    in_handle,
    out_handle
};

[[nodiscard]] auto c_str(Curve_point_part part) -> const char*;
[[nodiscard]] auto curve_point_part_from_string(std::string_view text, Curve_point_part& out_part) -> bool;

class Curve_point_ref
{
public:
    std::size_t      point{0};
    Curve_point_part part {Curve_point_part::position};
};

// The point edits (doc/editor/curves.md "Viewport editing"), each one Curve_set_operation, or
// null with error set when refused. The caller executes or queues the operation.
[[nodiscard]] auto make_curve_add_point_operation(
    const std::shared_ptr<erhe::scene::Curve>&    curve,
    erhe::math::Curve_end                         end,
    const std::optional<erhe::math::Curve_point>& point, // nullopt: erhe::math::make_curve_end_point()
    std::size_t&                                  out_index,
    std::string&                                  out_error
) -> std::shared_ptr<Curve_set_operation>;
[[nodiscard]] auto make_curve_insert_point_operation(
    const std::shared_ptr<erhe::scene::Curve>& curve,
    const erhe::math::Curve_location&          location,
    std::size_t&                               out_index,
    std::string&                               out_error
) -> std::shared_ptr<Curve_set_operation>;
[[nodiscard]] auto make_curve_remove_points_operation(
    const std::shared_ptr<erhe::scene::Curve>& curve,
    std::span<const std::size_t>               indices,
    std::string&                               out_error
) -> std::shared_ptr<Curve_set_operation>;
// Null without error when no point changes.
[[nodiscard]] auto make_curve_set_points_kind_operation(
    const std::shared_ptr<erhe::scene::Curve>& curve,
    std::span<const std::size_t>               indices,
    erhe::math::Curve_point_kind               kind
) -> std::shared_ptr<Curve_set_operation>;
[[nodiscard]] auto make_curve_toggle_closed_operation(const std::shared_ptr<erhe::scene::Curve>& curve) -> std::shared_ptr<Curve_set_operation>;

enum class Curve_tool_action : unsigned int
{
    remove_points = 0,
    add_point_at_start,
    add_point_at_end,
    set_kind_corner,
    set_kind_smooth,
    set_kind_free,
    toggle_closed
};

class Curve_tool_command : public erhe::commands::Command
{
public:
    Curve_tool_command(erhe::commands::Commands& commands, const char* name, Curve_tool& tool, Curve_tool_action action);
    void try_ready() override;
    auto try_call () -> bool override;

private:
    Curve_tool&       m_tool;
    Curve_tool_action m_action;
};

// Viewport editing of the selected curve prim's control points and handles
// (doc/editor/curves.md "Viewport editing"): a background point set tool whose
// target is the curve when it is the only selected item. The point set holds
// three entries per control point (Curve_point_part); the handles of a corner
// are not editable.
class Curve_tool : public Point_set_tool
{
public:
    Curve_tool(
        erhe::commands::Commands& commands,
        App_context&              context,
        App_message_bus&          app_message_bus,
        Tools&                    tools
    );

    [[nodiscard]] auto get_target_curve() const -> std::shared_ptr<erhe::scene::Curve>;
    // False for an index that is no editable point (a corner's handle).
    [[nodiscard]] auto get_point_world_position(std::size_t index, glm::vec3& out_position) const -> bool;
    // Segment handles (doc/editor/level_tools.md "Segment handles"): the target is a segments or floor kit layout.
    [[nodiscard]] auto has_segment_handles() const -> bool;
    [[nodiscard]] auto get_segment_count  () const -> std::size_t;
    [[nodiscard]] auto get_selected_segment() const -> std::optional<std::size_t>;
    // A gizmo gesture of the tool is editing curve's path.
    [[nodiscard]] auto is_editing(const erhe::scene::Curve& curve) const -> bool;
    // The point set index of a segment handle (after the three entries of every control point).
    [[nodiscard]] auto get_segment_point_index(std::size_t segment) const -> std::size_t;
    // Makes curve the only selected item and the tool's target, then selects the segment (nullopt clears it).
    auto select_curve_segment(const std::shared_ptr<erhe::scene::Curve>& curve, std::optional<std::size_t> segment) -> bool;
    void get_selected_points(std::vector<Curve_point_ref>& out_refs) const;
    // The control points with the position or a handle end selected, ascending.
    void get_selected_point_indices(std::vector<std::size_t>& out_indices) const;
    // Makes curve the only selected item and the tool's target, then sets its point selection.
    auto select_curve_points(const std::shared_ptr<erhe::scene::Curve>& curve, std::span<const Curve_point_ref> refs, Point_select select) -> bool;
    [[nodiscard]] auto can_run(Curve_tool_action action) const -> bool;
    auto run(Curve_tool_action action) -> bool;

    // Implements IPoint_set
    auto update_point_set() -> bool override;
    [[nodiscard]] auto has_point_set_target    () const -> bool override;
    [[nodiscard]] auto get_point_set_scene_root() const -> std::shared_ptr<Scene_root> override;
    void get_point_world_positions(std::vector<std::optional<glm::vec3>>& out_positions) const override;
    [[nodiscard]] auto is_point_selected       (std::size_t index) const -> bool override;
    [[nodiscard]] auto has_point_selection     () const -> bool override;
    void select_point                          (std::size_t index, Point_select select) override;
    auto clear_point_selection                 () -> bool override;
    [[nodiscard]] auto get_point_frame_rotation() const -> glm::quat override;
    [[nodiscard]] auto get_point_drawing       () const -> Point_drawing override;
    auto begin_point_edit () -> bool override;
    void apply_point_edit (const std::optional<glm::mat4>& world_delta) override;
    void commit_point_edit() override;
    void cancel_point_edit() override;

    static constexpr std::size_t parts_per_point = 3;

protected:
    [[nodiscard]] auto try_ready_miss(Viewport_scene_view& viewport_scene_view, const glm::vec2& pointer) -> bool override;
    auto on_select_miss() -> bool override;
    void render_extras(const Render_context& context, std::optional<std::size_t> hovered) override;

private:
    void set_target       (const std::shared_ptr<erhe::scene::Curve>& curve);
    void update_segment_handles();
    [[nodiscard]] auto get_segment_of(std::size_t index) const -> std::optional<std::size_t>;
    void prune_selection  ();
    void execute          (const std::shared_ptr<Curve_set_operation>& operation);
    [[nodiscard]] auto is_valid_index(const erhe::math::Curve_path& path, std::size_t index) const -> bool;
    [[nodiscard]] auto pick_insert_location(Viewport_scene_view& viewport_scene_view, const glm::vec2& pointer) const -> std::optional<erhe::math::Curve_location>;

    erhe::message_bus::Subscription<Selection_message>     m_selection_subscription;
    erhe::message_bus::Subscription<Curve_changed_message> m_curve_changed_subscription;
    erhe::message_bus::Subscription<Close_scene_message>   m_close_scene_subscription;
    erhe::message_bus::Subscription<Items_removed_message> m_items_removed_subscription;
    erhe::message_bus::Subscription<Kit_layout_changed_message> m_kit_layout_changed_subscription;

    std::vector<std::unique_ptr<Curve_tool_command>> m_commands;
    std::weak_ptr<erhe::scene::Curve>                m_target;
    bool                                             m_target_stale{true};
    std::vector<std::size_t>                         m_selection; // point set indices, ascending
    bool                                             m_segment_handles{false}; // the target is a segments or floor kit layout
    std::optional<std::size_t>                       m_selected_segment;       // exclusive with m_selection

    enum class Miss_action : unsigned int { none = 0, insert, clear };
    Miss_action                               m_miss_action{Miss_action::none};
    std::optional<erhe::math::Curve_location> m_insert_location;

    class Edit
    {
    public:
        std::shared_ptr<erhe::scene::Curve> curve;
        erhe::math::Curve_path              before;
        glm::mat4                           world_from_node{1.0f};
        glm::mat4                           node_from_world{1.0f};
        std::optional<std::size_t>          segment;               // a segment handle drag
        glm::vec3                           segment_midpoint{0.0f}; // node frame, at the press
        glm::vec3                           segment_normal  {0.0f}; // node frame, unit
        bool                                active{false};
    };
    Edit                   m_edit;
    erhe::math::Curve_path m_scratch_path; // the edited path of apply_point_edit (capacity kept)
};

}
