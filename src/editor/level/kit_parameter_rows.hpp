#pragma once

#include "level/kit_connector.hpp"
#include "level/kit_shape.hpp"
#include "level/kit_shape_library.hpp"

#include "erhe_imgui/imgui_item_recorder.hpp"
#include "erhe_scene/node.hpp"

#include <fmt/format.h>
#include <imgui/imgui.h>

#include <array>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace erhe::scene { class Mesh; }

namespace editor {

class App_context;

enum class Kit_row_edit : unsigned int {
    none = 0,
    cancelled, // an edit ended without a change (Escape in a typed value, a click without a drag)
    changed,   // a value changed this frame (also during a drag)
    committed // an edit ended: a drag released, a typed value entered or a combo entry chosen
};

enum class Kit_row_label : unsigned int {
    beside = 0, // the parameter name after the widget (Level Kit window)
    recorded    // the widget fills its cell; the name is the row's label (Properties window)
};

[[nodiscard]] auto merge(Kit_row_edit lhs, Kit_row_edit rhs) -> Kit_row_edit;

// The widget of one parameter of the values' shape: a drag per float (range, unit) or integer, a combo per enum;
// a derivable parameter has a "from seed" toggle in front, its masked value shown disabled, and the seed a New button
// (D4.5). A seed or mask change derives the masked values again, ranges resolved against the lengths.
[[nodiscard]] auto kit_parameter_row(Kit_parameter_values& values, std::size_t index, Kit_row_label label, const Kit_lengths& lengths) -> Kit_row_edit;

// kit_parameter_row() for every parameter, names beside the widgets.
[[nodiscard]] auto kit_parameter_rows(Kit_parameter_values& values, const Kit_lengths& lengths) -> Kit_row_edit;

// The read-only line of what stairs values produce (R3.5); nothing for other shapes.
void kit_stairs_line(const Kit_parameter_values& values);

// A validate() or record problem message in red; nothing when empty.
void kit_message_line(const std::string& message);

// One combo per material slot of the shape, keyed by the slot's name, offering the slot's default and alternatives
// (D3.4); returns the slot whose choice changed this frame, its new palette material name in out_name.
template <typename Current_name>
[[nodiscard]] auto slot_material_combos(
    const Kit_shape_library&    library,
    const Kit_shape_definition& shape,
    const Current_name&         current_name,
    std::string&                out_name
) -> std::optional<std::size_t>
{
    std::optional<std::size_t> changed;
    std::array<char, 64> label{};
    for (std::size_t slot = 0, end = shape.material_slots.size(); slot < end; ++slot) {
        const std::string&            slot_name = shape.material_slots[slot];
        const Kit_slot_palette* const palette   = library.find_slot_palette(slot_name);
        const std::string&            current   = current_name(slot);
        const fmt::format_to_n_result<char*> label_end = fmt::format_to_n(label.data(), label.size() - 1, "{} material", slot_name);
        *label_end.out = '\0';
        ImGui::PushID(slot_name.c_str());
        const bool open = ImGui::BeginCombo(label.data(), current.c_str());
        if (!open) {
            // Dear ImGui reports no label for a combo (doc/agents/mcp_ui_driving.md "How an item gets a label").
            erhe::imgui::set_item_debug_label(label.data());
        }
        if (open) {
            const auto option = [&](const std::string& name) {
                if (ImGui::Selectable(name.c_str(), name == current) && (name != current)) {
                    changed  = slot;
                    out_name = name;
                }
            };
            if (palette != nullptr) {
                option(palette->default_material);
                for (const std::string& alternative : palette->alternatives) {
                    option(alternative);
                }
            }
            ImGui::EndCombo();
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("%s", "The palette material of this slot: its default first, then its alternatives.");
        }
        ImGui::PopID();
    }
    return changed;
}

// C6.3, C9.2: one row per connection of a kit piece or a floor layout node (its own records and the records naming it): its
// connector and a text naming the other end, the twist and gap and, when not followed, why; with Remove and Detach.
class Kit_connection_rows
{
public:
    explicit Kit_connection_rows(App_context& context);

    class Row
    {
    public:
        std::string connector;
        std::string text;
    };

    // Reads the node's connections (a cold path): read() at once, update() when another node is given or after mark_dirty().
    void read(const std::shared_ptr<erhe::scene::Node>& node);
    void update(const std::shared_ptr<erhe::scene::Node>& node);
    void mark_dirty();
    [[nodiscard]] auto get_rows() const -> const std::vector<Row>&;
    // Queue Level_kits::remove_connection() of one connector and Level_kits::detach_pieces() of the node: one undo entry each.
    void remove_connection(const std::string& connector);
    void detach();

private:
    App_context&                     m_context;
    std::weak_ptr<erhe::scene::Node> m_node;
    std::size_t                      m_node_id{0};
    bool                             m_dirty{true};
    std::vector<Row>                 m_rows;
};

// The parameter rows of one placed piece (doc/editor/level_tools.md "Parameter rows"): the values
// read from its record, and an edit that ends with valid values queued as one set_parameters().
class Kit_piece_editor
{
public:
    explicit Kit_piece_editor(App_context& context);

    // The record is read again at the next update(); on selection and mesh geometry messages.
    void mark_dirty();

    // Reads the record when another piece is given or after mark_dirty(), not while a drag previews it.
    void update(const std::shared_ptr<erhe::scene::Mesh>& piece);

    // Validates after an edit; a valid change previews (Level_kits::preview_parameters()), an edit that
    // ends with valid values sets them (one undo entry), with refused values or cancelled restores (D3.6).
    void on_edit(Kit_row_edit edit);

    [[nodiscard]] auto get_shape     () const -> const Kit_shape_definition*; // null when the record does not read
    [[nodiscard]] auto get_shape_text() const -> const std::string&;          // the display name, else the record's id
    [[nodiscard]] auto get_problem   () const -> const std::string&;          // why the record does not read
    [[nodiscard]] auto get_message   () const -> const std::string&;          // validate() of the last edit
    [[nodiscard]] auto get_values    () -> Kit_parameter_values&;
    [[nodiscard]] auto get_lengths   () const -> const Kit_lengths&;          // of the piece's scene, read with the record

    // U33: every connector of the piece's shape and the piece's switches, read with the record.
    [[nodiscard]] auto get_connectors        () const -> const std::vector<Kit_connector>&;
    [[nodiscard]] auto get_connector_switches() const -> const Kit_connector_switches&;
    // Queues Level_kits::set_connector_switches() for the piece switch or one connector's switch: one undo entry.
    void set_piece_connectors(Kit_connector_state state);
    void set_connector       (const char* connector_name, Kit_connector_state state);

    // C6.3: the piece's connection rows, read with the record.
    [[nodiscard]] auto get_connection_rows() -> Kit_connection_rows&;
    // S7.1: the support and status of the piece's surface attachment, read with the record; empty without a record.
    [[nodiscard]] auto get_surface_attachment_text() const -> const std::string&;

private:
    App_context&                     m_context;
    std::weak_ptr<erhe::scene::Mesh> m_piece;
    std::size_t                      m_piece_id{0};
    bool                             m_dirty{true};
    std::string                      m_shape_text;
    std::string                      m_problem;
    Kit_parameter_values             m_values;
    Kit_lengths                      m_lengths;
    std::string                      m_message;
    std::vector<Kit_connector>       m_connectors;
    Kit_connector_switches           m_connector_switches;
    Kit_connection_rows              m_connection_rows;
    std::string                      m_surface_attachment_text;
};

} // namespace editor
