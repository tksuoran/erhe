#pragma once

#include "erhe_primitive/buffer_mesh.hpp"
#include "erhe_primitive/build_info.hpp"
#include "erhe_primitive/enums.hpp"

#include <memory>
#include <mutex>
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
    // AABB proxy: a 12-triangle box over the given bounds. Cheap enough for
    // load time, so picking works immediately after a deferred-raytrace
    // import; hits report the right primitive but approximate position /
    // normal and no facet mapping. Replaced by the real triangle raytrace
    // when the deferred finalize task commits (see Primitive_shape).
    explicit Primitive_raytrace(const erhe::math::Aabb& aabb);
    Primitive_raytrace(const Primitive_raytrace&) = delete;
    Primitive_raytrace& operator=(const Primitive_raytrace&) = delete;
    Primitive_raytrace(Primitive_raytrace&&) noexcept;
    Primitive_raytrace& operator=(Primitive_raytrace&&) noexcept;
    ~Primitive_raytrace() noexcept;

    [[nodiscard]] auto has_raytrace_triangles() const -> bool;
    [[nodiscard]] auto is_proxy              () const -> bool;
    void make_raytrace_geometry();

    [[nodiscard]] auto get_raytrace_mesh    () const -> const Buffer_mesh&;
    [[nodiscard]] auto get_raytrace_geometry() const -> const std::shared_ptr<erhe::raytrace::IGeometry>&;

private:
    // Order matters: m_rt_mesh must be destroyed before the buffers
    // it holds allocations from (m_rt_vertex_buffer, m_rt_index_buffer).
    // C++ destroys members in reverse declaration order.
    std::shared_ptr<erhe::buffer::Cpu_buffer>  m_rt_vertex_buffer{};
    std::shared_ptr<erhe::buffer::Cpu_buffer>  m_rt_index_buffer {};
    std::shared_ptr<erhe::raytrace::IGeometry> m_rt_geometry     {};
    Buffer_mesh                                m_rt_mesh;
    bool                                       m_is_proxy{false};
};

class Primitive_shape
{
public:
    Primitive_shape();
    Primitive_shape(const Primitive_shape&) = delete;
    Primitive_shape& operator=(const Primitive_shape&) = delete;
    Primitive_shape(Primitive_shape&& old) noexcept;
    Primitive_shape& operator=(Primitive_shape&& old) noexcept;
    explicit Primitive_shape(const std::shared_ptr<erhe::geometry::Geometry>& geometry);
    explicit Primitive_shape(const std::shared_ptr<Triangle_soup>& triangle_soup);
    ~Primitive_shape() noexcept;

    // Geometry creation and the deferred-finalize prepare/commit steps are
    // serialized on an internal mutex: deferred glTF finalize tasks run
    // make_geometry() on executor workers while on-demand callers (physics
    // import, geometry operations, properties) may call get_geometry()
    // concurrently. The mutex is the innermost lock: nothing here acquires a
    // scene (Item_host) lock while holding it.
    auto make_geometry() -> bool;
    auto make_raytrace() -> bool;
    auto make_raytrace(const GEO::Mesh& mesh) -> bool;
    // Installs an AABB proxy raytrace when no raytrace exists yet.
    auto make_raytrace_proxy(const erhe::math::Aabb& aabb) -> bool;
    // Worker step of the deferred raytrace: ensures Geometry exists and
    // builds the real triangle raytrace aside (no scene lock required).
    // Returns true when a real raytrace exists or is pending.
    auto prepare_real_raytrace() -> bool;
    // Swap step: installs the pending real raytrace built by
    // prepare_real_raytrace(). The caller must hold the item-host (scene)
    // lock of every mesh whose Raytrace_primitives reference this shape and
    // refresh them (Mesh::update_rt_primitives) afterwards. A replaced proxy
    // raytrace is retired but kept alive so raytrace instances of meshes
    // that have not refreshed yet stay valid. Returns true when a swap
    // happened.
    auto commit_real_raytrace() -> bool;
    [[nodiscard]] auto has_raytrace_triangles      () const -> bool;
    [[nodiscard]] auto has_real_raytrace           () const -> bool;
    [[nodiscard]] auto get_geometry                () -> const std::shared_ptr<erhe::geometry::Geometry>&;
    [[nodiscard]] auto get_geometry_const          () const -> const std::shared_ptr<erhe::geometry::Geometry>&;
    [[nodiscard]] auto get_raytrace                () -> Primitive_raytrace&;
    [[nodiscard]] auto get_raytrace                () const -> const Primitive_raytrace&;
    [[nodiscard]] auto get_triangle_soup           () const -> const std::shared_ptr<Triangle_soup>&;
    [[nodiscard]] auto get_element_mappings        () const -> const erhe::primitive::Element_mappings&;
    [[nodiscard]] auto get_mesh_facet_from_triangle(const uint32_t triangle) const -> GEO::index_t;

protected:
    auto make_geometry_locked() -> bool; // caller holds m_mutex

    // Keep this before members - at least m_renderable_mesh - which initialization
    // in constructors uses m_element_mappings.
    erhe::primitive::Element_mappings         m_element_mappings;
    std::shared_ptr<erhe::geometry::Geometry> m_geometry{};
    std::shared_ptr<Triangle_soup>            m_triangle_soup{};
    Primitive_raytrace                        m_raytrace{};
    mutable std::mutex                        m_mutex;
    std::unique_ptr<Primitive_raytrace>       m_pending_raytrace{};
    std::unique_ptr<Primitive_raytrace>       m_retired_proxy_raytrace{};
};

/////////////////////////

class Primitive_render_shape : public Primitive_shape
{
public:
    explicit Primitive_render_shape(const std::shared_ptr<erhe::geometry::Geometry>& geometry);
    explicit Primitive_render_shape(Buffer_mesh&& renderable_mesh);
    explicit Primitive_render_shape(const std::shared_ptr<Triangle_soup>& triangle_soup);

    auto make_buffer_mesh(const Build_info& build_info, Normal_style normal_style) -> bool;
    auto make_buffer_mesh(const Buffer_info& build_info) -> bool;
    [[nodiscard]] auto has_buffer_mesh_triangles  () const -> bool;
    [[nodiscard]] auto has_edge_lines             () const -> bool;
    [[nodiscard]] auto get_mutable_renderable_mesh() -> Buffer_mesh& { return m_renderable_mesh; }
    [[nodiscard]] auto get_renderable_mesh        () const -> const Buffer_mesh& { return m_renderable_mesh; }
    [[nodiscard]] auto get_normal_style           () const -> Normal_style { return m_normal_style; }

    // Deferred edge-lines finalize (glTF import): the load path builds a
    // fill-only buffer mesh straight from the triangle soup; the deferred
    // per-mesh task then prepares a full geometry-based buffer mesh (edge
    // lines, corner / centroid points, element mappings) aside on a worker
    // and commits it under the scene lock. prepare ensures Geometry exists
    // (thread-safe); commit swaps the renderable mesh + element mappings in.
    auto prepare_geometry_buffer_mesh(const Build_info& build_info, Normal_style normal_style) -> bool;
    auto commit_geometry_buffer_mesh() -> bool;

private:
    class Pending_buffer_mesh
    {
    public:
        Buffer_mesh                       buffer_mesh;
        erhe::primitive::Element_mappings element_mappings;
        Normal_style                      normal_style{Normal_style::none};
    };

    Normal_style                         m_normal_style   {Normal_style::none};
    Buffer_mesh                          m_renderable_mesh{};
    std::unique_ptr<Pending_buffer_mesh> m_pending_buffer_mesh{};
};

/////////////////////////

class Primitive
{
public:
    Primitive();
    Primitive(const Primitive&);
    Primitive(Primitive&&) noexcept;
    Primitive& operator=(const Primitive&);
    Primitive& operator=(Primitive&&) noexcept;
    ~Primitive() noexcept;
    explicit Primitive(const std::shared_ptr<Triangle_soup>& triangle_soup);
    explicit Primitive(Buffer_mesh&& renderable_mesh);
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
    [[nodiscard]] auto has_real_raytrace       () const -> bool;
    [[nodiscard]] auto make_geometry           () const -> bool;
    [[nodiscard]] auto make_renderable_mesh    (const Build_info& build_info, Normal_style normal_style) const -> bool;
    [[nodiscard]] auto make_renderable_mesh    (const erhe::primitive::Buffer_info& buffer_info) const -> bool;
    [[nodiscard]] auto make_raytrace           () const -> bool;
    // AABB proxy raytrace over the renderable-mesh bounds; no-op when a
    // raytrace (proxy or real) already exists. See Primitive_raytrace.
    [[nodiscard]] auto make_raytrace_proxy     () const -> bool;
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
