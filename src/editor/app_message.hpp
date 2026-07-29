#pragma once

#include <filesystem>
#include <memory>
#include <vector>

namespace erhe        { class Item_base; }
namespace erhe::scene { class Mesh; }
namespace erhe::scene { class Node; }
namespace erhe::scene { class Skin; }

struct Graphics_preset_entry;

namespace editor {

class Scene_root;
class Scene_view;

class Selection_change
{
public:
    std::vector<std::shared_ptr<erhe::Item_base>> no_longer_selected{};
    std::vector<std::shared_ptr<erhe::Item_base>> newly_selected    {};
};

struct Hover_scene_view_message
{
    // The scene view now under the pointer (nullptr when the pointer left all
    // viewports).
    Scene_view* scene_view{nullptr};

    // When non-null, this specific Scene_view is being destroyed (its viewport
    // was closed). Handlers that keep a persistent "last hovered" Scene_view*
    // must drop that cache if it matches, because a plain scene_view == nullptr
    // (pointer merely left all viewports) deliberately does NOT clear the
    // last-hover cache -- that cache is what lets File > Save Scene still find
    // the target scene while the pointer is over the menu. Without this, the
    // cache dangles and dereferencing it (get_target_scene_root / get_scene_root)
    // is a use-after-free (#256).
    Scene_view* destroyed_scene_view{nullptr};
};

struct Hover_mesh_message
{
};

struct Hover_tree_node_message
{
    std::shared_ptr<erhe::Item_base> item{};
};

struct Hover_scene_item_tree_message
{
    std::shared_ptr<Scene_root> scene_root{};
};

struct Selection_message
{
    Selection_change selection_change{};
};

// Published when the active scene changes (Selection::set_active_scene_root).
// The active scene is the scene that commands targeting scene-hosted items
// act on; the UI highlights its windows. scene_root is null when no scene is
// explicitly active (consumers fall back via Selection::get_active_scene_root).
struct Active_scene_changed_message
{
    std::shared_ptr<Scene_root> scene_root{};
};

struct Graphics_settings_message
{
    Graphics_preset_entry* graphics_preset{nullptr};
};

enum class Node_touch_source : int {
    operation_stack,
    navigation_gizmo
};

struct Node_touched_message
{
    Node_touch_source  source{Node_touch_source::operation_stack};
    erhe::scene::Node* node  {nullptr};
};

// Announced whenever an editor operation replaces a mesh's primitives via
// Mesh::set_primitives() (a geometry swap). Subscribers that cache anything
// keyed on a mesh's Geometry (currently Mesh_component_selection) reconcile on
// receipt. Sent by the operations that perform the swap (erhe::scene has no
// message bus), mirroring how Node_touched_message is sent by operations.
struct Mesh_geometry_changed_message
{
    std::shared_ptr<erhe::scene::Mesh> mesh{};
};

// Published when a scene_root is first created and registered -- by the
// scene.create startup command, or by loading a scene file. Global editor tools
// that must live inside a scene (Hud, Hotbar, the OpenXR Headset_view) build
// their scene content the first time this fires, since no scene exists at init.
struct Scene_created_message
{
    std::shared_ptr<Scene_root> scene_root{};
};

struct Load_scene_file_message
{
    std::filesystem::path path{};
};

// Published after a scene bundle (#241) has been written to disk, by both the
// File > Save Scene path (Operations::save_scene_to_bundle) and the MCP
// save_scene tool. The Asset_browser subscribes and rescans so the freshly
// saved bundle appears without a manual Scan (#256). The path is the saved
// bundle directory.
struct Scene_saved_message
{
    std::filesystem::path path{};
};

// Queued by the scene.create_new_scene command; handled by Scene_commands.
// Queue-only so the scene (and its ImGui windows) is created by the message
// bus pump, outside ImGui window iteration -- the command fires from a menu,
// which runs inside Imgui_windows::begin_frame() where registering new
// windows is forbidden.
struct Create_scene_message
{
};

// Queued by the Scene row's "Close" context menu entry; handled by Editor.
// Queue-only so the teardown (which destroys the scene's viewport and browser
// ImGui windows) runs from the message bus pump, outside ImGui iteration.
struct Close_scene_message
{
    std::shared_ptr<Scene_root> scene_root{};
};

struct Tool_select_message
{
};

// Published by Scene_root::register_skin / unregister_skin (a skinned mesh
// entered or left the scene). Queue-only on purpose: registration fires from
// inside node-attach traversal (e.g. while a glTF load walks the hierarchy),
// where a sync subscriber that attaches its own nodes - Bone_visualization
// creating bone proxies under the joints - would mutate child lists mid-walk.
// The shared_ptrs keep both alive until the pump, so a scene closed in between
// cannot dangle. scene_root is null when the skin (un)registers from inside
// ~Scene_root (scene close teardown, where no owning shared_ptr exists);
// handlers must skip work that needs the scene in that case.
struct Skin_registered_message
{
    std::shared_ptr<Scene_root>        scene_root{};
    std::shared_ptr<erhe::scene::Skin> skin      {};
    bool                               registered{true};
};

// Published by Mesh_component_selection::set_mode() when the mode actually
// changes. Carries no payload (the enum lives in mesh_component_selection.hpp,
// which includes this header); the mode is already updated when this fires, so
// subscribers read it back via App_context::mesh_component_selection.
struct Mesh_component_mode_changed_message
{
};

struct Render_scene_view_message
{
    Scene_view* scene_view{nullptr};
};

struct Animation_update_message
{
};

}
