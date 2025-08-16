#pragma once

#include "erhe_primitive/buffer_mesh.hpp"
#include "erhe_primitive/build_info.hpp"
#include "erhe_primitive/enums.hpp"

#include <memory>
#include <optional>
#include <string_view>

namespace GEO { class Mesh; }

namespace erhe::buffer   { class Cpu_buffer; }
namespace erhe::geometry { class Geometry; }
namespace erhe::raytrace { class IGeometry; }

namespace erhe::primitive {

class Build_info;
class Buffer_info;
class Material;
class Triangle_soup;

class Primitive_raytrace
{
public:
    Primitive_raytrace();
    explicit Primitive_raytrace(const GEO::Mesh& mesh, Element_mappings* element_mappings = nullptr);
    explicit Primitive_raytrace(erhe::primitive::Triangle_soup& triangle_soup);
    Primitive_raytrace(const Primitive_raytrace& other);
    Primitive_raytrace& operator=(const Primitive_raytrace& other);
    Primitive_raytrace(Primitive_raytrace&& old);
    Primitive_raytrace& operator=(Primitive_raytrace&& old);
    ~Primitive_raytrace() noexcept;

    auto has_raytrace_triangles() const -> bool;
    void make_raytrace_geometry();

    [[nodiscard]] auto get_raytrace_mesh    () const -> const Buffer_mesh&;
    [[nodiscard]] auto get_raytrace_geometry() const -> const std::shared_ptr<erhe::raytrace::IGeometry>&;

private:
    Buffer_mesh                                m_rt_mesh;
    std::shared_ptr<erhe::buffer::Cpu_buffer>  m_rt_vertex_buffer{};
    std::shared_ptr<erhe::buffer::Cpu_buffer>  m_rt_index_buffer {};
    std::shared_ptr<erhe::raytrace::IGeometry> m_rt_geometry     {};
};

class Primitive_shape
{
public:
    Primitive_shape();
    Primitive_shape(const Primitive_shape& other) noexcept;
    Primitive_shape(Primitive_shape&& old) noexcept;
    Primitive_shape& operator=(const Primitive_shape& other) noexcept;
    Primitive_shape& operator=(Primitive_shape&& old) noexcept;
    explicit Primitive_shape(const std::shared_ptr<erhe::geometry::Geometry>& geometry);
    explicit Primitive_shape(const std::shared_ptr<Triangle_soup>& triangle_soup);
    ~Primitive_shape() noexcept;

    auto make_geometry() -> bool;
    auto make_raytrace() -> bool;
    auto make_raytrace(const GEO::Mesh& mesh) -> bool;
    [[nodiscard]] auto has_raytrace_triangles      () const -> bool;
    [[nodiscard]] auto get_geometry                () -> const std::shared_ptr<erhe::geometry::Geometry>&;
    [[nodiscard]] auto get_geometry_const          () const -> const std::shared_ptr<erhe::geometry::Geometry>&;
    [[nodiscard]] auto get_raytrace                () -> Primitive_raytrace&;
    [[nodiscard]] auto get_raytrace                () const -> const Primitive_raytrace&;
    [[nodiscard]] auto get_triangle_soup           () const -> const std::shared_ptr<Triangle_soup>&;
    [[nodiscard]] auto get_element_mappings        () const -> const erhe::primitive::Element_mappings&;
    [[nodiscard]] auto get_mesh_facet_from_triangle(const uint32_t triangle) const -> GEO::index_t;

protected:
    // Keep this before members - at least m_renderable_mesh - which initialization
    // in constructors uses m_element_mappings.
    erhe::primitive::Element_mappings         m_element_mappings;
    std::shared_ptr<erhe::geometry::Geometry> m_geometry{};
    std::shared_ptr<Triangle_soup>            m_triangle_soup{};
    Primitive_raytrace                        m_raytrace{};
};

/////////////////////////

class Primitive_render_shape : public Primitive_shape
{
public:
    explicit Primitive_render_shape(const std::shared_ptr<erhe::geometry::Geometry>& geometry);
    explicit Primitive_render_shape(Buffer_mesh&& renderable_mesh);
    explicit Primitive_render_shape(const Buffer_mesh& renderable_mesh);
    explicit Primitive_render_shape(const std::shared_ptr<Triangle_soup>& triangle_soup);

    auto make_buffer_mesh(const Build_info& build_info, Normal_style normal_style) -> bool;
    auto make_buffer_mesh(const Buffer_info& build_info) -> bool;
    [[nodiscard]] auto has_buffer_mesh_triangles  () const -> bool;
    [[nodiscard]] auto get_mutable_renderable_mesh() -> Buffer_mesh& { return m_renderable_mesh; }
    [[nodiscard]] auto get_renderable_mesh        () const -> const Buffer_mesh& { return m_renderable_mesh; }
    [[nodiscard]] auto get_normal_style           () const -> Normal_style { return m_normal_style; }

private:
    Normal_style m_normal_style   {Normal_style::none};
    Buffer_mesh  m_renderable_mesh{};
};

/////////////////////////

class Primitive
{
public:
    Primitive();
    Primitive(const Primitive&);
    Primitive(Primitive&&);
    Primitive& operator=(const Primitive&);
    Primitive& operator=(Primitive&&);
    ~Primitive();
    explicit Primitive(const std::shared_ptr<Triangle_soup>& triangle_soup);
    explicit Primitive(const Buffer_mesh& renderable_mesh);
    explicit Primitive(const std::shared_ptr<erhe::geometry::Geometry>& geometry);
    Primitive(
        const std::shared_ptr<erhe::geometry::Geometry>& geometry,
        const Build_info&                                build_info,
        Normal_style                                     normal_style
    );
    Primitive(
        const std::shared_ptr<erhe::geometry::Geometry>& render_geometry,
        const std::shared_ptr<erhe::geometry::Geometry>& collision_geometry
    );

    [[nodiscard]] auto has_renderable_triangles() const -> bool;
    [[nodiscard]] auto has_raytrace_triangles  () const -> bool;
    [[nodiscard]] auto make_geometry           () -> bool;
    [[nodiscard]] auto make_renderable_mesh    (const Build_info& build_info, Normal_style normal_style) -> bool;
    [[nodiscard]] auto make_renderable_mesh    (const erhe::primitive::Buffer_info& buffer_info) -> bool;
    [[nodiscard]] auto make_raytrace           () -> bool;
    [[nodiscard]] auto get_renderable_mesh     () const -> const Buffer_mesh*;
    [[nodiscard]] auto get_name                () const -> std::string_view;
    [[nodiscard]] auto get_bounding_box        () const -> erhe::math::Aabb;
    [[nodiscard]] auto get_shape_for_raytrace  () const -> std::shared_ptr<Primitive_shape>;
    
    std::shared_ptr<Primitive_render_shape> render_shape;
    std::shared_ptr<Primitive_shape>        collision_shape;
};

auto build_buffer_mesh_from_triangle_soup(const Triangle_soup& triangle_soup, const Buffer_info& buffer_info) -> std::optional<Buffer_mesh>;

[[nodiscard]] auto primitive_type(Primitive_mode primitive_mode) -> Primitive_type;

} // namespace erhe::primitive
