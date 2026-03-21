#include "app_message_bus.hpp"

namespace editor {

App_message_bus::App_message_bus() = default;

void App_message_bus::update()
{
    // Only buses with queue_only or both policy have update()
    hover_mesh.update();
    hover_tree_node.update();
    graphics_settings.update();
    load_scene_file.update();
}

}
