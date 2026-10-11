#pragma once

#include "renderers/scene_tlas.hpp"

#include "erhe_graphics/acceleration_structure.hpp"
#include "erhe_math/aabb.hpp"

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace erhe::graphics {
    class Command_buffer;
    class Device;
}
namespace erhe::primitive {
    class Buffer_mesh;
    class Material;
    class Primitive;
    class Primitive_render_shape;
}
namespace erhe::scene {
    class Mesh;
    class Mesh_layer;
}
namespace erhe::scene_renderer {
    class Material_set;
    class Mesh_memory;
}

namespace editor {

// One unique ray-traced geometry: the original-variant Buffer_mesh of a
// content primitive, shared by every instance naming it and by every
// consumer (Ray_trace_renderer, Ddgi_renderer, Radiance_cascades_renderer).
//
// The prepare phase (M2) creates it and fills everything but the bottom
// level structure; the render phase (R) creates and builds the structure the
// first time a published slot names the geometry (Scene_tlas::update), and
// is the only one touching it. A geometry the prepare phase retires lives on
// through the slots that still hold it; ~Acceleration_structure defers the
// device destroy to frame completion.
class Ray_trace_geometry
{
public:
    // The shape owns the Buffer_mesh and through it the GPU vertex / index
    // ranges the structure and the record addresses reference; both are
    // pinned for as long as any slot or the working set names the geometry.
    std::shared_ptr<erhe::primitive::Primitive>              primitive;
    std::shared_ptr<erhe::primitive::Primitive_render_shape> render_shape;
    // Prepare phase only: the original-variant mesh of render_shape (its
    // bounding box drives the record's dequantization affine).
    const erhe::primitive::Buffer_mesh*                      buffer_mesh{nullptr};
    // The bottom level build input (positions read in place from the
    // Mesh_memory pools).
    erhe::graphics::Acceleration_structure_triangles triangles{};
    // The geometry half of the instance record: addresses, strides, position
    // encoding and dequantization; material_index and flags are per instance.
    Scene_tlas::Instance_record_data                record{};
    // False when any skip condition holds (no triangles, no stream 1, no
    // device address, unsupported position format, ...): no instance names an
    // invalid geometry and it never gets a structure.
    bool                                            valid{false};

    // Render phase only.
    std::unique_ptr<erhe::graphics::Acceleration_structure> blas;
};

class Ray_trace_slot_instance
{
public:
    glm::mat4 transform     {1.0f};
    uint32_t  geometry_index{0};   // into Ray_trace_slot::geometries
    uint32_t  mask          {0};
};

// What the render phase of one frame reads (Ray_trace_scene::get_slot()).
class Ray_trace_slot
{
public:
    uint64_t                                         frame_index      {0};
    bool                                             prepared         {false};
    uint64_t                                         structure_serial {0};
    // Every geometry the instances name, each once.
    std::vector<std::shared_ptr<Ray_trace_geometry>> geometries;
    // instance_custom_index order (content layer x primitive order).
    std::vector<Ray_trace_slot_instance>             instances;
    std::vector<Scene_tlas::Instance_record_data>    records;
    // Union of the visible, active content meshes' world bounds, skinned
    // meshes included (posed); invalid when there is no visible content.
    erhe::math::Aabb                                 content_bounds{};
    // Live slot count of the scene root's FORWARD material set, the set the
    // records' material_index values name.
    std::size_t                                      forward_material_live_count{0};
};

// The ray-tracing view of one Scene_root's content layer (doc/editor/raytrace.md
// "Ray_trace_scene"): the instances the top level structures are built from,
// the per-instance records the ray query shaders read, and the content bounds
// the indirect diffuse volumes are fitted to.
//
// Fed by the Scene_root mesh hooks (any thread, they only mark state), kept up
// to date in the prepare phase (prepare(), main thread, M2) and published into
// one slot per frame in flight; the render phase reads only the slot of its
// frame (get_slot()), so it never walks the live scene.
class Ray_trace_scene
{
public:
    Ray_trace_scene(erhe::graphics::Device& graphics_device, erhe::scene_renderer::Mesh_memory& mesh_memory);
    ~Ray_trace_scene() noexcept;
    Ray_trace_scene(const Ray_trace_scene&) = delete;
    auto operator=(const Ray_trace_scene&) -> Ray_trace_scene& = delete;

    // Mesh hooks, forwarded by Scene_root for content layer meshes. Any thread.
    // Before the first prepare() each only marks the structure dirty, so a root
    // the frame loop never prepares (the previews) accumulates nothing.
    void on_structure_changed    ();
    void on_primitives_replaced  (const std::shared_ptr<erhe::scene::Mesh>& mesh);
    void on_materials_changed    ();
    void on_transform_changed    (const std::shared_ptr<erhe::scene::Mesh>& mesh);
    void on_bounds_changed       ();

    // Prepare phase (M2), main thread, caller holds the scene's
    // item_host_mutex: brings the working set up to date and publishes it into
    // the slot of frame_index. material_set is the scene root's FORWARD set,
    // already updated for this frame.
    void prepare(
        const erhe::scene::Mesh_layer&            content_layer,
        const erhe::scene_renderer::Material_set& material_set,
        uint64_t                                  frame_index,
        std::size_t                               frames_in_flight
    );

    // Render phase: the slot the prepare phase of frame_index published.
    [[nodiscard]] auto get_slot(uint64_t frame_index) const -> const Ray_trace_slot&;

    // Unique valid geometries of the working set (each gets one bottom level
    // structure), for memory reporting (doc/editor/reloadable_asset_loads.md).
    // Main thread.
    [[nodiscard]] auto get_geometry_count() const -> std::size_t;

private:
    class Instance
    {
    public:
        glm::mat4                        transform      {1.0f};
        Ray_trace_geometry*              geometry       {nullptr};
        uint32_t                         geometry_index {0};
        uint32_t                         mask           {0};
        uint32_t                         primitive_index{0};
        Scene_tlas::Instance_record_data record{};
        // Bit s set: slot s has not received this instance's current fields.
        uint64_t                         pending_slots  {0};
    };

    class Mesh_entry
    {
    public:
        const erhe::scene::Mesh* mesh            {nullptr};
        uint64_t                 transform_serial{0};
        uint32_t                 first_instance  {0};
        uint32_t                 instance_count  {0};
        bool                     skinned         {false};
        erhe::math::Aabb         world_aabb      {};
    };

    void rebuild_structure    (const erhe::scene::Mesh_layer& content_layer, const erhe::scene_renderer::Material_set& material_set);
    void update_transforms    ();
    void update_materials     (const erhe::scene_renderer::Material_set& material_set);
    void update_static_bounds ();
    void update_bounds        ();
    [[nodiscard]] auto refresh_quantization(Instance& instance) const -> bool;
    void mark_changed         (std::size_t instance_index);
    void publish              (Ray_trace_slot& slot, std::size_t slot_index);
    [[nodiscard]] auto get_or_create_geometry(
        const std::shared_ptr<erhe::primitive::Primitive>&              primitive,
        const std::shared_ptr<erhe::primitive::Primitive_render_shape>& render_shape
    ) -> const std::shared_ptr<Ray_trace_geometry>&;
    [[nodiscard]] auto resolve_material(
        const erhe::primitive::Material*          material,
        const erhe::scene_renderer::Material_set& material_set,
        Instance&                                 instance
    ) const -> bool;

    erhe::graphics::Device&            m_graphics_device;
    erhe::scene_renderer::Mesh_memory& m_mesh_memory;

    // Written by the hooks (any thread) under m_pending_mutex, consumed by
    // prepare().
    std::mutex                                       m_pending_mutex;
    bool                                             m_prepared_once      {false};
    bool                                             m_structure_dirty    {true};
    bool                                             m_materials_dirty    {false};
    bool                                             m_bounds_dirty       {false};
    std::vector<std::shared_ptr<erhe::scene::Mesh>>  m_pending_transforms;
    std::vector<std::shared_ptr<erhe::scene::Mesh>>  m_pending_replaced;

    // Working set, prepare phase only. Scratch vectors swap with the pending
    // ones and are cleared after use, so their capacity stays.
    std::vector<std::shared_ptr<erhe::scene::Mesh>>  m_transform_scratch;
    std::vector<std::shared_ptr<erhe::scene::Mesh>>  m_replaced_scratch;
    std::unordered_map<const erhe::primitive::Buffer_mesh*, std::shared_ptr<Ray_trace_geometry>> m_geometries;
    std::unordered_map<const erhe::primitive::Buffer_mesh*, std::shared_ptr<Ray_trace_geometry>> m_previous_geometries;
    std::vector<std::shared_ptr<Ray_trace_geometry>> m_geometry_list;      // Ray_trace_slot::geometries order
    std::unordered_map<const Ray_trace_geometry*, uint32_t> m_geometry_list_index; // rebuild scratch
    std::size_t                                      m_valid_geometry_count{0};
    std::vector<Instance>                            m_instances;
    std::vector<Mesh_entry>                          m_mesh_entries;
    std::unordered_map<const erhe::scene::Mesh*, std::size_t> m_mesh_entry_index;
    std::vector<std::size_t>                         m_changed_instances;  // indices with pending_slots != 0
    erhe::math::Aabb                                 m_static_bounds{};    // non-skinned entries
    std::size_t                                      m_material_write_count{0};
    std::size_t                                      m_material_live_count {0};
    uint64_t                                         m_structure_serial   {0};
    uint64_t                                         m_all_slots_mask     {0};
    std::vector<Ray_trace_slot>                      m_slots;

    // Diagnostics, logged per prepare at trace level (editor.raytrace_frame).
    std::size_t m_structure_rebuilds{0};
    std::size_t m_transform_updates {0};
    std::size_t m_instance_copies   {0};
    std::size_t m_full_publishes    {0};
};

} // namespace editor
