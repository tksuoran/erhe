#pragma once

#include "erhe_primitive/buffer_mesh.hpp"
#include "erhe_primitive/build_info.hpp"
#include "erhe_primitive/enums.hpp"
#include "erhe_primitive/triangle_soup.hpp"

#include <geogram/basic/numeric.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <string_view>
#include <unordered_set>
#include <utility>

namespace GEO { class Mesh; }

namespace erhe::buffer   { class Cpu_buffer; }
namespace erhe::geometry { class Geometry; }
namespace erhe::raytrace { class IGeometry; class Prebuilt_bvh; }

namespace erhe::primitive {

class Build_info;
class Buffer_info;
class Material;
class Prepared_primitive_data;
class Triangle_soup;
class Triangle_soup_source;

// CPU memory held by primitive shapes, summed by
// Primitive_shape::add_memory_statistics() for memory reports (allocated
// capacities, in bytes). GPU buffers (Buffer_mesh allocations) are not
// counted: they belong to the mesh memory pools.
class Shape_memory_statistics
{
public:
    std::size_t shape_count               {0};
    std::size_t geometry_count            {0}; // shapes holding an erhe::geometry::Geometry
    std::size_t triangle_soup_count       {0};
    std::size_t triangle_soup_vertex_bytes{0};
    std::size_t triangle_soup_index_bytes {0};
    std::size_t triangle_soup_released_count{0}; // soups released after upload (release_triangle_soup()), not resident
    std::size_t element_mapping_bytes     {0};
    std::size_t prepared_data_count       {0};
    std::size_t prepared_colocation_bytes {0}; // ERHE_mesh_topology vertex points
    std::size_t prepared_bvh_bytes        {0}; // a BVH still inside the prepared data
    std::size_t stored_bvh_bytes          {0}; // the stored BVH waiting for make_prepared_raytrace()
    std::size_t raytrace_count            {0}; // real triangle raytraces
    std::size_t raytrace_proxy_count      {0}; // AABB proxies (current, pending or retired)
    std::size_t raytrace_buffer_bytes     {0}; // the raytrace's own vertex / index copies (Cpu_buffer capacity)
    std::size_t raytrace_facet_map_bytes  {0};
    std::size_t raytrace_bvh_node_bytes   {0}; // backend (erhe::raytrace::Geometry_memory_statistics)
    std::size_t raytrace_primitive_id_bytes{0};
    std::size_t raytrace_precomputed_triangle_bytes{0};
    std::size_t raytrace_pending_prebuilt_bvh_bytes{0};
};

class Primitive_raytrace
{
public:
    Primitive_raytrace();
    explicit Primitive_raytrace(const GEO::Mesh& mesh);
    // Real triangle raytrace straight from a soup, triangle t of the BVH
    // being soup triangle t. A non-null `prebuilt` is moved to the BVH
    // backend before the commit (IGeometry::set_prebuilt_bvh()); whether it
    // was used is get_bvh_source(). The facet mapping is the identity
    // (triangle t is facet t), which holds for a prepared soup core
    // (doc/gltf_extensions/ERHE_mesh_optimized.md contract item 5).
    Primitive_raytrace(const erhe::primitive::Triangle_soup& triangle_soup, std::unique_ptr<erhe::raytrace::Prebuilt_bvh> prebuilt);
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
    // How the BVH of the committed raytrace geometry was obtained
    // (erhe::raytrace::Bvh_source as unsigned int; none without a geometry).
    [[nodiscard]] auto get_bvh_source       () const -> unsigned int;
    // Maps a hit triangle index of *this* raytrace geometry to the GEO::Mesh
    // facet it was built from. The mapping is owned here, next to the
    // triangles it indexes, so it can never disagree with the geometry that
    // reported the hit. A triangle-soup raytrace maps triangle t to facet t
    // (held as a flag, not a table); a proxy has no facets and returns
    // GEO::NO_INDEX.
    [[nodiscard]] auto get_mesh_facet_from_triangle(const uint32_t triangle) const -> GEO::index_t;

    // Adds this raytrace's CPU memory to out. Buffers already in `counted`
    // (shared between raytraces) are not added again.
    void add_memory_statistics(Shape_memory_statistics& out, std::unordered_set<const void*>& counted) const;

private:
    // After the commit: frees the vertex / index copies when the backend
    // does not read them any more (IGeometry::release_buffers()). The
    // Buffer_mesh keeps its ranges and bounds.
    void release_buffers_after_commit();

    // Order matters: m_rt_mesh must be destroyed before the buffers
    // it holds allocations from (m_rt_vertex_buffer, m_rt_index_buffer).
    // C++ destroys members in reverse declaration order.
    std::shared_ptr<erhe::buffer::Cpu_buffer>  m_rt_vertex_buffer{};
    std::shared_ptr<erhe::buffer::Cpu_buffer>  m_rt_index_buffer {};
    std::shared_ptr<erhe::raytrace::IGeometry> m_rt_geometry     {};
    Buffer_mesh                                m_rt_mesh;
    std::vector<uint32_t>                      m_triangle_to_mesh_facet{};
    bool                                       m_identity_facet_map{false}; // triangle t is facet t (soup raytrace)
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
    // A soup shape loaded with validated prepared mesh data (the
    // ERHE_mesh_optimized / ERHE_mesh_topology / ERHE_raytrace_bvh glTF
    // extensions, doc/plans/gltf_fast_load.md). The data describes exactly
    // this soup and is fixed for the lifetime of the shape, except that its
    // `bvh` moves into the shape here (the stored BVH, has_stored_bvh()),
    // which make_prepared_raytrace() hands to the raytrace, so the BVH is
    // never held twice.
    Primitive_shape(const std::shared_ptr<Triangle_soup>& triangle_soup, std::shared_ptr<Prepared_primitive_data> prepared_data);
    ~Primitive_shape() noexcept;

    // Two locks, see doc/erhe/primitive_shape_locking.md:
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
    // - m_reload_mutex is held while a released triangle soup is read back
    //   from its source (get_triangle_soup()), so concurrent callers read the
    //   file once.
    //
    // Full lock order: Item_host::item_host_mutex -> m_build_mutex ->
    // m_reload_mutex -> m_state_mutex. All are innermost: no Primitive_shape
    // method touches scene state, so the reverse edge does not exist.
    //
    // Locking lives in the public entry points only; the *_build_locked() /
    // *_state_locked() helpers assume the corresponding lock is already held.
    auto make_geometry() -> bool;
    auto make_raytrace() -> bool;
    // Builds the raytrace from a caller-supplied mesh. Used by shapes that
    // carry a Buffer_mesh with no Geometry of their own (scene_builder's
    // instanced cubes), where the no-arg overload has nothing to build from.
    auto make_raytrace(const GEO::Mesh& mesh) -> bool;
    // Prepared soup shape (get_prepared_data() with a stored BVH): builds the
    // real raytrace from the soup with the stored BVH and installs it when no
    // raytrace exists yet. Returns false, installing nothing, when the shape
    // has no soup or no stored BVH, or the backend did not use the stored BVH
    // (the caller then takes the proxy + deferred raytrace path).
    auto make_prepared_raytrace() -> bool;
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
    // The triangle soup; null for a shape built from Geometry or a Buffer_mesh.
    // This is the one read path of the soup: a soup released with
    // release_triangle_soup() is read back here from its source, synchronously
    // (callers that run on the main thread and are not one-off user actions
    // read it in a task). While any caller holds the reloaded soup, every
    // other caller gets the same object; when the last one lets go it is
    // freed again. Null, with an error logged by the source, when the reload
    // fails (the source file changed or cannot be read).
    [[nodiscard]] auto get_triangle_soup           () const -> std::shared_ptr<Triangle_soup>;
    // The shape has a soup, resident or released with a source. Never reloads.
    [[nodiscard]] auto has_triangle_soup           () const -> bool;
    // Vertex and index count of the soup, recorded at construction. Never
    // reloads.
    [[nodiscard]] auto get_triangle_soup_counts    () const -> Triangle_soup_counts;
    // Where the soup is read back from after release_triangle_soup(). Set by
    // the loader that made the soup (erhe::gltf with
    // Triangle_soup_reload::record) before the shape is shared.
    void set_triangle_soup_source(std::shared_ptr<const Triangle_soup_source> source);
    [[nodiscard]] auto get_triangle_soup_source    () const -> std::shared_ptr<const Triangle_soup_source>;
    // Frees the in-memory soup of a shape that has a source; later readers
    // reload it through get_triangle_soup(). Returns false, keeping the soup,
    // when the shape has no soup or no source. Idempotent.
    auto release_triangle_soup() -> bool;
    [[nodiscard]] auto is_triangle_soup_released   () const -> bool;
    // The resident soups of both shapes are the same object.
    [[nodiscard]] auto shares_resident_triangle_soup(const Primitive_shape& other) const -> bool;
    // The validated prepared mesh data the shape was loaded with; null when
    // there is none. Immutable: set at construction, never replaced.
    [[nodiscard]] auto get_prepared_data           () const -> const std::shared_ptr<const Prepared_primitive_data>&;
    // The stored BVH of the prepared data is still waiting for
    // make_prepared_raytrace().
    [[nodiscard]] auto has_stored_bvh              () const -> bool;
    // In-place content edits (GPU vertex edits: paint, weight paint, mesh
    // component drags - everything that takes Mesh::begin_optimized_variant_edit())
    // change the shape's data without a new shape, so the prepared data no
    // longer describes it. Marks it so; is_prepared_data_current() is then
    // false for the rest of the shape's life.
    void mark_prepared_data_stale();
    // Prepared data is present and no in-place edit has touched the shape.
    [[nodiscard]] auto is_prepared_data_current    () const -> bool;
    // Mesh <-> vertex-buffer/triangle correspondence of THIS shape's
    // Buffer_mesh. The correspondence is order-dependent, so it lives in the
    // same object as the mesh it describes and the two can never disagree; a
    // differently ordered build is a separate shape (see Primitive).
    [[nodiscard]] auto get_element_mappings        () const -> const erhe::primitive::Element_mappings&;
    // Resolves a raytrace hit (the IGeometry that reported it + triangle
    // index) to a GEO::Mesh facet. The geometry may be the current raytrace
    // or the retired proxy a not-yet-refreshed sharer still references; the
    // mapping is looked up on the raytrace that owns that geometry, so the
    // triangle index is always interpreted against the triangles it came
    // from. Unknown geometry / proxy hits yield GEO::NO_INDEX.
    [[nodiscard]] auto get_mesh_facet_from_triangle(const erhe::raytrace::IGeometry* geometry, const uint32_t triangle) const -> GEO::index_t;
    // Adds the shape's CPU memory to out (memory reports): its triangle
    // soup, element mappings, prepared data, stored BVH and raytraces
    // (current, pending and retired proxy). Objects already in `counted` -
    // a soup or prepared data shared with another shape - are not added
    // again, so one set over many shapes counts each object once. Takes the
    // build and state locks.
    void add_memory_statistics(Shape_memory_statistics& out, std::unordered_set<const void*>& counted) const;

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
    std::shared_ptr<Triangle_soup>            m_triangle_soup{};            // under m_state_mutex; null once released
    std::shared_ptr<const Triangle_soup_source> m_triangle_soup_source{};   // under m_state_mutex
    mutable std::weak_ptr<Triangle_soup>      m_reloaded_triangle_soup{};   // under m_state_mutex
    Triangle_soup_counts                      m_triangle_soup_counts{};     // fixed at construction
    bool                                      m_triangle_soup_released{false}; // under m_state_mutex
    std::shared_ptr<const Prepared_primitive_data> m_prepared_data{};
    std::unique_ptr<erhe::raytrace::Prebuilt_bvh>  m_stored_bvh{};          // under m_build_mutex
    bool                                           m_prepared_data_stale{false}; // under m_state_mutex
    Primitive_raytrace                        m_raytrace{};
    mutable std::mutex                        m_build_mutex;
    mutable std::mutex                        m_reload_mutex;
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
    Primitive_render_shape(const std::shared_ptr<Triangle_soup>& triangle_soup, std::shared_ptr<Prepared_primitive_data> prepared_data);
    // A variant build: a soup that is a REORDERING of another shape's, with the
    // Element_mappings already composed to describe that new order
    // (compose_element_mappings()). Mappings normally arrive from a build, and
    // this build has none to derive them from - the order they describe was
    // decided by the optimizer, not here.
    //
    // Carries no Geometry and must never be asked to make one: its soup is
    // welded, so a Geometry built from it would have neither the source
    // topology nor valid facet ids. See Primitive::optimized_render_shape.
    Primitive_render_shape(const std::shared_ptr<Triangle_soup>& triangle_soup, erhe::primitive::Element_mappings&& element_mappings);
    // The same kind of variant build, from a Buffer_mesh instead of a soup:
    // what the geometry path produces, where the optimizer works on the bytes
    // the build already staged and there is no soup to keep. Carries no
    // Geometry for the same reason as the overload above.
    Primitive_render_shape(Buffer_mesh&& renderable_mesh, erhe::primitive::Element_mappings&& element_mappings);

    // `out_optimized_shape`, when non-null, receives the optimized variant of
    // this build - null unless Buffer_info::optimize_meshes is on and the whole
    // optimized build succeeded. It is handed out rather than stored: the
    // variant slot belongs to the owning Primitive, which is what chooses
    // between builds.
    auto make_buffer_mesh(const Build_info& build_info, Normal_style normal_style, std::shared_ptr<Primitive_render_shape>* out_optimized_shape = nullptr) -> bool;
    auto make_buffer_mesh(const Buffer_info& build_info) -> bool;
    [[nodiscard]] auto has_buffer_mesh_triangles  () const -> bool;
    [[nodiscard]] auto has_edge_lines             () const -> bool;
    // This shape's one Buffer_mesh, paired with the one Element_mappings that
    // describes it. Which BUILD of a primitive a caller wants is chosen a level
    // up, by picking the shape - see Primitive::get_resolved_renderable_mesh().
    [[nodiscard]] auto get_mutable_renderable_mesh() -> Buffer_mesh& { return m_renderable_mesh; }
    [[nodiscard]] auto get_renderable_mesh        () const -> const Buffer_mesh& { return m_renderable_mesh; }
    [[nodiscard]] auto get_normal_style           () const -> Normal_style { return m_normal_style; }

    // Deferred edge-lines finalize (glTF import): the load path builds a
    // fill-only buffer mesh straight from the triangle soup; the deferred
    // per-mesh task then prepares a full geometry-based buffer mesh (edge
    // lines, corner / centroid points, element mappings) aside on a worker
    // and commits it under the scene lock. prepare ensures Geometry exists
    // (thread-safe); commit swaps the renderable mesh + element mappings in.
    // `name` labels this shape in the optimization log line. It is a parameter
    // rather than the Geometry's own name because on this path there is none:
    // the Geometry is derived from an imported triangle soup and comes out
    // unnamed, so the caller passes the scene mesh's name instead. Empty falls
    // back to the Geometry name, which is what the procedural paths have.
    // `force_rebuild` bypasses the "edge lines already committed" idempotence
    // early-out: the background re-optimization of an edit commit rebuilds a
    // COMPLETE mesh precisely to get a fresh optimized variant out of the same
    // staged bytes (meshoptimizer doc, requirement 11). A pending build by
    // another task still wins either way.
    auto prepare_geometry_buffer_mesh(const Build_info& build_info, Normal_style normal_style, std::string_view name = {}, bool force_rebuild = false) -> bool;
    // `out_optimized_shape` receives the optimized variant prepared alongside
    // the buffer mesh, so the caller can attach it to the Primitive in the same
    // step - and, crucially, BEFORE the mesh is re-registered with the draw
    // list. Set to null when there is none, which clears any stale variant.
    auto commit_geometry_buffer_mesh(std::shared_ptr<Primitive_render_shape>& out_optimized_shape) -> bool;

private:
    // Caller holds m_build_mutex; see the locking note on Primitive_shape.
    auto make_buffer_mesh_build_locked(const Build_info& build_info, Normal_style normal_style, std::shared_ptr<Primitive_render_shape>* out_optimized_shape) -> bool;
    auto make_buffer_mesh_build_locked(const Buffer_info& buffer_info) -> bool;
    [[nodiscard]] auto has_edge_lines_state_locked() const -> bool;

    class Pending_buffer_mesh
    {
    public:
        Buffer_mesh                       buffer_mesh;
        erhe::primitive::Element_mappings element_mappings;
        Normal_style                      normal_style{Normal_style::none};
        // Built by the same worker pass, committed by the same atomic swap:
        // the variant describes exactly this buffer_mesh's build, so the two
        // must never be published apart.
        std::shared_ptr<Primitive_render_shape> optimized_shape;
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
    // A soup primitive with validated prepared mesh data on its render shape
    // (Primitive_shape::get_prepared_data()).
    Primitive(const std::shared_ptr<Triangle_soup>& triangle_soup, std::shared_ptr<Prepared_primitive_data> prepared_data);
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
    // These build the SOURCE shape only. The optimized variant is built before
    // it is attached (make_optimized_render_shape()), so it is never half-built.
    // Not const: it publishes optimized_render_shape alongside the source
    // build, and that slot is this object's.
    [[nodiscard]] auto make_renderable_mesh    (const Build_info& build_info, Normal_style normal_style) -> bool;
    [[nodiscard]] auto make_renderable_mesh    (const erhe::primitive::Buffer_info& buffer_info) const -> bool;
    [[nodiscard]] auto make_raytrace           () const -> bool;
    // Load path of a soup primitive carrying prepared mesh data
    // (Primitive_shape::get_prepared_data(), doc/plans/gltf_fast_load.md step
    // 6): the fill-only soup build of the render shape (as
    // make_renderable_mesh() builds a soup), the optimized variant as one
    // linear pack of the soup core into buffer_info.optimized_vertex_format
    // (no meshoptimizer: the core already is in that order; empty element
    // mappings, which is what composing the import path's empty mappings
    // through an identity remap gives), published when
    // buffer_info.optimize_meshes is set, and the real raytrace from the
    // stored BVH. No Geometry is built. Returns false when the primitive has
    // no prepared data or the base build fails; a missing raytrace (no stored
    // BVH, or the backend refused it) is reported by has_real_raytrace().
    // Same threading as make_renderable_mesh(): one caller per Primitive.
    [[nodiscard]] auto make_prepared_meshes    (const Build_info& build_info) -> bool;
    // The render shape carries prepared mesh data.
    [[nodiscard]] auto is_prepared             () const -> bool;
    // Releases the render shape's CPU triangle soup (and the optimized
    // variant's, when it shares it) once the GPU buffers and the raytrace are
    // built; readers reload it from the soup source through
    // Primitive_shape::get_triangle_soup(). Returns false, keeping the soup,
    // when the render shape has no soup source (a primitive not loaded from
    // a file with Triangle_soup_reload::record). The load.cpu_mesh_data
    // editor setting decides whether loads call this
    // (doc/editor/async_asset_loading.md "Releasing CPU mesh data").
    auto release_triangle_soup() -> bool;
    // AABB proxy raytrace over the renderable-mesh bounds; no-op when a
    // raytrace (proxy or real) already exists. See Primitive_raytrace.
    [[nodiscard]] auto make_raytrace_proxy     () const -> bool;
    // Which BUILD of this primitive's renderable data is meant is decided
    // here, because it is Primitive - not the shape - that owns more than one.
    //
    // has_renderable_mesh(): whether this primitive has a shape for that
    // build. `original` is there whenever the primitive renders at all (its
    // mesh is empty until the first build); `optimized` only while an
    // optimized build is live.
    [[nodiscard]] auto has_renderable_mesh     (Mesh_variant variant) const -> bool;
    // The shape holding the named build, or null when it is not present.
    // `original` is `render_shape` itself.
    [[nodiscard]] auto get_render_shape        (Mesh_variant variant) const -> const std::shared_ptr<Primitive_render_shape>&;
    // Resolves what a caller WANTS against what is actually present, and hands
    // back both the resolved variant and its mesh - so the two can never
    // disagree, and a caller cannot act on a presence check that has gone
    // stale by the time it fetches.
    //
    // `original` as the preference means original, full stop: that is how the
    // ID renderer, ray tracing and the GPU vertex edit paths pin themselves to
    // the build with valid facet ids. `optimized` means "optimized if it is
    // there AND it has this primitive_mode, else original" - the
    // content-rendering choice. The mode is part of the question because the
    // optimized build is fill triangles only: resolving to it for edge lines
    // would hand back an empty range, which every caller reads as "nothing to
    // draw" rather than as "ask the other build".
    //
    // Lifetime of the returned pointer: it points into the resolved shape, so
    // it stays valid as long as that shape does. `original` lives as long as
    // the Primitive; a dropped `optimized` shape is retired rather than freed
    // until in-flight frames have retired it (see optimized_render_shape).
    // Take get_render_shape() instead to hold one across frames.
    [[nodiscard]] auto get_resolved_renderable_mesh(Mesh_variant preference, Primitive_mode primitive_mode) const -> std::pair<Mesh_variant, const Buffer_mesh*>;
    // Null when there is no shape for the named variant. There is deliberately
    // no fallback: a silent one would blur which data a build or an edit
    // actually used. Use get_resolved_renderable_mesh() to ask for a fallback.
    // Same pointer lifetime as get_resolved_renderable_mesh().
    [[nodiscard]] auto get_renderable_mesh     (Mesh_variant variant) const -> const Buffer_mesh*;
    [[nodiscard]] auto get_name                () const -> std::string_view;
    [[nodiscard]] auto get_bounding_box        () const -> erhe::math::Aabb;
    [[nodiscard]] auto get_shape_for_raytrace  () const -> std::shared_ptr<Primitive_shape>;
    
    // The source build: authored vertex and triangle order, valid per-corner
    // facet ids, and the single maximum-precision Geometry, raytrace and
    // triangle soup this primitive is derived from. Always Mesh_variant::original.
    // Everything that is not choosing between builds - geometry, raytrace,
    // collision, export - goes straight through this member.
    std::shared_ptr<Primitive_render_shape> render_shape;
    std::shared_ptr<Primitive_shape>        collision_shape;

    // The meshoptimizer build of this same primitive: welded and reordered,
    // with the facet id bytes zeroed because welding makes them meaningless.
    // Null unless mesh optimization is enabled.
    //
    // It is an ordinary geometry-less Primitive_render_shape - the same
    // category as the Buffer_mesh-only shapes scene_builder makes for its
    // instanced cubes - so it carries its own Buffer_mesh, its own
    // Element_mappings and its own triangle soup, and a mesh can never be
    // paired with mappings describing a different order. It deliberately has
    // no Geometry, no raytrace and no collision shape, and nothing asks it for
    // one: no geometry / raytrace / collision / export site is variant selected.
    //
    // Optimization is keyed on the Primitive, which is already the unit
    // sharing dedups on (glTF import reuses one Primitive per distinct
    // primitive, brushes share them), so a primitive shared by many meshes is
    // optimized exactly once and its original is never mutated.
    //
    // Publication follows the same discipline as render_shape itself: attached
    // before the primitive is drawn from, and dropped only at a main-thread
    // flush point, with the shape kept alive until in-flight frames retire it.
    //
    // Unlike the shapes' own slots this one has NO lock of its own, so writing
    // it is only safe while nothing else touches this Primitive. Every writer
    // today satisfies that: the import build loop is serial over primitives by
    // explicit design (build_imported_buffer_meshes, whose comment says why),
    // and the finalize commits run on the main thread with the item host lock
    // held. A caller that builds one shared Primitive from two threads at once
    // would tear this shared_ptr - which, before it existed, was merely
    // duplicated work.
    //
    // Publication goes through publish_optimized_render_shape() below, never a
    // direct assignment (import-loop first construction aside): the publish is
    // refused while an optimization hold is active.
    std::shared_ptr<Primitive_render_shape> optimized_render_shape;

    // Live-edit optimization hold (meshoptimizer doc, requirement 11). A GPU
    // edit (paint, weight paint, component drag) addresses the base variant's
    // buffers in place, so an optimized shape built from pre-edit data must not
    // become visible while the edit is active. Mesh::begin/end_optimized_variant_edit
    // brackets the edit: begin increments this and drops the live variant, end
    // decrements it; publish_optimized_render_shape() refuses while it is
    // non-zero. A counter rather than a flag because instances share Primitives
    // and two tools could in principle bracket the same one. Same threading
    // discipline as optimized_render_shape: main-thread writers only, no lock.
    int optimization_hold_count{0};

    // The single publication point of optimized_render_shape after first
    // construction: assigns (null allowed - a geometry-path commit clears a
    // stale soup-path variant this way) unless an optimization hold is active,
    // in which case the shape is dropped and the variant stays absent - the
    // edit's end is responsible for queueing a fresh optimization.
    void publish_optimized_render_shape(std::shared_ptr<Primitive_render_shape> shape);

    // Releases one hold Mesh::begin_optimized_variant_edit() took ON THIS
    // OBJECT. The holder keeps the Primitive shared_ptr it bracketed and
    // releases through it, never through a (mesh, index) lookup: an operation
    // executing mid-edit (undo during a stroke) can swap the mesh's slot to a
    // different Primitive, and releasing the wrong object would corrupt both
    // counts.
    void release_optimization_hold();
};

auto build_buffer_mesh_from_triangle_soup(const Triangle_soup& triangle_soup, const Buffer_info& buffer_info) -> std::optional<Buffer_mesh>;

[[nodiscard]] auto primitive_type(Primitive_mode primitive_mode) -> Primitive_type;

} // namespace erhe::primitive
