#pragma once

#include "erhe_primitive/renderable_mesh.hpp"
#include "erhe_primitive/enums.hpp"

#include <memory>
#include <optional>
#include <string_view>

namespace erhe::geometry {
    class Geometry;
}
namespace erhe::raytrace {
    class IBuffer;
    class IGeometry;
}

namespace erhe::primitive {

class Build_info;
class Buffer_info;
class Material;
class Triangle_soup;

class Primitive_raytrace
{
public:
    Primitive_raytrace();
    explicit Primitive_raytrace(erhe::geometry::Geometry& geometry);
    explicit Primitive_raytrace(erhe::primitive::Triangle_soup& triangle_soup);
    Primitive_raytrace(const Primitive_raytrace& other);
    Primitive_raytrace& operator=(const Primitive_raytrace& other);
    Primitive_raytrace(Primitive_raytrace&& old);
    Primitive_raytrace& operator=(Primitive_raytrace&& old);
    ~Primitive_raytrace() noexcept;

    void make_geometry(std::string_view debug_label);

    Renderable_mesh                            m_mesh;
    std::shared_ptr<erhe::raytrace::IBuffer>   m_vertex_buffer{};
    std::shared_ptr<erhe::raytrace::IBuffer>   m_index_buffer {};
    std::shared_ptr<erhe::raytrace::IGeometry> m_rt_geometry  {};
};

class Triangle_soup;

class Primitive
{
public:
    Primitive();
    Primitive(const Primitive& other) noexcept;
    Primitive(Primitive&& old) noexcept;
    Primitive& operator=(const Primitive& other) noexcept;
    Primitive& operator=(Primitive&& old) noexcept;

    explicit Primitive(
        const std::shared_ptr<erhe::geometry::Geometry>& geometry,
        const std::shared_ptr<Material>&                 material = {}
    );

    explicit Primitive(
        Renderable_mesh&&                renderable_mesh,
        const std::shared_ptr<Material>& material = {}
    );
    explicit Primitive(
        const Renderable_mesh&           renderable_mesh,
        const std::shared_ptr<Material>& material = {}
    );

    Primitive(
        const std::shared_ptr<erhe::geometry::Geometry>& geometry,
        const std::shared_ptr<Material>&                 material,
        const Build_info&                                build_info,
        const Normal_style                               normal_style = Normal_style::corner_normals
    );
    Primitive(
        const std::shared_ptr<erhe::geometry::Geometry>& render_geometry,
        const std::shared_ptr<erhe::geometry::Geometry>& collision_geometry,
        const std::shared_ptr<Material>&                 material,
        const Build_info&                                build_info,
        const Normal_style                               normal_style = Normal_style::corner_normals
    );
    Primitive(
        const Triangle_soup&             triangle_soup,
        const std::shared_ptr<Material>& material,
        const Buffer_info&               buffer_info
    );
    Primitive(
        const std::shared_ptr<Triangle_soup>& triangle_soup,
        const std::shared_ptr<Material>&      material = {}
    );
    Primitive(
        const Primitive&                 primitive,
        const std::shared_ptr<Material>& material
    );

    ~Primitive() noexcept;

    void make_renderable_mesh    (const Build_info& build_info, Normal_style normal_style);
    void make_raytrace           ();

    void set_material            (const std::shared_ptr<Material>& material) { m_material = material; }

    auto has_renderable_triangles() const -> bool;
    auto has_raytrace_triangles  () const -> bool;

    auto get_renderable_mesh     () -> Renderable_mesh& { return m_renderable_mesh; }
    auto get_renderable_mesh     () const -> const Renderable_mesh& { return m_renderable_mesh; }
    auto get_geometry            () const -> const std::shared_ptr<erhe::geometry::Geometry>& { return m_geometry; }
    auto get_normal_style        () const -> Normal_style { return m_normal_style; }
    auto get_geometry_raytrace   () -> Primitive_raytrace& { return m_raytrace; }
    auto get_geometry_raytrace   () const -> const Primitive_raytrace& { return m_raytrace; }
    auto get_material            () const -> const std::shared_ptr<Material>& { return m_material; }
    auto get_mutable_material    () -> std::shared_ptr<Material>& { return m_material; }
    auto get_triangle_soup       () const -> const std::shared_ptr<Triangle_soup>& { return m_triangle_soup; }
    auto get_name                () const -> std::string_view;

private:
    std::shared_ptr<erhe::geometry::Geometry> m_geometry       {};
    std::shared_ptr<Material>                 m_material;
    std::shared_ptr<Triangle_soup>            m_triangle_soup;

    Normal_style                              m_normal_style   {Normal_style::none};
    Renderable_mesh                           m_renderable_mesh{};
    Primitive_raytrace                        m_raytrace;
};

Renderable_mesh build_renderable_mesh_from_triangle_soup(const Triangle_soup& triangle_soup, const Buffer_info& buffer_info);

[[nodiscard]] auto primitive_type(Primitive_mode primitive_mode) -> std::optional<gl::Primitive_type>;

} // namespace erhe::primitive
