#include "windows/dependency_property_rows.hpp"

#include "erhe_item/scope.hpp"
#include "windows/attached_property_listing.hpp"
#include "windows/item_reference.hpp"
#include "windows/property_editor.hpp"
#include "windows/property_origin.hpp"

#include "app_context.hpp"
#include "editor_log.hpp"
#include "operations/compound_operation.hpp"
#include "operations/operation_stack.hpp"
#include "operations/property_set_operation.hpp"
#include "operations/style_set_operation.hpp"
#include "scene/item_lookup.hpp"
#include "tools/clipboard.hpp"

#include "erhe_defer/defer.hpp"
#include "erhe_item/item.hpp"
#include "erhe_property/dependency_property.hpp"
#include "erhe_property/enum_info.hpp"
#include "erhe_property/expression.hpp"
#include "erhe_property/property_metadata.hpp"
#include "erhe_property/property_set.hpp"
#include "erhe_property/property_style.hpp"
#include "erhe_property/property_string.hpp"

#include <glm/gtc/quaternion.hpp>
#include <imgui/imgui.h>
#include <imgui/imgui_internal.h> // ImGuiItemFlags_MixedValue (mixed checkbox)
#include <imgui/misc/cpp/imgui_stdlib.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string_view>

namespace editor {

using erhe::property::Dependency_property;
using erhe::property::Property_metadata;
using erhe::property::Property_type;
using erhe::property::Property_ui;
using erhe::property::Property_value;
using erhe::property::Value_source;

namespace {

// Label hue per value source (D12). A member-backed (bridged, D18)
// property always reports Value_source::local, so it gets its own hue.
[[nodiscard]] auto label_color(const Value_source source, const bool bridged) -> uint32_t
{
    if (bridged) {
        return IM_COL32(200, 224, 255, 255); // blue: member-backed
    }
    switch (source) {
        case Value_source::default_value: return IM_COL32(205, 205, 205, 255); // gray: the metadata default
        case Value_source::local:         return IM_COL32(190, 235, 190, 255); // green: set on this object
        case Value_source::expression:    return IM_COL32(170, 240, 240, 255); // cyan: driven by a formula
        case Value_source::style:         return IM_COL32(255, 225, 170, 255); // orange: from the style
        case Value_source::inherited:     return IM_COL32(225, 195, 255, 255); // purple: from an ancestor
        case Value_source::computed:      return IM_COL32(165, 165, 165, 255); // dim gray: computed, read-only
        case Value_source::reference:     return IM_COL32(255, 190, 215, 255); // pink: from the reference counterpart (D33)
    }
    return IM_COL32(255, 255, 255, 255);
}

// Mixed values across a multi-selection are tracked per component: bit c
// of the mask is set when the items disagree on component c. A scalar,
// string, enumeration, object or quaternion value is one component.
[[nodiscard]] auto component_count(const Property_type type) -> int
{
    switch (type) {
        case Property_type::vec2:  return 2;
        case Property_type::ivec2: return 2;
        case Property_type::vec3:  return 3;
        case Property_type::ivec3: return 3;
        case Property_type::vec4:  return 4;
        case Property_type::ivec4: return 4;
        default:                   return 1;
    }
}

template <typename V>
[[nodiscard]] auto vector_mixed_mask(const V& first, const V& other) -> uint32_t
{
    uint32_t mask = 0;
    for (int c = 0; c < V::length(); ++c) {
        if (!(first[c] == other[c])) {
            mask |= (1u << c);
        }
    }
    return mask;
}

[[nodiscard]] auto mixed_mask(const Property_value& first, const Property_value& other) -> uint32_t
{
    if (first.index() != other.index()) {
        return 1u;
    }
    switch (static_cast<Property_type>(first.index())) {
        case Property_type::vec2:  return vector_mixed_mask(std::get<glm::vec2 >(first), std::get<glm::vec2 >(other));
        case Property_type::vec3:  return vector_mixed_mask(std::get<glm::vec3 >(first), std::get<glm::vec3 >(other));
        case Property_type::vec4:  return vector_mixed_mask(std::get<glm::vec4 >(first), std::get<glm::vec4 >(other));
        case Property_type::ivec2: return vector_mixed_mask(std::get<glm::ivec2>(first), std::get<glm::ivec2>(other));
        case Property_type::ivec3: return vector_mixed_mask(std::get<glm::ivec3>(first), std::get<glm::ivec3>(other));
        case Property_type::ivec4: return vector_mixed_mask(std::get<glm::ivec4>(first), std::get<glm::ivec4>(other));
        default:                   return (first == other) ? 0u : 1u;
    }
}

// The value item i receives when the widget turned `original` (the first
// item's value) into `edited`: the components the user changed, over the
// item's own value for the rest - so adjusting one component of a mixed
// vector leaves the other components of every item alone.
template <typename V>
[[nodiscard]] auto merge_vector_components(const V& item, const V& original, const V& edited) -> V
{
    V result = item;
    for (int c = 0; c < V::length(); ++c) {
        if (!(edited[c] == original[c])) {
            result[c] = edited[c];
        }
    }
    return result;
}

[[nodiscard]] auto merge_components(const Property_value& item, const Property_value& original, const Property_value& edited) -> Property_value
{
    if ((item.index() != edited.index()) || (original.index() != edited.index())) {
        return edited;
    }
    switch (static_cast<Property_type>(edited.index())) {
        case Property_type::vec2:  return merge_vector_components(std::get<glm::vec2 >(item), std::get<glm::vec2 >(original), std::get<glm::vec2 >(edited));
        case Property_type::vec3:  return merge_vector_components(std::get<glm::vec3 >(item), std::get<glm::vec3 >(original), std::get<glm::vec3 >(edited));
        case Property_type::vec4:  return merge_vector_components(std::get<glm::vec4 >(item), std::get<glm::vec4 >(original), std::get<glm::vec4 >(edited));
        case Property_type::ivec2: return merge_vector_components(std::get<glm::ivec2>(item), std::get<glm::ivec2>(original), std::get<glm::ivec2>(edited));
        case Property_type::ivec3: return merge_vector_components(std::get<glm::ivec3>(item), std::get<glm::ivec3>(original), std::get<glm::ivec3>(edited));
        case Property_type::ivec4: return merge_vector_components(std::get<glm::ivec4>(item), std::get<glm::ivec4>(original), std::get<glm::ivec4>(edited));
        default:                   return edited;
    }
}

// What an array row shows (M6): how many values the array holds, then the
// first few of them in the D16 text form, so a long primvar stays one line.
[[nodiscard]] auto array_summary(const Property_value& value) -> std::string
{
    constexpr std::size_t max_shown = 4;
    const bool        is_float = (erhe::property::type_of(value) == Property_type::float_array);
    const std::size_t count    = is_float
        ? std::get<std::vector<float>>(value).size()
        : std::get<std::vector<int>>(value).size();
    std::string text = std::to_string(count) + ((count == 1) ? " value" : " values");
    if (count == 0) {
        return text;
    }
    const std::size_t shown = std::min(count, max_shown);
    const Property_value head = is_float
        ? Property_value{
            std::vector<float>{
                std::get<std::vector<float>>(value).begin(),
                std::get<std::vector<float>>(value).begin() + static_cast<std::ptrdiff_t>(shown)
            }
        }
        : Property_value{
            std::vector<int>{
                std::get<std::vector<int>>(value).begin(),
                std::get<std::vector<int>>(value).begin() + static_cast<std::ptrdiff_t>(shown)
            }
        };
    text += ": " + erhe::property::to_string(head);
    if (shown < count) {
        text += " ...";
    }
    return text;
}

constexpr const char* c_mixed_format = "mixed"; // a format without a conversion: the drag prints it verbatim
constexpr ImVec4      c_mixed_frame_color{0.45f, 0.35f, 0.15f, 0.6f};

// `count` drag widgets side by side, one per component (the layout of
// ImGui::DragScalarN); a mixed component shows "mixed" in a tinted
// frame in place of its value. Ctrl+click still opens the text input
// with the first item's value (ImGui substitutes the type's default
// print format for a format without a conversion).
[[nodiscard]] auto drag_components(
    const ImGuiDataType data_type,
    void* const         values,
    const int           count,
    const uint32_t      mixed,
    const float         speed,
    const void* const   min,
    const void* const   max,
    const char* const   format
) -> bool
{
    const std::size_t stride  = (data_type == ImGuiDataType_Float) ? sizeof(float) : sizeof(int);
    const float       spacing = ImGui::GetStyle().ItemInnerSpacing.x;
    const float       width   = std::max(1.0f, (ImGui::CalcItemWidth() - spacing * static_cast<float>(count - 1)) / static_cast<float>(count));
    bool changed = false;
    ImGui::BeginGroup(); // one item for the caller's IsItemActivated / IsItemDeactivatedAfterEdit, as DragScalarN
    ImGui::PushID("components");
    for (int c = 0; c < count; ++c) {
        if (c > 0) {
            ImGui::SameLine(0.0f, spacing);
        }
        const bool component_mixed = ((mixed & (1u << c)) != 0u);
        if (component_mixed) {
            ImGui::PushStyleColor(ImGuiCol_FrameBg, c_mixed_frame_color);
        }
        ImGui::PushID(c);
        ImGui::SetNextItemWidth(width);
        void* const value = static_cast<char*>(values) + (static_cast<std::size_t>(c) * stride);
        changed = ImGui::DragScalar("##", data_type, value, speed, min, max, component_mixed ? c_mixed_format : format) || changed;
        if (component_mixed && ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Mixed values across the selection; editing sets this component on every item");
        }
        ImGui::PopID();
        if (component_mixed) {
            ImGui::PopStyleColor();
        }
    }
    ImGui::PopID();
    ImGui::EndGroup();
    return changed;
}

void lower_ascii_into(const std::string_view text, std::string& out)
{
    out.clear();
    for (const char c : text) {
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
}

} // anonymous namespace

Dependency_property_rows::Dependency_property_rows(App_context& context)
    : m_context{context}
{
}

void Dependency_property_rows::add_rows(Property_editor& editor, const std::vector<std::shared_ptr<erhe::Item_base>>& items)
{
    std::vector<std::shared_ptr<erhe::Item_base>> snapshot;
    for (const std::shared_ptr<erhe::Item_base>& item : items) {
        if (item) {
            snapshot.push_back(item);
        }
    }
    m_items = std::make_shared<const std::vector<std::shared_ptr<erhe::Item_base>>>(std::move(snapshot));
    m_sub_object.reset();
    ERHE_DEFER( m_items.reset(); ); // the snapshot lives in the row lambdas, not in this object (scene-close leak class)
    if (m_items->empty()) {
        return;
    }
    draw_rows(editor);
}

void Dependency_property_rows::add_sub_object_rows(Property_editor& editor, const std::shared_ptr<erhe::Item_base>& item, const std::size_t sub_object)
{
    if (!item || (item->get_property_sub_object(sub_object) == nullptr)) {
        return;
    }
    m_items      = std::make_shared<const std::vector<std::shared_ptr<erhe::Item_base>>>(std::vector<std::shared_ptr<erhe::Item_base>>{item});
    m_sub_object = sub_object;
    ERHE_DEFER( m_items.reset(); );
    draw_rows(editor);
}

auto Dependency_property_rows::target(const std::size_t i) const -> erhe::property::Dependency_object*
{
    erhe::Item_base& item = *(*m_items)[i];
    return m_sub_object.has_value() ? item.get_property_sub_object(m_sub_object.value()) : &item;
}

void Dependency_property_rows::draw_rows(Property_editor& editor)
{
    // Properties common to every selected item's type, in the first item's
    // registration order.
    const erhe::property::Property_registry& registry = erhe::property::Property_registry::get();
    std::vector<const Dependency_property*> properties;
    registry.for_each_property_of_object(
        target(0)->get_property_owner_type(),
        [&properties](const Dependency_property& property) { properties.push_back(&property); }
    );
    for (std::size_t i = 1; i < m_items->size(); ++i) {
        const erhe::property::Owner_type object_type = target(i)->get_property_owner_type();
        std::erase_if(
            properties,
            [&registry, object_type](const Dependency_property* property) {
                return registry.find_for_object(object_type, property->get_name()) != property;
            }
        );
    }
    const erhe::property::Owner_type owner_type = target(0)->get_property_owner_type();
    if (!m_context.developer_mode) {
        std::erase_if(properties, [owner_type](const Dependency_property* property) { return property->get_metadata(owner_type).ui.developer_only; });
    }
    // Conditional rows (Property_ui::visible_when): shown while the
    // predicate holds for every selected item.
    std::erase_if(
        properties,
        [this, owner_type](const Dependency_property* property) {
            const Property_ui::Visible_when& visible_when = property->get_metadata(owner_type).ui.visible_when;
            if (!visible_when) {
                return false;
            }
            for (std::size_t i = 0; i < m_items->size(); ++i) {
                if (!visible_when(*target(i))) {
                    return true;
                }
            }
            return false;
        }
    );
    // Attached properties (R7): listed when the D12 listing rule
    // (is_extra_property_listed, which includes the holder-type check)
    // holds for every selected item; the first item's type seeds the
    // candidates, the per-item rule filters the rest.
    registry.for_each_attached_property_of(
        owner_type,
        [this, &properties, owner_type](const Dependency_property& property) {
            if (!m_context.developer_mode && property.get_metadata(owner_type).ui.developer_only) {
                return;
            }
            for (std::size_t i = 0; i < m_items->size(); ++i) {
                if (!is_extra_property_listed(*target(i), property)) {
                    return;
                }
            }
            properties.push_back(&property);
        }
    );
    // Secondary properties (D30, a content-library folder's category
    // properties): listed when every selected item holds a local value.
    registry.for_each_secondary_property(
        *target(0),
        [this, &properties, owner_type](const Dependency_property& property) {
            if (!m_context.developer_mode && property.get_metadata(owner_type).ui.developer_only) {
                return;
            }
            for (std::size_t i = 0; i < m_items->size(); ++i) {
                if (!is_extra_property_listed(*target(i), property)) {
                    return;
                }
            }
            properties.push_back(&property);
        }
    );
    // Ungrouped rows first, then each group in order of first appearance.
    for (const Dependency_property* property : properties) {
        if (property->get_metadata(owner_type).ui.group.empty()) {
            row(editor, *property);
        }
    }
    std::vector<std::string_view> groups_done;
    for (const Dependency_property* property : properties) {
        const std::string_view group = property->get_metadata(owner_type).ui.group;
        if (group.empty() || (std::find(groups_done.begin(), groups_done.end(), group) != groups_done.end())) {
            continue;
        }
        groups_done.push_back(group);
        editor.push_group(std::string{group}, ImGuiTreeNodeFlags_DefaultOpen);
        for (const Dependency_property* grouped : properties) {
            if (grouped->get_metadata(owner_type).ui.group == group) {
                row(editor, *grouped);
            }
        }
        editor.pop_group();
    }
    if (!m_sub_object.has_value()) { // a sub-object (D29) has no attached properties
        add_property_row(editor);
    }
}

void Dependency_property_rows::add_property_row(Property_editor& editor)
{
    editor.add_entry(
        "Add Property",
        [this, items = m_items]() {
            m_items = items; // this row's items, bound for this lambda only
            m_sub_object.reset();
            ERHE_DEFER( m_items.reset(); );
            ImGui::PushID(static_cast<int>(m_items->front()->get_id()));
            ERHE_DEFER( ImGui::PopID(); );

            // Candidates (R1, R5): the union over the items, sorted by owner
            // type so the picker can group them, registry order within.
            const Developer_mode developer_mode = m_context.developer_mode ? Developer_mode::shown : Developer_mode::hidden;
            m_add_candidates.clear();
            collect_addable_properties(*target(0), developer_mode, m_add_candidates);
            for (std::size_t i = 1; i < m_items->size(); ++i) {
                m_add_scratch.clear();
                collect_addable_properties(*target(i), developer_mode, m_add_scratch);
                for (const Dependency_property* candidate : m_add_scratch) {
                    if (std::find(m_add_candidates.begin(), m_add_candidates.end(), candidate) == m_add_candidates.end()) {
                        m_add_candidates.push_back(candidate);
                    }
                }
            }
            // A `Scope` offers the classes of the prims below it first: a
            // Materials scope lists `Material.*` above every other class
            // (doc/content-library-folders.md D8).
            m_add_preferred_owner_types.clear();
            if (m_items->size() == 1) {
                const std::shared_ptr<erhe::Scope> scope = std::dynamic_pointer_cast<erhe::Scope>(m_items->front());
                if (scope) {
                    const std::function<void(const erhe::Hierarchy&)> collect_owner_types =
                        [this, &collect_owner_types](const erhe::Hierarchy& prim) -> void {
                            for (const std::shared_ptr<erhe::Hierarchy>& child : prim.get_children()) {
                                const erhe::property::Owner_type owner_type = child->get_property_owner_type();
                                if (
                                    std::find(m_add_preferred_owner_types.begin(), m_add_preferred_owner_types.end(), owner_type) ==
                                    m_add_preferred_owner_types.end()
                                ) {
                                    m_add_preferred_owner_types.push_back(owner_type);
                                }
                                collect_owner_types(*child);
                            }
                        };
                    collect_owner_types(*scope);
                }
            }
            const std::vector<erhe::property::Owner_type>& preferred = m_add_preferred_owner_types;
            const auto rank = [&preferred](const erhe::property::Owner_type owner_type) -> int {
                return (std::find(preferred.begin(), preferred.end(), owner_type) != preferred.end()) ? 0 : 1;
            };
            std::sort(
                m_add_candidates.begin(), m_add_candidates.end(),
                [&rank](const Dependency_property* lhs, const Dependency_property* rhs) {
                    const int lhs_rank = rank(lhs->get_owner_type());
                    const int rhs_rank = rank(rhs->get_owner_type());
                    if (lhs_rank != rhs_rank) {
                        return lhs_rank < rhs_rank;
                    }
                    return (lhs->get_owner_type() != rhs->get_owner_type())
                        ? (lhs->get_owner_type() < rhs->get_owner_type())
                        : (lhs->get_index() < rhs->get_index());
                }
            );

            const bool sealed = m_items->front()->is_sealed(); // D24
            ImGui::BeginDisabled(sealed || m_add_candidates.empty());
            if (ImGui::Button("Add Property")) {
                m_add_filter.clear();
                m_add_focus_filter = true;
                ImGui::OpenPopup("add_property_popup");
            }
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                ImGui::SetTooltip(
                    "%s",
                    sealed                   ? "Sealed (lock_edit)" :
                    m_add_candidates.empty() ? "Every attached property is already listed" :
                                               "Add an attached property to this item"
                );
            }
            draw_add_property_popup();
        }
    );
}

void Dependency_property_rows::draw_add_property_popup()
{
    if (!ImGui::BeginPopup("add_property_popup")) {
        return;
    }
    ERHE_DEFER( ImGui::EndPopup(); );
    if (m_add_focus_filter) {
        ImGui::SetKeyboardFocusHere();
        m_add_focus_filter = false;
    }
    ImGui::SetNextItemWidth(260.0f);
    ImGui::InputTextWithHint("##add_property_filter", "Filter properties...", &m_add_filter);
    lower_ascii_into(m_add_filter, m_add_filter_lower);
    const bool filtering = !m_add_filter_lower.empty();

    if (m_add_candidates.empty()) {
        ImGui::TextDisabled("No properties to add");
        return;
    }
    const erhe::property::Property_registry& registry = erhe::property::Property_registry::get();
    erhe::property::Owner_type header_owner   = erhe::property::Owner_type{0};
    bool                       header_pending = true;
    for (const Dependency_property* candidate : m_add_candidates) {
        if (header_pending || (candidate->get_owner_type() != header_owner)) {
            header_owner   = candidate->get_owner_type();
            header_pending = true;
        }
        const Property_metadata& metadata = candidate->get_metadata(target(0)->get_property_owner_type());
        m_text_scratch = registry.qualified_name(*target(0), *candidate);
        if (!metadata.ui.label.empty()) {
            m_text_scratch += "  (";
            m_text_scratch += metadata.ui.label;
            m_text_scratch += ")";
        }
        if (filtering) {
            lower_ascii_into(m_text_scratch, m_add_label_lower);
            if (m_add_label_lower.find(m_add_filter_lower) == std::string::npos) {
                continue;
            }
        }
        if (header_pending) { // an owner group is shown only with a matching entry
            m_add_owner_scratch = registry.get_owner_name(header_owner);
            ImGui::SeparatorText(m_add_owner_scratch.c_str());
            header_pending = false;
        }
        if (ImGui::Selectable(m_text_scratch.c_str())) {
            queue_add(*candidate);
            ImGui::CloseCurrentPopup();
            return;
        }
        if (!metadata.ui.tooltip.empty() && ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%.*s", static_cast<int>(metadata.ui.tooltip.size()), metadata.ui.tooltip.data());
        }
    }
}

// The add (R4): the item's current effective value becomes its local
// value, so nothing changes but the layer and the row appears; undo
// clears it again. Items already holding a local value are skipped.
void Dependency_property_rows::queue_add(const Dependency_property& property)
{
    if (m_context.operation_stack == nullptr) {
        return;
    }
    Compound_operation::Parameters parameters;
    for (std::size_t i = 0; i < m_items->size(); ++i) {
        if (target(i)->has_local_value(property)) {
            continue;
        }
        parameters.operations.push_back(
            std::make_shared<Property_set_operation>(
                (*m_items)[i], m_sub_object, property, std::nullopt, erhe::property::Local_state{target(i)->get_value(property)}
            )
        );
    }
    if (parameters.operations.empty()) {
        return;
    }
    if (parameters.operations.size() == 1) {
        m_context.operation_stack->queue(parameters.operations.front());
        return;
    }
    m_context.operation_stack->queue(std::make_shared<Compound_operation>(std::move(parameters)));
}

void Dependency_property_rows::row(Property_editor& editor, const Dependency_property& property)
{
    const erhe::Item_base&                    first_item = *m_items->front();
    const erhe::property::Dependency_object&  first      = *target(0);
    const Property_metadata&                  metadata   = property.get_metadata(first.get_property_owner_type());

    // Label: Property_ui::label or the property name; "*" marks a local
    // value that differs from the default, "=" a property driven by an
    // expression (D22).
    const std::optional<std::string_view> expression = first.get_expression(property);
    const bool             differs_from_default = first.has_local_value(property) && !(first.get_value(property) == first.get_default_value(property));
    const std::string      qualified            = erhe::property::Property_registry::get().qualified_name(first, property);
    const std::string_view label_text           = metadata.ui.label.empty() ? std::string_view{qualified} : metadata.ui.label;
    std::string label = expression.has_value()
        ? std::string{"= "} + std::string{label_text}
        : differs_from_default ? std::string{"* "} + std::string{label_text} : std::string{label_text};

    std::string tooltip{metadata.ui.tooltip};
    if (!tooltip.empty()) {
        tooltip += "\n";
    }
    tooltip += "Source: ";
    tooltip += erhe::property::c_str(first.get_value_source(property));
    if (metadata.bridge.is_bound()) {
        tooltip += " (member-backed)";
    }
    if (first.is_coerced(property)) {
        tooltip += " (coerced)";
    }
    if (first.get_value_source(property) == Value_source::style) {
        tooltip += " (";
        tooltip += first.get_style()->get_reference_path();
        tooltip += ")";
    }
    if (first_item.is_sealed()) {
        tooltip += "\nSealed (lock_edit)";
    }
    if (expression.has_value()) {
        tooltip += "\nValue: ";
        tooltip += erhe::property::to_string(property, first.get_value(property));
        if (const std::string_view error = first.get_expression_error(property); !error.empty()) {
            tooltip += "\nExpression error: ";
            tooltip += error;
        }
    }
    if (!metadata.is_computed()) { // D26: a computed property has no default layer
        tooltip += "\nDefault: ";
        tooltip += erhe::property::to_string(property, first.get_default_value(property)); // D31: the inspected object's own default
    } else if (metadata.is_computed_writable()) {
        tooltip += "\nWrites: ";
        tooltip += metadata.compute_writes->get_name();
    }
    if (property.get_type() == Property_type::quat) {
        tooltip += "\nx y z w: ";
        tooltip += erhe::property::to_string(property, first.get_value(property));
    }

    const bool sealed = first_item.is_write_sealed(property); // D24: a sealed item's rows are read-only (lock_edit stays writable)
    // Tinted label (D12): one hue per value source, so a registered
    // property is told apart from a hand-written row and its layer is
    // read at a glance - the tooltip names the source in words.
    const uint32_t c_property_row_label_color = label_color(first.get_value_source(property), metadata.bridge.is_bound());
    editor.add_entry(
        std::move(label),
        [this, items = m_items, sub_object = m_sub_object, &property, &metadata, sealed]() {
            m_items      = items; // this row's items, bound for this lambda only
            m_sub_object = sub_object;
            ERHE_DEFER( m_items.reset(); );
            if (target(0) == nullptr) {
                return; // the sub-object is gone (primitive list rebuilt)
            }
            const bool inline_remove = !sealed && inline_remove_offered(property, metadata);
            if (inline_remove) { // leave room for the "x" after the widget
                ImGui::SetNextItemWidth(std::max(1.0f, ImGui::GetContentRegionAvail().x - ImGui::GetFrameHeight() - ImGui::GetStyle().ItemInnerSpacing.x));
            }
            if (const std::optional<std::string_view> text = target(0)->get_expression(property); text.has_value()) {
                draw_expression(property, text.value());
                context_menu(property, metadata);
                if (inline_remove) {
                    draw_inline_remove(property);
                }
                return;
            }
            // The first item's value seeds the widget; `mixed` marks the
            // components the items disagree on (shown as "mixed", no
            // preview value). A write applies only the components the
            // widget changed, over each item's own value.
            const Property_value original = target(0)->get_value(property);
            Property_value       value    = original;
            uint32_t             mixed    = 0u;
            for (std::size_t i = 1; i < m_items->size(); ++i) {
                mixed |= mixed_mask(original, target(i)->get_value(property));
            }
            const bool read_only = property.is_read_only() || sealed;
            ImGui::BeginDisabled(read_only);
            bool immediate = false;
            const bool changed = draw_widget(property, metadata, value, mixed, immediate);
            ImGui::EndDisabled();
            if (read_only) {
                return;
            }
            if (immediate) {
                if (changed) {
                    begin_edit(property);
                    queue_set(property, value);
                    m_edit_property = nullptr;
                }
            } else {
                if (ImGui::IsItemActivated()) {
                    begin_edit(property);
                }
                if (changed) {
                    if (m_edit_property != &property) {
                        begin_edit(property); // keyboard edits can change without a separate activation frame
                    }
                    for (std::size_t i = 0; i < m_items->size(); ++i) {
                        target(i)->set_value(property, merge_components(target(i)->get_value(property), original, value));
                    }
                }
                if (ImGui::IsItemDeactivatedAfterEdit()) {
                    end_edit(property);
                } else if ((m_edit_property == &property) && ImGui::IsItemDeactivated()) {
                    m_edit_property = nullptr; // activated, never edited
                }
            }
            context_menu(property, metadata);
            if (inline_remove) {
                draw_inline_remove(property);
            }
        },
        std::move(tooltip),
        c_property_row_label_color
    );
    // The composition origin (doc/usd-compatibility-plan.md X5) is appended
    // to the tooltip only while the row is hovered: it walks the item's
    // ancestors and formats strings, which no frame should pay per row. The
    // item is captured by weak reference so a hovered row cannot keep the
    // content of a closed scene alive. A sub-object row (D29) reads the
    // sub-object's own layers, which no file spells as a prim of its own, so
    // it gets no origin.
    if (m_sub_object.has_value()) {
        return;
    }
    editor.set_entry_tooltip_extra(
        [this, item = std::weak_ptr<erhe::Item_base>{m_items->front()}, &property]() -> std::string {
            const std::shared_ptr<erhe::Item_base> locked = item.lock();
            if (!locked) {
                return {};
            }
            return property_origin_tooltip(describe_property_origin(m_context, *locked.get(), property));
        }
    );
}

auto Dependency_property_rows::inline_remove_offered(const Dependency_property& property, const Property_metadata& metadata) const -> bool
{
    if (property.is_read_only()) {
        return false;
    }
    if (!property.is_attached()) {
        // A secondary property (D30) is listed because of its own value;
        // the "x" clears a local one. Its visible_when is never evaluated
        // on this object.
        return erhe::property::Property_registry::get().is_secondary_property(*target(0), property) && target(0)->has_local_value(property);
    }
    const Property_ui::Visible_when& visible_when = metadata.ui.visible_when;
    return !(visible_when && visible_when(*target(0)));
}

void Dependency_property_rows::draw_inline_remove(const Dependency_property& property)
{
    ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
    ImGui::PushID(&property);
    if (ImGui::Button("x", ImVec2{ImGui::GetFrameHeight(), ImGui::GetFrameHeight()})) {
        remove_property(property);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Remove this property from the item (undoable)");
    }
    ImGui::PopID();
}

void Dependency_property_rows::remove_property(const Dependency_property& property)
{
    reset_to_default(property);
}

auto Dependency_property_rows::draw_widget(
    const Dependency_property& property,
    const Property_metadata&   metadata,
    Property_value&            value,
    const uint32_t             mixed,
    bool&                      immediate
) -> bool
{
    const Property_ui& ui = metadata.ui;
    const float speed = ui.step.value_or(0.01f);
    const float min   = ui.min.value_or(0.0f);
    const float max   = ui.max.value_or(0.0f);
    const bool  has_range = ui.min.has_value() && ui.max.has_value();
    const bool  slider    = has_range && (ui.presentation == Property_ui::Presentation::slider);
    const bool  degrees   = (ui.presentation == Property_ui::Presentation::angle_degrees);
    const bool  color     = (ui.presentation == Property_ui::Presentation::color);
    const bool  any_mixed = (mixed != 0u);
    const int   imin      = static_cast<int>(min);
    const int   imax      = static_cast<int>(max);
    const int   ispeed    = static_cast<int>(std::max(speed, 1.0f));
    immediate = false;

    // One-component widgets show "mixed" in place of the value; vectors
    // go through drag_components, which marks each mixed component.
    if (any_mixed) {
        ImGui::PushStyleColor(ImGuiCol_FrameBg, c_mixed_frame_color);
    }
    ERHE_DEFER( if (any_mixed) { ImGui::PopStyleColor(); } );

    switch (property.get_type()) {
        case Property_type::boolean: {
            bool v = std::get<bool>(value);
            if (any_mixed) {
                ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, true);
            }
            const bool changed = ImGui::Checkbox("##", &v);
            if (any_mixed) {
                ImGui::PopItemFlag();
            }
            if (changed) {
                value = v;
            }
            return changed;
        }
        case Property_type::integer: {
            int v = std::get<int>(value);
            const char* const format = any_mixed ? c_mixed_format : "%d";
            const bool changed = slider
                ? ImGui::SliderInt("##", &v, imin, imax, format)
                : ImGui::DragInt("##", &v, std::max(speed, 1.0f), has_range ? imin : 0, has_range ? imax : 0, format);
            if (changed) {
                value = v;
            }
            return changed;
        }
        case Property_type::floating: {
            float v = std::get<float>(value);
            bool changed = false;
            if (degrees) {
                float d = glm::degrees(v);
                changed = ImGui::DragFloat("##", &d, ui.step.value_or(0.5f), has_range ? glm::degrees(min) : 0.0f, has_range ? glm::degrees(max) : 0.0f, any_mixed ? c_mixed_format : "%.2f°");
                v = glm::radians(d);
            } else if (slider && ui.logarithmic) {
                changed = ImGui::SliderFloat("##", &v, min, max, any_mixed ? c_mixed_format : "%.4f", ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_NoRoundToFormat);
            } else if (slider) {
                changed = ImGui::SliderFloat("##", &v, min, max, any_mixed ? c_mixed_format : "%.3f");
            } else {
                changed = ImGui::DragFloat("##", &v, speed, has_range ? min : 0.0f, has_range ? max : 0.0f, any_mixed ? c_mixed_format : "%.3f");
            }
            if (changed) {
                value = v;
            }
            return changed;
        }
        case Property_type::vec2: {
            glm::vec2 v = std::get<glm::vec2>(value);
            const bool changed = drag_components(ImGuiDataType_Float, &v.x, 2, mixed, speed, has_range ? &min : nullptr, has_range ? &max : nullptr, "%.3f");
            if (changed) {
                value = v;
            }
            return changed;
        }
        case Property_type::vec3: {
            glm::vec3 v = std::get<glm::vec3>(value);
            bool changed = false;
            if (color && !any_mixed) {
                changed = ImGui::ColorEdit3("##", &v.x, ImGuiColorEditFlags_Float);
            } else if (degrees) {
                glm::vec3 d = glm::degrees(v);
                changed = drag_components(ImGuiDataType_Float, &d.x, 3, mixed, ui.step.value_or(0.5f), nullptr, nullptr, "%.2f°");
                v = glm::radians(d);
            } else {
                // A mixed color edits as three components (the picker has no
                // per-channel mixed state).
                changed = drag_components(ImGuiDataType_Float, &v.x, 3, mixed, speed, has_range ? &min : nullptr, has_range ? &max : nullptr, "%.3f");
            }
            if (changed) {
                value = v;
            }
            return changed;
        }
        case Property_type::vec4: {
            glm::vec4 v = std::get<glm::vec4>(value);
            const bool changed = (color && !any_mixed)
                ? ImGui::ColorEdit4("##", &v.x, ImGuiColorEditFlags_Float)
                : drag_components(ImGuiDataType_Float, &v.x, 4, mixed, speed, has_range ? &min : nullptr, has_range ? &max : nullptr, "%.3f");
            if (changed) {
                value = v;
            }
            return changed;
        }
        case Property_type::quat: {
            // Edited as Euler degrees; the session keeps its own Euler
            // vector so a drag does not re-derive (and jitter) from the
            // quaternion every frame. Mixed as a whole (the Euler
            // components of different rotations are not comparable).
            const glm::quat q = std::get<glm::quat>(value);
            if (m_edit_property != &property) {
                m_edit_euler_degrees = glm::degrees(glm::eulerAngles(q));
            }
            glm::vec3 d = m_edit_euler_degrees;
            const bool changed = drag_components(ImGuiDataType_Float, &d.x, 3, any_mixed ? 0x7u : 0u, ui.step.value_or(0.5f), nullptr, nullptr, "%.2f°");
            if (changed) {
                m_edit_euler_degrees = d;
                value = glm::normalize(glm::quat{glm::radians(d)});
            }
            return changed;
        }
        case Property_type::string: {
            if (any_mixed && (m_edit_property != &property)) {
                m_text_scratch.clear();
            } else if (!any_mixed) {
                m_text_scratch = std::get<std::string>(value);
            }
            const bool changed = any_mixed
                ? ImGui::InputTextWithHint("##", c_mixed_format, &m_text_scratch)
                : ImGui::InputText("##", &m_text_scratch);
            if (changed) {
                value = m_text_scratch;
            }
            return changed;
        }
        case Property_type::object: {
            // D28: a drop target / picker for an item; commits on selection.
            immediate = true;
            std::shared_ptr<erhe::Item_base> current = any_mixed
                ? std::shared_ptr<erhe::Item_base>{}
                : std::dynamic_pointer_cast<erhe::Item_base>(std::get<erhe::property::Object_reference>(value).object);
            const uint64_t allowed_types = (ui.reference_item_types != 0) ? ui.reference_item_types : ~uint64_t{0};
            collect_reference_candidates(m_context, *m_items->front(), allowed_types, m_reference_candidates);
            Item_reference_options options;
            options.candidates                  = m_reference_candidates;
            options.show_clear_button           = ui.show_clear_button;
            if (any_mixed) {
                options.none_text = "(mixed)";
            }
            const bool changed = item_reference_imgui(m_context, "##", current, allowed_types, options);
            m_reference_candidates.clear(); // strong references must not outlive the draw
            if (changed) {
                value = erhe::property::Object_reference{std::move(current)};
            }
            return changed;
        }
        case Property_type::ivec2: {
            glm::ivec2 v = std::get<glm::ivec2>(value);
            const bool changed = drag_components(ImGuiDataType_S32, &v.x, 2, mixed, static_cast<float>(ispeed), has_range ? &imin : nullptr, has_range ? &imax : nullptr, "%d");
            if (changed) {
                value = v;
            }
            return changed;
        }
        case Property_type::ivec3: {
            glm::ivec3 v = std::get<glm::ivec3>(value);
            const bool changed = drag_components(ImGuiDataType_S32, &v.x, 3, mixed, static_cast<float>(ispeed), has_range ? &imin : nullptr, has_range ? &imax : nullptr, "%d");
            if (changed) {
                value = v;
            }
            return changed;
        }
        case Property_type::ivec4: {
            glm::ivec4 v = std::get<glm::ivec4>(value);
            const bool changed = drag_components(ImGuiDataType_S32, &v.x, 4, mixed, static_cast<float>(ispeed), has_range ? &imin : nullptr, has_range ? &imax : nullptr, "%d");
            if (changed) {
                value = v;
            }
            return changed;
        }
        case Property_type::double_floating: {
            // USD `double` (transforms, time codes): the float row's range
            // and speed metadata, at double precision.
            double       v    = std::get<double>(value);
            const double dmin = static_cast<double>(min);
            const double dmax = static_cast<double>(max);
            const char* const format = any_mixed ? c_mixed_format : "%.6f";
            const bool changed = slider
                ? ImGui::SliderScalar("##", ImGuiDataType_Double, &v, &dmin, &dmax, format)
                : ImGui::DragScalar("##", ImGuiDataType_Double, &v, speed, has_range ? &dmin : nullptr, has_range ? &dmax : nullptr, format);
            if (changed) {
                value = v;
            }
            return changed;
        }
        case Property_type::mat4: {
            // One DragFloat4 row per column, in glm storage order. Edited as
            // a whole value: a matrix mixed across the selection has no
            // meaningful per-component merge.
            glm::mat4 m = std::get<glm::mat4>(value);
            const uint32_t column_mixed = any_mixed ? 0xfu : 0u;
            bool changed = false;
            ImGui::BeginGroup();
            for (int column = 0; column < 4; ++column) {
                ImGui::PushID(column);
                changed = drag_components(ImGuiDataType_Float, &m[column].x, 4, column_mixed, speed, nullptr, nullptr, "%.3f") || changed;
                ImGui::PopID();
            }
            ImGui::EndGroup();
            if (changed) {
                value = m;
            }
            return changed;
        }
        case Property_type::asset_path: {
            // The path as text. An asset path is authored by an import or by
            // the file it came from; there is no file dialog here.
            if (any_mixed && (m_edit_property != &property)) {
                m_text_scratch.clear();
            } else if (!any_mixed) {
                m_text_scratch = std::get<erhe::property::Asset_path>(value).path;
            }
            const bool changed = any_mixed
                ? ImGui::InputTextWithHint("##", c_mixed_format, &m_text_scratch)
                : ImGui::InputText("##", &m_text_scratch);
            if (changed) {
                value = erhe::property::Asset_path{m_text_scratch};
            }
            return changed;
        }
        case Property_type::float_array:
        case Property_type::int_array: {
            // Read-only: how many values the array holds and the first few of
            // them. Per-element editing is not part of the row.
            const std::string text = any_mixed
                ? std::string{c_mixed_format}
                : array_summary(value);
            ImGui::TextUnformatted(text.c_str());
            return false;
        }
        case Property_type::enumeration: {
            immediate = true;
            const erhe::property::Enum_info* info = property.get_enum_info();
            const int32_t current = std::get<erhe::property::Enum_value>(value).value;
            const std::string_view current_label = any_mixed ? std::string_view{c_mixed_format} : (info != nullptr) ? info->label_for(current) : std::string_view{};
            bool changed = false;
            if (ImGui::BeginCombo("##", std::string{current_label}.c_str())) {
                if (info != nullptr) {
                    for (const erhe::property::Enum_entry& entry : info->get_entries()) {
                        const bool selected = !any_mixed && (entry.value == current);
                        if (ImGui::Selectable(std::string{entry.label}.c_str(), selected) && !selected) {
                            value   = erhe::property::Enum_value{entry.value};
                            changed = true;
                        }
                        if (selected) {
                            ImGui::SetItemDefaultFocus();
                        }
                    }
                }
                ImGui::EndCombo();
            }
            return changed;
        }
    }
    return false;
}

void Dependency_property_rows::draw_expression(const Dependency_property& property, const std::string_view text)
{
    const std::string_view error   = target(0)->get_expression_error(property);
    const bool             editing = (m_edit_property == &property);
    // The row mirrors the item's formula until the field is activated;
    // from then on the typed text lives in m_expression_scratch (rows of
    // other driven properties keep mirroring their own text).
    std::string  mirror{text};
    std::string& buffer = editing ? m_expression_scratch : mirror;
    if (!error.empty()) {
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4{0.55f, 0.15f, 0.15f, 0.6f});
    }
    ImGui::BeginDisabled(property.is_read_only());
    const bool entered = ImGui::InputText("##", &buffer, ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::EndDisabled();
    if (!error.empty()) {
        ImGui::PopStyleColor();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", error.empty() ? "Expression: {[object/]property[.x|.y|.z|.w]}, one formula per component separated by commas" : std::string{error}.c_str());
    }
    if (ImGui::IsItemActivated() && !editing) {
        begin_edit(property);
        m_expression_scratch = mirror;
        return;
    }
    if (!editing) {
        return;
    }
    if (entered || ImGui::IsItemDeactivatedAfterEdit()) {
        std::string compile_error;
        if (m_expression_scratch == text) {
            // unchanged
        } else if (!erhe::property::validate_expression_text(property, m_expression_scratch, compile_error)) {
            log_operations->warn("expression '{}' for '{}' rejected: {}", m_expression_scratch, property.get_name(), compile_error);
        } else {
            queue_set(property, erhe::property::Local_state{erhe::property::Expression_text{m_expression_scratch}});
        }
        m_edit_property = nullptr;
    } else if (ImGui::IsItemDeactivated()) {
        m_edit_property = nullptr; // escaped
    }
}

auto Dependency_property_rows::recorded_property(const Dependency_property& property) const -> const Dependency_property&
{
    const Property_metadata& metadata = property.get_metadata(target(0)->get_property_owner_type());
    return metadata.is_computed_writable() ? *metadata.compute_writes : property;
}

void Dependency_property_rows::begin_edit(const Dependency_property& property)
{
    const Dependency_property& recorded = recorded_property(property);
    m_edit_property = &property;
    m_edit_before.clear();
    for (std::size_t i = 0; i < m_items->size(); ++i) {
        m_edit_before.push_back(target(i)->read_local_state(recorded));
    }
}

void Dependency_property_rows::end_edit(const Dependency_property& property)
{
    if (m_edit_property != &property) {
        return;
    }
    // Live edits already wrote the local value (of the recorded property:
    // a writable computed row's setter wrote the stored property it
    // derives from, D26); the operation records the session's before /
    // after and re-applies after (idempotent) so the consequence hook runs
    // once per completed edit.
    const Dependency_property& recorded = recorded_property(property);
    if (m_items->size() == 1) {
        queue_set(recorded, target(0)->read_local_state(recorded));
    } else {
        Compound_operation::Parameters parameters;
        for (std::size_t i = 0; i < m_items->size(); ++i) {
            parameters.operations.push_back(
                std::make_shared<Property_set_operation>((*m_items)[i], m_sub_object, recorded, m_edit_before[i], target(i)->read_local_state(recorded))
            );
        }
        m_context.operation_stack->queue(std::make_shared<Compound_operation>(std::move(parameters)));
    }
    m_edit_property = nullptr;
}

// Queues after = `after` for every item, with the before values captured by
// begin_edit(). For a writable computed property (D26) `after` is a value
// of that property: it goes through the setter now, and the operation
// records the stored property the setter wrote.
void Dependency_property_rows::queue_set(const Dependency_property& property, const std::optional<erhe::property::Local_state>& after)
{
    if ((m_context.operation_stack == nullptr) || (m_edit_before.size() != m_items->size())) {
        return;
    }
    const Dependency_property& recorded = recorded_property(property);
    if (&recorded != &property) {
        const erhe::property::Property_value* value = after.has_value() ? std::get_if<erhe::property::Property_value>(&after.value()) : nullptr;
        if (value == nullptr) {
            return; // no local layer to clear, no expression can drive it
        }
        Compound_operation::Parameters parameters;
        for (std::size_t i = 0; i < m_items->size(); ++i) {
            if (target(i)->set_value(property, *value)) {
                parameters.operations.push_back(std::make_shared<Property_set_operation>((*m_items)[i], m_sub_object, recorded, m_edit_before[i], target(i)->read_local_state(recorded)));
            }
        }
        if (parameters.operations.size() == 1) {
            m_context.operation_stack->queue(parameters.operations.front());
        } else if (!parameters.operations.empty()) {
            m_context.operation_stack->queue(std::make_shared<Compound_operation>(std::move(parameters)));
        }
        return;
    }
    if (m_items->size() == 1) {
        m_context.operation_stack->queue(std::make_shared<Property_set_operation>(m_items->front(), m_sub_object, property, m_edit_before.front(), after));
        return;
    }
    Compound_operation::Parameters parameters;
    for (std::size_t i = 0; i < m_items->size(); ++i) {
        parameters.operations.push_back(std::make_shared<Property_set_operation>((*m_items)[i], m_sub_object, property, m_edit_before[i], after));
    }
    m_context.operation_stack->queue(std::make_shared<Compound_operation>(std::move(parameters)));
}

void Dependency_property_rows::context_menu(const Dependency_property& property, const Property_metadata& metadata)
{
    if (!ImGui::BeginPopupContextItem("property_row_context")) {
        return;
    }
    bool any_local = false;
    for (std::size_t i = 0; i < m_items->size(); ++i) {
        any_local = any_local || target(i)->has_local_value(property);
    }
    const bool writable = !property.is_read_only() && !m_items->front()->is_write_sealed(property); // D24
    if (ImGui::MenuItem("Reset to default", nullptr, false, any_local && writable)) {
        reset_to_default(property);
    }
    const bool removable = property.is_attached() || erhe::property::Property_registry::get().is_secondary_property(*target(0), property);
    if (removable && ImGui::MenuItem("Remove Property", nullptr, false, any_local && writable)) {
        remove_property(property);
    }
    const bool driven      = target(0)->get_expression(property).has_value();
    // A type with no components (string, object, mat4, asset path, array)
    // is the one Expression::compile refuses.
    const bool can_drive   = !driven && writable && !metadata.is_computed() && (erhe::property::Expression::component_count(property.get_type()) > 0);
    if (ImGui::MenuItem("Edit as expression", nullptr, false, can_drive)) {
        edit_as_expression(property);
    }
    if (ImGui::MenuItem("Remove expression", nullptr, false, driven && writable)) {
        remove_expression(property);
    }
    if (m_sub_object.has_value()) {
        ImGui::EndPopup(); // the bag entries below act on the item, not its sub-object
        return;
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Copy Properties", nullptr, false, (m_items->size() == 1) && (m_context.clipboard != nullptr))) {
        copy_properties();
    }
    const bool can_paste = (m_context.clipboard != nullptr) && m_context.clipboard->has_property_contents() && !m_items->front()->is_sealed();
    if (ImGui::MenuItem("Paste Properties", nullptr, false, can_paste)) {
        paste_properties();
    }
    // Style layer (D25): the clipboard bag as the selection's style, named
    // after the item it was copied from; local values stay on top of it.
    if (ImGui::MenuItem("Paste Properties as Style", nullptr, false, can_paste)) {
        paste_properties_as_style();
    }
    bool any_style = false;
    for (const std::shared_ptr<erhe::Item_base>& item : *m_items) {
        any_style = any_style || static_cast<bool>(item->get_style());
    }
    if (ImGui::MenuItem("Clear Style", nullptr, false, any_style && !m_items->front()->is_sealed())) {
        clear_style();
    }
    ImGui::EndPopup();
}

void Dependency_property_rows::reset_to_default(const Dependency_property& property)
{
    begin_edit(property);
    queue_set(property, std::nullopt);
    m_edit_property = nullptr;
}

// Starts a formula from the current value in formula form (components
// separated by commas, bool as 1 / 0, an enumeration as its integer), so the
// row turns into an editable expression that evaluates to the same value.
void Dependency_property_rows::edit_as_expression(const Dependency_property& property)
{
    const Property_value value = target(0)->get_value(property);
    std::string text;
    const auto number = [](const float f) { return erhe::property::to_string(Property_value{f}); };
    switch (property.get_type()) {
        case Property_type::boolean:     text = std::get<bool>(value) ? "1" : "0"; break;
        case Property_type::integer:     text = std::to_string(std::get<int>(value)); break;
        case Property_type::floating:    text = number(std::get<float>(value)); break;
        case Property_type::vec2:        { const glm::vec2 v = std::get<glm::vec2>(value); text = number(v.x) + ", " + number(v.y); break; }
        case Property_type::vec3:        { const glm::vec3 v = std::get<glm::vec3>(value); text = number(v.x) + ", " + number(v.y) + ", " + number(v.z); break; }
        case Property_type::vec4:        { const glm::vec4 v = std::get<glm::vec4>(value); text = number(v.x) + ", " + number(v.y) + ", " + number(v.z) + ", " + number(v.w); break; }
        case Property_type::quat:        { const glm::quat q = std::get<glm::quat>(value); text = number(q.x) + ", " + number(q.y) + ", " + number(q.z) + ", " + number(q.w); break; }
        case Property_type::enumeration: text = std::to_string(std::get<erhe::property::Enum_value>(value).value); break;
        case Property_type::ivec2:       { const glm::ivec2 v = std::get<glm::ivec2>(value); text = std::to_string(v.x) + ", " + std::to_string(v.y); break; }
        case Property_type::ivec3:       { const glm::ivec3 v = std::get<glm::ivec3>(value); text = std::to_string(v.x) + ", " + std::to_string(v.y) + ", " + std::to_string(v.z); break; }
        case Property_type::ivec4:       { const glm::ivec4 v = std::get<glm::ivec4>(value); text = std::to_string(v.x) + ", " + std::to_string(v.y) + ", " + std::to_string(v.z) + ", " + std::to_string(v.w); break; }
        case Property_type::double_floating: text = erhe::property::to_string(Property_value{std::get<double>(value)}); break;
        case Property_type::string:      return;
        case Property_type::object:      return;
        case Property_type::mat4:        return; // 16 components: not an expression target
        case Property_type::asset_path:  return;
        case Property_type::float_array: return; // any component count: not an expression target
        case Property_type::int_array:   return;
    }
    begin_edit(property);
    queue_set(property, erhe::property::Local_state{erhe::property::Expression_text{std::move(text)}});
    m_edit_property = nullptr;
}

// Bakes the current result as the local value.
void Dependency_property_rows::remove_expression(const Dependency_property& property)
{
    begin_edit(property);
    queue_set(property, erhe::property::Local_state{target(0)->get_value(property)});
    m_edit_property = nullptr;
}

void Dependency_property_rows::copy_properties()
{
    if ((m_context.clipboard == nullptr) || (m_items->size() != 1)) {
        return;
    }
    m_context.clipboard->set_property_contents(erhe::property::Property_set::read_local_values(*m_items->front()), m_items->front()->get_name());
}

void Dependency_property_rows::paste_properties_as_style()
{
    if ((m_context.clipboard == nullptr) || !m_context.clipboard->has_property_contents()) {
        return;
    }
    const std::shared_ptr<Compound_operation> compound = make_style_from_values(
        m_context,
        *m_items,
        m_context.clipboard->get_property_contents(),
        m_context.clipboard->get_property_contents_name()
    );
    if (compound) {
        m_context.operation_stack->queue(compound);
    }
}

void Dependency_property_rows::clear_style()
{
    Compound_operation::Parameters parameters;
    for (const std::shared_ptr<erhe::Item_base>& item : *m_items) {
        if (item->is_sealed() || !item->get_style()) {
            continue;
        }
        parameters.operations.push_back(std::make_shared<Style_set_operation>(item, item->get_style(), nullptr));
    }
    if (parameters.operations.empty()) {
        return;
    }
    m_context.operation_stack->queue(std::make_shared<Compound_operation>(std::move(parameters)));
}

void Dependency_property_rows::paste_properties()
{
    if ((m_context.clipboard == nullptr) || (m_context.operation_stack == nullptr)) {
        return;
    }
    const erhe::property::Property_set& source = m_context.clipboard->get_property_contents();
    const erhe::property::Property_registry& registry = erhe::property::Property_registry::get();
    Compound_operation::Parameters parameters;
    for (const std::shared_ptr<erhe::Item_base>& item : *m_items) {
        if (item->is_sealed()) {
            continue; // D24
        }
        // Only the properties this item's type has (by identity, not name).
        erhe::property::Property_set filtered;
        for (const erhe::property::Property_set::Entry& entry : source.entries()) {
            if (entry.property->is_read_only()) {
                continue;
            }
            if (registry.find_for_object(item->get_property_owner_type(), registry.qualified_name(*entry.property)) == entry.property) {
                filtered.set(*entry.property, entry.value);
            }
        }
        if (!filtered.empty()) {
            parameters.operations.push_back(
                std::make_shared<Property_set_apply_operation>(std::vector<std::shared_ptr<erhe::Item_base>>{item}, std::move(filtered))
            );
        }
    }
    if (parameters.operations.empty()) {
        return;
    }
    if (parameters.operations.size() == 1) {
        m_context.operation_stack->queue(parameters.operations.front());
    } else {
        m_context.operation_stack->queue(std::make_shared<Compound_operation>(std::move(parameters)));
    }
}

}
