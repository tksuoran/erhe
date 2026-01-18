#include "erhe_scene/mesh_raytrace.hpp"

#include "erhe_scene/mesh.hpp"
#include "erhe_raytrace/igeometry.hpp"
#include "erhe_raytrace/iinstance.hpp"
#include "erhe_raytrace/iscene.hpp"
#include "erhe_verify/verify.hpp"

#include <fmt/format.h>

namespace erhe::scene {

using erhe::raytrace::IGeometry;
using erhe::raytrace::IInstance;
using erhe::scene::Node_attachment;
using erhe::Item_flags;

Raytrace_primitive::Raytrace_primitive(Raytrace_primitive&&) noexcept            = default;
Raytrace_primitive& Raytrace_primitive::operator=(Raytrace_primitive&&) noexcept = default;
Raytrace_primitive::~Raytrace_primitive() noexcept                               = default;

Raytrace_primitive::Raytrace_primitive(erhe::scene::Mesh* mesh, std::size_t primitive_index, erhe::raytrace::IGeometry* rt_geometry)
    : mesh           {mesh}
    , primitive_index{primitive_index}
{
    ERHE_VERIFY(mesh != nullptr);

    const std::string name = fmt::format("{}[{}]", mesh->get_name(), primitive_index);
    rt_instance = erhe::raytrace::IInstance::create_unique(name);
    rt_scene    = erhe::raytrace::IScene::create_unique(name);
    rt_instance->set_user_data(this);
    rt_instance->set_scene(rt_scene.get());
    rt_scene   ->attach(rt_geometry);
    if (mesh->is_visible()) {
        rt_instance->enable();
    } else {
        rt_instance->disable();
    }
    rt_scene   ->commit();
    rt_instance->commit();
}

}
