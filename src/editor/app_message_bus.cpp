#include "app_message_bus.hpp"

#include "erhe_utility/thread_roles.hpp"

namespace editor {

App_message_bus::App_message_bus() = default;

void App_message_bus::update()
{
    erhe::utility::Thread_roles::assert_main_thread_in_phase(erhe::utility::Frame_phase::m2_prepare);
    // Only buses with queue_only or both policy have update()
    hover_mesh.update();
    hover_tree_node.update();
    graphics_settings.update();
    load_scene_file.update();
    create_scene.update();
    close_scene.update();
    skin_registered.update();
    bone_changed.update();
    mesh_component_selection_changed.update();
    scene_lighting_changed.update();
}

}
