#include "scene/child_prim_types.hpp"

#include "scene/scene_commands.hpp"

#include "erhe_item/hierarchy.hpp"

namespace editor {

namespace {

void make_camera(Scene_commands& sc, erhe::Hierarchy& parent) { sc.create_new_camera(&parent); }
void make_light (Scene_commands& sc, erhe::Hierarchy& parent) { sc.create_new_light (&parent); }
void make_mesh  (Scene_commands& sc, erhe::Hierarchy& parent) { sc.create_new_mesh  (&parent); }
void make_joint (Scene_commands& sc, erhe::Hierarchy& parent) { sc.create_new_joint (&parent); }

} // anonymous namespace

auto get_child_prim_types() -> const std::vector<Child_prim_type_info>&
{
    static const std::vector<Child_prim_type_info> catalog = {
        {"mesh",   "Mesh",   make_mesh  },
        {"camera", "Camera", make_camera},
        {"light",  "Light",  make_light },
        {"joint",  "Joint",  make_joint }
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

} // namespace editor
