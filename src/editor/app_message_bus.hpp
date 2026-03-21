#pragma once

#include "app_message.hpp"
#include "erhe_message_bus/message_bus.hpp"

namespace editor {

class App_message_bus
{
public:
    App_message_bus();

    void update();

    static constexpr auto sync  = erhe::message_bus::Dispatch_policy::sync_only;
    static constexpr auto queue = erhe::message_bus::Dispatch_policy::queue_only;
    static constexpr auto both  = erhe::message_bus::Dispatch_policy::both;

    erhe::message_bus::Message_bus<Hover_scene_view_message,      sync>  hover_scene_view;
    erhe::message_bus::Message_bus<Hover_mesh_message,            both>  hover_mesh;
    erhe::message_bus::Message_bus<Hover_tree_node_message,       queue> hover_tree_node;
    erhe::message_bus::Message_bus<Hover_scene_item_tree_message, sync>  hover_scene_item_tree;
    erhe::message_bus::Message_bus<Selection_message,             sync>  selection;
    erhe::message_bus::Message_bus<Graphics_settings_message,     queue> graphics_settings;
    erhe::message_bus::Message_bus<Node_touched_message,          sync>  node_touched;
    erhe::message_bus::Message_bus<Open_scene_message,            sync>  open_scene;
    erhe::message_bus::Message_bus<Load_scene_file_message,       queue> load_scene_file;
    erhe::message_bus::Message_bus<Tool_select_message,           sync>  tool_select;
    erhe::message_bus::Message_bus<Render_scene_view_message,     sync>  render_scene_view;
    erhe::message_bus::Message_bus<Animation_update_message,      sync>  animation_update;
};

}
