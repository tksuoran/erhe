#include "content_library/content_library_window.hpp"

#include "app_context.hpp"
#include "content_library/content_library.hpp"
#include "operations/item_insert_remove_operation.hpp"
#include "operations/operation_stack.hpp"
#include "windows/item_tree_window.hpp"

#include "erhe_primitive/material.hpp"

#include <fmt/format.h>

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>

namespace editor {

Content_library_window::Content_library_window(
    erhe::imgui::Imgui_renderer&            imgui_renderer,
    erhe::imgui::Imgui_windows&             imgui_windows,
    App_context&                            context,
    const std::shared_ptr<Content_library>& content_library,
    const std::string_view                  scene_name
)
    : m_context        {context}
    , m_content_library{content_library}
{
    const std::size_t library_id = m_content_library->root->get_id();
    m_tree_window = std::make_shared<Item_tree_window>(
        imgui_renderer,
        imgui_windows,
        context,
        fmt::format("Content Library - {}###{}", scene_name, library_id),
        fmt::format("Content_library_{}", library_id)
    );
    m_tree_window->set_root(m_content_library->root);
    m_tree_window->set_item_filter(
        erhe::Item_filter{
            .require_all_bits_set           = 0,
            .require_at_least_one_bit_set   = 0,
            .require_all_bits_clear         = 0,
            .require_at_least_one_bit_clear = 0
        }
    );
    m_tree_window->add_item_context_menu_callback(
        [this](
            const std::shared_ptr<erhe::Item_base>& item,
            std::vector<std::function<void()>>&     deferred_operations,
            bool&                                   close
        ) {
            const auto& content_node = std::dynamic_pointer_cast<Content_library_node>(item);
            if (!content_node) {
                return;
            }
            const bool is_folder = !content_node->item && (content_node->type_code != 0);
            auto materials = m_content_library->materials;
            App_context* context_ptr = &m_context;
            if (is_folder && content_node->type_code == erhe::Item_type::material) {
                if (ImGui::MenuItem("Create Material")) {
                    deferred_operations.push_back(
                        [context_ptr, materials]() {
                            auto new_material = std::make_shared<erhe::primitive::Material>(
                                erhe::primitive::Material_create_info{
                                    .name = "New Material",
                                    .data = {
                                        .base_color = glm::vec3{0.5f, 0.5f, 0.5f},
                                        .roughness  = glm::vec2{0.5f, 0.5f},
                                        .metallic   = 1.0f
                                    }
                                }
                            );
                            auto new_node = std::make_shared<Content_library_node>(new_material);
                            auto op = std::make_shared<Item_insert_remove_operation>(
                                Item_insert_remove_operation::Parameters{
                                    .context = *context_ptr,
                                    .item    = new_node,
                                    .parent  = materials,
                                    .mode    = Item_insert_remove_operation::Mode::insert
                                }
                            );
                            context_ptr->operation_stack->queue(op);
                        }
                    );
                    close = true;
                }
            }
        }
    );
    m_tree_window->set_item_callback(
        [this](const std::shared_ptr<erhe::Item_base>& item) -> bool {
            if (!ImGui::IsDragDropActive()) {
                return false;
            }
            auto content_node = std::dynamic_pointer_cast<Content_library_node>(item);
            if (!content_node) {
                return false;
            }

            const auto& materials_folder = m_content_library->materials;
            const bool is_materials_folder = (content_node == materials_folder);
            const bool is_material_item    = !is_materials_folder &&
                content_node->item &&
                std::dynamic_pointer_cast<erhe::primitive::Material>(content_node->item) &&
                (content_node->get_parent().lock() == materials_folder);
            if (!is_materials_folder && !is_material_item) {
                return false;
            }

            const ImGuiPayload* payload_peek = ImGui::GetDragDropPayload();
            if (!payload_peek || !payload_peek->IsDataType("Content_library_node")) {
                return false;
            }
            erhe::Item_base* payload_item_base = *(static_cast<erhe::Item_base**>(payload_peek->Data));
            auto source_node = std::dynamic_pointer_cast<Content_library_node>(payload_item_base->shared_from_this());
            if (!source_node || !source_node->item) {
                return false;
            }
            auto source_material = std::dynamic_pointer_cast<erhe::primitive::Material>(source_node->item);
            if (!source_material) {
                return false;
            }

            bool is_same_library = false;
            for (auto check = source_node->get_parent().lock(); check; check = check->get_parent().lock()) {
                if (check == m_content_library->root) {
                    is_same_library = true;
                    break;
                }
            }
            if (is_same_library) {
                return false;
            }

            if (ImGui::BeginDragDropTarget()) {
                const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("Content_library_node");
                if (payload != nullptr) {
                    auto new_material = std::make_shared<erhe::primitive::Material>(*source_material);
                    auto new_node = std::make_shared<Content_library_node>(new_material);
                    auto op = std::make_shared<Item_insert_remove_operation>(
                        Item_insert_remove_operation::Parameters{
                            .context = m_context,
                            .item    = new_node,
                            .parent  = materials_folder,
                            .mode    = Item_insert_remove_operation::Mode::insert
                        }
                    );
                    m_context.operation_stack->queue(op);
                }
                ImGui::EndDragDropTarget();
                return true;
            }
            return false;
        }
    );
}

}
