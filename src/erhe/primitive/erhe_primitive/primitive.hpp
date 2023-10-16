#pragma once

#include "erhe_primitive/geometry_mesh.hpp"
#include "erhe_primitive/enums.hpp"

#include <memory>
#include <optional>

namespace erhe::geometry {
    class Geometry;
}
namespace erhe::raytrace {
    class IBuffer;
    class IGeometry;
}

namespace erhe::primitive
{

class Build_info;
class Material;

class Geometry_raytrace
{
public:
    Geometry_raytrace();
    explicit Geometry_raytrace(erhe::geometry::Geometry& geometry);
    ~Geometry_raytrace() noexcept;
    Geometry_raytrace& operator=(Geometry_raytrace&& other);

    Geometry_mesh                              rt_geometry_mesh;
    std::shared_ptr<erhe::raytrace::IBuffer>   rt_vertex_buffer{};
    std::shared_ptr<erhe::raytrace::IBuffer>   rt_index_buffer {};
    std::unique_ptr<erhe::raytrace::IGeometry> rt_geometry     {};
};

class Geometry_primitive
{
public:
    explicit Geometry_primitive(
        const std::shared_ptr<erhe::geometry::Geometry>& geometry
    );
    explicit Geometry_primitive(Geometry_mesh&& gl_geometry_mesh);
    Geometry_primitive(
        const std::shared_ptr<erhe::geometry::Geometry>& geometry,
        const Build_info&                                build_info,
        const Normal_style                               normal_style = Normal_style::corner_normals
    );
    Geometry_primitive(
        const std::shared_ptr<erhe::geometry::Geometry>& render_geometry,
        const std::shared_ptr<erhe::geometry::Geometry>& collision_geometry,
        const Build_info&                                build_info,
        const Normal_style                               normal_style = Normal_style::corner_normals
    );
    ~Geometry_primitive() noexcept;

    void build_from_geometry(
        const Build_info&  build_info,
        const Normal_style normal_style
    );

    std::shared_ptr<erhe::geometry::Geometry> source_geometry {};
    Normal_style                              normal_style    {Normal_style::none};
    Geometry_mesh                             gl_geometry_mesh{};
    Geometry_raytrace                         raytrace;
};

class Primitive
{
public:
    std::shared_ptr<Material>           material;
    std::shared_ptr<Geometry_primitive> geometry_primitive;
};

[[nodiscard]] auto primitive_type(Primitive_mode primitive_mode) -> std::optional<igl::PrimitiveType>;

} // namespace erhe::primitive
