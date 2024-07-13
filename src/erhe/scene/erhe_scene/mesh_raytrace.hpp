#pragma once

#include "erhe_primitive/buffer_mesh.hpp"
#include "erhe_scene/node_attachment.hpp"

#include <functional>

namespace erhe::geometry {
    class Geometry;
}
namespace erhe::raytrace {
    class IGeometry;
    class IInstance;
    class IScene;
    class Hit;
    class Ray;
}
namespace erhe::primitive {
    class Primitive;
}
namespace erhe::renderer {
    class Line_renderer;
}

namespace erhe::scene {

class Mesh;

class Raytrace_primitive
{
public:
    Raytrace_primitive(Raytrace_primitive&&);
    Raytrace_primitive& operator=(Raytrace_primitive&&);
    ~Raytrace_primitive() noexcept;

    Raytrace_primitive(erhe::scene::Mesh* mesh, std::size_t primitive_index, erhe::raytrace::IGeometry* rt_geometry);
    Raytrace_primitive(const Raytrace_primitive&) = delete;
    Raytrace_primitive& operator=(const Raytrace_primitive&) = delete;

    erhe::scene::Mesh*                         mesh{nullptr};
    std::size_t                                primitive_index{0};
    std::unique_ptr<erhe::raytrace::IInstance> rt_instance;
    std::unique_ptr<erhe::raytrace::IScene>    rt_scene;
};

} // namespace erhe::scene
