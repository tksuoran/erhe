// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "windows/item_tree_window.hpp"
#include "windows/inventory_slot_payload.hpp"

#include "app_context.hpp"
#include "app_message_bus.hpp"
#include "app_scenes.hpp"
#include "app_settings.hpp"
#include "brushes/brush.hpp"
#include "editor_log.hpp"
#include "graphics/icon_set.hpp"
#include "graphics/thumbnails.hpp"
#include "operations/compound_operation.hpp"
#include "operations/item_insert_remove_operation.hpp"
#include "operations/item_parent_change_operation.hpp"
#include "operations/item_reposition_in_parent_operation.hpp"
#include "operations/operation_stack.hpp"
#include "preview/brush_preview.hpp"
#include "scene/scene_root.hpp"
#include "tools/clipboard.hpp"
#include "tools/selection_tool.hpp"
#include "tools/tool.hpp"

#include "erhe_defer/defer.hpp"
#include "erhe_imgui/imgui_windows.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_scene/light.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_utility/bit_helpers.hpp"

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>

#include <algorithm>
#include <limits>

#define ICON_MDI_FILTER                                   "\xf3\xb0\x88\xb2" // U+F0232

namespace editor {

using Light_type = erhe::scene::Light_type;

Item_tree::Item_tree(App_context& context)
    : m_context{context}
    , m_filter{
        .require_all_bits_set           = 0,
        .require_at_least_one_bit_set   = erhe::Item_flags::show_in_ui,
        .require_all_bits_clear         = 0, //erhe::Item_flags::tool | erhe::Item_flags::brush,
        .require_at_least_one_bit_clear = 0
    }
{
}

void Item_tree::set_root(const std::shared_ptr<erhe::Hierarchy>& root)
{
    m_root = root;
    m_flat_rows_dirty = true;
}

void Item_tree::set_item_filter(const erhe::Item_filter& filter)
{
    m_filter = filter;
    m_flat_rows_dirty = true;
}

void Item_tree::clear_cached_rows()
{
    m_flat_rows.clear();
    m_flat_rows_dirty = true;
}

void Item_tree::set_item_callback(std::function<bool(const std::shared_ptr<erhe::Item_base>&)> fun)
{
    m_item_callback = fun;
}

void Item_tree::set_hover_callback(std::function<void()> fun)
{
    m_hover_callback = fun;
}

void Item_tree::add_item_context_menu_callback(Context_menu_callback fun)
{
    m_item_context_menu_callbacks.push_back(std::move(fun));
}

void Item_tree::clear_selection()
{
    SPDLOG_LOGGER_TRACE(log_tree, "clear_selection()");

    m_context.selection->clear_selection();
}

void Item_tree::recursive_add_to_selection(const std::shared_ptr<erhe::Item_base>& item)
{
    m_context.selection->add_to_selection(item);
    auto hierarchy = std::dynamic_pointer_cast<erhe::Hierarchy>(item);
    if (!hierarchy) {
        return;
    }

    for (const auto& child : hierarchy->get_children()) {
        recursive_add_to_selection(child);
    }
}

void Item_tree::select_all()
{
    SPDLOG_LOGGER_TRACE(log_tree, "select_all()");

    m_context.selection->clear_selection();
    const auto& scene_roots = m_context.app_scenes->get_scene_roots();
    for (const auto& scene_root : scene_roots) {
        const auto& scene = scene_root->get_scene();
        for (const auto& node : scene.get_root_node()->get_children()) {
            recursive_add_to_selection(node);
        }
    }
}

template <typename T, typename U>
[[nodiscard]] auto is_in(const T& item, const std::vector<U>& items) -> bool
{
    return std::find(items.begin(), items.end(), item) != items.end();
}

void Item_tree::move_selection(const std::shared_ptr<erhe::Item_base>& target_node, erhe::Item_base* payload_item, const Placement placement)
{
    log_tree->trace(
        "move_selection(anchor = {}, {})",
        target_node ? target_node->get_name() : "(empty)",
        (placement == Placement::Before_anchor)
            ? "Before_anchor"
            : "After_anchor"
    );

    Compound_operation::Parameters compound_parameters;
    const auto& selection = m_context.selection->get_selected_items();
    const std::shared_ptr<erhe::Item_base> drag_item = payload_item->shared_from_this();

    std::shared_ptr<erhe::Item_base> anchor = target_node;
    if (is_in(drag_item, selection)) {
        // Dragging node which is part of the selection.
        // In this case we apply reposition to whole selection.
        if (placement == Placement::Before_anchor) {
            for (const auto& item : selection) {
                const auto& node = std::dynamic_pointer_cast<erhe::scene::Node>(item);
                if (node) {
                    reposition(compound_parameters, anchor, node, placement, Selection_usage::Selection_used);
                }
            }
        } else { // if (placement == Placement::After_anchor)
            for (auto i = selection.rbegin(), end = selection.rend(); i < end; ++i) {
                const auto& item = *i;
                const auto& node = std::dynamic_pointer_cast<erhe::scene::Node>(item);
                if (node) {
                    reposition(compound_parameters, anchor, node, placement, Selection_usage::Selection_used);
                }
            }
        }
    } else if (compound_parameters.operations.empty()) {
        // Dragging a single node which is not part of the selection.
        // In this case we ignore selection and apply operation only to dragged node.
        const auto& drag_node = std::dynamic_pointer_cast<erhe::scene::Node>(drag_item);
        if (drag_node) {
            reposition(compound_parameters, anchor, drag_node, placement, Selection_usage::Selection_ignored);
        }
    }

    if (!compound_parameters.operations.empty()) {
        m_operation = std::make_shared<Compound_operation>(std::move(compound_parameters));
    }
}

namespace {

[[nodiscard]] auto get_ancestor_in(const std::shared_ptr<erhe::Item_base>& item, const std::vector<std::shared_ptr<erhe::Item_base>>& selection) -> std::shared_ptr<erhe::Item_base>
{
    const auto hierarchy = std::dynamic_pointer_cast<erhe::Hierarchy>(item);
    if (!hierarchy) {
        return {};
    }

    const auto& parent = hierarchy->get_parent().lock();
    if (parent) {
        if (is_in(parent, selection)) {
            return parent;
        }
        return get_ancestor_in(parent, selection);
    } else {
        return {};
    }
}

} // anonymous namespace

void Item_tree::reposition(
    Compound_operation::Parameters&         compound_parameters,
    const std::shared_ptr<erhe::Item_base>& anchor,
    const std::shared_ptr<erhe::Item_base>& item,
    const Placement                         placement,
    const Selection_usage                   selection_usage
)
{
    SPDLOG_LOGGER_TRACE(
        log_tree,
        "reposition(anchor_node = {}, node = {}, placement = {})",
        anchor ? anchor->get_name() : "(empty)",
        item ? item->get_name() : "(empty)",
        (placement == Placement::Before_anchor)
            ? "Before_anchor"
            : "After_anchor"
    );

    if (!item) {
        SPDLOG_LOGGER_WARN(log_tree, "Bad empty item");
        return;
    }

    if (!anchor) {
        SPDLOG_LOGGER_WARN(log_tree, "Bad empty anchor");
        return;
    }

    // Nodes cannot be attached to themselves
    if (item == anchor) {
        return;
    }

    const auto hierarchy        = std::dynamic_pointer_cast<erhe::Hierarchy>(item);
    const auto anchor_hierarchy = std::dynamic_pointer_cast<erhe::Hierarchy>(anchor);
    if (!hierarchy || !anchor_hierarchy) {
        return;
    }

    // Ancestors cannot be attached to descendants
    if (anchor_hierarchy->is_ancestor(hierarchy.get())) {
        SPDLOG_LOGGER_WARN(log_tree, "Ancestors cannot be moved as child of descendant");
        return;
    }

    if (selection_usage == Selection_usage::Selection_used) {
        const auto& selection = m_context.selection->get_selected_items();

        // Ignore nodes if their ancestors is in selection
        const auto ancestor_in_selection = get_ancestor_in(item, selection);
        if (ancestor_in_selection) {
            SPDLOG_LOGGER_TRACE(
                log_tree,
                "Ignoring node {} because ancestor {} is in selection",
                item->get_name(),
                ancestor_in_selection->get_name()
            );
            return;
        }
    }

    if (anchor_hierarchy->get_parent().lock() != hierarchy->get_parent().lock()) {
        compound_parameters.operations.push_back(
            std::make_shared<Item_parent_change_operation>(
                anchor_hierarchy->get_parent().lock(),
                hierarchy,
                (placement == Placement::Before_anchor) ? anchor_hierarchy : std::shared_ptr<erhe::Hierarchy>{},
                (placement == Placement::After_anchor ) ? anchor_hierarchy : std::shared_ptr<erhe::Hierarchy>{}
            )
        );
        return;
    }

    compound_parameters.operations.push_back(
        std::make_shared<Item_reposition_in_parent_operation>(
            hierarchy,
            (placement == Placement::Before_anchor) ? anchor_hierarchy : std::shared_ptr<erhe::Hierarchy>{},
            (placement == Placement::After_anchor ) ? anchor_hierarchy : std::shared_ptr<erhe::Hierarchy>{}
        )
    );
}

void Item_tree::try_add_to_attach(
    Compound_operation::Parameters&         compound_parameters,
    const std::shared_ptr<erhe::Item_base>& target,
    const std::shared_ptr<erhe::Item_base>& item,
    const Selection_usage                   selection_usage
)
{
    SPDLOG_LOGGER_TRACE(
        log_tree,
        "try_add_to_attach(target = {}, item = {})",
        target ? target->get_name() : "(empty)",
        item   ? item->get_name()   : "(empty)"
    );

    if (!item) {
        SPDLOG_LOGGER_WARN(log_tree, "Bad empty item");
        return;
    }

    if (!target) {
        SPDLOG_LOGGER_WARN(log_tree, "Bad empty target");
        return;
    }

    // Nodes cannot be attached to themselves
    if (item == target) {
        SPDLOG_LOGGER_WARN(log_tree, "Nodes cannot be moved as child of themselves");
        return;
    }

    const auto hierarchy        = std::dynamic_pointer_cast<erhe::Hierarchy>(item);
    const auto target_hierarchy = std::dynamic_pointer_cast<erhe::Hierarchy>(target);
    if (!hierarchy || !target_hierarchy) {
        return;
    }

    // Ancestors cannot be attached to descendants
    if (target_hierarchy->is_ancestor(hierarchy.get())) {
        SPDLOG_LOGGER_WARN(log_tree, "Ancestors cannot be moved to child of descendant");
        return;
    }

    if (selection_usage == Selection_usage::Selection_used) {
        const auto& selection = m_context.selection->get_selected_items();

        // Ignore item if their ancestors is in selection
        const auto ancestor_in_selection = get_ancestor_in(item, selection);
        if (ancestor_in_selection) {
            SPDLOG_LOGGER_TRACE(
                log_tree,
                "Ignoring item '{}' because ancestor '{}' is in selection",
                item->describe(),
                ancestor_in_selection->describe()
            );
            return;
        }
    }

    compound_parameters.operations.push_back(
        std::make_shared<Item_parent_change_operation>(
            target_hierarchy,
            hierarchy,
            std::shared_ptr<erhe::Hierarchy>{},
            std::shared_ptr<erhe::Hierarchy>{}
        )
    );
}

void Item_tree::attach_selection_to(const std::shared_ptr<erhe::Item_base>& target, erhe::Item_base* payload_item)
{
    SPDLOG_LOGGER_TRACE(log_tree, "attach_selection_to()");

    //// log_tools->trace(
    ////     "attach_selection_to(target_node = {}, payload_id = {})",
    ////     target_node->get_name(),
    ////     payload_id
    //// );
    Compound_operation::Parameters compound_parameters;
    const auto& selection = m_context.selection->get_selected_items();
    const std::shared_ptr<erhe::Item_base> drag_item = payload_item->shared_from_this();

    if (is_in(drag_item, selection)) {
        for (const auto& item : selection) {
            try_add_to_attach(compound_parameters, target, item, Selection_usage::Selection_used);
        }
    } else if (compound_parameters.operations.empty()) {
        try_add_to_attach(compound_parameters, target, drag_item, Selection_usage::Selection_ignored);
    }

    if (!compound_parameters.operations.empty()) {
        m_operation = std::make_shared<Compound_operation>(std::move(compound_parameters));
    }
}

void Item_tree::drag_and_drop_source(const std::shared_ptr<erhe::Item_base>& item)
{
    ERHE_PROFILE_SCOPE("drag_and_drop_source"); // named zone: no per-row callstack capture

    // log_tree_frame->trace("DnD source: '{}'", item->describe());

    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
        erhe::Item_base* item_raw = item.get();
        ImGui::SetDragDropPayload(item->get_type_name().data(), &item_raw, sizeof(item_raw));

        const auto& selection = m_context.selection->get_selected_items();
        if (is_in(item, selection)) {
            for (const auto& selection_item : selection) {
                item_icon_and_text(selection_item);
            }
        } else {
            item_icon_and_text(item);
        }
        ImGui::EndDragDropSource();
    }
}

namespace {

void drag_and_drop_rectangle_preview(const ImRect rect)
{
    const auto* g       = ImGui::GetCurrentContext();
    const auto* window  = g->CurrentWindow;
    const auto& payload = g->DragDropPayload;

    if (payload.Preview) {
        window->DrawList->AddRect(
            rect.Min - ImVec2{0.0f, 3.5f},
            rect.Max + ImVec2{0.0f, 3.5f},
            ImGui::GetColorU32(ImGuiCol_DragDropTarget),
            0.0f,
            0,
            2.0f
        );
    }
}

void drag_and_drop_gradient_preview(
    const float x0,
    const float x1,
    const float y0,
    const float y1,
    const ImU32 top,
    const ImU32 bottom
)
{
    const auto* g       = ImGui::GetCurrentContext();
    const auto* window  = g->CurrentWindow;
    const auto& payload = g->DragDropPayload;

    if (payload.Preview) {
        window->DrawList->AddRectFilledMultiColor(
            ImVec2{x0, y0},
            ImVec2{x1, y1},
            top,
            top,
            bottom,
            bottom
        );
    }
}

}

auto Item_tree::drag_and_drop_target(const std::shared_ptr<erhe::Item_base>& item) -> bool
{
    ERHE_PROFILE_FUNCTION();

    if (!item) {
        log_tree_frame->trace("DnD target item is empty");
        return false;
    }

    const auto  rect_min = ImGui::GetItemRectMin();
    const auto  rect_max = ImGui::GetItemRectMax();
    const float height   = rect_max.y - rect_min.y;
    const float y0       = rect_min.y;
    const float y1       = rect_min.y + 0.3f * height;
    const float y2       = rect_max.y - 0.3f * height;
    const float y3       = rect_max.y;
    const float x0       = rect_min.x;
    const float x1       = rect_max.x;

    const auto        id              = item->get_id();
    const std::string label_top       = fmt::format("node dnd top {}: {} {}",    id, item->get_type_name(), item->get_name());
    const std::string label_center    = fmt::format("node dnd center {}: {} {}", id, item->get_type_name(), item->get_name());
    const std::string label_bottom    = fmt::format("node dnd bottom {}: {} {}", id, item->get_type_name(), item->get_name());
    const ImGuiID     imgui_id_top    = ImGui::GetID(label_top.c_str());
    const ImGuiID     imgui_id_center = ImGui::GetID(label_center.c_str());
    const ImGuiID     imgui_id_bottom = ImGui::GetID(label_bottom.c_str());

    const ImGuiPayload* payload_peek = ImGui::GetDragDropPayload();

    // Handle material drop onto a brush in the content library
    const auto& target_cl_node = std::dynamic_pointer_cast<Content_library_node>(item);
    if (target_cl_node && payload_peek && payload_peek->IsDataType("Content_library_node")) {
        const auto target_brush = std::dynamic_pointer_cast<Brush>(target_cl_node->item);
        if (target_brush) {
            const ImRect rect{rect_min, rect_max};
            if (ImGui::BeginDragDropTargetCustom(rect, imgui_id_center)) {
                drag_and_drop_rectangle_preview(rect);
                const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(
                    "Content_library_node", ImGuiDragDropFlags_AcceptNoDrawDefaultRect
                );
                if (payload != nullptr) {
                    erhe::Item_base* payload_item_base = *static_cast<erhe::Item_base**>(payload->Data);
                    Content_library_node* payload_node = dynamic_cast<Content_library_node*>(payload_item_base);
                    if (payload_node != nullptr) {
                        std::shared_ptr<erhe::primitive::Material> dropped_material =
                            std::dynamic_pointer_cast<erhe::primitive::Material>(payload_node->item);
                        if (dropped_material) {
                            // Find content library containing this brush
                            std::shared_ptr<erhe::geometry::Geometry> original_geometry = target_brush->get_geometry();
                            std::shared_ptr<erhe::Hierarchy> parent = target_cl_node->get_parent().lock();
                            Content_library_node* brushes_folder = dynamic_cast<Content_library_node*>(parent.get());
                            if (brushes_folder != nullptr) {
                                // Check for existing fork with same geometry and material
                                bool found = false;
                                const std::vector<std::shared_ptr<Brush>>& all_brushes = brushes_folder->get_all<Brush>();
                                for (const std::shared_ptr<Brush>& b : all_brushes) {
                                    if ((b->get_geometry() == original_geometry) && (b->get_material() == dropped_material)) {
                                        found = true;
                                        break;
                                    }
                                }
                                if (!found) {
                                    std::shared_ptr<Brush> forked = target_brush->make_with_material(dropped_material);
                                    brushes_folder->add(forked);
                                }
                            }
                        }
                    }
                }
                ImGui::EndDragDropTarget();
                return true;
            }
        }
    }

    const auto& node = std::dynamic_pointer_cast<erhe::scene::Node>(item);
    if (!node) {
        return false;
    }

    const bool payload_is_node = payload_peek->IsDataType("Node");
    std::shared_ptr<erhe::primitive::Material> material{};
    std::shared_ptr<Brush>                     brush{};

    if (!payload_is_node && payload_peek->IsDataType("Content_library_node")){
        erhe::Item_base* payload_item_base = *(static_cast<erhe::Item_base**>(payload_peek->Data));
        std::shared_ptr<erhe::Item_base> shared_item_base = payload_item_base->shared_from_this();
        std::shared_ptr<Content_library_node> content_library_node = std::dynamic_pointer_cast<Content_library_node>(
            shared_item_base
        );
        if (content_library_node) {
            material = std::dynamic_pointer_cast<erhe::primitive::Material>(content_library_node->item);
            brush    = std::dynamic_pointer_cast<Brush>(content_library_node->item);
        }
    }
    if (!payload_is_node && payload_peek->IsDataType(c_inventory_slot_payload_type)) {
        // An inventory slot can define a brush, a material, or both.
        const Slot_drag_payload& slot_payload = *static_cast<const Slot_drag_payload*>(payload_peek->Data);
        if (slot_payload.brush != nullptr) {
            brush = std::dynamic_pointer_cast<Brush>(slot_payload.brush->shared_from_this());
        }
        if (slot_payload.material != nullptr) {
            material = std::dynamic_pointer_cast<erhe::primitive::Material>(slot_payload.material->shared_from_this());
        }
    }

    // Accept either payload type that can carry a brush / material
    const auto accept_brush_or_material_payload = []() -> const ImGuiPayload* {
        const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("Content_library_node", ImGuiDragDropFlags_AcceptNoDrawDefaultRect);
        if (payload == nullptr) {
            payload = ImGui::AcceptDragDropPayload(c_inventory_slot_payload_type, ImGuiDragDropFlags_AcceptNoDrawDefaultRect);
        }
        return payload;
    };

    // When an inventory slot defines both brush and material, the brush wins:
    // a new node is created and the slot material is applied to its mesh.
    if (material && !brush) {
        const std::shared_ptr<erhe::scene::Mesh> mesh = erhe::scene::get_attachment<erhe::scene::Mesh>(node.get());
        if (mesh) {
            std::vector<erhe::scene::Mesh_primitive>& mesh_primitives = mesh->get_mutable_primitives();
            if (!mesh_primitives.empty()) {
                const ImRect rect{rect_min, rect_max};
                if (ImGui::BeginDragDropTargetCustom(rect, imgui_id_top)) {
                    drag_and_drop_rectangle_preview(rect);
                    const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(
                        "Content_library_node", ImGuiDragDropFlags_AcceptBeforeDelivery | ImGuiDragDropFlags_AcceptNoDrawDefaultRect
                    );
                    if (payload == nullptr) {
                        payload = ImGui::AcceptDragDropPayload(
                            c_inventory_slot_payload_type, ImGuiDragDropFlags_AcceptBeforeDelivery | ImGuiDragDropFlags_AcceptNoDrawDefaultRect
                        );
                    }
                    if (payload != nullptr) {
                        // TODO payload->Preview
                        if (payload->Delivery) {
                            for (erhe::scene::Mesh_primitive& mesh_primitive : mesh_primitives) {
                                mesh_primitive.material = material;
                            }
                        }
                    }
                    ImGui::EndDragDropTarget();
                    return true;
                }
            }
        }
    } else if (brush) {
        // Brush drop onto a scene node: create a new node with a mesh instance of the brush.
        Scene_root* scene_root = static_cast<Scene_root*>(node->get_item_host());
        if (scene_root == nullptr) {
            return false;
        }
        const auto insert_brush_instance = [this, &brush, &material, scene_root](
            const std::shared_ptr<erhe::scene::Node>& parent,
            const std::size_t                         index_in_parent
        ) {
            if (!parent) {
                return;
            }
            // Material priority: payload material (inventory slot) > brush material > default
            std::shared_ptr<erhe::primitive::Material> brush_material = material ? material : brush->get_material();
            if (!brush_material) {
                brush_material = get_default_material(m_context, *scene_root);
            }
            if (!brush_material) {
                return;
            }
            place_brush_in_scene(
                m_context,
                *brush,
                *scene_root,
                parent->world_from_node(), // identity local transform under the chosen parent
                brush_material,
                1.0,
                erhe::physics::Motion_mode::e_dynamic,
                parent,
                index_in_parent
            );
        };

        // Insert as sibling before drop target
        const ImRect top_rect{rect_min, ImVec2{rect_max.x, y1}};
        if (ImGui::BeginDragDropTargetCustom(top_rect, imgui_id_top)) {
            drag_and_drop_gradient_preview(x0, x1, y0, y2, ImGui::GetColorU32(ImGuiCol_DragDropTarget), 0);
            const ImGuiPayload* payload = accept_brush_or_material_payload();
            if (payload != nullptr) {
                insert_brush_instance(node->get_parent_node(), node->get_index_in_parent());
            }
            ImGui::EndDragDropTarget();
            return true;
        }

        // Insert as last child of drop target
        const ImRect middle_rect{ImVec2{rect_min.x, y1}, ImVec2{rect_max.x, y2}};
        if (ImGui::BeginDragDropTargetCustom(middle_rect, imgui_id_center)) {
            drag_and_drop_rectangle_preview(middle_rect);
            const ImGuiPayload* payload = accept_brush_or_material_payload();
            if (payload != nullptr) {
                insert_brush_instance(node, std::numeric_limits<std::size_t>::max());
            }
            ImGui::EndDragDropTarget();
            return true;
        }

        // Insert as sibling after drop target
        const ImRect bottom_rect{ImVec2{rect_min.x, y2}, rect_max};
        if (ImGui::BeginDragDropTargetCustom(bottom_rect, imgui_id_bottom)) {
            drag_and_drop_gradient_preview(x0, x1, y1, y3, 0, ImGui::GetColorU32(ImGuiCol_DragDropTarget));
            const ImGuiPayload* payload = accept_brush_or_material_payload();
            if (payload != nullptr) {
                insert_brush_instance(node->get_parent_node(), node->get_index_in_parent() + 1);
            }
            ImGui::EndDragDropTarget();
            return true;
        }
    } else if (payload_is_node) {
        log_tree_frame->trace("Dnd item is Node: {}", node->describe());
        const ImRect top_rect{rect_min, ImVec2{rect_max.x, y1}};
        if (ImGui::BeginDragDropTargetCustom(top_rect, imgui_id_top)) {
            {
                drag_and_drop_gradient_preview(x0, x1, y0, y2, ImGui::GetColorU32(ImGuiCol_DragDropTarget), 0);
                const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("Node", ImGuiDragDropFlags_AcceptNoDrawDefaultRect);
                if (payload != nullptr) {
                    if (payload != nullptr) {
                        log_tree_frame->trace("Dnd payload is Node (top rect)");
                        IM_ASSERT(payload->DataSize == sizeof(erhe::Item_base*));
                        erhe::Item_base* payload_item = *(static_cast<erhe::Item_base**>(payload->Data));
                        move_selection(node, payload_item, Placement::Before_anchor);
                    } else {
                        log_tree_frame->trace("Dnd payload is not Node (top rect)");
                    }
                }
            }
            ImGui::EndDragDropTarget();
            return true;
        }

        // Attach selection to target
        const ImRect middle_rect{ImVec2{rect_min.x, y1}, ImVec2{rect_max.x, y2}};
        if (ImGui::BeginDragDropTargetCustom(middle_rect, imgui_id_center)) {
            drag_and_drop_rectangle_preview(middle_rect);
            const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("Node", ImGuiDragDropFlags_AcceptNoDrawDefaultRect);
            if (payload != nullptr) {
                log_tree_frame->trace("Dnd payload is Node (middle rect)");
                IM_ASSERT(payload->DataSize == sizeof(erhe::Item_base*));
                erhe::Item_base* payload_item = *(static_cast<erhe::Item_base**>(payload->Data));
                attach_selection_to(node, payload_item);
            } else {
                log_tree_frame->trace("Dnd payload is not Node (middle rect)");
            }
            ImGui::EndDragDropTarget();
            return true;
        }

        // Move selection after drop target
        const ImRect bottom_rect{ImVec2{rect_min.x, y2}, rect_max};
        if (ImGui::BeginDragDropTargetCustom(bottom_rect, imgui_id_bottom)) {
            const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("Node", ImGuiDragDropFlags_AcceptNoDrawDefaultRect);
            if (payload != nullptr) {
                drag_and_drop_gradient_preview(x0, x1, y1, y3, 0, ImGui::GetColorU32(ImGuiCol_DragDropTarget));
                log_tree_frame->trace("Dnd payload is Node (bottom rect)");
                IM_ASSERT(payload->DataSize == sizeof(erhe::Item_base*));
                erhe::Item_base* payload_item = *(static_cast<erhe::Item_base**>(payload->Data));
                move_selection(node, payload_item, Placement::After_anchor);
            } else {
                log_tree_frame->trace("Dnd payload is not Node (bottom rect)");
            }
            ImGui::EndDragDropTarget();
            return true;
        }
    } else {
        log_tree_frame->trace("Dnd item is not Node / Material: {}", item->describe());
    }
    return false;
}

void Item_tree::set_item_selection_terminator(const std::shared_ptr<erhe::Item_base>& item)
{
    auto& range_selection = m_context.selection->range_selection();
    range_selection.set_terminator(item);
    m_range_selection_edited = true; // feed range selection entries this frame
}

void Item_tree::set_item_selection(const std::shared_ptr<erhe::Item_base>& item, bool selected)
{
    if (selected) {
        m_context.selection->add_to_selection(item);
    } else {
        m_context.selection->remove_from_selection(item);
    }
}

void Item_tree::item_update_selection(const std::shared_ptr<erhe::Item_base>& item)
{
    ERHE_PROFILE_SCOPE("item_update_selection"); // named zone: no per-row callstack capture

    auto& range_selection = m_context.selection->range_selection();

    drag_and_drop_source(item);

    const bool shift_down          = ImGui::IsKeyDown(ImGuiKey_LeftShift) || ImGui::IsKeyDown(ImGuiKey_RightShift);
    const bool ctrl_down           = ImGui::IsKeyDown(ImGuiKey_LeftCtrl ) || ImGui::IsKeyDown(ImGuiKey_RightCtrl);
    const bool mouse_down          = ImGui::IsMouseDown(ImGuiMouseButton_Left);
    const bool mouse_released      = ImGui::IsMouseReleased(ImGuiMouseButton_Left);
    const bool focused             = ImGui::IsItemFocused();
    const bool hovered             = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup);
    const bool was_selected        = item->is_selected();
    const bool non_mouse_activated = ImGui::IsItemActivated() && !mouse_released && !mouse_down;

    if (hovered) {
        m_hovered_item = item;
    }
    if (!shift_down) {
        m_shift_down_range_selection_started = false;
    }

    if (item->is_hovered()) {
        const ImVec2 rect_min = ImGui::GetItemRectMin();
        const ImVec2 rect_max = ImGui::GetItemRectMax();
        const ImRect rect{rect_min, rect_max};
        const auto* g       = ImGui::GetCurrentContext();
        const auto* window  = g->CurrentWindow;

        window->DrawList->AddRect(
            rect.Min - ImVec2{0.0f, 2.0f},
            rect.Max + ImVec2{0.0f, 2.0f},
            ImGui::GetColorU32(ImVec4{0.0f, 0.5f, 1.0f, 1.0f}),
            0.0f,
            0,
            1.0f
        );
    }

    std::shared_ptr<erhe::Item_base> last_focus_item = m_last_focus_item.lock();
    bool have_last_focus_item = last_focus_item.operator bool();
    bool focus_change = have_last_focus_item && focused && (last_focus_item != item);

    if (focus_change) {
        SPDLOG_LOGGER_TRACE(
            log_tree,
            "focus change from {} to {}: {}, {}, {}",
            last_focus_item->get_name(),
            item->get_name(),
            shift_down ? "shift" : "no shift",
            m_shift_down_range_selection_started ? "range already started" : "range not yet started",
            have_last_focus_item ? "have last focus item" : "no last focus item"
        );
        if (shift_down && !m_shift_down_range_selection_started && have_last_focus_item) {
            m_shift_down_range_selection_started = true;
            range_selection.reset();
            set_item_selection_terminator(last_focus_item);
            SPDLOG_LOGGER_TRACE(
                log_tree,
                "nav with shift: resetting range {}, {} last {}, {}",
                item->get_type_name(),
                item->get_name(),
                last_focus_item->get_type_name(),
                last_focus_item->get_name()
            );
        }
    }

    if (non_mouse_activated || (hovered && mouse_released)) {
        if (ctrl_down) {
            range_selection.reset();
            if (item->is_selected()) {
                set_item_selection(item, false);
            } else {
                set_item_selection(item, true);
                set_item_selection_terminator(item);
            }
        } else if (shift_down) {
            SPDLOG_LOGGER_TRACE(log_tree, "click with shift down on {} {} - range select", item->get_type_name(), item->get_name());
            set_item_selection_terminator(item);
        } else {
            range_selection.reset();
            m_context.selection->clear_selection();
            SPDLOG_LOGGER_TRACE(
                log_tree,
                "mouse button release without modifier keys on {} {} - {} selecting it",
                item->get_type_name(),
                item->get_name(),
                was_selected ? "de" : ""
            );
            if (!was_selected) {
                set_item_selection(item, true);
                set_item_selection_terminator(item);
            }
        }
    }

    if (focused) {
        if (item != m_last_focus_item.lock()) {
            if (shift_down) {
                SPDLOG_LOGGER_TRACE(log_tree, "key with shift down on {} {} - range select", item->get_type_name(), item->get_name());
                set_item_selection_terminator(item);
            }
            // else
            // {
            //     SPDLOG_LOGGER_TRACE(
            //         log_tree,
            //         "key without modifier key on node {} - clearing range select and select",
            //         node->get_name()
            //     );
            //     range_selection.reset();
            //     range_selection.set_terminator(node);
            // }
        }
        m_last_focus_item = item;
    }

    // CTRL-A to select all
    if (ctrl_down) {
        const bool a_pressed = ImGui::IsKeyPressed(ImGuiKey_A);
        if (a_pressed) {
            SPDLOG_LOGGER_TRACE(log_tree, "ctrl a pressed - select all");
            select_all();
        }
    }
}

void Item_tree::item_popup_menu(const std::shared_ptr<erhe::Item_base>& item)
{
    const auto& hierarchy = std::dynamic_pointer_cast<erhe::Hierarchy>(item);
    if (!hierarchy) {
        return;
    }

    if (
        ImGui::IsMouseReleased(ImGuiMouseButton_Right) &&
        ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup) &&
        !m_popup_item
    ) {
        m_popup_item = item;
        m_popup_id_string = fmt::format("{}##{}-popup-menu", item->get_name(), item->get_id());
        m_popup_id = ImGui::GetID(m_popup_id_string.c_str());
        ImGui::OpenPopupEx(
            m_popup_id,
            ImGuiPopupFlags_MouseButtonRight
        );
    }

    if ((m_popup_item != item) || m_popup_id_string.empty()) {
        return;
    }

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{10.0f, 10.0f});
    const bool begin_popup_context_item = ImGui::BeginPopupEx(
        m_popup_id,
        ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings
    );
    if (begin_popup_context_item) {
        bool close{false};

        if (!m_item_context_menu_callbacks.empty()) {
            for (const Context_menu_callback& cb : m_item_context_menu_callbacks) {
                cb(item, m_operations, close);
            }
            ImGui::Separator();
        }

        // In the content library, only Materials are copyable for now.
        const auto& content_node = std::dynamic_pointer_cast<Content_library_node>(item);
        const bool is_content_library_non_copyable = content_node && (
            !content_node->item || !std::dynamic_pointer_cast<erhe::primitive::Material>(content_node->item)
        );

        const bool selected_or_hierarchy = item->is_selected() || hierarchy;
        const bool can_copy = selected_or_hierarchy && !is_content_library_non_copyable;
        if (!can_copy) {
            ImGui::BeginDisabled();
        }
        if (ImGui::MenuItem("Cut")) {
            if (item->is_selected()) {
                m_context.selection->cut_selection();
            } else {
                m_context.clipboard->set_contents(item);
                auto op = std::make_shared<Item_insert_remove_operation>(
                    Item_insert_remove_operation::Parameters{
                        .context = m_context,
                        .item    = hierarchy,
                        .parent  = hierarchy->get_parent().lock(),
                        .mode    = Item_insert_remove_operation::Mode::remove,
                    }
                );
                m_context.operation_stack->queue(op);
            }
        }
        if (ImGui::MenuItem("Copy")) {
            if (item->is_selected()) {
                m_context.selection->copy_selection();
            } else {
                m_context.clipboard->set_contents(item->clone());
            }
        }
        if (!can_copy) {
            ImGui::EndDisabled();
        }

        // For content library nodes, paste always targets the folder.
        // If the target is a leaf item, redirect to its parent folder.
        // Only allow paste into the Materials folder for now.
        std::shared_ptr<erhe::Hierarchy> paste_target = hierarchy;
        if (content_node && content_node->item) {
            paste_target = hierarchy->get_parent().lock();
        }
        const auto& paste_target_content_node = std::dynamic_pointer_cast<Content_library_node>(paste_target);
        const bool is_materials_folder = paste_target_content_node &&
            !paste_target_content_node->item &&
            paste_target_content_node->type_code == erhe::Item_type::material;

        const std::vector<std::shared_ptr<erhe::Item_base>>& clipboard_contents = m_context.clipboard->get_contents();
        const bool can_paste = !clipboard_contents.empty() && paste_target && (!content_node || is_materials_folder);
        if (!can_paste) {
            ImGui::BeginDisabled();
        }
        if (ImGui::MenuItem("Paste")) {
            m_context.clipboard->try_paste(paste_target, paste_target->get_child_count());
        }
        if (!can_paste) {
            ImGui::EndDisabled();
        }

        if (!can_copy) {
            ImGui::BeginDisabled();
        }
        if (ImGui::MenuItem("Duplicate")) {
            if (item->is_selected()) {
                m_context.selection->duplicate_selection();
            } else {
                auto op = std::make_shared<Item_insert_remove_operation>(
                    Item_insert_remove_operation::Parameters{
                        .context         = m_context,
                        .item            = std::dynamic_pointer_cast<erhe::Hierarchy>(hierarchy->clone()),
                        .parent          = hierarchy->get_parent().lock(),
                        .mode            = Item_insert_remove_operation::Mode::insert,
                        .index_in_parent = hierarchy->get_index_in_parent() + 1
                    }
                );
                m_context.operation_stack->queue(op);
             }
        }
        if (!can_copy) {
            ImGui::EndDisabled();
        }

        if (!selected_or_hierarchy) {
            ImGui::BeginDisabled();
        }
        if (ImGui::MenuItem("Delete")) {
            if (item->is_selected()) {
                m_context.selection->delete_selection();
            } else {
                auto op = std::make_shared<Item_insert_remove_operation>(
                    Item_insert_remove_operation::Parameters{
                        .context = m_context,
                        .item    = hierarchy,
                        .parent  = hierarchy->get_parent().lock(),
                        .mode    = Item_insert_remove_operation::Mode::remove,
                    }
                );
                m_context.operation_stack->queue(op);
            }
        }
        if (!selected_or_hierarchy) {
            ImGui::EndDisabled();
        }

        ImGui::EndPopup();
        if (close) {
            m_popup_item.reset();
            m_popup_id_string.clear();
            m_popup_id = 0;
        }
    } else {
        m_popup_item.reset();
        m_popup_id_string.clear();
        m_popup_id = 0;
    }
    ImGui::PopStyleVar(1);
}

void Item_tree::root_popup_menu()
{
    if (!m_root) {
        return;
    }

    static bool opened = false;
    if (
        ImGui::IsMouseReleased(ImGuiMouseButton_Right) &&
        !m_popup_item
    ) {
        m_popup_item = m_root;
        m_popup_id_string = fmt::format("{}##{}-popup-menu", m_root->get_name(), m_root->get_id());
        m_popup_id = ImGui::GetID(m_popup_id_string.c_str());
        ImGui::OpenPopupEx(
            m_popup_id,
            ImGuiPopupFlags_MouseButtonRight
        );
        opened = true;
    }

    if ((m_popup_item != m_root) || m_popup_id_string.empty()) {
        if (opened) {
            opened = false;
        }
        return;
    }

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{10.0f, 10.0f});
    const bool begin_popup_context_item = ImGui::BeginPopupEx(
        m_popup_id,
        ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings
    );
    if (begin_popup_context_item) {
        bool close{false};

        if (!m_item_context_menu_callbacks.empty()) {
            for (const Context_menu_callback& cb : m_item_context_menu_callbacks) {
                cb(m_root, m_operations, close);
            }
            ImGui::Separator();
        }

        const std::vector<std::shared_ptr<erhe::Item_base>>& clipboard_contents = m_context.clipboard->get_contents();
        const bool clipboard_is_empty = clipboard_contents.empty();
        if (clipboard_is_empty) {
            ImGui::BeginDisabled();
        }
        if (ImGui::MenuItem("Paste")) {
            const auto& hierarchy = std::dynamic_pointer_cast<erhe::Hierarchy>(m_root);
            if (hierarchy) {
                m_context.clipboard->try_paste(hierarchy, hierarchy->get_child_count());
            }
        }
        if (clipboard_is_empty) {
            ImGui::EndDisabled();
        }

        ImGui::EndPopup();
        if (close) {
            m_popup_item.reset();
            m_popup_id_string.clear();
            m_popup_id = 0;
        }
    } else {
        m_popup_item.reset();
        m_popup_id_string.clear();
        m_popup_id = 0;
    }
    ImGui::PopStyleVar(1);
}

namespace {

// Content library leaf nodes are labeled with their payload item
[[nodiscard]] auto get_label_item(const std::shared_ptr<erhe::Item_base>& item) -> const std::shared_ptr<erhe::Item_base>&
{
    const auto content_library_node = std::dynamic_pointer_cast<Content_library_node>(item);
    if (content_library_node && (content_library_node->get_child_count() == 0) && content_library_node->item) {
        return content_library_node->item;
    }
    return item;
}

}

void Item_tree::imgui_row(const Flat_row& row)
{
    // Named zone instead of ERHE_PROFILE_FUNCTION(): the latter captures a
    // 32-frame callstack per zone (ZoneScopedS), which costs microseconds and
    // would dominate the measurement of this per-row function.
    ERHE_PROFILE_SCOPE("row");

    {
        ERHE_PROFILE_SCOPE("table_row");
        ImGui::TableNextRow(ImGuiTableRowFlags_None);
        ImGui::TableSetColumnIndex(0);
    }

    const ImVec2 row_pos   = ImGui::GetCursorScreenPos();
    const float  row_right = row_pos.x + ImGui::GetContentRegionAvail().x;

    const ImGuiTreeNodeFlags flags =
        row.tree_node_flags |
        (row.item->is_selected() ? ImGuiTreeNodeFlags_Selected : ImGuiTreeNodeFlags_None);

    // Tree node with an empty visible label; the icon and the label text are drawn manually
    // below, after the interaction handlers - those must see the tree node as the last item.
    {
        ERHE_PROFILE_SCOPE("tree_node");
        ImGui::TreeNodeEx(row.debug_label.data(), flags, "%s", "");
    }

    bool consumed = false;
    if (m_item_callback) {
        ERHE_PROFILE_SCOPE("callback");
        consumed = m_item_callback(row.item);
    }

    {
        ERHE_PROFILE_SCOPE("interact");
        const bool is_item_toggled_open = ImGui::IsItemToggledOpen();
        if (is_item_toggled_open) {
            m_toggled_open = true;
            m_flat_rows_dirty = true; // open/close changes the visible row set
        }
        if (!m_toggled_open) {
            item_popup_menu(row.item);

            if (!consumed) {
                ERHE_PROFILE_SCOPE("update");
                item_update_selection(row.item);
            }
        }
    }

    // Row visuals from the precomputed layout. The icon glyphs and the label
    // are passive decorations (the tree node owns all row interaction), so
    // they are emitted directly into the draw list, skipping per-item ImGui
    // submission entirely.
    {
        ERHE_PROFILE_SCOPE("decor");
        ImDrawList* const draw_list = ImGui::GetWindowDrawList();
        const ImGuiStyle& style     = ImGui::GetStyle();

        bool thumbnail_drawn = false;
        if (row.brush && m_context.thumbnails) {
            ImGui::SameLine();
            const std::shared_ptr<Brush>& brush = row.brush;
            thumbnail_drawn = m_context.thumbnails->draw(
                brush,
                [this, brush](const std::shared_ptr<erhe::graphics::Texture>& texture, unsigned int texture_layer, int64_t time) {
                    m_context.brush_preview->render_preview(texture, texture_layer, brush, time);
                },
                m_cached_icon_font_size // keep row height identical to icon-only rows
            );
        }
        if (!thumbnail_drawn && (row.primary_icon.code != nullptr)) {
            const Row_icon& icon  = row.primary_icon;
            const glm::vec4 color = (icon.live_color != nullptr) ? glm::vec4{*icon.live_color, 1.0f} : icon.color;
            draw_list->AddText(
                icon.font,
                m_cached_icon_font_size,
                ImVec2{row_pos.x + m_icon_x_offset, row_pos.y + m_icon_y_offset},
                ImGui::GetColorU32(ImVec4{color.x, color.y, color.z, color.w}),
                icon.code
            );
        }

        float icons_start_x = row_right - row.right_icons_width;
        if (row.right_icon_count > 0) {
            // Never draw the right-aligned icons over a long label
            const float label_end_x = row_pos.x + row.label_x_offset + row.label_width;
            icons_start_x = std::max(icons_start_x, label_end_x + style.ItemInnerSpacing.x);
        }

        // Label, clipped so it does not run under the right-aligned icons
        const ImVec4 label_clip{row_pos.x, row_pos.y, icons_start_x - style.ItemInnerSpacing.x, row_pos.y + ImGui::GetFrameHeight()};
        draw_list->AddText(
            ImGui::GetFont(),
            ImGui::GetFontSize(),
            ImVec2{row_pos.x + row.label_x_offset, row_pos.y + m_label_y_offset},
            ImGui::GetColorU32(ImGuiCol_Text),
            row.label_text.data(),
            row.label_text.data() + row.label_text.size(),
            0.0f,
            (row.right_icon_count > 0) ? &label_clip : nullptr
        );

        for (std::size_t i = 0; i < row.right_icon_count; ++i) {
            const Row_icon& icon  = row.right_icons[i];
            const glm::vec4 color = (icon.live_color != nullptr) ? glm::vec4{*icon.live_color, 1.0f} : icon.color;
            draw_list->AddText(
                icon.font,
                m_cached_icon_font_size,
                ImVec2{icons_start_x + icon.x_offset, row_pos.y + m_icon_y_offset},
                ImGui::GetColorU32(ImVec4{color.x, color.y, color.z, color.w}),
                icon.code
            );
        }
    }
}

// Simple icon + name line used for the drag-and-drop payload preview
void Item_tree::item_icon_and_text(const std::shared_ptr<erhe::Item_base>& item)
{
    m_context.icon_set->item_icon(item, m_ui_scale); // ends with SameLine()
    const std::shared_ptr<erhe::Item_base>& label_item = get_label_item(item);
    const std::string& name = label_item->get_name();
    ImGui::TextUnformatted(name.c_str(), name.c_str() + name.size());
}

auto Item_tree::should_show(const std::shared_ptr<erhe::Item_base>& item) -> Show_mode
{
    const bool show_by_type = m_filter(item->get_flag_bits());
    const bool show_by_name = m_text_filter.PassFilter(item->get_name().c_str());
    if (show_by_type && show_by_name) {
        return Show_mode::Show;
    }

    bool show_by_attachments = false;
    const auto& node = std::dynamic_pointer_cast<erhe::scene::Node>(item);
    if (node) {
        for (const auto& node_attachment : node->get_attachments()) {
            if (should_show(node_attachment) != Show_mode::Hide) {
                show_by_attachments = true;
                break;
            }
        }
    }
    if (show_by_attachments) {
        return Show_mode::Show;
    }

    const auto& hierarchy = std::dynamic_pointer_cast<erhe::Hierarchy>(item);
    if (hierarchy) {
        for (const auto& child_node : hierarchy->get_children()) {
            if (should_show(child_node) != Show_mode::Hide) {
                return Show_mode::Show_expanded;
            }
        }
    }

    return Show_mode::Hide;
}

void Item_tree::flatten_visible_rows(const std::shared_ptr<erhe::Item_base>& item, const float indent)
{
    // Special handling for invisible parents (scene root)
    if (erhe::utility::test_bit_set(item->get_flag_bits(), erhe::Item_flags::invisible_parent)) {
        const auto& hierarchy = std::dynamic_pointer_cast<erhe::Hierarchy>(item);
        if (hierarchy) {
            for (const auto& child_node : hierarchy->get_children()) {
                flatten_visible_rows(child_node, indent);
            }
        }
        return;
    }

    ERHE_PROFILE_SCOPE("flatten_visible_rows"); // named zone: no per-call callstack capture

    const Show_mode show = should_show(item);
    if (show == Show_mode::Hide) {
        //// log_tree->info("filtered {}", item->describe());
        return;
    }
    const bool force_expand = (show == Show_mode::Show_expanded);

    const auto& hierarchy = std::dynamic_pointer_cast<erhe::Hierarchy   >(item);
    const auto& scene     = std::dynamic_pointer_cast<erhe::scene::Scene>(item);
    const auto& node      = std::dynamic_pointer_cast<erhe::scene::Node >(item);
    bool is_leaf = true;
    if (hierarchy && (hierarchy->get_child_count(m_filter) > 0)) {
        is_leaf = false;
    }
    if (scene && scene->get_root_node() && scene->get_root_node()->get_child_count(m_filter) > 0) {
        is_leaf = false;
    }

    const bool expand =
        erhe::utility::test_bit_set(item->get_flag_bits(), erhe::Item_flags::expand) ||
        force_expand;

    const std::shared_ptr<erhe::Item_base>& label_item   = get_label_item(item);
    const erhe::utility::Debug_label        debug_label  = label_item->get_debug_label(); // "<name>##<id>" - tree node id

    {
        // NOTE: this reference must not be held across the recursive calls
        // below; they can reallocate m_flat_rows.
        Flat_row& row = m_flat_rows.emplace_back();
        row.item        = item;
        row.indent      = indent;
        row.debug_label = debug_label;
        row.label_text  = std::string_view{label_item->get_name()};

        row.tree_node_flags =
            ImGuiTreeNodeFlags_SpanAvailWidth |
            ImGuiTreeNodeFlags_NoTreePushOnOpen | // rows are flat; indentation comes from the row list
            (expand ? ImGuiTreeNodeFlags_DefaultOpen : ImGuiTreeNodeFlags_None) |
            (is_leaf ? ImGuiTreeNodeFlags_Leaf : ImGuiTreeNodeFlags_OpenOnArrow);

        Icon_set&         icon_set = *m_context.icon_set;
        const ImGuiStyle& style    = ImGui::GetStyle();

        // Primary visual: brush thumbnail (drawn live) or a resolved icon
        const auto& content_library_node = std::dynamic_pointer_cast<Content_library_node>(item);
        if (content_library_node && m_context.thumbnails) {
            row.brush = std::dynamic_pointer_cast<Brush>(content_library_node->item);
        }
        float primary_width = m_cached_icon_font_size; // thumbnails are square
        if (!row.brush) {
            const Icon_set::Item_icon icon = icon_set.get_item_icon(item);
            row.primary_icon = Row_icon{.font = icon.font, .code = icon.code, .color = icon.color, .live_color = icon.live_color};
            primary_width = icon_set.get_icon_width(icon);
        }
        row.label_x_offset = m_icon_x_offset + primary_width + style.ItemInnerSpacing.x;
        row.label_width    = ImGui::CalcTextSize(row.label_text.data(), row.label_text.data() + row.label_text.size()).x;

        // Attachment icons, right-aligned at render time using these offsets
        if (node && !m_context.app_settings->node_tree_expand_attachments) {
            const float icon_spacing = style.ItemSpacing.x;
            float x = 0.0f;
            for (const auto& node_attachment : node->get_attachments()) {
                if (row.right_icon_count >= Flat_row::max_right_icon_count) {
                    break; // out of slots; remaining attachment icons are dropped
                }
                const Icon_set::Item_icon icon = icon_set.get_item_icon(node_attachment);
                if (icon.code == nullptr) {
                    continue;
                }
                const float width = icon_set.get_icon_width(icon);
                row.right_icons[row.right_icon_count] = Row_icon{
                    .font = icon.font, .code = icon.code, .color = icon.color, .live_color = icon.live_color, .x_offset = x
                };
                ++row.right_icon_count;
                row.right_icons_width = x + width;
                x += width + icon_spacing;
            }
        }
    }

    // ImGui treats leaf tree nodes as always open (TreeNodeUpdateNextOpen); for
    // non-leaf rows the open state lives in ImGui per-window storage, keyed by
    // the same ID and with the same default imgui_row passes to TreeNodeEx.
    bool is_open = true;
    if (!is_leaf) {
        const ImGuiID id = ImGui::GetID(debug_label.data());
        is_open = ImGui::GetStateStorage()->GetInt(id, expand ? 1 : 0) != 0;
    }
    if (!is_open) {
        return;
    }

    if (m_context.app_settings->node_tree_expand_attachments) {
        if (node) {
            const float attachment_indent = 15.0f; // TODO
            for (const auto& node_attachment : node->get_attachments()) {
                flatten_visible_rows(node_attachment, indent + attachment_indent);
            }
        }
    }
    if (hierarchy) {
        const float indent_spacing = ImGui::GetStyle().IndentSpacing;
        for (const auto& child_node : hierarchy->get_children()) {
            flatten_visible_rows(child_node, indent + indent_spacing);
        }
    }
}

void Item_tree::imgui_tree(float ui_scale)
{
    ERHE_PROFILE_FUNCTION();

    if ((m_filter.require_at_least_one_bit_set & erhe::Item_flags::show_in_ui) != 0) {
        m_filter.require_at_least_one_bit_set = m_context.developer_mode
            ? (erhe::Item_flags::show_in_ui | erhe::Item_flags::show_in_developer_ui)
            : erhe::Item_flags::show_in_ui;
    }

    m_hovered_item.reset();

    if (!m_root) {
        return;
    }

    ///ImGuiStyle& style = ImGui::GetCurrentContext()->Style;
    ///ImVec4 not_selected_color     = style.Colors[ImGuiCol_WindowBg];
    ///ImVec4 selected_color         = style.Colors[ImGuiCol_Selected];
    ///ImVec4 selected_hovered_color = style.Colors[ImGuiCol_SelectedHovered];
    ///ImVec4 selected_active_color  = style.Colors[ImGuiCol_SelectedActive];
    ///ImGui::PushStyleColor(ImGuiCol_Header, last_selected_color);

    m_ui_scale = ui_scale;

    const std::size_t root_id = m_root->get_id();
    const int table_id = static_cast<int>(root_id);
    ImGui::PushID(table_id);
    ERHE_DEFER( ImGui::PopID(); );

    ImGui::PushFont(
        m_context.imgui_renderer->material_design_font(),
        m_context.imgui_renderer->get_imgui_settings().scale_factor *
        m_context.imgui_renderer->get_imgui_settings().font_size
    );
    ImGui::TextUnformatted(ICON_MDI_FILTER);
    ImGui::PopFont();
    ImGui::SameLine();
    if (m_text_filter.Draw("##Filter", -FLT_MIN)) {
        m_flat_rows_dirty = true;
    }

    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2{0.0f, 0.0f});
    ERHE_DEFER( ImGui::PopStyleVar(1); );

    bool table_visible = ImGui::BeginTable("##", 1, ImGuiTableFlags_RowBg);
    if (!table_visible) {
        return;
    }

    ImGui::TableSetupColumn("entry", ImGuiTableColumnFlags_WidthStretch);

#if 0 //// TODO
    ImGui::Checkbox("Expand Attachments", &m_context.app_settings->node_tree_expand_attachments);
    ImGui::Checkbox("Show All",           &m_context.app_settings->node_tree_show_all);
#endif

    m_context.selection->range_selection().begin();

    // TODO Handle cross scene drags and drops
#if 0 //// TODO
    if (ImGui::Button("Create Scene")) {
        auto content_library = std::make_shared<Content_library>();
        content_library->materials.make("Default");
        const bool enable_physics = m_context.editor_settings->physics.static_enable;
        auto scene_root = std::make_shared<Scene_root>(
            nullptr,
            content_library,
            "new scene",
            enable_physics
        );

        using Item_flags = erhe::Item_flags;

        auto camera_node = std::make_shared<erhe::scene::Node>("Camera Node");
        auto camera = std::make_shared<erhe::scene::Camera>("Camera");
        camera_node->enable_flag_bits(Item_flags::content | Item_flags::show_in_ui);
        camera     ->enable_flag_bits(Item_flags::content | Item_flags::show_in_ui);
        camera_node->attach    (camera);
        camera_node->set_parent(scene_root->get_hosted_scene()->get_root_node());
        camera_node->set_world_from_node(
            erhe::math::create_look_at(
                glm::vec3{0.0f, 1.0f, 1.0f},
                glm::vec3{0.0f, 1.0f, 0.0f},
                glm::vec3{0.0f, 1.0f, 0.0f}
            )
        );

        auto light_node = std::make_shared<erhe::scene::Node>("Light Node");
        auto light = std::make_shared<erhe::scene::Light>("Light");
        light_node->enable_flag_bits(Item_flags::content | Item_flags::show_in_ui);
        light     ->enable_flag_bits(Item_flags::content | Item_flags::show_in_ui);
        light     ->layer_id  = scene_root->layers().light()->id;
        light_node->attach    (light);
        light_node->set_parent(scene_root->get_hosted_scene()->get_root_node());
        light_node->set_world_from_node(
            erhe::math::create_look_at(
                glm::vec3{0.0f, 3.0f, 0.0f},
                glm::vec3{0.0f, 0.0f, 0.0f},
                glm::vec3{0.0f, 0.0f, 1.0f}
            )
        );

        m_context.app_scenes->register_scene_root(scene_root);
    }
#endif
    // Flatten the visible tree into uniform-height rows, then submit only the
    // on-screen range. The flattened list is cached across frames: it is
    // rebuilt when the item mutation serial moves (any hierarchy, attachment,
    // name or non-transient flag change anywhere), or when this tree's own
    // inputs change (open/close toggle, text filter, root, item filter,
    // expand-attachments mode, indent spacing). flatten_visible_rows() must
    // run inside the BeginTable scope so its ImGui::GetID() calls match the
    // TreeNodeEx IDs.
    const uint64_t item_mutation_serial = erhe::get_item_mutation_serial();
    const bool     expand_attachments   = m_context.app_settings->node_tree_expand_attachments;
    const float    indent_spacing       = ImGui::GetStyle().IndentSpacing;
    const float    font_size            = ImGui::GetFontSize();
    const float    icon_font_size       = m_context.icon_set->get_icon_font_size();
    const bool rebuild =
        m_flat_rows_dirty ||
        (m_last_mutation_serial != item_mutation_serial) ||
        !(m_cached_filter == m_filter) ||
        (m_cached_expand_attachments != expand_attachments) ||
        (m_cached_indent_spacing != indent_spacing) ||
        (m_cached_font_size != font_size) ||
        (m_cached_icon_font_size != icon_font_size);
    if (rebuild) {
        ERHE_PROFILE_SCOPE("flatten");
        m_flat_rows_dirty           = false;
        m_last_mutation_serial      = item_mutation_serial;
        m_cached_filter             = m_filter;
        m_cached_expand_attachments = expand_attachments;
        m_cached_indent_spacing     = indent_spacing;
        m_cached_font_size          = font_size;
        m_cached_icon_font_size     = icon_font_size;
        // Row layout constants: the empty-label tree node advances the cursor
        // by FontSize + 2 * FramePadding.x (+ ItemSpacing.x); icon glyphs are
        // vertically centered in the frame height, the label sits at the tree
        // node text baseline.
        const ImGuiStyle& style = ImGui::GetStyle();
        m_icon_x_offset  = font_size + 2.0f * style.FramePadding.x + style.ItemSpacing.x;
        m_icon_y_offset  = std::max(0.0f, (ImGui::GetFrameHeight() - icon_font_size) * 0.5f);
        m_label_y_offset = style.FramePadding.y;
        m_flat_rows.clear();
        flatten_visible_rows(m_root, 0.0f);
    }

    {
        ERHE_PROFILE_SCOPE("rows");
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(m_flat_rows.size()), -1.0f);
        while (clipper.Step()) {
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                const Flat_row& row = m_flat_rows[i];
                // Indent must be applied before the row's TableNextRow(), which
                // snapshots the window indent for the cell start position.
                if (row.indent > 0.0f) {
                    ImGui::Indent(row.indent);
                }
                imgui_row(row);
                if (row.indent > 0.0f) {
                    ImGui::Unindent(row.indent);
                }
            }
        }
        clipper.End();
    }

    for (const auto& fun : m_operations) {
        fun();
    }
    m_operations.clear();

    if (m_operation) {
        m_context.operation_stack->queue(m_operation);
        m_operation.reset();
    }

    // Range selection entries are needed only on frames where this tree set a
    // terminator (range_selection.end() ignores them otherwise). Feed every
    // visible row in top-to-bottom order, which end() requires.
    if (m_range_selection_edited) {
        auto& range_selection = m_context.selection->range_selection();
        for (const Flat_row& row : m_flat_rows) {
            range_selection.entry(row.item);
        }
        m_range_selection_edited = false;
    }
    m_context.selection->range_selection().end();

    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        m_toggled_open = false;
    }

    if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByPopup)) {
        if (m_hover_callback) {
            m_hover_callback();
        }
        root_popup_menu();
    }

    // Clear stale popup state when the popup item was removed from the tree
    // (e.g. after Delete) and ImGui already closed the popup.
    if (m_popup_item && (m_popup_id != 0) && !ImGui::IsPopupOpen(m_popup_id, ImGuiPopupFlags_None)) {
        m_popup_item.reset();
        m_popup_id_string.clear();
        m_popup_id = 0;
    }

    ImGui::EndTable();

    //// m_context.app_scenes->sanity_check();
}

auto Item_tree::get_hovered_item() const -> const std::shared_ptr<erhe::Item_base>&
{
    return m_hovered_item;
}

////////////////////////////

Item_tree_window::Item_tree_window(
    erhe::imgui::Imgui_renderer& imgui_renderer,
    erhe::imgui::Imgui_windows&  imgui_windows,
    App_context&                 context,
    const std::string_view       window_title,
    const std::string_view       ini_label
)
    : erhe::imgui::Imgui_window{imgui_renderer, imgui_windows, window_title, ini_label}
    , Item_tree{context}
{
}

void Item_tree_window::on_begin()
{
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,      ImVec2{0.0f, 0.0f});
    ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, ImVec2{3.0f, 3.0f});
}

void Item_tree_window::on_end()
{
    ImGui::PopStyleVar(2);
}

void Item_tree_window::hidden()
{
    // Drop cached rows so a hidden tree does not keep deleted items alive
    // through the cached shared_ptrs; rebuilt when the window is shown again.
    clear_cached_rows();
}

void Item_tree_window::imgui()
{
    ERHE_PROFILE_FUNCTION();

    imgui_tree(get_scale_value());

    if (ImGui::IsWindowHovered()) {
        m_context.app_message_bus->hover_tree_node.queue_message(
            Hover_tree_node_message{
                .item = get_hovered_item()
            }
        );
    }

}

}
