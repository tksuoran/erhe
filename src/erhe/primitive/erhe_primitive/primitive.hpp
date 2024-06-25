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
class Buffer_info;
class Material;
class Triangle_soup;

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
    explicit Geometry_primitive(const std::shared_ptr<erhe::geometry::Geometry>& geometry);
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
    Geometry_primitive(const Triangle_soup& triangle_soup, const Buffer_info& buffer_info);
    ~Geometry_primitive() noexcept;

    void build_from_geometry(const Build_info& build_info, Normal_style normal_style);

    auto get_geometry() const -> const std::shared_ptr<erhe::geometry::Geometry>& { return m_geometry; }
    auto get_normal_style() const -> Normal_style { return m_normal_style; }
    auto get_geometry_mesh() -> Geometry_mesh& { return m_geometry_mesh; }
    auto get_geometry_mesh() const -> const Geometry_mesh& { return m_geometry_mesh; }
    auto get_geometry_raytrace() -> Geometry_raytrace& { return m_raytrace; }

private:
    std::shared_ptr<erhe::geometry::Geometry> m_geometry     {};
    Normal_style                              m_normal_style {Normal_style::none};
    Geometry_mesh                             m_geometry_mesh{};
    Geometry_raytrace                         m_raytrace;
};

class Triangle_soup;

class Primitive
{
public:
    std::shared_ptr<Material>           material;
    std::shared_ptr<Geometry_primitive> geometry_primitive;
    std::shared_ptr<Triangle_soup>      triangle_soup;
};

[[nodiscard]] auto primitive_type(Primitive_mode primitive_mode) -> std::optional<gl::Primitive_type>;

} // namespace erhe::primitive
