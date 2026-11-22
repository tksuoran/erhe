#pragma once

#include "tools/tool.hpp"
#include "level/foliage_palette.hpp"
#include "level/foliage_stroke.hpp"
#include "app_message.hpp"

#include "erhe_commands/command.hpp"
#include "erhe_message_bus/message_bus.hpp"

#include <glm/glm.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

struct Foliage_paint_config;

namespace erhe { class Item_host; }

// The Foliage Paint tool (doc/editor/level_tools.md "Foliage paint").
namespace editor {

class App_context;
class App_message_bus;
class Foliage_paint_tool;
class Icon_set;
class Level_kits;
class Scene_root;
class Tools;

inline constexpr float c_foliage_min_brush_radius   = 0.1f;
inline constexpr float c_foliage_max_brush_radius   = 50.0f;
inline constexpr float c_foliage_min_paint_density  = 0.01f;
inline constexpr float c_foliage_max_paint_density  = 100.0f;
inline constexpr float c_foliage_radius_wheel_step  = 1.1f;
inline constexpr std::size_t c_foliage_brush_segments = 64;
inline constexpr std::size_t c_foliage_walk_stamps_per_frame = 16;  // a frame's walk, every path together, while at most
inline constexpr std::size_t c_foliage_walk_pending_stamps   = 256; // this many stamps are pending; the excess is walked too
inline constexpr std::size_t c_foliage_walk_ring_size        = 32;  // the pending hover changes; a full ring walks its head
inline constexpr std::size_t c_foliage_stamp_centers_size    = 1024; // the stamp centers kept for checks; recording stops when full

// The left (paint) or right (remove) drag: the press begins a stroke and stamps, the release ends it.
class Foliage_paint_stroke_command : public erhe::commands::Command
{
public:
    Foliage_paint_stroke_command(erhe::commands::Commands& commands, const char* name, Foliage_paint_tool& tool, Foliage_stroke_kind kind);
    void try_ready          () override;
    auto try_call_with_input(erhe::commands::Input_arguments& input) -> bool override;
    void on_inactive        () override;

private:
    Foliage_paint_tool& m_tool;
    Foliage_stroke_kind m_kind;
};

// Shift + wheel: the brush radius.
class Foliage_paint_radius_command : public erhe::commands::Command
{
public:
    Foliage_paint_radius_command(erhe::commands::Commands& commands, Foliage_paint_tool& tool);
    auto try_call_with_input(erhe::commands::Input_arguments& input) -> bool override;

private:
    Foliage_paint_tool& m_tool;
};

// Each frame (an update binding): continues a travel walk the last frame's budget left pending.
class Foliage_paint_walk_command : public erhe::commands::Command
{
public:
    Foliage_paint_walk_command(erhe::commands::Commands& commands, Foliage_paint_tool& tool);
    auto try_call() -> bool override;

private:
    Foliage_paint_tool& m_tool;
};

// Escape: cancels the open stroke.
class Foliage_paint_cancel_command : public erhe::commands::Command
{
public:
    Foliage_paint_cancel_command(erhe::commands::Commands& commands, Foliage_paint_tool& tool);
    auto try_call() -> bool override;

private:
    Foliage_paint_tool& m_tool;
};

// The surface point under the pointer the tool acts at (F4.1).
class Foliage_brush
{
public:
    std::weak_ptr<Scene_root> scene_root;
    glm::vec3                 center{0.0f};
    glm::vec3                 normal{0.0f, 1.0f, 0.0f}; // unit
    glm::vec3                 ray_origin{0.0f};         // the pointer ray that found the center
    glm::vec3                 ray_direction{0.0f, 0.0f, -1.0f}; // unit
};

// How a queued brush continues the travel: along the chord from the walked end, or after the pointer left the viewport.
enum class Foliage_walk_start : unsigned int {
    continued = 0,
    new_segment
};

class Foliage_walk_point
{
public:
    Foliage_brush      brush;
    Foliage_walk_start start{Foliage_walk_start::continued};
};

enum class Foliage_walk_limit : unsigned int {
    frame_budget = 0, // the frame's shared budget
    head_segment,     // the head segment to its end (a full ring)
    complete          // everything pending (the release)
};

// How the tool's last stroke ended.
enum class Foliage_stroke_outcome : unsigned int {
    none = 0,  // no stroke yet
    ended,     // the release, a deactivation, a viewport going away
    cancelled, // Escape
    abandoned, // the stack abandoned it: a scene close, the shutdown
    refused    // begin() refused it
};

[[nodiscard]] auto c_str(Foliage_stroke_outcome outcome) -> const char*;

class Foliage_paint_last_stroke
{
public:
    Foliage_stroke_kind    kind   {Foliage_stroke_kind::paint};
    Foliage_stroke_outcome outcome{Foliage_stroke_outcome::none};
    Foliage_stroke_result  result;
    std::string            refusal;
    std::string            scene_name;
    int                    seed{0}; // the seed the stroke drew from
};

// The parameter rows' state of one palette entry: its values at the default level lengths.
class Foliage_palette_entry_rows
{
public:
    Kit_parameter_values values;
    std::string          problem; // why the entry's parameters do not read
    std::string          message; // validate() of the last edit
};

class Foliage_paint_tool : public Tool
{
public:
    static constexpr int c_priority{4};

    Foliage_paint_tool(
        Foliage_paint_config&     config,
        Level_kits&               level_kits,
        erhe::commands::Commands& commands,
        App_context&              context,
        App_message_bus&          app_message_bus,
        Icon_set&                 icon_set,
        Tools&                    tools
    );
    ~Foliage_paint_tool() noexcept override;
    Foliage_paint_tool(const Foliage_paint_tool&) = delete;
    auto operator=(const Foliage_paint_tool&) -> Foliage_paint_tool& = delete;

    // Implements Tool
    void tool_render           (const Render_context& context)           override;
    void tool_properties       (erhe::imgui::Imgui_window& imgui_window) override;
    void handle_priority_update(int old_priority, int new_priority)      override;

    // Commands
    [[nodiscard]] auto is_stroke_ready(Foliage_stroke_kind kind) const -> bool;
    // The press: begins a stroke and stamps when a stroke can begin; else the press does nothing.
    void               begin_stroke   (Foliage_stroke_kind kind);
    // The release: ends the open stroke of the kind.
    void               end_stroke     (Foliage_stroke_kind kind);
    [[nodiscard]] auto cancel_stroke  () -> bool;
    [[nodiscard]] auto adjust_radius  (float wheel_steps) -> bool;
    void               continue_walk  ();
    // A scene view hidden (Viewport_window::hidden(), on its edge) or destroyed: ends the stroke made in it.
    void               on_stroke_view_gone(const Scene_view* scene_view);

    // The values and palette (MCP level_set_foliage_paint_settings writes them through these).
    [[nodiscard]] auto get_config         () const -> const Foliage_paint_config&;
    void               set_brush_radius   (float radius);
    void               set_paint_density  (float density);
    void               set_remove_density (float density);
    void               set_up_axis        (Foliage_up up);
    void               set_remove_filter  (Foliage_remove_filter filter);
    void               set_seed           (int seed);
    void               set_palette        (std::span<const Foliage_palette_entry> entries);
    [[nodiscard]] auto get_palette        () const -> std::span<const Foliage_palette_entry>;
    // F3.3: why a paint stroke cannot begin with this palette; empty when it can.
    [[nodiscard]] auto get_paint_refusal  () const -> const std::string&;

    [[nodiscard]] auto get_brush          () const -> const std::optional<Foliage_brush>&;
    [[nodiscard]] auto is_stroke_open     () const -> bool;
    [[nodiscard]] auto get_stroke_kind    () const -> Foliage_stroke_kind;
    [[nodiscard]] auto get_stroke_result  () const -> const Foliage_stroke_result&;
    [[nodiscard]] auto get_stroke_scene   () const -> std::shared_ptr<Scene_root>;
    [[nodiscard]] auto get_last_stroke    () const -> const Foliage_paint_last_stroke&;
    // The first stamp centers of the open stroke, else of the last one that began, in stamp order.
    [[nodiscard]] auto get_stamp_centers  () const -> std::span<const glm::vec3>;
    [[nodiscard]] auto is_stamp_centers_truncated() const -> bool; // the stroke stamped more than c_foliage_stamp_centers_size

private:
    void on_hover_scene_view(Hover_scene_view_message& message);
    void on_close_scene     (const erhe::Item_host* closing_host);
    void update_brush       ();
    // The pointer is inside the view's viewport rectangle (a view that is no viewport: always).
    [[nodiscard]] auto is_pointer_in_view(const Scene_view& scene_view) const -> bool;
    void update_brush_circle();
    void stamp_by_travel    ();
    void walk_travel        (Foliage_walk_limit limit);
    void push_walk_point    (const Foliage_brush& brush, Foliage_walk_start start);
    [[nodiscard]] auto get_pending_stamps() const -> std::size_t;
    void clear_walk         ();
    // The release: the travel to the pointer at the release event, walked to the end before the stroke ends.
    void walk_to_release    ();
    void stamp_at           (glm::vec3 center, glm::vec3 normal);
    void finish_stroke      (Foliage_stroke_outcome outcome);
    void on_stroke_abandoned();
    void touch_settings     ();
    void parse_palette      ();
    void palette_imgui      (const std::shared_ptr<Scene_root>& scene_root);
    // The shape's parameter rows of one entry; true when the edit wrote the entry's parameters.
    [[nodiscard]] auto entry_parameter_rows(Foliage_palette_entry& entry, Foliage_palette_entry_rows& rows) -> bool;

    Foliage_paint_config&                                     m_config;
    Level_kits&                                               m_level_kits;
    erhe::message_bus::Subscription<Hover_scene_view_message> m_hover_scene_view_subscription;
    erhe::message_bus::Subscription<Hover_mesh_message>       m_hover_mesh_subscription;
    erhe::message_bus::Subscription<Close_scene_message>      m_close_scene_subscription;
    Foliage_paint_stroke_command                              m_paint_command;
    Foliage_paint_stroke_command                              m_remove_command;
    Foliage_paint_radius_command                              m_radius_command;
    Foliage_paint_cancel_command                              m_cancel_command;
    Foliage_paint_walk_command                                m_walk_command;

    std::vector<Foliage_palette_entry>                        m_palette;
    std::vector<Foliage_palette_entry_rows>                   m_entry_rows; // one per m_palette entry, read at the palette's writes
    std::string                                               m_palette_problem; // the config text's parse error
    std::string                                               m_paint_refusal;
    std::optional<Foliage_brush>                              m_brush;
    std::array<glm::vec3, c_foliage_brush_segments>           m_circle_points{};
    Foliage_stroke                                            m_stroke;
    Foliage_stroke_kind                                       m_stroke_kind{Foliage_stroke_kind::paint};
    Foliage_brush                                             m_walk_from;        // the brush the walked travel ends at
    std::array<Foliage_walk_point, c_foliage_walk_ring_size>  m_walk_ring{};      // the brushes whose travel is pending, a ring
    std::size_t                                               m_walk_head{0};
    std::size_t                                               m_walk_count{0};
    float                                                     m_walked{0.0f};     // meters walked along the head segment
    float                                                     m_since_stamp{0.0f}; // meters of travel since the last stamp
    glm::vec3                                                 m_last_stamp_center{0.0f};
    std::array<glm::vec3, c_foliage_stamp_centers_size>       m_stamp_centers{}; // diagnostic; reset at a press
    std::size_t                                               m_stamp_center_count{0};
    bool                                                      m_stamp_centers_truncated{false};
    bool                                                      m_travel_broken{false}; // the pointer left the viewport
    std::uint64_t                                             m_budget_frame{0};
    std::size_t                                               m_budget_used{0};
    const Scene_view*                                         m_stroke_scene_view{nullptr}; // the view of the press
    std::uint32_t                                             m_press_button_bits{0};
    std::weak_ptr<Scene_root>                                 m_stroke_scene;
    int                                                       m_stroke_seed{0};
    Foliage_paint_last_stroke                                 m_last_stroke;
    std::string                                               m_stroke_scene_name;
};

} // namespace editor
