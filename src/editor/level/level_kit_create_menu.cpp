#include "level/level_kit_create_menu.hpp"

#include "level/kit_layout_values.hpp"
#include "level/kit_shape.hpp"
#include "level/kit_shape_library.hpp"
#include "level/level_kits.hpp"

#include "app_context.hpp"
#include "app_scenes.hpp"
#include "editor_log.hpp"
#include "scene/scene_root.hpp"

#include "erhe_imgui/imgui_windows.hpp"
#include "erhe_item/hierarchy.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_scene/trs_transform.hpp"

#include <imgui/imgui.h>

namespace editor {

namespace {

// The parent's world pose, where a Create entry puts the new node unless it is given a position; identity for a non-node.
void get_parent_pose(const erhe::Hierarchy& parent, glm::vec3& out_position, glm::quat& out_rotation)
{
    const erhe::scene::Node* const node = dynamic_cast<const erhe::scene::Node*>(&parent);
    if (node == nullptr) {
        out_position = glm::vec3{0.0f};
        out_rotation = glm::quat{1.0f, 0.0f, 0.0f, 0.0f};
        return;
    }
    const erhe::scene::Trs_transform world = node->world_from_node_transform();
    out_position = world.get_translation();
    out_rotation = world.get_rotation();
}

void disabled_reason(const char* const reason)
{
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("%s", reason);
    }
}

} // anonymous namespace

auto level_kit_create_menu(
    App_context&                            context,
    const std::shared_ptr<Scene_root>&      scene_root,
    const std::shared_ptr<erhe::Hierarchy>& parent,
    const std::optional<glm::vec3>&         world_position
) -> bool
{
    if (context.level_kits == nullptr) {
        return false;
    }
    // A kit node goes under a node (the scene root when none is given).
    const erhe::Hierarchy* const target = parent ? parent.get() : (scene_root ? scene_root->get_scene().get_root_node().get() : nullptr);
    const char* const refusal =
        (scene_root == nullptr)
            ? "no scene: open or create a scene first"
            : ((target == nullptr) || (dynamic_cast<const erhe::scene::Node*>(target) == nullptr))
                ? "level kit nodes go under a node"
                : nullptr;
    ImGui::BeginDisabled(refusal != nullptr);
    const bool open = ImGui::BeginMenu("Level Kit");
    ImGui::EndDisabled();
    if (refusal != nullptr) {
        disabled_reason(refusal);
    }
    if (!open) {
        return false;
    }
    bool chosen = false;
    const std::weak_ptr<Scene_root>       weak_scene_root = scene_root;
    const std::weak_ptr<erhe::Hierarchy>  weak_parent     = parent;
    const bool                            has_parent      = static_cast<bool>(parent);
    const Kit_shape_library& library = context.level_kits->get_library();
    for (const Kit_definition& kit : library.get_kits()) {
        if (!ImGui::BeginMenu(kit.display_name.c_str())) {
            continue;
        }
        for (const Kit_shape_definition& shape : library.get_shapes()) {
            if (shape.kit != kit.name) {
                continue;
            }
            ImGui::PushID(shape.id.c_str());
            if (ImGui::MenuItem(shape.display_name.c_str())) {
                chosen = true;
                const Kit_shape_definition* const shape_ptr = &shape;
                context.imgui_windows->queue(
                    [&context, weak_scene_root, weak_parent, has_parent, shape_ptr, world_position]() {
                        const std::shared_ptr<Scene_root>      target_scene  = weak_scene_root.lock();
                        const std::shared_ptr<erhe::Hierarchy> target_parent = weak_parent.lock();
                        if (!target_scene || !context.app_scenes->is_host_registered(target_scene.get()) || (has_parent && !target_parent)) {
                            return;
                        }
                        glm::vec3 position{0.0f};
                        glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
                        if (target_parent) {
                            get_parent_pose(*target_parent, position, rotation);
                        }
                        if (world_position.has_value()) {
                            position = world_position.value();
                        }
                        const Kit_place_result result = context.level_kits->add_piece_of_shape(*target_scene, *shape_ptr, target_parent, position, rotation);
                        if (!result.error.empty()) {
                            log_scene->warn("Create > Level Kit: {} refused: {}", shape_ptr->id, result.error);
                        }
                    }
                );
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s\n%s\nA piece at its default values at the parent's origin; one undo step; selected.", shape.id.c_str(), shape.description.c_str());
            }
            ImGui::PopID();
        }
        ImGui::EndMenu();
    }
    ImGui::Separator();
    if (ImGui::BeginMenu("Layouts")) {
        for (const Repeat_pattern_kind kind : c_layout_kinds) {
            const char* const kind_refusal = Level_kits::find_layout_parent_refusal(*target, kind);
            if (ImGui::MenuItem(get_layout_kind_create_label(kind), nullptr, false, kind_refusal == nullptr)) {
                chosen = true;
                context.imgui_windows->queue(
                    [&context, weak_scene_root, weak_parent, has_parent, kind, world_position]() {
                        const std::shared_ptr<Scene_root>      target_scene  = weak_scene_root.lock();
                        const std::shared_ptr<erhe::Hierarchy> target_parent = weak_parent.lock();
                        if (!target_scene || !context.app_scenes->is_host_registered(target_scene.get()) || (has_parent && !target_parent)) {
                            return;
                        }
                        glm::vec3 position{0.0f};
                        glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
                        if (target_parent) {
                            get_parent_pose(*target_parent, position, rotation);
                        }
                        if (world_position.has_value()) {
                            position = world_position.value();
                        }
                        const Kit_layout_result result = context.level_kits->add_layout_of_kind(*target_scene, kind, target_parent, position, rotation, std::nullopt);
                        if (!result.error.empty()) {
                            log_scene->warn("Create > Level Kit: {} refused: {}", get_layout_kind_create_label(kind), result.error);
                        }
                    }
                );
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                ImGui::SetTooltip(
                    "%s",
                    (kind_refusal != nullptr)
                        ? kind_refusal
                        : (kind == Repeat_pattern_kind::stairs)
                            ? "A staircase generated along a curve (a stairs kit layout): a flight rising along +x at the\n"
                              "Level Kit window's stairs values; edit the curve's points to shape it; one undo step; selected."
                        : (kind == Repeat_pattern_kind::floor)
                            ? "A polygon floor (a floor kit layout): a closed curve of four corners four modules apart filled\n"
                              "with a slab; edit the curve's points to shape it; one undo step; selected."
                            : "A kit layout node of this kind with the Level Kit window's initial values and a basic block\n"
                              "as its item, at the parent's origin; one undo step; selected."
                );
            }
        }
        ImGui::EndMenu();
    }
    ImGui::EndMenu();
    return chosen;
}

} // namespace editor
