#include "windows/item_reference.hpp"

#include "app_context.hpp"
#include "content_library/content_library.hpp"
#include "graphics/icon_set.hpp"
#include "tools/selection_tool.hpp"

#include "erhe_imgui/imgui_helpers.hpp"
#include "erhe_imgui/imgui_item_recorder.hpp"

#include <imgui/imgui.h>

#include <algorithm>

namespace editor {

namespace {

// Drag sources (item tree, content library, this widget) set the payload type to the item's leaf
// class name (erhe::Item_type::label() of its class) and the data to an erhe::Item_base*. A payload
// whose type names an item class is read; the item is accepted when one of its type bits is in
// allowed_types (a base-type bit such as texture_reference included) and it is shown in the UI.
[[nodiscard]] auto is_shown(const App_context& context, const erhe::Item_base& item) -> bool
{
    return item.is_shown_in_ui() || (context.developer_mode && ((item.get_flag_bits() & Editor_item_flags::show_in_developer_ui) != 0));
}

[[nodiscard]] auto try_accept_item_payload(App_context& context, const uint64_t allowed_types) -> std::shared_ptr<erhe::Item_base>
{
    const ImGuiPayload* const payload = ImGui::GetDragDropPayload();
    if ((payload == nullptr) || (payload->Data == nullptr) || (payload->DataSize != sizeof(erhe::Item_base*))) {
        return {};
    }
    bool names_item_class = false;
    for (uint64_t i = 1; i < erhe::Item_type::index_count; ++i) {
        const char* const type_label = erhe::Item_type::label(i);
        if ((type_label != nullptr) && payload->IsDataType(type_label)) {
            names_item_class = true;
            break;
        }
    }
    if (!names_item_class) {
        return {};
    }
    erhe::Item_base* const raw = *static_cast<erhe::Item_base* const*>(payload->Data);
    if ((raw == nullptr) || ((raw->get_type() & allowed_types) == 0) || !is_shown(context, *raw)) {
        return {};
    }
    if (ImGui::AcceptDragDropPayload(payload->DataType) == nullptr) {
        return {}; // hovered, not yet released
    }
    return raw->shared_from_this();
}

} // anonymous namespace

auto item_reference_imgui(
    App_context&                      context,
    const char*                       label,
    std::shared_ptr<erhe::Item_base>& io_value,
    const uint64_t                    allowed_types,
    const Item_reference_options&     options
) -> bool
{
    bool changed = false;

    ImGui::PushID(label);

    const bool  have_value   = static_cast<bool>(io_value);
    const bool  have_picker  = !options.candidates.empty();
    const bool  have_select  = options.show_select_button && have_value;
    const bool  have_clear   = options.show_clear_button  && have_value;
    const float spacing      = ImGui::GetStyle().ItemSpacing.x;
    const float button_size  = ImGui::GetFrameHeight();
    const int   trailing      = (have_picker ? 1 : 0) + (have_select ? 1 : 0) + (have_clear ? 1 : 0);

    // Icon (decorative; drawn before the value button which is the drag source / drop target).
    if (have_value) {
        context.icon_set->item_icon(io_value, 1.0f); // draws the icon (followed by SameLine when the item has one)
    }

    // Value button: drag source for the referenced item, drop target for compatible items.
    const float reserved = static_cast<float>(trailing) * (button_size + spacing);
    const float value_w  = std::max(ImGui::GetContentRegionAvail().x - reserved, 1.0f);
    ImGui::Button(have_value ? io_value->get_name().c_str() : options.none_text, ImVec2{value_w, 0.0f});

    if (have_value && is_shown(context, *io_value) && erhe::imgui::begin_drag_drop_source(ImGuiDragDropFlags_SourceAllowNullID)) {
        erhe::Item_base* const raw = io_value.get();
        ImGui::SetDragDropPayload(io_value->get_type_name().data(), &raw, sizeof(raw));
        context.icon_set->item_icon(io_value, 1.0f);
        ImGui::TextUnformatted(io_value->get_name().c_str());
        ImGui::EndDragDropSource();
    }

    // No ImGuiDragDropFlags_AcceptNoDrawDefaultRect here: that lets ImGui draw the standard
    // drop-target highlight rectangle while a compatible payload hovers (GitHub issue #231).
    if (ImGui::BeginDragDropTarget()) {
        const std::shared_ptr<erhe::Item_base> dropped = try_accept_item_payload(context, allowed_types);
        if (dropped && (dropped != io_value)) {
            io_value = dropped;
            changed  = true;
        }
        ImGui::EndDragDropTarget();
    }

    // Picker popup (optional).
    if (have_picker) {
        ImGui::SameLine();
        if (ImGui::ArrowButton("##pick", ImGuiDir_Down)) {
            ImGui::OpenPopup("##pick_popup");
        }
        erhe::imgui::set_item_debug_role("pick");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Pick from list");
        }
        if (ImGui::BeginPopup("##pick_popup")) {
            for (const std::shared_ptr<erhe::Item_base>& candidate : options.candidates) {
                if (!candidate || ((candidate->get_type() & allowed_types) == 0)) {
                    continue;
                }
                const bool is_current = (candidate == io_value);
                // Scope by item pointer so candidates sharing a name get distinct ImGui ids.
                ImGui::PushID(static_cast<const void*>(candidate.get()));
                context.icon_set->item_icon(candidate, 1.0f); // draws the icon (followed by SameLine when the item has one)
                if (ImGui::Selectable(candidate->get_name().c_str(), is_current)) {
                    io_value = candidate;
                    changed  = true;
                }
                if (is_current) {
                    ImGui::SetItemDefaultFocus();
                }
                ImGui::PopID();
            }
            ImGui::EndPopup();
        }
    }

    // Add-to-selection button (so the referenced item's properties show in the Properties window).
    if (have_select) {
        ImGui::SameLine();
        if (ImGui::Button("+", ImVec2{button_size, button_size})) {
            context.selection->add_to_selection(io_value);
        }
        erhe::imgui::set_item_debug_role("select");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Add to selection");
        }
    }

    // Clear button.
    if (have_clear) {
        ImGui::SameLine();
        if (ImGui::Button("x", ImVec2{button_size, button_size})) {
            io_value.reset();
            changed = true;
        }
        erhe::imgui::set_item_debug_role("clear");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Clear reference");
        }
    }

    ImGui::PopID();

    return changed;
}

} // namespace editor
