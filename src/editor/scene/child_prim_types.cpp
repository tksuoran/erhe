#include "scene/child_prim_types.hpp"

#include "scene/joint.hpp"
#include "scene/scene_commands.hpp"

#include "erhe_item/hierarchy.hpp"
#include "erhe_scene/camera.hpp"
#include "erhe_scene/curve.hpp"
#include "erhe_scene/light.hpp"
#include "erhe_scene/mesh.hpp"

namespace editor {

namespace {

auto make_camera(Scene_commands& sc, erhe::Hierarchy& parent) -> std::shared_ptr<erhe::Hierarchy> { return sc.create_new_camera(&parent); }
auto make_light(Scene_commands& sc, erhe::Hierarchy& parent) -> std::shared_ptr<erhe::Hierarchy> { return sc.create_new_light(&parent); }
auto make_mesh(Scene_commands& sc, erhe::Hierarchy& parent) -> std::shared_ptr<erhe::Hierarchy> { return sc.create_new_mesh(&parent); }
auto make_joint(Scene_commands& sc, erhe::Hierarchy& parent) -> std::shared_ptr<erhe::Hierarchy> { return sc.create_new_joint(&parent); }
auto make_curve(Scene_commands& sc, erhe::Hierarchy& parent) -> std::shared_ptr<erhe::Hierarchy> { return sc.create_new_curve(&parent); }

} // anonymous namespace

auto get_child_prim_types() -> const std::vector<Child_prim_type_info>&
{
    static const std::vector<Child_prim_type_info> catalog = {
        {"mesh",   "Mesh",   make_mesh  },
        {"camera", "Camera", make_camera},
        {"light",  "Light",  make_light },
        {"joint",  "Joint",  make_joint },
        {"curve",  "Curve",  make_curve }
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
