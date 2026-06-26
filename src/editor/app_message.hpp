#pragma once

#include <filesystem>
#include <memory>
#include <vector>

namespace erhe        { class Item_base; }
namespace erhe::scene { class Mesh; }
namespace erhe::scene { class Node; }

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
    Scene_view* scene_view{nullptr};
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

struct Open_scene_message
{
    std::shared_ptr<Scene_root> scene_root{};
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

struct Tool_select_message
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
