#include "scene/attachment_types.hpp"

#include "scene/node_physics.hpp"
#include "scene/scene_commands.hpp"

#include "erhe_item/hierarchy.hpp"
#include "erhe_item/item.hpp"
#include "erhe_scene/camera.hpp"
#include "erhe_scene/light.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"

#include <memory>

namespace editor {

namespace {

using erhe::scene::Node;

void make_camera(Scene_commands& sc, erhe::Hierarchy& parent) { sc.create_new_camera(&parent); }
void make_light (Scene_commands& sc, erhe::Hierarchy& parent) { sc.create_new_light (&parent); }
void make_mesh  (Scene_commands& sc, erhe::Hierarchy& parent) { sc.create_new_mesh  (&parent); }

auto joint_gate           (const Node&     ) -> bool { return true; } // multiple joints per node are legal

void make_joint           (Scene_commands& sc, Node& node) { sc.create_new_joint            (&node); }

} // anonymous namespace

auto get_child_prim_types() -> const std::vector<Child_prim_type_info>&
{
    static const std::vector<Child_prim_type_info> catalog = {
        {"mesh",   "Mesh",   make_mesh  },
        {"camera", "Camera", make_camera},
        {"light",  "Light",  make_light }
    };
    return catalog;
}

auto get_attachment_types() -> const std::vector<Attachment_type_info>&
{
    static const std::vector<Attachment_type_info> catalog = {
        {"joint",            "Joint",            joint_gate,            make_joint           }
    };
    return catalog;
}

auto find_child_prim_type(std::string_view key) -> const Child_prim_type_info*
{
    for (const Child_prim_type_info& info : get_child_prim_types()) {
        if (info.key == key) {
            return &info;
        }
    }
    return nullptr;
}

auto find_attachment_type(std::string_view key) -> const Attachment_type_info*
{
    for (const Attachment_type_info& info : get_attachment_types()) {
        if (info.key == key) {
            return &info;
        }
    }
    return nullptr;
}

} // namespace editor
