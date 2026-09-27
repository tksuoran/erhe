#pragma once

#include "app_message.hpp"
#include "windows/geometry_spreadsheet_model.hpp"

#include "erhe_imgui/imgui_window.hpp"
#include "erhe_message_bus/message_bus.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace erhe::imgui    { class Imgui_windows; }
namespace erhe::scene    { class Mesh; }

namespace editor {

class App_context;
class App_message_bus;

enum class Spreadsheet_target_mode : unsigned int {
    follow_selection,
    pinned
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
    void imgui_table     ();

    App_context&                                                   m_context;
    erhe::message_bus::Subscription<Selection_message>             m_selection_subscription;
    erhe::message_bus::Subscription<Active_item_changed_message>   m_active_item_subscription;
    erhe::message_bus::Subscription<Mesh_component_mode_changed_message> m_mode_subscription;
    erhe::message_bus::Subscription<Items_removed_message>         m_items_removed_subscription;
    erhe::message_bus::Subscription<Mesh_geometry_changed_message> m_geometry_changed_subscription;

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

    // Initial column widths, measured once per column layout.
    std::vector<float>               m_column_widths;
    Spreadsheet_domain               m_widths_domain   {Spreadsheet_domain::vertex};
    uint64_t                         m_widths_serial   {0};
    float                            m_widths_font_size{0.0f};
};

} // namespace editor
