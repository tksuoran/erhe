#include "scene/scene_root.hpp"
#include "scene/item_lookup.hpp"

#include "config/generated/physics_config.hpp"
#include "editor_log.hpp"
#include "app_message_bus.hpp"
#include "app_scenes.hpp"
#include "app_settings.hpp"
#include "rendertarget_mesh.hpp"

#include "app_context.hpp"
#include "asset_browser/asset_browser.hpp"
#include "assets/asset_manager.hpp"
#include "assets/asset_workflow.hpp"
#include "graphics/texture_file_loader.hpp"
#include "brushes/brush.hpp"
#include "content_library/content_library.hpp"
#include "content_library/style.hpp"
#include "geometry_graph/graph_mesh.hpp"
#include "geometry_graph/geometry_graph_window.hpp"
#include "texture_graph/graph_texture.hpp"
#include "texture_graph/texture_graph_window.hpp"
#include "operations/item_insert_remove_operation.hpp"
#include "operations/compound_operation.hpp"
#include "operations/item_set_flag_bits_operation.hpp"
#include "operations/property_set_operation.hpp"
#include "operations/operation_stack.hpp"
#include "operations/variant_select_operation.hpp"
#include "prefabs/instance_structure.hpp"
#include "prefabs/prefab_instance.hpp"
#include "scene/attachment_types.hpp"
#include "scene/node_joint.hpp"
#include "scene/node_physics.hpp"
#include "scene/scene_commands.hpp"
#include "scene/node_raytrace.hpp"
#include "scene/node_raytrace_mask.hpp"
#include "tools/selection_tool.hpp"
#include "windows/editor_windows.hpp"
#include "windows/item_tree_window.hpp"

#include "erhe_file/file.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_imgui/imgui_windows.hpp"
#include "erhe_physics/iworld.hpp"
#include "erhe_physics/irigid_body.hpp"
#include "erhe_physics/physics_material.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_raytrace/iscene.hpp"
#include "erhe_scene/camera.hpp"
#include "erhe_scene/light.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/node_attachment.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_scene/skin.hpp"
#include "erhe_scene_renderer/draw_list_scene.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_verify/verify.hpp"

#include <fmt/format.h>

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>
#include <chrono>
#include <ctime>
#include <functional>
#include <random>
#include <unordered_set>

namespace editor {

using erhe::scene::Light;
using erhe::scene::Light_layer;
using erhe::scene::Mesh;
using erhe::scene::Mesh_layer;
using erhe::scene::Scene;

Scene_layers::Scene_layers()
{
    m_brush        = std::make_shared<Mesh_layer>("brush",        erhe::Item_flags::brush,        Mesh_layer_id::brush);
    m_content      = std::make_shared<Mesh_layer>("content",      erhe::Item_flags::content,      Mesh_layer_id::content);
    m_controller   = std::make_shared<Mesh_layer>("controller",   erhe::Item_flags::controller,   Mesh_layer_id::controller);
    m_rendertarget = std::make_shared<Mesh_layer>("rendertarget", erhe::Item_flags::rendertarget, Mesh_layer_id::rendertarget);
    m_tool         = std::make_shared<Mesh_layer>("tool",         erhe::Item_flags::tool,         Mesh_layer_id::tool);
    m_bone         = std::make_shared<Mesh_layer>("bone",         erhe::Item_flags::bone_proxy,   Mesh_layer_id::bone);

    m_light        = std::make_shared<Light_layer>("lights", 0);
}

void Scene_layers::add_layers_to_scene(erhe::scene::Scene& scene)
{
    scene.add_mesh_layer(m_brush);
    scene.add_mesh_layer(m_content);
    scene.add_mesh_layer(m_controller);
    scene.add_mesh_layer(m_rendertarget);
    scene.add_mesh_layer(m_tool);
    scene.add_mesh_layer(m_bone);

    scene.add_light_layer(m_light);
}

auto Scene_layers::brush() const -> erhe::scene::Mesh_layer*
{
    return m_brush.get();
}

auto Scene_layers::content() const -> erhe::scene::Mesh_layer*
{
    return m_content.get();
}

auto Scene_layers::controller() const -> erhe::scene::Mesh_layer*
{
    return m_controller.get();
}

auto Scene_layers::tool() const -> erhe::scene::Mesh_layer*
{
    return m_tool.get();
}

auto Scene_layers::rendertarget() const -> erhe::scene::Mesh_layer*
{
    return m_rendertarget.get();
}

auto Scene_layers::bone() const -> erhe::scene::Mesh_layer*
{
    return m_bone.get();
}

auto Scene_layers::light() const -> erhe::scene::Light_layer*
{
    return m_light.get();
}

auto Scene_layers::mesh_layers() const -> std::array<erhe::scene::Mesh_layer*, 6>
{
    return std::array<erhe::scene::Mesh_layer*, 6>{
        content(),
        controller(),
        tool(),
        brush(),
        rendertarget(),
        bone()
    };
};

Scene_root::Scene_root(
    App_message_bus*                                     app_message_bus,
    const std::shared_ptr<Content_library>&              content_library,
    const std::string_view                               name,
    bool                                                 enable_physics,
    const Draw_list_scene_dependencies*                  draw_list_dependencies,
    const erhe::scene_renderer::Material_set_create_info& material_set_create_info
)
    : m_app_message_bus{app_message_bus}
    , m_content_library{content_library}
    , m_material_set{material_set_create_info}
{
    ERHE_PROFILE_FUNCTION();

    if ((draw_list_dependencies != nullptr) && draw_list_dependencies->is_valid()) {
        m_draw_list_scene = std::make_unique<erhe::scene_renderer::Draw_list_scene>(
            erhe::scene_renderer::Draw_list_scene_create_info{
                .mesh_memory              = draw_list_dependencies->mesh_memory,
                .shader_variant_cache     = draw_list_dependencies->shader_variant_cache,
                .primitive_interface      = draw_list_dependencies->primitive_interface,
                .multiview_view_counts    = std::span<const uint32_t>{draw_list_dependencies->multiview_view_counts},
                .material_set_create_info = draw_list_dependencies->material_set_create_info
            }
        );
    }

    m_scene = std::make_shared<Scene>(name, this);

    // The scene owns its content library: its resources are prims of this
    // scene's tree, under the kind scopes the library keeps below the root
    // node, and report this Scene_root as their Item_host. Resources added
    // before this point (e.g. the default materials created ahead of
    // Scene_root construction) move into the tree here
    // (doc/usd-compatibility-plan.md U4).
    if (m_content_library) {
        m_content_library->set_owner(this, m_scene->get_root_node());
    }
    m_layers.add_layers_to_scene(*m_scene.get());

    // The Scene item is selectable and shown as the top row of the Hierarchy
    // window (issue #240); make it pass the window's show_in_ui filter.
    m_scene->enable_flag_bits(erhe::Item_flags::show_in_ui);
    m_scene->get_root_node()->enable_flag_bits(erhe::Item_flags::invisible_parent);
    if (enable_physics) {
        m_physics_world = erhe::physics::IWorld::create_unique();
        m_physics_world->set_on_body_activated(
            [this](erhe::physics::IRigid_body* rigid_body) {
                ERHE_VERIFY(rigid_body != nullptr);
                if (rigid_body->get_motion_mode() != erhe::physics::Motion_mode::e_dynamic) {
                    return;
                }
                void* owner = rigid_body->get_owner();
                Node_physics* node_physics = reinterpret_cast<Node_physics*>(owner);
                if (node_physics == nullptr) {
                    return;
                }
                erhe::scene::Node* node = node_physics->get_node();
                if (node == nullptr) {
                    return;
                }
                // Activation events can be dispatched while the simulation is
                // paused (remove_rigid_body() drains the pending queue); the
                // flag must not be set then, or it sticks until the next
                // resume and hierarchy edits stop propagating to the node.
                if (!m_physics_simulation_running) {
                    return;
                }
                node->enable_flag_bits(erhe::Item_flags::no_transform_update);
            }
        );
        m_physics_world->set_on_body_deactivated(
            [this](erhe::physics::IRigid_body* rigid_body) {
                ERHE_VERIFY(rigid_body != nullptr);
                //if (rigid_body->get_motion_mode() != erhe::physics::Motion_mode::e_dynamic) {
                //    return;
                //}
                void* owner = rigid_body->get_owner();
                Node_physics* node_physics = reinterpret_cast<Node_physics*>(owner);
                if (node_physics == nullptr) {
                    return;
                }
                erhe::scene::Node* node = node_physics->get_node();
                if (node == nullptr) {
                    return;
                }
                node->disable_flag_bits(erhe::Item_flags::no_transform_update);
            }
        );
        m_physics_world->set_on_trigger_enter(
            [this](const erhe::physics::Trigger_event& event) {
                add_trigger_event(true, event);
            }
        );
        m_physics_world->set_on_trigger_exit(
            [this](const erhe::physics::Trigger_event& event) {
                add_trigger_event(false, event);
            }
        );
    }

    m_raytrace_scene = erhe::raytrace::IScene::create_unique("rt_root_scene");

    // NOTE: register_to_editor_scenes() must be called by the caller after
    // make_shared<Scene_root>() returns, because shared_from_this() cannot
    // be used during construction.

    if (app_message_bus != nullptr) {
        m_selection_subscription = app_message_bus->selection.subscribe(
            [this](Selection_message& message) {
                Selection_change& selection_change = message.selection_change;
                for (const auto& item : selection_change.no_longer_selected) {
                    if (item->get_item_host() != this) {
                        continue;
                    }
                    const auto& node = std::dynamic_pointer_cast<erhe::scene::Node>(item);
                    if (!node) {
                        continue;
                    }
                    const auto& node_physics = erhe::scene::get_attachment<Node_physics>(node.get());
                    if (!node_physics) {
                        continue;
                    }
                    auto* rigid_body = node_physics->get_rigid_body();
                    if (rigid_body == nullptr) {
                        continue;
                    }
                    log_physics->trace("release physics: {}", node->describe());
                    node_physics->end_interaction();
                    const auto i = std::remove(m_physics_disabled_nodes.begin(), m_physics_disabled_nodes.end(), item);
                    if (i == m_physics_disabled_nodes.end()) {
                        log_physics->error("node {} not in physics disabled nodes", item->get_name());
                    } else {
                        m_physics_disabled_nodes.erase(i, m_physics_disabled_nodes.end());
                    }
                }

                for (const auto& item : selection_change.newly_selected) {
                    if (item->get_item_host() != this) {
                        continue;
                    }
                    const auto node = std::dynamic_pointer_cast<erhe::scene::Node>(item);
                    if (!node) {
                        continue;
                    }
                    const auto node_physics = erhe::scene::get_attachment<Node_physics>(node.get());
                    if (!node_physics) {
                        continue;
                    }
                    auto* rigid_body = node_physics->get_rigid_body();
                    if (rigid_body == nullptr) {
                        continue;
                    }
                    log_physics->trace("acquire physics: {}", node->describe());
                    node_physics->begin_interaction();

                    const auto i = std::find(m_physics_disabled_nodes.begin(), m_physics_disabled_nodes.end(), item);
                    if (i != m_physics_disabled_nodes.end()) {
                        log_physics->warn("node {} already in physics disabled nodes", item->get_name());
                    } else {
                        m_physics_disabled_nodes.push_back(item);
                    }
                }
            }
        );
        // Content taken out of the editor without a scene closing - an undo of
        // the import that brought a variant set in - takes the set out of the
        // table, so a dead set is never offered (AGENTS.md "Scene-hosted
        // references in editor parts").
        m_items_removed_subscription = app_message_bus->items_removed.subscribe(
            [this](Items_removed_message& message) {
                m_variant_table.on_items_removed(*message.removed.get());
                on_items_removed(*message.removed.get());
            }
        );
    }
}

void Scene_root::on_items_removed(const Removed_items& removed)
{
    // A brush keeps the material a placed instance gets; when that material
    // leaves the editor (undo of the import or of the create that brought it
    // in) the brush lets go, or the dead material lives on in the library
    // and is handed to the next placement. One set lookup per brush of this
    // scene's library - bounded by the library, not by the message.
    if (!m_content_library) {
        return;
    }
    const std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_content_library->mutex};
    for (const std::shared_ptr<Brush>& brush : m_content_library->get_all<Brush>()) {
        const std::shared_ptr<erhe::primitive::Material>& material = brush->get_material();
        if (material && removed.lookup.contains(material.get())) {
            brush->set_material({});
        }
    }
}

Scene_root::~Scene_root() noexcept
{
    if (m_is_registered) {
        unregister_from_editor_scenes(*m_app_scenes);
    }

    // Draw lists keep registered meshes alive; drop them (and any queued,
    // never-flushed changes) while m_raytrace_scene is still alive so a Mesh
    // released here can still detach its raytrace instances.
    m_draw_list_scene.reset();

    // The Scene and its content (nodes, meshes, node_physics) hold non-owning
    // back-pointers into this Scene_root and the resources it owns (the raytrace
    // scene m_raytrace_scene and physics world m_physics_world). The Scene may be
    // co-owned elsewhere (selection, undo stack, clipboard, ...) and outlive this
    // host: closing a still-selected scene destroys the Scene_root while the
    // Selection keeps a shared_ptr to the Scene, whose deferred ~Scene then runs
    // after this host and its resources are gone. Detach all content now, while
    // m_raytrace_scene and m_physics_world are still alive, so the later teardown
    // does not dereference freed state (see node_sanity_check,
    // Mesh::detach_rt_from_scene, Node_physics world removal).
    if (m_scene) {
        m_scene->sever_host();
    }

    // Library items (and possibly the library itself, via browser windows or
    // clipboard/selection references) can outlive this host; detach them now
    // so no item keeps a dangling Item_host pointer.
    if (m_content_library) {
        m_content_library->set_owner(nullptr, {});
    }
}

namespace {

// Early-exit depth-first check used to gate the Add Bone Tip Nodes context
// menu entry; only runs while the popup is open.
[[nodiscard]] auto subtree_contains_bone(const erhe::scene::Node& node) -> bool
{
    if (erhe::scene::is_bone(&node)) {
        return true;
    }
    for (const std::shared_ptr<erhe::Hierarchy>& child : node.get_children()) {
        const erhe::scene::Node* const child_node = dynamic_cast<const erhe::scene::Node*>(child.get());
        if ((child_node != nullptr) && subtree_contains_bone(*child_node)) {
            return true;
        }
    }
    return false;
}

} // anonymous namespace

auto Scene_root::make_browser_window(
    erhe::imgui::Imgui_renderer& imgui_renderer,
    erhe::imgui::Imgui_windows&  imgui_windows,
    App_context&                 context,
    App_settings&                app_settings
) -> std::shared_ptr<Item_tree_window>
{
    // Scene-independent, slot-based window identity (issue #265): both the
    // window title ("Scene Hierarchy [N]") and the ini label
    // ("Hierarchy_window N") carry the lowest free slot (1, 2, ...) among the
    // live Hierarchy windows, so the imgui.ini dock layout and the
    // windows.json open state persist across sessions regardless of scene
    // names or the order scenes were opened in.
    int window_slot = 1;
    for (;;) {
        bool slot_in_use = false;
        for (erhe::imgui::Imgui_window* window : imgui_windows.get_windows()) {
            Item_tree_window* tree_window = dynamic_cast<Item_tree_window*>(window);
            if ((tree_window != nullptr) && (tree_window->get_scene_hierarchy_slot() == window_slot)) {
                slot_in_use = true;
                break;
            }
        }
        if (!slot_in_use) {
            break;
        }
        ++window_slot;
    }
    m_node_tree_window = std::make_shared<Item_tree_window>(
        imgui_renderer,
        imgui_windows,
        context,
        fmt::format("Scene Hierarchy [{}]", window_slot),
        fmt::format("Hierarchy_window {}", window_slot)
    );
    m_node_tree_window->set_scene_hierarchy(true);
    m_node_tree_window->set_scene_hierarchy_slot(window_slot);
    m_node_tree_window->set_root(m_scene->get_root_node());
    // Show a selectable Scene item at the top of the Hierarchy window, with the
    // Content Library nested under it (issue #240). The scene root node's child
    // nodes continue to render at top level via set_root above.
    m_node_tree_window->set_header_item(get_scene_item());
    m_node_tree_window->set_item_filter(
        app_settings.node_tree_show_all
            ? erhe::Item_filter{
                .require_all_bits_set           = 0,
                .require_at_least_one_bit_set   = 0,
                .require_all_bits_clear         = 0,//erhe::Item_flags::tool | erhe::Item_flags::brush,
                .require_at_least_one_bit_clear = 0
            }
            : erhe::Item_filter{
                .require_all_bits_set           = 0,
                .require_at_least_one_bit_set   = erhe::Item_flags::show_in_ui,
                .require_all_bits_clear         = 0, //erhe::Item_flags::tool | erhe::Item_flags::brush,
                .require_at_least_one_bit_clear = 0
            }
    );
    m_node_tree_window->set_item_callback(
        [this, &context](const std::shared_ptr<erhe::Item_base>& item) -> bool {
            if (!ImGui::IsDragDropActive()) {
                // Texture preview on hover, the same preview the asset browser
                // shows for the source file. The texture is already resident,
                // so nothing is loaded here.
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup)) {
                    const std::shared_ptr<erhe::graphics::Texture> hovered_texture =
                        std::dynamic_pointer_cast<erhe::graphics::Texture>(item);
                    if (hovered_texture) {
                        ImGui::BeginTooltip();
                        ImGui::TextUnformatted(hovered_texture->get_name().c_str());
                        draw_texture_preview(context, hovered_texture, 256.0f);
                        ImGui::EndTooltip();
                    }
                }
                return false;
            }
            const std::shared_ptr<Content_library> library = get_content_library();
            if (!library) {
                return m_node_tree_window->drag_and_drop_target(item);
            }
            // Texture file drop from the asset browser: dropping an image file
            // onto this scene's Textures scope (or a texture in it) imports it
            // into this content library, the same verb the asset browser's
            // context menu offers.
            {
                const std::shared_ptr<erhe::Scope> textures_scope = library->find_scope(erhe::Item_type::texture);
                const bool is_textures_scope = textures_scope && (item == textures_scope);
                const bool is_texture_item   = !is_textures_scope &&
                    (std::dynamic_pointer_cast<erhe::graphics::Texture>(item) != nullptr) &&
                    library->has_item(*item);
                if (is_textures_scope || is_texture_item) {
                    const ImGuiPayload* payload_peek = ImGui::GetDragDropPayload();
                    if ((payload_peek != nullptr) && payload_peek->IsDataType(Asset_file_texture::static_type_name.data())) {
                        if (ImGui::BeginDragDropTarget()) {
                            const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(Asset_file_texture::static_type_name.data());
                            if (payload != nullptr) {
                                erhe::Item_base* payload_item_base = *(static_cast<erhe::Item_base**>(payload->Data));
                                const std::filesystem::path* source_path = (payload_item_base != nullptr)
                                    ? payload_item_base->get_source_path()
                                    : nullptr;
                                if (source_path != nullptr) {
                                    import_texture_into_scene(
                                        context,
                                        std::dynamic_pointer_cast<Scene_root>(shared_from_this()),
                                        *source_path
                                    );
                                }
                            }
                            ImGui::EndDragDropTarget();
                            return true;
                        }
                    }
                }
            }
            // Material cross-library drop (migrated from the removed Content
            // Library window, #241 follow-up): dropping a material from another
            // scene's content library onto this scene's Materials scope (or a
            // material in it) copies the material into this library.
            {
                const std::shared_ptr<erhe::Scope> materials_scope = library->find_scope(erhe::Item_type::material);
                const bool is_materials_scope = materials_scope && (item == materials_scope);
                const bool is_material_item   = !is_materials_scope &&
                    (std::dynamic_pointer_cast<erhe::primitive::Material>(item) != nullptr) &&
                    library->has_item(*item);
                if (is_materials_scope || is_material_item) {
                    const ImGuiPayload* payload_peek = ImGui::GetDragDropPayload();
                    if ((payload_peek != nullptr) && payload_peek->IsDataType(erhe::primitive::Material::static_type_name.data())) {
                        erhe::Item_base* payload_item_base = *(static_cast<erhe::Item_base**>(payload_peek->Data));
                        const std::shared_ptr<erhe::primitive::Material> source_material =
                            (payload_item_base != nullptr)
                                ? std::dynamic_pointer_cast<erhe::primitive::Material>(payload_item_base->shared_from_this())
                                : std::shared_ptr<erhe::primitive::Material>{};
                        if (source_material && !library->has_item(*source_material) && ImGui::BeginDragDropTarget()) {
                            const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(erhe::primitive::Material::static_type_name.data());
                            if (payload != nullptr) {
                                const std::shared_ptr<erhe::primitive::Material> new_material =
                                    context.asset_manager->create<erhe::primitive::Material>(*this, *source_material);
                                auto op = std::make_shared<Item_insert_remove_operation>(
                                    Item_insert_remove_operation::Parameters{
                                        .context = context,
                                        .item    = new_material,
                                        .parent  = library->get_scope(erhe::Item_type::material),
                                        .mode    = Item_insert_remove_operation::Mode::insert
                                    }
                                );
                                context.operation_stack->queue(op);
                            }
                            ImGui::EndDragDropTarget();
                            return true;
                        }
                    }
                }
            }
            return m_node_tree_window->drag_and_drop_target(item);
        }
    );
    m_node_tree_window->set_hover_callback(
        [this, &context]() {
            context.app_message_bus->hover_scene_item_tree.send_message(
                Hover_scene_item_tree_message{
                    .scene_root = dynamic_pointer_cast<Scene_root>(shared_from_this())
                }
            );
        }
    );
    m_node_tree_window->add_item_context_menu_callback(
        [this, &context](
            const std::shared_ptr<erhe::Item_base>& item,
            std::vector<std::function<void()>>&     deferred_operations,
            bool&                                   close
        ) {
            const auto& node = std::dynamic_pointer_cast<erhe::scene::Node>(item);
            if (!node) {
                return;
            }
            // "Create": every prim kind the editor creates, each landing as a
            // child of the clicked prim (any prim parents any prim,
            // doc/usd-compatibility-plan.md C5). The catalog's child-prim
            // entries (Mesh, Camera, Light) sit beside the kinds that only
            // Scene_commands builds (Xform, Scope, Rendertarget, Layout).
            // Structure protection (doc/usd-compatibility-plan.md X2):
            // nothing is created under a reference instance carrier or
            // inside one; the whole menu is greyed with the reason.
            const std::optional<std::string> child_refusal = instance_child_refusal(*node);
            if (child_refusal.has_value()) {
                ImGui::BeginDisabled();
            }
            if (ImGui::BeginMenu("Create")) {
                if (ImGui::MenuItem("Xform")) {
                    deferred_operations.push_back(
                        [&context, node]() {
                            context.scene_commands->create_new_xform(node.get());
                        }
                    );
                    close = true;
                }
                // A Scope: children and nothing else (C5); the entry the
                // content library's "Create Folder" became.
                if (ImGui::MenuItem("Scope")) {
                    deferred_operations.push_back(
                        [&context, node]() {
                            context.scene_commands->create_new_scope(node.get());
                        }
                    );
                    close = true;
                }
                for (const Attachment_type_info& type_info : get_attachment_types()) {
                    if (type_info.kind != Attachment_kind::child_prim) {
                        continue;
                    }
                    const bool can_add = type_info.can_add(*node);
                    if (ImGui::MenuItem(std::string{type_info.display_name}.c_str(), nullptr, false, can_add)) {
                        deferred_operations.push_back(
                            [&context, node, make = type_info.make]() {
                                make(*context.scene_commands, *node);
                            }
                        );
                        close = true;
                    }
                }
                if (ImGui::MenuItem("Rendertarget")) {
                    deferred_operations.push_back(
                        [&context, node]() {
                            context.scene_commands->create_new_rendertarget(node.get());
                        }
                    );
                    close = true;
                }
                // An Xform carrying a Layout attachment.
                if (ImGui::MenuItem("Layout")) {
                    deferred_operations.push_back(
                        [&context, node]() {
                            context.scene_commands->create_new_layout(node.get());
                        }
                    );
                    close = true;
                }
                ImGui::EndMenu();
            }
            if (child_refusal.has_value()) {
                ImGui::EndDisabled();
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                    ImGui::SetTooltip("%s", child_refusal.value().c_str());
                }
            }
            // Rigging: offered only when the clicked subtree contains a bone
            // (early-exit walk; runs only while the popup is open).
            if (subtree_contains_bone(*node)) {
                if (ImGui::MenuItem("Add Bone Tip Nodes")) {
                    deferred_operations.push_back(
                        [&context, node]() {
                            context.scene_commands->add_bone_tip_nodes(node);
                        }
                    );
                    close = true;
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(
                        "Add an empty child node at the tip of every leaf bone in the\n"
                        "selected subtrees (this subtree when nothing relevant is selected)"
                    );
                }
            }
            // "Add Attachment": the catalog's applied-API-schema entries (issue
            // #249), each entry disabled when the node cannot take that kind.
            // Joint keeps its richer connect-to-selection behaviour instead of
            // the catalog make.
            if (ImGui::BeginMenu("Add Attachment")) {
                for (const Attachment_type_info& type_info : get_attachment_types()) {
                    if (type_info.kind != Attachment_kind::api_schema) {
                        continue;
                    }
                    const bool can_add = type_info.can_add(*node);
                    if (type_info.key == "joint") {
                        if (ImGui::MenuItem("Joint", nullptr, false, can_add)) {
                            deferred_operations.push_back(
                                [&context, node]() {
                                    // Connect to the first selected node other than
                                    // the menu node, when there is one in this scene.
                                    std::shared_ptr<erhe::scene::Node> connected{};
                                    for (const std::shared_ptr<erhe::Item_base>& selected_item : context.selection->get_selected_items()) {
                                        const std::shared_ptr<erhe::scene::Node> other = std::dynamic_pointer_cast<erhe::scene::Node>(selected_item);
                                        if (other && (other != node) && (other->get_item_host() == node->get_item_host())) {
                                            connected = other;
                                            break;
                                        }
                                    }
                                    context.scene_commands->create_new_joint(node.get(), connected);
                                }
                            );
                            close = true;
                        }
                        continue;
                    }
                    if (ImGui::MenuItem(std::string{type_info.display_name}.c_str(), nullptr, false, can_add)) {
                        deferred_operations.push_back(
                            [&context, node, make = type_info.make]() {
                                make(*context.scene_commands, *node);
                            }
                        );
                        close = true;
                    }
                }
                ImGui::EndMenu();
            }

            // "Remove Attachment": one undoable pure-detach entry per existing
            // attachment (only shown when the node has any).
            const std::vector<std::shared_ptr<erhe::scene::Node_attachment>>& attachments = node->get_attachments();
            if (!attachments.empty() && ImGui::BeginMenu("Remove Attachment")) {
                for (const std::shared_ptr<erhe::scene::Node_attachment>& attachment : attachments) {
                    std::string label = fmt::format("{} '{}'", attachment->get_type_name(), attachment->get_name());
                    if (ImGui::MenuItem(label.c_str())) {
                        deferred_operations.push_back(
                            [&context, attachment]() {
                                context.scene_commands->remove_attachment(attachment);
                            }
                        );
                        close = true;
                    }
                }
                ImGui::EndMenu();
            }

            // Lightmapped (undoable): the lightmapped property is inherited
            // down the node tree (D23), so the recursive command writes the
            // local value on the clicked node - or on every selected node
            // when the clicked node is selected (the Cut / Delete convention
            // above) - and clears the local values of every node and mesh
            // below it, so the subtree follows the root afterward. The
            // items are collected in the deferred operation (selection can
            // change between the click and the deferred run); items whose
            // local state already matches are skipped, so undo is an exact
            // inverse.
            {
                const auto queue_lightmap_flag = [&context, node, &deferred_operations](const bool enable) {
                    deferred_operations.push_back(
                        [&context, node, enable]() {
                            std::vector<std::shared_ptr<erhe::scene::Node>> roots;
                            if (node->is_selected()) {
                                for (const std::shared_ptr<erhe::Item_base>& selected_item : context.selection->get_selected_items()) {
                                    const std::shared_ptr<erhe::scene::Node> selected_node = std::dynamic_pointer_cast<erhe::scene::Node>(selected_item);
                                    if (selected_node) {
                                        roots.push_back(selected_node);
                                    }
                                }
                            }
                            if (roots.empty()) {
                                roots.push_back(node);
                            }
                            const erhe::property::Dependency_property& property = erhe::scene::Mesh::lightmapped_property.get();
                            // Guard against double-collect when the selection
                            // holds both an ancestor and its descendant.
                            std::unordered_set<const erhe::Item_base*> seen;
                            Compound_operation::Parameters             parameters;
                            const auto queue_state = [&](const std::shared_ptr<erhe::Item_base>& item, const std::optional<erhe::property::Local_state>& after) {
                                if (!seen.insert(item.get()).second) {
                                    return;
                                }
                                const std::optional<erhe::property::Local_state> before = item->read_local_state(property);
                                if (before == after) {
                                    return;
                                }
                                parameters.operations.push_back(std::make_shared<Property_set_operation>(item, property, before, after));
                            };
                            const std::function<void(erhe::scene::Node&)> clear_below = [&](erhe::scene::Node& visited_node) {
                                for (const std::shared_ptr<erhe::scene::Node_attachment>& attachment : visited_node.get_attachments()) {
                                    if (attachment) {
                                        queue_state(attachment, std::nullopt);
                                    }
                                }
                                for (const std::shared_ptr<erhe::Hierarchy>& child : visited_node.get_children()) {
                                    const std::shared_ptr<erhe::scene::Node> child_node = std::dynamic_pointer_cast<erhe::scene::Node>(child);
                                    if (child_node) {
                                        queue_state(child_node, std::nullopt);
                                        clear_below(*child_node);
                                    }
                                }
                            };
                            for (const std::shared_ptr<erhe::scene::Node>& root : roots) {
                                queue_state(root, erhe::property::Local_state{erhe::property::Property_value{enable}});
                                clear_below(*root);
                            }
                            if (parameters.operations.empty()) {
                                return;
                            }
                            context.operation_stack->queue(std::make_shared<Compound_operation>(std::move(parameters)));
                        }
                    );
                };
                if (ImGui::MenuItem("Enable Lightmap (Recursive)")) {
                    queue_lightmap_flag(true);
                    close = true;
                }
                if (ImGui::MenuItem("Disable Lightmap (Recursive)")) {
                    queue_lightmap_flag(false);
                    close = true;
                }
            }

            // No-transform-update flag (undoable): set / clear
            // Item_flags::no_transform_update on every node in the clicked
            // node's subtree - or under every selected node when the clicked
            // node is selected (the Cut / Delete convention above). Same
            // deferred-collect + pre-filter scheme as the lightmap flag
            // above, but targeting the nodes themselves; the flag change
            // re-buckets each node in the Scene's transform-update split.
            {
                const auto queue_no_transform_update_flag = [&context, node, &deferred_operations](const bool enable) {
                    deferred_operations.push_back(
                        [&context, node, enable]() {
                            std::vector<std::shared_ptr<erhe::scene::Node>> roots;
                            if (node->is_selected()) {
                                for (const std::shared_ptr<erhe::Item_base>& selected_item : context.selection->get_selected_items()) {
                                    const std::shared_ptr<erhe::scene::Node> selected_node = std::dynamic_pointer_cast<erhe::scene::Node>(selected_item);
                                    if (selected_node) {
                                        roots.push_back(selected_node);
                                    }
                                }
                            }
                            if (roots.empty()) {
                                roots.push_back(node);
                            }
                            // Guard against double-collect when the selection
                            // holds both an ancestor and its descendant.
                            std::unordered_set<const erhe::Item_base*>    seen;
                            std::vector<std::shared_ptr<erhe::Item_base>> nodes;
                            const std::function<void(const std::shared_ptr<erhe::scene::Node>&)> visit = [&](const std::shared_ptr<erhe::scene::Node>& visited_node) {
                                if (seen.insert(visited_node.get()).second && (visited_node->is_no_transform_update() != enable)) {
                                    nodes.push_back(visited_node);
                                }
                                for (const std::shared_ptr<erhe::Hierarchy>& child : visited_node->get_children()) {
                                    const std::shared_ptr<erhe::scene::Node> child_node = std::dynamic_pointer_cast<erhe::scene::Node>(child);
                                    if (child_node) {
                                        visit(child_node);
                                    }
                                }
                            };
                            for (const std::shared_ptr<erhe::scene::Node>& root : roots) {
                                visit(root);
                            }
                            if (nodes.empty()) {
                                return;
                            }
                            auto op = std::make_shared<Item_set_flag_bits_operation>(
                                std::move(nodes),
                                erhe::Item_flags::no_transform_update,
                                enable,
                                enable ? "Set No Transform Update" : "Clear No Transform Update"
                            );
                            context.operation_stack->queue(op);
                        }
                    );
                };
                if (ImGui::MenuItem("Set No Transform Update (Recursive)")) {
                    queue_no_transform_update_flag(true);
                    close = true;
                }
                if (ImGui::MenuItem("Clear No Transform Update (Recursive)")) {
                    queue_no_transform_update_flag(false);
                    close = true;
                }
            }
        }
    );
    // Content-library context menu (migrated from the removed Content Library
    // window, #241 follow-up): the resource verbs on a kind `Scope`, on a
    // folder scope below one, and on a resource prim
    // (doc/usd-compatibility-plan.md U4).
    m_node_tree_window->add_item_context_menu_callback(
        [this, &context](
            const std::shared_ptr<erhe::Item_base>& item,
            std::vector<std::function<void()>>&     deferred_operations,
            bool&                                   close
        ) {
            const std::shared_ptr<Content_library> library = get_content_library();
            if (!library) {
                return;
            }
            const std::shared_ptr<erhe::Scope> scope = std::dynamic_pointer_cast<erhe::Scope>(item);
            const uint64_t scope_kind = scope ? library->find_scope_kind(*scope) : 0;
            App_context* const context_ptr = &context;
            // A scope below a kind scope is a content-library folder
            // (doc/content-library-folders.md D2).
            if (scope && (scope_kind != 0)) {
                if (ImGui::MenuItem("Create Scope")) {
                    deferred_operations.push_back(
                        [context_ptr, scope]() {
                            std::shared_ptr<erhe::Scope> new_scope = std::make_shared<erhe::Scope>("New Scope");
                            new_scope->enable_flag_bits(erhe::Item_flags::show_in_ui);
                            auto op = std::make_shared<Item_insert_remove_operation>(
                                Item_insert_remove_operation::Parameters{
                                    .context = *context_ptr,
                                    .item    = new_scope,
                                    .parent  = scope,
                                    .mode    = Item_insert_remove_operation::Mode::insert
                                }
                            );
                            context_ptr->operation_stack->queue(op);
                        }
                    );
                    close = true;
                }
            }
            // Creating a resource places the prim under the scope the menu was
            // opened on, so a resource created on a folder lands in it.
            if (scope && (scope_kind == erhe::Item_type::material)) {
                if (ImGui::MenuItem("Create Material")) {
                    deferred_operations.push_back(
                        [this, context_ptr, scope]() {
                            const std::shared_ptr<erhe::primitive::Material> new_material =
                                context_ptr->asset_manager->create<erhe::primitive::Material>(
                                    *this,
                                    erhe::primitive::Material_create_info{
                                        .name = "New Material",
                                        .values = {
                                            .base_color = glm::vec3{0.5f, 0.5f, 0.5f},
                                            .roughness  = glm::vec2{0.5f, 0.5f},
                                            .metallic   = 1.0f
                                        }
                                    }
                                );
                            auto op = std::make_shared<Item_insert_remove_operation>(
                                Item_insert_remove_operation::Parameters{
                                    .context = *context_ptr,
                                    .item    = new_material,
                                    .parent  = scope,
                                    .mode    = Item_insert_remove_operation::Mode::insert
                                }
                            );
                            context_ptr->operation_stack->queue(op);
                        }
                    );
                    close = true;
                }
            }
            // doc/property-system.md section 4.12.
            if (scope && (scope_kind == erhe::Item_type::physics_material)) {
                if (ImGui::MenuItem("Create Physics Material")) {
                    deferred_operations.push_back(
                        [context_ptr, scope]() {
                            auto new_material = std::make_shared<erhe::physics::Physics_material>("New Physics Material");
                            auto op = std::make_shared<Item_insert_remove_operation>(
                                Item_insert_remove_operation::Parameters{
                                    .context = *context_ptr,
                                    .item    = new_material,
                                    .parent  = scope,
                                    .mode    = Item_insert_remove_operation::Mode::insert
                                }
                            );
                            context_ptr->operation_stack->queue(op);
                        }
                    );
                    close = true;
                }
            }
            // doc/style-library.md R1: an empty style.
            if (scope && (scope_kind == erhe::Item_type::style)) {
                if (ImGui::MenuItem("Create Style")) {
                    deferred_operations.push_back(
                        [this, context_ptr, scope]() {
                            auto new_style = std::make_shared<Style>(make_unique_style_name(*get_content_library(), "New Style"));
                            auto op = std::make_shared<Item_insert_remove_operation>(
                                Item_insert_remove_operation::Parameters{
                                    .context = *context_ptr,
                                    .item    = new_style,
                                    .parent  = scope,
                                    .mode    = Item_insert_remove_operation::Mode::insert
                                }
                            );
                            context_ptr->operation_stack->queue(op);
                        }
                    );
                    close = true;
                }
            }
            if (scope && (scope_kind == erhe::Item_type::graph_texture)) {
                if (ImGui::MenuItem("Create Graph Texture")) {
                    deferred_operations.push_back(
                        [context_ptr, scope]() {
                            auto new_graph_texture = std::make_shared<Graph_texture>("Graph Texture");
                            auto op = std::make_shared<Item_insert_remove_operation>(
                                Item_insert_remove_operation::Parameters{
                                    .context = *context_ptr,
                                    .item    = new_graph_texture,
                                    .parent  = scope,
                                    .mode    = Item_insert_remove_operation::Mode::insert
                                }
                            );
                            context_ptr->operation_stack->queue(op);
                            // Issue #252: point the Texture Graph window at the
                            // new asset explicitly (no longer via the global
                            // selection).
                            if (context_ptr->texture_graph_window != nullptr) {
                                context_ptr->texture_graph_window->set_target(new_graph_texture);
                            }
                        }
                    );
                    close = true;
                }
            }
            if (scope && (scope_kind == erhe::Item_type::graph_mesh)) {
                if (ImGui::MenuItem("Create Graph Mesh")) {
                    deferred_operations.push_back(
                        [context_ptr, scope]() {
                            auto new_graph_mesh = std::make_shared<Graph_mesh>("Graph Mesh");
                            auto op = std::make_shared<Item_insert_remove_operation>(
                                Item_insert_remove_operation::Parameters{
                                    .context = *context_ptr,
                                    .item    = new_graph_mesh,
                                    .parent  = scope,
                                    .mode    = Item_insert_remove_operation::Mode::insert
                                }
                            );
                            context_ptr->operation_stack->queue(op);
                            // Issue #252: point the Geometry Graph window at the
                            // new asset explicitly (no longer via the global
                            // selection).
                            if (context_ptr->geometry_graph_window != nullptr) {
                                context_ptr->geometry_graph_window->set_target(new_graph_mesh);
                            }
                        }
                    );
                    close = true;
                }
            }

            if (!library->has_item(*item)) {
                return; // not a resource this library lists
            }
            // R7 asset workflow verbs on materials: "Make External" moves a
            // definition into a fresh asset container file (the listing flips
            // to a reference; the next scene save writes an R6 proxy); "Make
            // Internal" copies a referenced material's data into a scene-owned
            // definition and de-links. Neither is undoable (they alter
            // container files / manager state).
            {
                const std::shared_ptr<erhe::primitive::Material> leaf_material =
                    std::dynamic_pointer_cast<erhe::primitive::Material>(item);
                if (leaf_material && (context.asset_manager != nullptr)) {
                    const bool is_external = library->has_item(*leaf_material) && !is_asset_definition(*leaf_material);
                    if (!is_external && is_asset_definition(*leaf_material)) {
                        if (ImGui::MenuItem("Make External")) {
                            deferred_operations.push_back(
                                [this, context_ptr, leaf_material]() {
                                    // Default location; a name collision on disk
                                    // gets a numeric suffix instead of
                                    // overwriting.
                                    const std::filesystem::path directory =
                                        std::filesystem::path{"res"} / "editor" / "assets" / "materials";
                                    std::filesystem::path path = directory / (leaf_material->get_name() + ".glb");
                                    std::error_code error_code;
                                    for (int suffix = 2; std::filesystem::exists(path, error_code) && (suffix < 100); ++suffix) {
                                        path = directory / fmt::format("{} ({}).glb", leaf_material->get_name(), suffix);
                                    }
                                    std::string error;
                                    if (!make_material_external(*context_ptr, *this, leaf_material, path, error)) {
                                        log_scene->warn("Make External failed: {}", error);
                                    }
                                }
                            );
                            close = true;
                        }
                    }
                    if (is_external) {
                        if (ImGui::MenuItem("Make Internal")) {
                            deferred_operations.push_back(
                                [this, context_ptr, leaf_material]() {
                                    std::string error;
                                    if (!make_material_internal(*context_ptr, *this, leaf_material, error)) {
                                        log_scene->warn("Make Internal failed: {}", error);
                                    }
                                }
                            );
                            close = true;
                        }
                    }
                }
            }
            // "Place in Scene": instance a brush at the world origin with the
            // library's first material. The placement shares the brush's
            // primitive (one GPU allocation for every instance of the brush)
            // and inserts undoably.
            {
                const std::shared_ptr<Brush> leaf_brush = std::dynamic_pointer_cast<Brush>(item);
                if (leaf_brush && ImGui::MenuItem("Place in Scene")) {
                    deferred_operations.push_back(
                        [this, context_ptr, leaf_brush]() {
                            std::shared_ptr<erhe::primitive::Material> material;
                            const std::shared_ptr<Content_library>& library_shared = get_content_library();
                            if (library_shared) {
                                const std::vector<std::shared_ptr<erhe::primitive::Material>>& materials =
                                    library_shared->get_all<erhe::primitive::Material>();
                                if (!materials.empty()) {
                                    material = materials.front();
                                }
                            }
                            if (!material) {
                                log_scene->warn("Place in Scene: scene has no material");
                                return;
                            }
                            place_brush_in_scene(
                                *context_ptr, *leaf_brush, *this,
                                glm::mat4{1.0f}, material, 1.0,
                                erhe::physics::Motion_mode::e_static
                            );
                        }
                    );
                    close = true;
                }
            }
            // "Copy to Scene": copy a library resource into another open
            // scene's library. Copies never alias - each library owns its
            // resources; shown only for copyable kinds
            // (copy_library_item_to_library rejects textures and graph assets,
            // which are shared GPU / graph resources).
            const uint64_t copyable_types =
                erhe::Item_type::brush |
                erhe::Item_type::material |
                erhe::Item_type::physics_material |
                erhe::Item_type::collision_filter |
                erhe::Item_type::physics_joint_settings;
            if (((item->get_type() & copyable_types) != 0) && (context.app_scenes != nullptr)) {
                const std::vector<std::shared_ptr<Scene_root>>& scene_roots = context.app_scenes->get_scene_roots();
                bool has_other_scene = false;
                for (const std::shared_ptr<Scene_root>& other : scene_roots) {
                    if (other && (other.get() != this)) {
                        has_other_scene = true;
                        break;
                    }
                }
                if (has_other_scene && ImGui::BeginMenu("Copy to Scene")) {
                    for (const std::shared_ptr<Scene_root>& other : scene_roots) {
                        if (!other || (other.get() == this)) {
                            continue;
                        }
                        if (ImGui::MenuItem(other->get_name().c_str())) {
                            const std::shared_ptr<erhe::Item_base> library_item = item;
                            deferred_operations.push_back(
                                [library_item, other]() {
                                    const std::shared_ptr<Content_library> target_library = other->get_content_library();
                                    if (!target_library) {
                                        return;
                                    }
                                    std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{target_library->mutex};
                                    const std::shared_ptr<erhe::Item_base> copy = copy_library_item_to_library(library_item, *target_library.get());
                                    if (!copy) {
                                        log_scene->warn("Copy to Scene: {} '{}' is not copyable", library_item->get_type_name(), library_item->get_name());
                                    }
                                }
                            );
                            close = true;
                        }
                    }
                    ImGui::EndMenu();
                }
            }
        }
    );
    // Scene row context menu (#240): "Close" tears down this scene. The actual
    // close is queued to the message bus because it destroys ImGui windows
    // (viewports, this browser), which must not happen inside ImGui iteration.
    m_node_tree_window->add_item_context_menu_callback(
        [this, &context](
            const std::shared_ptr<erhe::Item_base>& item,
            std::vector<std::function<void()>>&,
            bool&                                   close
        ) {
            if (item.get() != get_scene_item().get()) {
                return;
            }
            if (ImGui::MenuItem("Close")) {
                context.app_message_bus->close_scene.queue_message(
                    Close_scene_message{
                        .scene_root = std::dynamic_pointer_cast<Scene_root>(shared_from_this())
                    }
                );
                close = true;
            }
        }
    );
    // Prefab instance root context menu: "Load '<source>'" opens the
    // instance's source glTF file as its own scene -- the same entry (and
    // the same File > Load Scene message path) the Asset browser offers on
    // the file itself.
    m_node_tree_window->add_item_context_menu_callback(
        [&context](
            const std::shared_ptr<erhe::Item_base>& item,
            std::vector<std::function<void()>>&,
            bool&                                   close
        ) {
            std::shared_ptr<Prefab_instance> prefab_instance = std::dynamic_pointer_cast<Prefab_instance>(item);
            if (!prefab_instance) {
                const std::shared_ptr<erhe::scene::Node> node = std::dynamic_pointer_cast<erhe::scene::Node>(item);
                if (node) {
                    prefab_instance = erhe::scene::get_attachment<Prefab_instance>(node.get());
                }
            }
            if (!prefab_instance || prefab_instance->get_prefab_source_path().empty()) {
                return;
            }
            const std::filesystem::path& source_path = prefab_instance->get_prefab_source_path();
            const std::string label = fmt::format("Load '{}'", erhe::file::to_string(source_path));
            if (ImGui::MenuItem(label.c_str())) {
                context.app_message_bus->load_scene_file.queue_message(
                    Load_scene_file_message{ .path = source_path }
                );
                close = true;
            }
        }
    );
    // Issue #252: "Open Editor" (graph assets -> their graph editor; a scene ->
    // a new viewport) and "Open Properties" (any item -> a new pinned
    // Properties window), each targeting the item explicitly, decoupled from
    // the global selection. Window creation is deferred (Editor_windows queues
    // it) so it is safe from inside the popup.
    m_node_tree_window->add_item_context_menu_callback(
        [&context](
            const std::shared_ptr<erhe::Item_base>& item,
            std::vector<std::function<void()>>&     deferred_operations,
            bool&                                   close
        ) {
            if (context.editor_windows == nullptr) {
                return;
            }
            if (Editor_windows::item_has_editor(item)) {
                if (ImGui::MenuItem("Open Editor")) {
                    deferred_operations.push_back(
                        [&context, item]() { context.editor_windows->open_editor_for_item(item); }
                    );
                    close = true;
                }
            }
            if (ImGui::MenuItem("Open Properties")) {
                deferred_operations.push_back(
                    [&context, item]() { context.editor_windows->open_properties_for_item(item); }
                );
                close = true;
            }
        }
    );
    return m_node_tree_window;
}

void Scene_root::remove_browser_window()
{
    m_node_tree_window.reset();
}

void Scene_root::register_to_editor_scenes(App_scenes& app_scenes)
{
    ERHE_VERIFY(m_is_registered == false);
    ERHE_VERIFY(m_app_scenes == nullptr);
    m_app_scenes = &app_scenes;
    app_scenes.register_scene_root(shared_from_this());
    m_is_registered = true;
}

void Scene_root::unregister_from_editor_scenes(App_scenes& app_scenes)
{
    ERHE_VERIFY(m_is_registered == true);
    ERHE_VERIFY(m_app_scenes == &app_scenes);
    m_app_scenes = nullptr;
    app_scenes.unregister_scene_root(this);
    m_is_registered = false;
}

void Scene_root::detach_from_editor_scenes(App_scenes& app_scenes)
{
    ERHE_VERIFY(m_is_registered == true);
    ERHE_VERIFY(m_app_scenes == &app_scenes);
    m_app_scenes    = nullptr;
    m_is_registered = false;
}

auto Scene_root::find_hosted_item(const std::string_view name_or_path) -> erhe::Item_base*
{
    return find_item_in_scene_by_reference(*this, name_or_path).get();
}

auto Scene_root::get_host_name() const -> const char*
{
    return "Scene_root";
}

auto Scene_root::get_hosted_scene() -> Scene*
{
    return m_scene.get();
}

void Scene_root::register_node(const std::shared_ptr<erhe::scene::Node>& node)
{
    if (m_scene) {
        m_scene->register_node(node);
    }
}

void Scene_root::unregister_node(const std::shared_ptr<erhe::scene::Node>& node)
{
    if (m_scene) {
        m_scene->unregister_node(node);
    }
}

void Scene_root::register_camera(const std::shared_ptr<erhe::scene::Camera>& camera)
{
    if (m_scene) {
        m_scene->register_camera(camera);
    }
}
void Scene_root::unregister_camera(const std::shared_ptr<erhe::scene::Camera>& camera)
{
    if (m_scene) {
        m_scene->unregister_camera(camera);
    }
}

void Scene_root::begin_mesh_rt_update(const std::shared_ptr<erhe::scene::Mesh>& mesh)
{
    mesh->detach_rt_from_scene();
}

auto Scene_root::get_mesh_rt_mask(erhe::scene::Mesh* mesh) -> uint32_t
{
    if ((mesh != nullptr) && mesh->skin) {
        // GPU-skinned mesh: the raytrace BVH was built from rest-pose
        // vertices and is never refit, so any hit here would correspond
        // to the unposed surface. Drop the role bits the node would
        // otherwise contribute and carry only the `skinned` marker so
        // picking-tool rays (which mask on role bits) skip the instance.
        // The ID renderer covers skinned meshes correctly. To raytrace
        // a skinned mesh on purpose, set ray.mask |= Raytrace_node_mask::skinned.
        return Raytrace_node_mask::skinned;
    }
    if (mesh == nullptr) {
        return 0;
    }
    // A Mesh is a prim (doc/usd-compatibility-plan.md C5): its own flags
    // carry the role bits (content, tool, brush, rendertarget, ...), and
    // any attachment it holds contributes its bits on top.
    uint32_t mask = raytrace_node_mask(*mesh);
    for (const std::shared_ptr<erhe::scene::Node_attachment>& attachment : mesh->get_attachments()) {
        mask = mask | raytrace_node_mask(*attachment);
    }
    log_raytrace->debug("RT mask for mesh '{}' in scene '{}' = {:#x}", mesh->get_name(), m_scene->get_name(), mask);
    return mask;
}

void Scene_root::end_mesh_rt_update(const std::shared_ptr<erhe::scene::Mesh>& mesh)
{
    mesh->set_rt_mask(get_mesh_rt_mask(mesh.get()));
    mesh->attach_rt_to_scene(m_raytrace_scene.get());
}

void Scene_root::register_mesh(const std::shared_ptr<erhe::scene::Mesh>& mesh)
{
    ERHE_VERIFY(mesh);

    log_scene->debug("Registering Mesh '{}' into scene", mesh->get_name());

    mesh->attach_rt_to_scene(m_raytrace_scene.get());
    mesh->set_rt_mask(get_mesh_rt_mask(mesh.get())); // TODO If scene changes, the mesh/node masks need to be updated somehow

    if (m_scene) {
        m_scene->register_mesh(mesh);
    }

    enqueue_mesh_materials(mesh);

    // May run on a worker thread (item attach during async load): enqueue
    // only; flush_draw_lists() applies on the main thread.
    if (m_draw_list_scene) {
        m_draw_list_scene->enqueue_register(
            erhe::scene_renderer::Draw_list_object_create_info{
                .mesh     = mesh,
                .mobility = erhe::scene_renderer::Draw_mobility::dynamic // no static source yet (R10a)
            }
        );
    }

    if (mesh->skin) {
        register_skin(mesh->skin);
    }

    if (erhe::is<Rendertarget_mesh>(mesh)) {
        const std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_rendertarget_meshes_mutex};
        m_rendertarget_meshes.push_back(std::dynamic_pointer_cast<Rendertarget_mesh>(mesh));
    }

    // Make sure the materials this scene DEFINES are listed. A material this
    // scene defines (is_asset_definition: this scene's container record is
    // its defining container) is placed under the Materials scope; any other
    // material - a mesh migrating between scenes (e.g. the Hotbar
    // rendertarget following the active scene), a prefab template resource,
    // another scene's definition, a loaded container's asset - is listed by
    // nobody here: it renders because the mesh binding gives it a slot in
    // this scene's Material_set (enqueue_mesh_materials below), and its
    // membership stays with its owner (doc/usd-compatibility-plan.md U4). A
    // material has a live home when this scene defines it, when the asset
    // manager knows it, when it sits in a prim tree (U4: a resource is a prim
    // where it sits, and the tree that holds it is what owns it - this
    // scene's own tree for a material a USD file placed, another scene's for
    // a mesh that migrated), when the prefab library's templates supply it,
    // or when this library lists it. A material with NO live home at all -
    // placed nowhere and known to nobody - means a missing explicit
    // registration at its creation site (R5.2b removed the implicit
    // adoption): warn loudly; rendering keeps working, but nothing claims
    // ownership (a definition must never appear as a side effect of mesh
    // registration).
    Asset_manager* const asset_manager = get_content_library()->get_asset_manager();
    Content_library& material_library = *get_content_library().get();
    for (const auto& primitive : mesh->get_primitives()) {
        if (!primitive.material) {
            continue;
        }
        if (is_asset_definition(*primitive.material)) {
            material_library.add(primitive.material);
        } else {
            const bool sits_in_a_tree = primitive.material->get_parent().lock().operator bool();
            const bool has_live_home =
                sits_in_a_tree ||
                (
                    (asset_manager != nullptr) &&
                    (
                        asset_manager->is_managed(*primitive.material) ||
                        asset_manager->is_prefab_template_material(*primitive.material)
                    )
                );
            if (!has_live_home && !material_library.has_item(*primitive.material)) {
                log_scene->warn(
                    "Material '{}' on mesh '{}' entered scene '{}' unowned and unregistered;"
                    " listing it as a reference without ownership. Register the material explicitly"
                    " at its creation site (R5.2b: ownership never comes from mesh registration).",
                    primitive.material->get_name(),
                    mesh->get_name(),
                    get_name()
                );
            }

        }
    }
}

auto Scene_root::is_asset_definition(const erhe::Item_base& item) const -> bool
{
    // R5.6 classification (asset-manager plan, R5 sub-plan resolution 2):
    // a definition is an asset whose defining container is this scene's
    // record - recorded manager state, never derived from hosting. Hosting
    // says which scene's tree HOLDS a resource, which is not the same
    // question: a scene holds a resource another container defines. Scenes
    // without a record (previews, the tool scene; the manager hook is only
    // armed for registered scenes) define nothing. Keep every
    // definition-vs-reference decision routed through here.
    Asset_manager* const asset_manager = m_content_library ? m_content_library->get_asset_manager() : nullptr;
    if (asset_manager == nullptr) {
        return false;
    }
    return asset_manager->is_defined_by(item, static_cast<const erhe::Item_host*>(this));
}

void Scene_root::unregister_mesh(const std::shared_ptr<erhe::scene::Mesh>& mesh)
{
    ERHE_VERIFY(mesh);

    log_scene->debug("Unregistering Mesh '{}' from scene", mesh->get_name());

    if (m_draw_list_scene) {
        m_draw_list_scene->enqueue_unregister(mesh);
    }

    enqueue_release_mesh_materials(mesh);

    mesh->detach_rt_from_scene();

    if (erhe::is<Rendertarget_mesh>(mesh)) {
        const std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_rendertarget_meshes_mutex};
        const auto rendertarget = std::dynamic_pointer_cast<Rendertarget_mesh>(mesh);
        const auto i = std::remove(m_rendertarget_meshes.begin(), m_rendertarget_meshes.end(), rendertarget);
        if (i == m_rendertarget_meshes.end()) {
            log_scene->error("rendertarget mesh {} not in scene root", rendertarget->get_name());
        } else {
            m_rendertarget_meshes.erase(i, m_rendertarget_meshes.end());
        }
    }

    if (mesh->skin) {
        unregister_skin(mesh->skin);
    }

    if (m_scene) {
        m_scene->unregister_mesh(mesh);
    }

    // TODO reference count? Remove materials from material library
    // auto& material_library = get_content_library()->materials;
    // material_library.remove(m_material);
}

void Scene_root::register_skin(const std::shared_ptr<erhe::scene::Skin>& skin)
{
    // One Skin is shared by every Mesh it skins, and each of those meshes
    // registers it: the scene counts the uses and reports whether this call
    // is the first one, so the joint marking and the message below run once
    // per skin rather than once per skinned mesh.
    if (m_scene && (m_scene->register_skin(skin) == erhe::scene::Skin_registry_change::unchanged)) {
        return;
    }
    // Flag the joint nodes so a bone is identifiable without walking every skin
    // (item tree icon, bone selection mode, bone proxies).
    if (skin) {
        erhe::scene::mark_skin_joints(*skin);
    }
    // Bone_visualization creates the bone proxies for this skin's joints on
    // this message. Queued (see Skin_registered_message) so proxy nodes are
    // not attached mid node-attach traversal. weak_from_this() rather than
    // shared_from_this(): skins can (un)register from inside ~Scene_root
    // (scene close teardown), where shared_from_this() throws bad_weak_ptr.
    // A scene tearing down needs no proxies, and the handler skips a null
    // scene_root.
    if ((m_app_message_bus != nullptr) && skin) {
        m_app_message_bus->skin_registered.queue_message(
            Skin_registered_message{
                .scene_root = weak_from_this().lock(),
                .skin       = skin,
                .registered = true
            }
        );
    }
}

void Scene_root::unregister_skin(const std::shared_ptr<erhe::scene::Skin>& skin)
{
    // The skin leaves the scene with the last Mesh it skins (see
    // register_skin): until then its registration and its bone proxies stand.
    if (m_scene && (m_scene->unregister_skin(skin) == erhe::scene::Skin_registry_change::unchanged)) {
        return;
    }
    // weak_from_this(), not shared_from_this(): see register_skin. The
    // unregister handler only needs the skin.
    if ((m_app_message_bus != nullptr) && skin) {
        m_app_message_bus->skin_registered.queue_message(
            Skin_registered_message{
                .scene_root = weak_from_this().lock(),
                .skin       = skin,
                .registered = false
            }
        );
    }
}

void Scene_root::register_layout(const std::shared_ptr<erhe::scene::Layout>& layout)
{
    if (m_scene) {
        m_scene->register_layout(layout);
    }
}

void Scene_root::unregister_layout(const std::shared_ptr<erhe::scene::Layout>& layout)
{
    if (m_scene) {
        m_scene->unregister_layout(layout);
    }
}

void Scene_root::register_light(const std::shared_ptr<erhe::scene::Light>& light)
{
    if (m_scene) {
        m_scene->register_light(light);
    }
    m_light_set.invalidate();
}

void Scene_root::unregister_light(const std::shared_ptr<erhe::scene::Light>& light)
{
    if (m_scene) {
        m_scene->unregister_light(light);
    }
    m_light_set.invalidate();
}

// Light hook: any thread, mark stale only (Scene_host contract).
void Scene_root::on_light_changed(const std::shared_ptr<erhe::scene::Light>& light)
{
    static_cast<void>(light);
    m_light_set.invalidate();
}

auto Scene_root::get_light_set() -> erhe::scene_renderer::Light_set&
{
    return m_light_set;
}

// The forward Material_set's object references (D1c). These four hooks -
// register / unregister / material changed / primitives changed - are already
// where every mutation of a mesh's materials arrives, so the set needs no
// notification of its own: Mesh::set_primitive_material calls
// on_mesh_material_changed, and add_primitive / set_primitives reach
// on_mesh_primitives_changed / on_mesh_primitive_data_changed through
// notify_primitives_changed().
//
// It is a DIFF against the list the mesh last contributed, which is what keeps
// membership bounded for the previews: the material a thumbnail used is
// released as soon as its mesh stops naming it, rather than holding a slot for
// the session.
//
// Enqueued rather than applied: register_mesh may run on a worker thread
// during an async glTF load (R13). App_scenes' per-frame flush applies them on
// the main thread, before this frame's Material_set::update().
void Scene_root::enqueue_mesh_materials(const std::shared_ptr<erhe::scene::Mesh>& mesh)
{
    if (!mesh) {
        return;
    }
    std::vector<std::shared_ptr<erhe::primitive::Material>> materials;
    const std::vector<erhe::scene::Mesh_primitive>& primitives = mesh->get_primitives();
    materials.reserve(primitives.size());
    for (const erhe::scene::Mesh_primitive& primitive : primitives) {
        if (primitive.material) {
            materials.push_back(primitive.material);
        }
    }
    m_material_set.enqueue_object_materials(
        static_cast<uint64_t>(mesh->get_id()),
        std::span<const std::shared_ptr<erhe::primitive::Material>>{materials}
    );
}

void Scene_root::enqueue_release_mesh_materials(const std::shared_ptr<erhe::scene::Mesh>& mesh)
{
    if (!mesh) {
        return;
    }
    m_material_set.enqueue_release_object(static_cast<uint64_t>(mesh->get_id()));
}

// Draw list hooks: any thread, enqueue only (Scene_host contract).
void Scene_root::on_mesh_primitives_changed(const std::shared_ptr<erhe::scene::Mesh>& mesh)
{
    enqueue_mesh_materials(mesh);
    if (m_draw_list_scene) {
        m_draw_list_scene->enqueue_reregister(mesh);
    }
}

void Scene_root::on_mesh_material_changed(const std::shared_ptr<erhe::scene::Mesh>& mesh)
{
    enqueue_mesh_materials(mesh);
    if (m_draw_list_scene) {
        // A material swap that leaves the mesh's draw list membership alone -
        // the common case, and the one the material preview takes several
        // times per frame - only needs the slot fields of the existing records
        // rewritten. enqueue_material_update() decides that at flush time by
        // re-classifying, and falls back to a full re-register when the
        // entries would move.
        m_draw_list_scene->enqueue_material_update(mesh);
    }
}

void Scene_root::on_mesh_flags_changed(const std::shared_ptr<erhe::scene::Mesh>& mesh, const uint64_t, const uint64_t new_flag_bits)
{
    if (m_draw_list_scene) {
        m_draw_list_scene->enqueue_set_flags(mesh, new_flag_bits);
    }
}

void Scene_root::on_mesh_transform_changed(const std::shared_ptr<erhe::scene::Mesh>& mesh)
{
    if (m_draw_list_scene) {
        m_draw_list_scene->enqueue_transform_update(mesh);
    }
}

void Scene_root::on_mesh_primitive_data_changed(const std::shared_ptr<erhe::scene::Mesh>& mesh)
{
    enqueue_mesh_materials(mesh);
    if (m_draw_list_scene) {
        m_draw_list_scene->enqueue_refresh(mesh);
    }
}

auto Scene_root::get_material_set() -> erhe::scene_renderer::Material_set&
{
    return m_material_set;
}

auto Scene_root::get_material_set() const -> const erhe::scene_renderer::Material_set&
{
    return m_material_set;
}

auto Scene_root::get_draw_list_scene() -> erhe::scene_renderer::Draw_list_scene*
{
    return m_draw_list_scene.get();
}

void Scene_root::flush_draw_lists()
{
    if (!m_draw_list_scene) {
        return;
    }
    // Always flush (even with an empty queue): flush_pending() also runs the
    // per-frame material identity check (R12 material-content edits).
    // item_host_mutex keeps worker-side Buffer_mesh replacement
    // (commit_geometry_buffer_mesh under this mutex) from racing the
    // registration reads. Lock order: item_host_mutex -> pending mutex.
    const std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{item_host_mutex};
    m_draw_list_scene->flush_pending();
}

void Scene_root::register_node_physics(const std::shared_ptr<Node_physics>& node_physics)
{
    if (!m_physics_world) {
        return;
    }
    // An inactive item and everything below it is out of the simulation
    // (doc/usd-compatibility-plan.md X2); the body enters the world when
    // Node_physics::handle_flag_bits_update sees the bit come back.
    if (!node_physics->is_active()) {
        return;
    }
    // No caller registers a body that is already in a world: attach
    // registers only after the old host unregistered, the active-bit flip
    // only after the body left, and recreate_rigid_body unregisters first.
    ERHE_VERIFY(node_physics->get_physics_world() == nullptr);

#ifndef NDEBUG
    const auto i = std::find(m_node_physics.begin(), m_node_physics.end(), node_physics);
    if (i != m_node_physics.end()) {
        auto* node = node_physics->get_node();
        log_physics->error("Node_physics for '{}' already in Scene_root", (node != nullptr) ? node->get_name().c_str() : "");
    } else
#endif
    {
        m_node_physics.push_back(node_physics);
        m_node_physics_sorted = false;
    }

    node_physics->set_physics_world(m_physics_world.get());
    erhe::physics::IRigid_body* rigid_body = node_physics->get_rigid_body();
    if (rigid_body != nullptr) {
        m_physics_world->add_rigid_body(node_physics->get_rigid_body());
    }

    // The newly registered rigid body may be the missing body of a pending
    // Node_joint (scene load / paste order); retry constraint creation.
    for (const auto& node_joint : m_node_joints) {
        static_cast<void>(node_joint->try_create_constraint());
    }
}

void Scene_root::unregister_node_physics(const std::shared_ptr<Node_physics>& node_physics)
{
    if (!m_physics_world) {
        return;
    }
    if (node_physics->get_physics_world() == nullptr) {
        return; // not in the world (an inactive item's body never entered it)
    }

    // Tear down joint constraints referencing this rigid body before it
    // leaves the world; the affected joints return to the pending state.
    erhe::physics::IRigid_body* rigid_body_for_joints = node_physics->get_rigid_body();
    if (rigid_body_for_joints != nullptr) {
        for (const auto& node_joint : m_node_joints) {
            node_joint->handle_rigid_body_removed(rigid_body_for_joints);
        }
    }

    const auto i = std::remove(
        m_node_physics.begin(),
        m_node_physics.end(),
        node_physics
    );
    if (i == m_node_physics.end()) {
        auto* node = node_physics->get_node();
        log_physics->error("Node_physics for '{}' not in Scene_root", (node != nullptr) ? node->get_name().c_str() : "");
    } else {
        m_node_physics.erase(i, m_node_physics.end());
        m_node_physics_sorted = false;
    }

    erhe::physics::IRigid_body* rigid_body = node_physics->get_rigid_body();
    if (rigid_body != nullptr) {
        m_physics_world->remove_rigid_body(node_physics->get_rigid_body());
    }
    node_physics->set_physics_world(nullptr);
}

void Scene_root::register_node_joint(const std::shared_ptr<Node_joint>& node_joint)
{
    if (!m_physics_world) {
        return;
    }

#ifndef NDEBUG
    const auto i = std::find(m_node_joints.begin(), m_node_joints.end(), node_joint);
    if (i != m_node_joints.end()) {
        auto* node = node_joint->get_node();
        log_physics->error("Node_joint for '{}' already in Scene_root", (node != nullptr) ? node->get_name().c_str() : "");
    } else
#endif
    {
        m_node_joints.push_back(node_joint);
    }

    node_joint->set_physics_world(m_physics_world.get());
    // The needed rigid bodies may not be registered yet (scene load / paste
    // order); when this returns false the joint stays pending and is retried
    // from register_node_physics().
    static_cast<void>(node_joint->try_create_constraint());
}

void Scene_root::unregister_node_joint(const std::shared_ptr<Node_joint>& node_joint)
{
    if (!m_physics_world) {
        return;
    }

    const auto i = std::remove(m_node_joints.begin(), m_node_joints.end(), node_joint);
    if (i == m_node_joints.end()) {
        auto* node = node_joint->get_node();
        log_physics->error("Node_joint for '{}' not in Scene_root", (node != nullptr) ? node->get_name().c_str() : "");
    } else {
        m_node_joints.erase(i, m_node_joints.end());
    }

    node_joint->set_physics_world(nullptr);
}

void Scene_root::set_physics_simulation_running(const bool running)
{
    if (running == m_physics_simulation_running) {
        return;
    }
    m_physics_simulation_running = running;
    if (!m_physics_world) {
        return;
    }
    for (const auto& node_physics : m_node_physics) {
        auto* rigid_body = node_physics->get_rigid_body();
        if (rigid_body == nullptr) {
            continue;
        }
        erhe::scene::Node* node = node_physics->get_node();
        if (node == nullptr) {
            continue;
        }
        if (running) {
            // Awake bodies stayed active across the pause; their activation
            // events were consumed long ago, so restore the flag here.
            if (rigid_body->is_active() && (rigid_body->get_motion_mode() == erhe::physics::Motion_mode::e_dynamic)) {
                node->enable_flag_bits(erhe::Item_flags::no_transform_update);
            }
        } else {
            node->disable_flag_bits(erhe::Item_flags::no_transform_update);
        }
    }
}

void Scene_root::before_physics_simulation_steps()
{
    for (const auto& node_physics : m_node_physics) {
        auto* rigid_body = node_physics->get_rigid_body();
        if (rigid_body == nullptr) {
            continue;
        }
        node_physics->before_physics_simulation();
    }
}

void Scene_root::update_physics_simulation_fixed_step(const double dt, const Physics_config& physics)
{
    if (!m_physics_world) {
        return;
    }
    apply_wind_forces(static_cast<float>(dt), physics);
    m_physics_world->update_fixed_step(dt);
}

void Scene_root::apply_wind_forces(const float dt, const Physics_config& physics)
{
    if (!physics.wind_enable) {
        return;
    }
    const float direction_length = glm::length(physics.wind_direction);
    if (direction_length < 1e-6f) {
        return;
    }
    m_wind_time += static_cast<double>(dt);

    const glm::vec3 direction = physics.wind_direction / direction_length;
    // Lateral axis for the turbulence component; when the wind blows straight
    // up or down any horizontal axis serves.
    glm::vec3 lateral = glm::cross(direction, glm::vec3{0.0f, 1.0f, 0.0f});
    const float lateral_length = glm::length(lateral);
    lateral = (lateral_length > 1e-6f) ? (lateral / lateral_length) : glm::vec3{1.0f, 0.0f, 0.0f};

    const float two_pi     = glm::two_pi<float>();
    const float t          = static_cast<float>(m_wind_time);
    const float wavelength = std::max(physics.wind_wavelength, 0.01f);

    for (const std::shared_ptr<Node_physics>& node_physics : m_node_physics) {
        // The physics material carries the receptivity; a body without a
        // material is unaffected (the material default is 0).
        const std::shared_ptr<erhe::physics::Physics_material>& material = node_physics->get_physics_material();
        const float receptivity = material ? material->get_wind_receptivity() : erhe::physics::c_default_wind_receptivity;
        if (receptivity <= 0.0f) {
            continue;
        }
        erhe::physics::IRigid_body* rigid_body = node_physics->get_rigid_body();
        if ((rigid_body == nullptr) || (rigid_body->get_motion_mode() != erhe::physics::Motion_mode::e_dynamic)) {
            continue;
        }
        const glm::vec3 position = rigid_body->get_center_of_mass();
        // Traveling gust wave: phase advances along the wind direction, so
        // plants a wavelength apart move a full cycle out of phase.
        const float phase  = two_pi * (glm::dot(position, direction) / wavelength);
        const float gust   = physics.wind_gust_amplitude * std::sin((two_pi * physics.wind_gust_frequency * t) - phase);
        // Off-frequency secondary wave drives the lateral turbulence so the
        // motion does not read as a single mechanical oscillation.
        const float wobble = std::sin((two_pi * 0.37f * physics.wind_gust_frequency * t) + (2.0f * phase));
        const float speed  = std::max(physics.wind_speed + gust, 0.0f);
        const glm::vec3 wind_velocity = (direction * speed) + (lateral * (physics.wind_turbulence * speed * wobble));
        // Relative-velocity drag: bodies already moving with the wind feel no
        // force, which both damps the response and lets gusts hand energy back.
        const glm::vec3 force = receptivity * (wind_velocity - rigid_body->get_linear_velocity());
        if (glm::dot(force, force) < 1e-8f) {
            continue; // do not wake a sleeping body for a negligible force
        }
        rigid_body->apply_force(force);
    }
}

void Scene_root::after_physics_simulation_steps()
{
    if (!m_physics_world) {
        return;
    }

    // Sort nodes, so that parent transforms are updated before child nodes
    if (!m_node_physics_sorted) {
        std::sort(
            m_node_physics.begin(),
            m_node_physics.end(),
            [](const auto& lhs, const auto& rhs) -> bool {
                erhe::scene::Node* lhs_node = lhs->get_node();
                erhe::scene::Node* rhs_node = rhs->get_node();
                if ((lhs_node == nullptr) || (rhs_node == nullptr)) {
                    return true;
                }
                return lhs_node->get_depth() < rhs_node->get_depth();
            }
        );
        m_node_physics_sorted = true;
    }

    // Owner-write bracket: dirt recorded by these body -> node writes keeps
    // the propagation skip over no_transform_update children (the writeback
    // covers every body-driven node itself); dirt from any other writer
    // carries body-driven subtrees with their edited ancestor instead (see
    // Scene::Transform_owner_writes_scope).
    const erhe::scene::Scene::Transform_owner_writes_scope owner_writes_scope{*m_scene};
    for (const auto& node_physics : m_node_physics) {
        auto* rigid_body = node_physics->get_rigid_body();
        if (rigid_body) {
            if (rigid_body->is_active()) {
                node_physics->after_physics_simulation();
            }
        }
    }
}

auto Scene_root::layers() -> Scene_layers&
{
    return m_layers;
}

auto Scene_root::layers() const -> const Scene_layers&
{
    return m_layers;
}

auto Scene_root::has_physics_world() const -> bool
{
    return static_cast<bool>(m_physics_world);
}

auto Scene_root::get_physics_world() -> erhe::physics::IWorld&
{
    ERHE_VERIFY(m_physics_world);
    return *m_physics_world.get();
}

void Scene_root::add_trigger_event(const bool enter, const erhe::physics::Trigger_event& event)
{
    auto label = [](erhe::physics::IRigid_body* rigid_body) -> const char* {
        return (rigid_body != nullptr) ? rigid_body->get_debug_label() : "<null>";
    };
    ++m_trigger_event_counter;
    if (m_trigger_event_log.size() >= s_max_trigger_event_log_entries) {
        m_trigger_event_log.pop_front();
    }
    m_trigger_event_log.push_back(
        fmt::format(
            "{} {} '{}' {} '{}'",
            m_trigger_event_counter,
            enter ? "enter" : "exit",
            label(event.sensor),
            enter ? "<-" : "->",
            label(event.other)
        )
    );
}

auto Scene_root::get_trigger_event_log() const -> const std::deque<std::string>&
{
    return m_trigger_event_log;
}

auto Scene_root::get_trigger_event_count() const -> uint64_t
{
    return m_trigger_event_counter;
}

void Scene_root::clear_trigger_event_log()
{
    m_trigger_event_log.clear();
}

auto Scene_root::get_raytrace_scene() -> erhe::raytrace::IScene&
{
    ERHE_VERIFY(m_raytrace_scene);
    return *m_raytrace_scene.get();
}

auto Scene_root::get_scene() -> erhe::scene::Scene&
{
    ERHE_VERIFY(m_scene);
    return *m_scene.get();
}

auto Scene_root::get_scene() const -> const erhe::scene::Scene&
{
    ERHE_VERIFY(m_scene);
    return *m_scene.get();
}

auto Scene_root::get_scene_item() -> std::shared_ptr<erhe::scene::Scene>
{
    return m_scene;
}

auto Scene_root::get_name() const -> const std::string&
{
    ERHE_VERIFY(m_scene);
    return m_scene->get_name();
}

auto Scene_root::get_source_path() const -> const std::filesystem::path&
{
    return m_source_path;
}

auto c_str(const Scene_source_format format) -> const char*
{
    switch (format) {
        case Scene_source_format::none: return "none";
        case Scene_source_format::gltf: return "gltf";
        case Scene_source_format::usd:  return "usd";
        default:                        return "none";
    }
}

auto Scene_root::get_source_format() const -> Scene_source_format
{
    return m_source_format;
}

auto Scene_root::get_usd_dome_lights() const -> const std::vector<Usd_dome_light_record>&
{
    return m_usd_dome_lights;
}

void Scene_root::set_usd_dome_lights(std::vector<Usd_dome_light_record>&& dome_lights)
{
    m_usd_dome_lights = std::move(dome_lights);
}

auto Scene_root::get_usd_sublayers() const -> const std::vector<std::string>&
{
    return m_usd_sublayers;
}

void Scene_root::set_usd_sublayers(std::vector<std::string>&& sublayers)
{
    m_usd_sublayers = std::move(sublayers);
}

auto Scene_root::get_usd_time_codes() const -> const Usd_time_code_record&
{
    return m_usd_time_codes;
}

void Scene_root::set_usd_time_codes(const Usd_time_code_record& time_codes)
{
    m_usd_time_codes = time_codes;
}

void Scene_root::set_source_path(const std::filesystem::path& path, const Scene_source_format format)
{
    m_source_path   = path;
    m_source_format = format;
    // R5.3: the scene's container record follows the source path (first
    // save binds it, save-as re-homes it). The open paths call this before
    // register_to_editor_scenes(); the record then picks the path up at
    // registration instead.
    if (m_is_registered && (m_app_scenes != nullptr)) {
        m_app_scenes->notify_scene_source_path_changed(*this);
    }
}

auto Scene_root::get_scene_settings() -> Scene_settings&
{
    return m_scene_settings;
}

auto Scene_root::get_scene_settings() const -> const Scene_settings&
{
    return m_scene_settings;
}

auto Scene_root::get_variant_table() -> Variant_table&
{
    return m_variant_table;
}

auto Scene_root::get_variant_table() const -> const Variant_table&
{
    return m_variant_table;
}

auto Scene_root::select_variant(
    App_context&              context,
    const std::string&        prim_path,
    const std::string&        set_name,
    const std::string&        variant_name,
    const Variant_switch_mode mode
) -> std::string
{
    const std::shared_ptr<Operation> operation = make_select_variant_operation(
        shared_from_this(), prim_path, set_name, variant_name
    );
    if (!operation) {
        return fmt::format(
            "scene '{}' has no variant '{}' in set '{}' on '{}'",
            get_name(), variant_name, set_name, prim_path
        );
    }
    if (mode == Variant_switch_mode::undoable) {
        if (context.operation_stack == nullptr) {
            return "no operation stack";
        }
        context.operation_stack->queue(operation);
    } else {
        operation->execute(context);
    }
    return std::string{};
}

void Scene_root::apply_variant_selections(App_context& context)
{
    // The file's own selection is already applied by the importer; only an
    // entry that names a different variant has anything to do.
    // A COPY: applying a selection rewrites m_scene_settings.variant_selections.
    const std::vector<Variant_selection> selections = m_scene_settings.variant_selections;
    for (const Variant_selection& selection : selections) {
        const Variant_set* const set = m_variant_table.find(selection.prim_path, selection.set_name);
        if (set == nullptr) {
            log_scene->warn(
                "scene '{}': variant selection '{}' names set '{}' on '{}', which the scene does not carry",
                get_name(), selection.variant_name, selection.set_name, selection.prim_path
            );
            continue;
        }
        if (set->selected == selection.variant_name) {
            continue;
        }
        const std::string error = select_variant(
            context, selection.prim_path, selection.set_name, selection.variant_name, Variant_switch_mode::immediate
        );
        if (!error.empty()) {
            log_scene->warn("scene '{}': {}", get_name(), error);
        }
    }
}

auto Scene_root::get_scene_id() -> const std::string&
{
    if (m_scene_settings.scene_id.empty()) {
        // Creation timestamp (UTC, second granularity - human-readable
        // provenance) + 64 random bits (uniqueness).
        const auto now = std::chrono::system_clock::now();
        const std::time_t now_time = std::chrono::system_clock::to_time_t(now);
        std::tm utc{};
#if defined(_WIN32)
        gmtime_s(&utc, &now_time);
#else
        gmtime_r(&now_time, &utc);
#endif
        std::random_device device;
        const uint64_t random_bits = (static_cast<uint64_t>(device()) << 32) ^ static_cast<uint64_t>(device());
        m_scene_settings.scene_id = fmt::format(
            "{:04}{:02}{:02}-{:02}{:02}{:02}-{:016x}",
            utc.tm_year + 1900, utc.tm_mon + 1, utc.tm_mday,
            utc.tm_hour, utc.tm_min, utc.tm_sec,
            random_bits
        );
        log_scene->info("Scene '{}' assigned scene_id {}", get_name(), m_scene_settings.scene_id);
    }
    return m_scene_settings.scene_id;
}

namespace {

// True when the camera reached the scene embedded in content -- under a
// sealed prefab instance or a glTF import wrapper -- rather than being
// authored in the scene itself.
auto is_content_embedded_camera(const erhe::scene::Camera& camera) -> bool
{
    for (const erhe::scene::Node* node = &camera; node != nullptr; node = node->get_parent_node().get()) {
        if ((node->get_flag_bits() & erhe::Item_flags::import_root) != 0) {
            return true;
        }
        if (erhe::scene::get_attachment<Prefab_instance>(node)) {
            return true;
        }
    }
    return false;
}

} // anonymous namespace

auto get_selectable_cameras(const erhe::scene::Scene& scene) -> std::vector<std::shared_ptr<erhe::scene::Camera>>
{
    std::vector<std::shared_ptr<erhe::scene::Camera>> cameras;
    for (const std::shared_ptr<erhe::scene::Camera>& camera : scene.get_cameras()) {
        if (!is_content_embedded_camera(*camera)) {
            cameras.push_back(camera);
        }
    }
    if (cameras.empty()) {
        cameras = scene.get_cameras();
    }
    return cameras;
}

auto get_hosting_scene_root(const erhe::Item_base* const item) -> std::shared_ptr<Scene_root>
{
    if (item == nullptr) {
        return {};
    }
    erhe::Item_host* const host       = item->get_item_host();
    Scene_root* const      scene_root = dynamic_cast<Scene_root*>(host);
    if (scene_root == nullptr) {
        return {};
    }
    return scene_root->shared_from_this();
}

auto Scene_root::camera_combo(
    const char*           label,
    erhe::scene::Camera*& selected_camera,
    const bool            nullptr_option
) const -> bool
{
    int selected_camera_index = 0;
    int index = 0;
    std::vector<const char*>          names;
    std::vector<erhe::scene::Camera*> cameras;
    if (nullptr_option || (selected_camera == nullptr)) {
        names.push_back("(none)");
        cameras.push_back(nullptr);
        ++index; // keep index in sync with the names / cameras entries
    }
    bool selected_present = (selected_camera == nullptr);
    const std::vector<std::shared_ptr<erhe::scene::Camera>> scene_cameras = get_selectable_cameras(get_scene());
    for (const auto& camera : scene_cameras) {
        names.push_back(camera->get_name().c_str());
        cameras.push_back(camera.get());
        if (selected_camera == camera.get()) {
            selected_camera_index = index;
            selected_present = true;
        }
        ++index;
    }
    // The current selection can be a content-embedded camera (not offered
    // above, e.g. set programmatically); keep it listed so the combo
    // reflects the actual selection.
    if (!selected_present) {
        names.push_back(selected_camera->get_name().c_str());
        cameras.push_back(selected_camera);
        selected_camera_index = index;
        ++index;
    }

    const bool camera_changed =
        ImGui::Combo(
            label,
            &selected_camera_index,
            names.data(),
            static_cast<int>(names.size()),
            static_cast<int>(names.size())
        ) &&
        (selected_camera != cameras[selected_camera_index]);
    if (camera_changed) {
        selected_camera = cameras[selected_camera_index];
    }
    return camera_changed;
}

auto Scene_root::camera_combo(
    const char*                           label,
    std::shared_ptr<erhe::scene::Camera>& selected_camera,
    const bool                            nullptr_option
) const -> bool
{
    int selected_camera_index = 0;
    int index = 0;
    std::vector<const char*> names;
    std::vector<std::shared_ptr<erhe::scene::Camera>> cameras;
    if (nullptr_option || (selected_camera == nullptr)) {
        names.push_back("(none)");
        cameras.push_back(nullptr);
        ++index; // keep index in sync with the names / cameras entries
    }
    bool selected_present = (selected_camera == nullptr);
    const std::vector<std::shared_ptr<erhe::scene::Camera>> scene_cameras = get_selectable_cameras(get_scene());
    for (const auto& camera : scene_cameras) {
        names.push_back(camera->get_name().c_str());
        cameras.push_back(camera);
        if (selected_camera == camera) {
            selected_camera_index = index;
            selected_present = true;
        }
        ++index;
    }
    // The current selection can be a content-embedded camera (not offered
    // above, e.g. set programmatically); keep it listed so the combo
    // reflects the actual selection.
    if (!selected_present) {
        names.push_back(selected_camera->get_name().c_str());
        cameras.push_back(selected_camera);
        selected_camera_index = index;
        ++index;
    }

    const bool camera_changed =
        ImGui::Combo(
            label,
            &selected_camera_index,
            names.data(),
            static_cast<int>(names.size()),
            static_cast<int>(names.size())
        ) &&
        (selected_camera != cameras[selected_camera_index]);
    if (camera_changed) {
        selected_camera = cameras[selected_camera_index];
    }
    return camera_changed;
}

auto Scene_root::camera_combo(
    const char*                         label,
    std::weak_ptr<erhe::scene::Camera>& selected_camera,
    const bool                          nullptr_option
) const -> bool
{
    int selected_camera_index = 0;
    int index = 0;
    std::vector<const char*> names;
    std::vector<std::weak_ptr<erhe::scene::Camera>> cameras;
    const std::shared_ptr<erhe::scene::Camera> selected = selected_camera.lock();
    if (nullptr_option || !selected) {
        names.push_back("(none)");
        cameras.push_back({});
        ++index; // keep index in sync with the names / cameras entries
    }
    bool selected_present = !selected;
    const std::vector<std::shared_ptr<erhe::scene::Camera>> scene_cameras = get_selectable_cameras(get_scene());
    for (const auto& camera : scene_cameras) {
        names.push_back(camera->get_name().c_str());
        cameras.push_back(camera);
        if (selected == camera) {
            selected_camera_index = index;
            selected_present = true;
        }
        ++index;
    }
    // The current selection can be a content-embedded camera (not offered
    // above, e.g. set programmatically); keep it listed so the combo
    // reflects the actual selection.
    if (!selected_present) {
        names.push_back(selected->get_name().c_str());
        cameras.push_back(selected);
        selected_camera_index = index;
        ++index;
    }

    const bool camera_changed =
        ImGui::Combo(
            label,
            &selected_camera_index,
            names.data(),
            static_cast<int>(names.size()),
            static_cast<int>(names.size())
        ) &&
        (selected_camera.lock() != cameras[selected_camera_index].lock());
    if (camera_changed) {
        selected_camera = cameras[selected_camera_index];
    }
    return camera_changed;
}

void Scene_root::update_pointer_for_rendertarget_meshes(Scene_view* scene_view)
{
    std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock(m_rendertarget_meshes_mutex);

    for (const auto& rendertarget_mesh : m_rendertarget_meshes) {
        rendertarget_mesh->update_pointer(scene_view);
    }
}

auto Scene_root::get_content_library() const -> std::shared_ptr<Content_library>
{
    return m_content_library;
}

void Scene_root::register_prim(const std::shared_ptr<erhe::Typed>& prim)
{
    if (m_content_library) {
        m_content_library->register_prim(prim);
    }
}

void Scene_root::unregister_prim(const std::shared_ptr<erhe::Typed>& prim)
{
    if (m_content_library) {
        m_content_library->unregister_prim(prim);
    }
}

void Scene_root::sanity_check()
{
    m_scene->sanity_check();
}

void Scene_root::imgui()
{
    ImGui::Text("Scene_root %s disabled items:", get_name().c_str());
    for (const auto& item : m_physics_disabled_nodes) {
        ImGui::BulletText("%s", item->describe().c_str());
    }
}

}
