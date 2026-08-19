#pragma once

#include "erhe_primitive/buffer_mesh.hpp"
#include "erhe_primitive/build_info.hpp"
#include "erhe_primitive/enums.hpp"

#include <atomic>
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
    explicit Primitive_raytrace(const GEO::Mesh& mesh);
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
    // Maps a hit triangle index of *this* raytrace geometry to the GEO::Mesh
    // facet it was built from. The mapping is owned here, next to the
    // triangles it indexes, so it can never disagree with the geometry that
    // reported the hit. Proxy and triangle-soup raytraces have no facets and
    // return GEO::NO_INDEX.
    [[nodiscard]] auto get_mesh_facet_from_triangle(const uint32_t triangle) const -> GEO::index_t;

private:
    // Order matters: m_rt_mesh must be destroyed before the buffers
    // it holds allocations from (m_rt_vertex_buffer, m_rt_index_buffer).
    // C++ destroys members in reverse declaration order.
    std::shared_ptr<erhe::buffer::Cpu_buffer>  m_rt_vertex_buffer{};
    std::shared_ptr<erhe::buffer::Cpu_buffer>  m_rt_index_buffer {};
    std::shared_ptr<erhe::raytrace::IGeometry> m_rt_geometry     {};
    Buffer_mesh                                m_rt_mesh;
    std::vector<uint32_t>                      m_triangle_to_mesh_facet{};
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

    // Two locks, see doc/primitive-shape-lock-split-plan.md:
    //
    // - m_build_mutex is held for the duration of the expensive idempotent
    //   builds (geometry conversion, BVH build, buffer mesh build). Its only
    //   job is dedup: a Primitive shared by many meshes (glTF instances,
    //   brush instances) is built exactly once. Long hold times are intended.
    // - m_state_mutex is held only for short reads/writes of the mutable
    //   slots. It is never held across a build, so main-thread state reads
    //   (raytrace hover, the deferred-finalize commits) never wait for a
    //   loader worker.
    //
    // Full lock order: Item_host::item_host_mutex -> m_build_mutex ->
    // m_state_mutex. Both are innermost: no Primitive_shape method touches
    // scene state, so the reverse edge does not exist.
    //
    // Locking lives in the public entry points only; the *_build_locked() /
    // *_state_locked() helpers assume the corresponding lock is already held.
    auto make_geometry() -> bool;
    auto make_raytrace() -> bool;
    // Builds the raytrace from a caller-supplied mesh. Used by shapes that
    // carry a Buffer_mesh with no Geometry of their own (scene_builder's
    // instanced cubes), where the no-arg overload has nothing to build from.
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
    // Builds the Geometry on demand and MAY BLOCK for seconds behind a loader
    // worker building this same shape. Main-thread per-frame code must not
    // call this - use get_geometry_const().
    [[nodiscard]] auto get_geometry                () -> const std::shared_ptr<erhe::geometry::Geometry>&;
    // Never builds, never blocks, never locks. Returns null until the
    // geometry has been published; a shape that still carries only its
    // load-time AABB proxy raytrace reads as null here. The slot is written
    // exactly once (publish-once, see m_geometry_published), so the returned
    // reference stays valid.
    [[nodiscard]] auto get_geometry_const          () const -> const std::shared_ptr<erhe::geometry::Geometry>&;
    [[nodiscard]] auto get_raytrace                () -> Primitive_raytrace&;
    [[nodiscard]] auto get_raytrace                () const -> const Primitive_raytrace&;
    [[nodiscard]] auto get_triangle_soup           () const -> const std::shared_ptr<Triangle_soup>&;
    [[nodiscard]] auto get_element_mappings        () const -> const erhe::primitive::Element_mappings&;
    // Resolves a raytrace hit (the IGeometry that reported it + triangle
    // index) to a GEO::Mesh facet. The geometry may be the current raytrace
    // or the retired proxy a not-yet-refreshed sharer still references; the
    // mapping is looked up on the raytrace that owns that geometry, so the
    // triangle index is always interpreted against the triangles it came
    // from. Unknown geometry / proxy hits yield GEO::NO_INDEX.
    [[nodiscard]] auto get_mesh_facet_from_triangle(const erhe::raytrace::IGeometry* geometry, const uint32_t triangle) const -> GEO::index_t;

protected:
    // Caller holds m_build_mutex. Returns the published geometry (never null
    // on success) so callers do not have to read m_geometry themselves.
    auto make_geometry_build_locked() -> std::shared_ptr<erhe::geometry::Geometry>;
    // Caller holds m_build_mutex. Builds the raytrace aside and installs it
    // under m_state_mutex.
    auto make_raytrace_build_locked(const GEO::Mesh& mesh) -> bool;
    // Caller holds m_state_mutex.
    [[nodiscard]] auto has_real_raytrace_state_locked() const -> bool;

    // Keep this before members - at least m_renderable_mesh - which initialization
    // in constructors uses m_element_mappings.
    erhe::primitive::Element_mappings         m_element_mappings;
    std::shared_ptr<erhe::geometry::Geometry> m_geometry{};
    std::shared_ptr<Triangle_soup>            m_triangle_soup{};
    Primitive_raytrace                        m_raytrace{};
    mutable std::mutex                        m_build_mutex;
    mutable std::mutex                        m_state_mutex;
    // Set (release) right after m_geometry is published under m_state_mutex.
    // m_geometry is written exactly once after construction, never cleared or
    // replaced, so an acquire load of this flag is all a lock-free reader
    // needs. This is about the slot, not the pointee: the Geometry object
    // itself is still mutated in place by e.g. gltf.cpp.
    std::atomic<bool>                         m_geometry_published{false};
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
    // Caller holds m_build_mutex; see the locking note on Primitive_shape.
    auto make_buffer_mesh_build_locked(const Build_info& build_info, Normal_style normal_style) -> bool;
    auto make_buffer_mesh_build_locked(const Buffer_info& buffer_info) -> bool;
    [[nodiscard]] auto has_edge_lines_state_locked() const -> bool;

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
