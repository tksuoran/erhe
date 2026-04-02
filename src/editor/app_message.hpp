#pragma once

#include <filesystem>
#include <memory>
#include <vector>

namespace erhe        { class Item_base; }
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

struct Open_scene_message
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
