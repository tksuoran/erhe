#pragma once

#include "level/kit_layout.hpp"
#include "level/kit_layout_values.hpp"
#include "level/kit_parameter_rows.hpp"
#include "level/kit_shape.hpp"
#include "level/level_kits.hpp"

#include <array>
#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace erhe::scene {
    class Curve;
    class Mesh;
    class Xformable;
    using Node = Xformable;
}

namespace editor {

class App_context;
class Scene_root;

// The kinds a pattern's Kind row offers (R5.12.8): every kind (initial values), an Xform node's or a curve node's.
enum class Kit_layout_kinds : unsigned int {
    all = 0,
    xform, // line, grid, ring
    curve  // path
};

// One row of a pattern's values; get_layout_rows() lists the rows the pattern's kind and count mode show.
enum class Kit_layout_row : unsigned int {
    kind = 0,
    count_mode,
    count,
    gap,
    direction,
    step,
    length,
    count_x,
    count_z,
    spacing_x,
    spacing_z,
    radius,
    arc,
    facing,
    orientation
};

// One row of a layout's snap and seed values.
enum class Kit_layout_setting_row : unsigned int {
    module_snap = 0,
    surface_snap,
    surface_orient,
    random_seeds,
    seed
};

inline constexpr std::array<Kit_layout_setting_row, 5> c_layout_setting_rows{
    Kit_layout_setting_row::module_snap, Kit_layout_setting_row::surface_snap, Kit_layout_setting_row::surface_orient,
    Kit_layout_setting_row::random_seeds, Kit_layout_setting_row::seed
};

[[nodiscard]] auto get_layout_rows       (const Kit_layout_pattern& pattern) -> std::span<const Kit_layout_row>;
[[nodiscard]] auto get_layout_row_name   (Kit_layout_row row) -> const char*;
[[nodiscard]] auto get_layout_setting_name(Kit_layout_setting_row row) -> const char*;

// The widget of one pattern row (doc/editor/level_tools.md "Kit layout rows"): changed during a drag or typing, committed
// when a drag is released, a value entered, a radio button or checkbox pressed.
[[nodiscard]] auto layout_pattern_row(Kit_layout_pattern& pattern, Kit_layout_row row, Kit_layout_kinds kinds, Kit_row_label label) -> Kit_row_edit;
[[nodiscard]] auto layout_setting_row(Kit_layout_data& data, Kit_layout_setting_row row, Kit_row_label label) -> Kit_row_edit;

// One item of the shown layout node as its rows show it.
class Kit_layout_item_rows
{
public:
    Kit_parameter_values     values;
    std::vector<std::string> materials; // by slot, the slot's default filled in
    std::string              label;     // "Item <n>: <shape>"
    std::string              problem;   // why the item does not read
    std::string              message;   // validate() of the last edit
};

// The values of one kit layout node, the Properties window's "Kit Layout" group (R5.12.9, D4.9). A drag of a locked
// layout previews each change (Level_kits::preview_layout_values()); every edit commits one Level_kits call at its end.
class Kit_layout_editor
{
public:
    explicit Kit_layout_editor(App_context& context);

    // The node's values are read again (queued); on selection, Kit_layout value and item removal messages.
    void mark_dirty();

    // The shown layout's own curve changed (Curve_changed_message): its run is read again.
    void on_curve_changed(const erhe::scene::Curve& curve);

    // Reads the node's values (queued) when another node is given or after mark_dirty(), never while an item is active.
    void update(const std::shared_ptr<erhe::scene::Node>& layout);

    [[nodiscard]] auto is_ready    () const -> bool;
    [[nodiscard]] auto get_problem () const -> const std::string&; // why the values do not read
    [[nodiscard]] auto get_pattern () const -> const Kit_layout_pattern&;
    [[nodiscard]] auto get_items   () const -> std::span<const Kit_layout_item_rows>;
    [[nodiscard]] auto get_message () const -> const std::string&;

    // The row widgets, each drawn in its own Properties row (Kit_row_label::recorded).
    void state_line           ();
    void pattern_row          (Kit_layout_row row);
    void run_line             ();
    void item_shape_line      (std::size_t item);
    void item_parameter_row   (std::size_t item, std::size_t parameter);
    void item_material_row    (std::size_t item, std::size_t slot);
    void item_remove_button   (std::size_t item);
    // Adds the shape the Brush tool places (the Level Kit window's chosen shape, its values and slot materials).
    void add_item_button      ();
    void setting_row          (Kit_layout_setting_row row);
    void lock_buttons         ();
    void message_line         () const;

private:
    void read_layout  (Scene_root& scene_root, const erhe::scene::Node& layout);
    void queue_read   ();
    void on_row_edit  (Kit_row_edit edit, const Kit_layout_data& data);
    void commit       (const Kit_layout_data& data);
    void queue_preview(const Kit_layout_data& data);
    void queue_end_preview();
    void queue_set    (const Kit_layout_data& data);
    void queue_lock   (Kit_layout_lock lock);
    void queue_refresh();

    App_context& m_context;

    // The shown layout node's values as last read; the rows edit this copy and commit it.
    std::weak_ptr<erhe::scene::Node>  m_layout;
    std::weak_ptr<Scene_root>         m_scene_root;
    bool                              m_dirty      {true};
    bool                              m_read_queued{false};
    bool                              m_ready      {false};
    std::string                       m_problem;
    Kit_layout_data                   m_data;
    Kit_layout_kinds                  m_kinds{Kit_layout_kinds::xform};
    std::vector<Kit_layout_item_rows> m_items;
    Kit_lengths                       m_lengths;
    Kit_layout_run                    m_run;
    std::string                       m_run_problem;
    float                             m_item_extent{0.0f};

    // The last edit's result: a refusal in red, else a confirmation.
    std::string m_message;
    bool        m_failed{false};
};

// The Level Kit window's Layout section (R5.12.9): the initial values of a new kit layout node (editor setting
// level_kit), Make Layout from the selected piece and Place As Layout for the Brush tool. Editing them changes no node.
class Kit_layout_initial_editor
{
public:
    explicit Kit_layout_initial_editor(App_context& context);

    void imgui(const std::shared_ptr<Scene_root>& scene_root, const std::shared_ptr<erhe::scene::Mesh>& piece);

private:
    void store();
    void queue_make(const std::shared_ptr<Scene_root>& scene_root, const std::shared_ptr<erhe::scene::Mesh>& piece);

    App_context&              m_context;
    // Read from the editor setting once, written back at each edit.
    Kit_layout_initial_values m_values;
    bool                      m_loaded{false};
    std::string               m_message;
    bool                      m_failed{false};
};

} // namespace editor
