#pragma once

#include "app_message.hpp"
#include "windows/geometry_spreadsheet_model.hpp"

#include "erhe_imgui/imgui_window.hpp"
#include "erhe_message_bus/message_bus.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <set>
#include <vector>

namespace erhe::imgui    { class Imgui_windows; }
namespace erhe::scene    { class Mesh; }

namespace editor {

class App_context;
class App_message_bus;
class Mesh_component_entry;

enum class Row_select_op : unsigned int {
    add,
    toggle
};

enum class Cell_value_op : unsigned int {
    set,
    remove
};

// The one cell being edited in place.
class Spreadsheet_cell_edit
{
public:
    bool               active       {false};
    bool               request_focus{false};
    bool               was_active   {false};
    int                frames       {0};
    Spreadsheet_domain domain       {Spreadsheet_domain::vertex};
    std::size_t        row          {0};
    GEO::index_t       element      {0};
    int                column       {0};
    uint64_t           layout_serial{0};
    double             value        {0.0};
    double             original     {0.0};
};

enum class Spreadsheet_target_mode : unsigned int {
    follow_selection,
    pinned
};

enum class Spreadsheet_row_filter : unsigned int {
    all,
    selected
};

// Rows the window submitted in its last drawn frame, as [first, last) row
// ranges of the list clipper (doc/editor/geometry_spreadsheet.md).
class Spreadsheet_drawn_rows
{
public:
    static constexpr std::size_t c_max_ranges = 4;

    class Range
    {
    public:
        std::size_t first{0};
        std::size_t last {0};
    };

    std::array<Range, c_max_ranges> ranges{};
    std::size_t                     range_count{0};
};

// Spreadsheet view of one mesh primitive's Geometry: one tab per element
// domain, one row per element, one column per attribute component
// (doc/editor/geometry_spreadsheet.md).
class Geometry_spreadsheet_window : public erhe::imgui::Imgui_window
{
public:
    Geometry_spreadsheet_window(
        erhe::imgui::Imgui_renderer& imgui_renderer,
        erhe::imgui::Imgui_windows&  imgui_windows,
        App_context&                 app_context,
        App_message_bus&             app_message_bus
    );

    // Implements Imgui_window
    void imgui () override;
    void hidden() override;

    // Target (D2). set_target() pins the window to the given mesh primitive.
    void set_target     (const std::shared_ptr<erhe::scene::Mesh>& mesh, std::size_t primitive_index);
    void set_target_mode(Spreadsheet_target_mode mode);
    void set_domain     (Spreadsheet_domain domain);

    [[nodiscard]] auto get_target_mesh    () const -> std::shared_ptr<erhe::scene::Mesh>;
    [[nodiscard]] auto get_primitive_index() const -> std::size_t;
    [[nodiscard]] auto get_target_mode    () const -> Spreadsheet_target_mode;
    [[nodiscard]] auto get_domain         () const -> Spreadsheet_domain;
    [[nodiscard]] auto get_drawn_rows     () const -> const Spreadsheet_drawn_rows&;
    [[nodiscard]] auto get_row_filter     () const -> Spreadsheet_row_filter;
    void               set_row_filter     (Spreadsheet_row_filter row_filter);

    // Whether `element` of the current domain is selected: the target's
    // Mesh_component_selection entry for Vertex / Facet / Edge, the window's
    // own corner selection for Corner.
    [[nodiscard]] auto is_element_selected(GEO::index_t element) -> bool;

    // The model as of the last drawn frame; call update_model() first to bring
    // it up to date with the current target outside of imgui().
    [[nodiscard]] auto get_model   () const -> const Geometry_spreadsheet_model&;
    void               update_model();

private:
    void on_selection_changed ();
    void on_items_removed     (const Removed_items& removed);
    void on_geometry_changed  (const Mesh_geometry_changed_message& message);
    void resolve_follow_target();
    void drop_target          ();
    // Validates the target and feeds its current Geometry to the model.
    // Returns the target mesh, or null when there is no valid target.
    auto refresh_target       () -> std::shared_ptr<erhe::scene::Mesh>;

    void imgui_target_row(const std::shared_ptr<erhe::scene::Mesh>& mesh);
    void imgui_domain_tabs();
    void imgui_table     (const std::shared_ptr<erhe::scene::Mesh>& mesh);

    // Row selection (doc/editor/geometry_spreadsheet.md section 5).
    void update_row_filter(const std::shared_ptr<erhe::scene::Mesh>& mesh);
    void on_row_clicked   (const std::shared_ptr<erhe::scene::Mesh>& mesh, std::size_t row, GEO::index_t element);
    void select_element   (Mesh_component_entry& entry, GEO::index_t element, Row_select_op op);
    void set_hovered_element(const std::shared_ptr<erhe::scene::Mesh>& mesh, GEO::index_t element);
    [[nodiscard]] auto find_selection_entry(const std::shared_ptr<erhe::scene::Mesh>& mesh) -> Mesh_component_entry*;
    // The selected elements of the current domain, ascending, into `out`.
    void gather_selected_elements(const std::shared_ptr<erhe::scene::Mesh>& mesh, std::vector<GEO::index_t>& out);

    // Cell editing (doc/editor/geometry_spreadsheet.md section 6).
    void begin_cell_edit (std::size_t row, GEO::index_t element, int column);
    void imgui_cell_editor(const Spreadsheet_column& column);
    void imgui_cell_menu (const std::shared_ptr<erhe::scene::Mesh>& mesh);
    // Writes `value` into component `column.component` of every element, as
    // one undoable operation; Cell_value_op::remove clears the elements'
    // values instead.
    void apply_cell_value(
        const std::shared_ptr<erhe::scene::Mesh>& mesh,
        const Spreadsheet_column&                 column,
        const std::vector<GEO::index_t>&          elements,
        double                                    value,
        Cell_value_op                             op
    );

    App_context&                                                   m_context;
    erhe::message_bus::Subscription<Selection_message>             m_selection_subscription;
    erhe::message_bus::Subscription<Active_item_changed_message>   m_active_item_subscription;
    erhe::message_bus::Subscription<Mesh_component_mode_changed_message> m_mode_subscription;
    erhe::message_bus::Subscription<Items_removed_message>         m_items_removed_subscription;
    erhe::message_bus::Subscription<Mesh_geometry_changed_message> m_geometry_changed_subscription;
    erhe::message_bus::Subscription<Mesh_component_selection_changed_message> m_components_changed_subscription;

    std::weak_ptr<erhe::scene::Mesh> m_target_mesh;
    std::size_t                      m_primitive_index{0};
    Spreadsheet_target_mode          m_target_mode    {Spreadsheet_target_mode::follow_selection};
    bool                             m_follow_dirty   {true};
    Spreadsheet_domain               m_domain         {Spreadsheet_domain::vertex};
    Spreadsheet_domain               m_requested_domain{Spreadsheet_domain::vertex};
    bool                             m_domain_requested{false};
    Geometry_spreadsheet_model       m_model;
    bool                             m_model_released {true};
    Spreadsheet_drawn_rows           m_drawn_rows;

    Spreadsheet_row_filter           m_row_filter      {Spreadsheet_row_filter::all};
    bool                             m_row_filter_dirty{true};
    std::vector<GEO::index_t>        m_row_filter_scratch;
    std::set<GEO::index_t>           m_corner_selection;   // the Corner tab's own row selection
    std::size_t                      m_anchor_row      {0}; // Shift+click range anchor, display order
    GEO::index_t                     m_hovered_element {GEO::NO_INDEX};
    Spreadsheet_domain               m_hovered_domain  {Spreadsheet_domain::vertex};

    Spreadsheet_cell_edit            m_edit;
    bool                             m_edit_commit     {false};
    GEO::index_t                     m_menu_element    {GEO::NO_INDEX};
    int                              m_menu_column     {-1};
    std::vector<GEO::index_t>        m_selected_scratch;

    // Initial column widths, measured once per column layout.
    std::vector<float>               m_column_widths;
    Spreadsheet_domain               m_widths_domain   {Spreadsheet_domain::vertex};
    uint64_t                         m_widths_serial   {0};
    float                            m_widths_font_size{0.0f};
};

} // namespace editor
