#pragma once

#include "erhe_property/expression.hpp"
#include "erhe_property/owner_type.hpp"
#include "erhe_property/property_value.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace erhe           { class Item_base; }
namespace erhe::property { class Dependency_property; class Property_metadata; }

namespace editor {

class App_context;
class Property_editor;

// Generic Properties-window rows for the registered properties of one or
// more items (doc/property-system.md D12): one widget per
// Property_type shaped by the property's Property_ui metadata, a value
// source indicator, "Reset to default", Copy / Paste Properties, mixed-value
// display for multi-selection, undo through Property_set_operation /
// Property_set_apply_operation (one operation per completed drag), and the
// formula text in place of the widget for a property driven by an
// expression (D22), with "Edit as expression" / "Remove expression" in the
// context menu. Each item section ends with an "Add Property" row whose
// button opens a filterable picker of the attached properties (R7) the
// D12 rule does not list for the items; choosing one makes the item's
// effective value local so the row appears. An attached row offers
// "Remove Property" in its context menu, and an "x" after its widget when
// the local value is the only reason it is listed; both clear the local
// value (doc/property-system.md D12).
class Dependency_property_rows
{
public:
    explicit Dependency_property_rows(App_context& context);

    // Adds rows to `editor` for the properties every item's type has, in
    // registration order, grouped by Property_ui::group. Call between the
    // caller's push_group() / pop_group(); the rows draw when the caller
    // calls show_entries().
    void add_rows(Property_editor& editor, const std::vector<std::shared_ptr<erhe::Item_base>>& items);

    // Rows for the properties of one of the item's property sub-objects
    // (doc/property-system.md D29, e.g. a mesh primitive): the sub-object's
    // owner type lists the properties, every write is a Property_set_operation
    // on (item, index), and the bag entries of the context menu (copy, paste,
    // style) are not offered. Same push_group() / show_entries() protocol.
    void add_sub_object_rows(Property_editor& editor, const std::shared_ptr<erhe::Item_base>& item, std::size_t sub_object);

private:
    void draw_rows(Property_editor& editor);
    void row(Property_editor& editor, const erhe::property::Dependency_property& property);

    // The Dependency_object row i reads and writes: item i itself, or its
    // m_sub_object (null when the sub-object no longer exists).
    [[nodiscard]] auto target(std::size_t i) const -> erhe::property::Dependency_object*;

    // Returns true when the widget produced a new value this frame.
    // `mixed` is the per-component mask of values the selected items
    // disagree on (bit c = component c; one bit for a one-component
    // type): those components show "mixed" instead of a value.
    // `immediate` is set for widgets that commit on selection (combo)
    // instead of on deactivation.
    auto draw_widget(
        const erhe::property::Dependency_property& property,
        const erhe::property::Property_metadata&   metadata,
        erhe::property::Property_value&            value,
        uint32_t                                   mixed,
        bool&                                      immediate
    ) -> bool;

    // The formula row of a driven property; commits on Enter or deactivation.
    void draw_expression(const erhe::property::Dependency_property& property, std::string_view text);

    void begin_edit (const erhe::property::Dependency_property& property);
    void end_edit   (const erhe::property::Dependency_property& property);
    void queue_set  (const erhe::property::Dependency_property& property, const std::optional<erhe::property::Local_state>& after);
    // The property an edit of `property` records: `property` itself, or
    // for a writable computed property (D26) the stored property its
    // setter writes.
    [[nodiscard]] auto recorded_property(const erhe::property::Dependency_property& property) const -> const erhe::property::Dependency_property&;
    void context_menu(const erhe::property::Dependency_property& property, const erhe::property::Property_metadata& metadata);
    void reset_to_default  (const erhe::property::Dependency_property& property);
    void edit_as_expression(const erhe::property::Dependency_property& property);
    void remove_expression (const erhe::property::Dependency_property& property);
    // The "Add Property" row and its picker (doc/property-system.md D12):
    // the candidates are collect_addable_properties over the
    // items, the add is one Property_set_operation per item without a
    // local value, writing its current effective value.
    void add_property_row       (Property_editor& editor);
    void draw_add_property_popup();
    void queue_add              (const erhe::property::Dependency_property& property);
    // Remove Property (R7): the clear of reset_to_default under the name
    // of what it does to an attached row. `inline_remove_offered` is true
    // when the first item's attached row is listed only because of its
    // local value, so removing it makes the row disappear.
    void remove_property        (const erhe::property::Dependency_property& property);
    [[nodiscard]] auto inline_remove_offered(const erhe::property::Dependency_property& property, const erhe::property::Property_metadata& metadata) const -> bool;
    void draw_inline_remove     (const erhe::property::Dependency_property& property);
    void copy_properties ();
    void paste_properties();
    void paste_properties_as_style(); // D25
    void clear_style              ();

    App_context& m_context;

    // Items the currently executing code operates on, bound only while
    // add_rows() / add_sub_object_rows() build the rows and while a row
    // lambda runs, and null in between: add_rows() can be called more than
    // once per frame (the node and each of its attachments), so every row
    // lambda captures its own call's snapshot and re-binds m_items from it;
    // and a snapshot kept past the draw would hold the items of a closed
    // scene alive (the scene-close leak class, AGENTS.md).
    std::shared_ptr<const std::vector<std::shared_ptr<erhe::Item_base>>> m_items;
    std::optional<std::size_t>                                            m_sub_object; // D29: rows address items' sub-object of this index

    // Drag / type session: `before` captured at widget activation, one per
    // item, committed as an operation at deactivation.
    const erhe::property::Dependency_property*               m_edit_property{nullptr};
    std::vector<std::optional<erhe::property::Local_state>>  m_edit_before;
    glm::vec3                                                m_edit_euler_degrees{0.0f}; // quaternion rows edit in Euler space
    std::string                                              m_text_scratch;
    std::vector<std::shared_ptr<erhe::Item_base>>            m_reference_candidates; // object rows: picker candidates, cleared after each draw
    std::string                                              m_expression_scratch; // the formula being typed in the active expression row
    std::vector<const erhe::property::Dependency_property*>  m_add_candidates;     // Add Property: the picker's entries, refilled each frame the row draws
    std::vector<const erhe::property::Dependency_property*>  m_add_scratch;        // Add Property: one item's candidates while forming the union
    // Owner types a Scope offers first in Add Property: the classes of the
    // prims below it (doc/content-library-folders.md D8).
    std::vector<erhe::property::Owner_type>                  m_add_preferred_owner_types;
    std::string                                              m_add_filter;         // Add Property: the filter text
    std::string                                              m_add_filter_lower;   // Add Property: the filter lowered for matching
    std::string                                              m_add_label_lower;    // Add Property: a candidate's label lowered for matching
    std::string                                              m_add_owner_scratch;  // Add Property: the owner group header text
    bool                                                     m_add_focus_filter{false}; // Add Property: focus the filter on the popup's first frame
};

}
