#include "windows/item_reference.hpp"

#include "app_context.hpp"
#include "content_library/content_library.hpp"
#include "graphics/icon_set.hpp"
#include "tools/selection_tool.hpp"

#include <imgui/imgui.h>

#include <algorithm>

namespace editor {

namespace {

// Peek the drag-drop payload and, if it carries an erhe::Item_base* of an allowed type, return it.
// Drag sources (item tree, content library) set the payload type to the item's leaf class name and
// the data to an erhe::Item_base* (see windows/item_tree_window.cpp). Those class names match
// erhe::Item_type::c_bit_labels[], so each set bit of allowed_types maps to one acceptable payload
// type string. Note: a mask using a base-type bit (e.g. node_attachment) does not match a leaf
// payload string (e.g. "Mesh"); pass the leaf type bits whose class-name labels you want to accept.
[[nodiscard]] auto try_accept_item_payload(
    const uint64_t                      allowed_types,
    const bool                          accept_content_library_node
) -> std::shared_ptr<erhe::Item_base>
{
    const auto read_item = [](const ImGuiPayload* payload) -> erhe::Item_base* {
        if ((payload == nullptr) || (payload->Data == nullptr) || (payload->DataSize != sizeof(erhe::Item_base*))) {
            return nullptr;
        }
        return *static_cast<erhe::Item_base**>(payload->Data);
    };

    for (uint64_t i = 1; i < erhe::Item_type::count; ++i) {
        const uint64_t bit = (uint64_t{1} << i);
        if ((allowed_types & bit) == 0) {
            continue;
        }
        erhe::Item_base* const raw = read_item(ImGui::AcceptDragDropPayload(erhe::Item_type::c_bit_labels[i]));
        if ((raw != nullptr) && ((raw->get_type() & allowed_types) != 0)) {
            return raw->shared_from_this();
        }
    }

    if (accept_content_library_node) {
        erhe::Item_base* const raw = read_item(ImGui::AcceptDragDropPayload(Content_library_node::static_type_name.data()));
        Content_library_node* const node = dynamic_cast<Content_library_node*>(raw);
        if ((node != nullptr) && node->item && ((node->item->get_type() & allowed_types) != 0)) {
            return node->item;
        }
    }

    return {};
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

    if (have_value && ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
        erhe::Item_base* const raw = io_value.get();
        ImGui::SetDragDropPayload(io_value->get_type_name().data(), &raw, sizeof(raw));
        context.icon_set->item_icon(io_value, 1.0f);
        ImGui::TextUnformatted(io_value->get_name().c_str());
        ImGui::EndDragDropSource();
    }

    // No ImGuiDragDropFlags_AcceptNoDrawDefaultRect here: that lets ImGui draw the standard
    // drop-target highlight rectangle while a compatible payload hovers (GitHub issue #231).
    if (ImGui::BeginDragDropTarget()) {
        const std::shared_ptr<erhe::Item_base> dropped = try_accept_item_payload(allowed_types, options.accept_content_library_node);
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
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Clear reference");
        }
    }

    ImGui::PopID();

    return changed;
}

} // namespace editor
