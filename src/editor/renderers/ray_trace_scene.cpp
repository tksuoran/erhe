#include "renderers/ray_trace_scene.hpp"
#include "editor_log.hpp"

#include "erhe_dataformat/vertex_format.hpp"
#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_primitive/buffer_mesh.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_primitive/primitive.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_scene_renderer/material_set.hpp"
#include "erhe_scene_renderer/mesh_memory.hpp"
#include "erhe_scene_renderer/primitive_buffer.hpp"
#include "erhe_utility/thread_roles.hpp"
#include "erhe_verify/verify.hpp"

#include <algorithm>

namespace editor {

Ray_trace_scene::Ray_trace_scene(erhe::graphics::Device& graphics_device, erhe::scene_renderer::Mesh_memory& mesh_memory)
    : m_graphics_device{graphics_device}
    , m_mesh_memory    {mesh_memory}
{
}

Ray_trace_scene::~Ray_trace_scene() noexcept = default;

// --- Hooks (any thread) ---------------------------------------------------------

void Ray_trace_scene::on_structure_changed()
{
    const std::lock_guard<std::mutex> lock{m_pending_mutex};
    m_structure_dirty = true;
}

void Ray_trace_scene::on_primitives_replaced(const std::shared_ptr<erhe::scene::Mesh>& mesh)
{
    const std::lock_guard<std::mutex> lock{m_pending_mutex};
    m_structure_dirty = true;
    // A primitive's renderable Buffer_mesh may have been replaced in place,
    // at the same address: the rebuild must not reuse the geometry it keyed.
    // Before the first prepare there is nothing to reuse.
    if (m_prepared_once && mesh) {
        m_pending_replaced.push_back(mesh);
    }
}

void Ray_trace_scene::on_materials_changed()
{
    const std::lock_guard<std::mutex> lock{m_pending_mutex};
    if (!m_prepared_once) {
        m_structure_dirty = true;
        return;
    }
    m_materials_dirty = true;
}

void Ray_trace_scene::on_transform_changed(const std::shared_ptr<erhe::scene::Mesh>& mesh)
{
    const std::lock_guard<std::mutex> lock{m_pending_mutex};
    if (!m_prepared_once) {
        m_structure_dirty = true;
        return;
    }
    if (!m_structure_dirty && mesh) {
        m_pending_transforms.push_back(mesh);
    }
}

void Ray_trace_scene::on_bounds_changed()
{
    const std::lock_guard<std::mutex> lock{m_pending_mutex};
    if (!m_prepared_once) {
        m_structure_dirty = true;
        return;
    }
    m_bounds_dirty = true;
}

// --- Prepare phase ---------------------------------------------------------------

auto Ray_trace_scene::get_or_create_geometry(
    const std::shared_ptr<erhe::primitive::Primitive>&              primitive,
    const std::shared_ptr<erhe::primitive::Primitive_render_shape>& render_shape
) -> const std::shared_ptr<Ray_trace_geometry>&
{
    const erhe::primitive::Buffer_mesh& buffer_mesh = render_shape->get_renderable_mesh();
    const std::unordered_map<const erhe::primitive::Buffer_mesh*, std::shared_ptr<Ray_trace_geometry>>::iterator current = m_geometries.find(&buffer_mesh);
    if (current != m_geometries.end()) {
        return current->second;
    }
    const std::unordered_map<const erhe::primitive::Buffer_mesh*, std::shared_ptr<Ray_trace_geometry>>::iterator previous = m_previous_geometries.find(&buffer_mesh);
    if (previous != m_previous_geometries.end()) {
        return m_geometries.emplace(&buffer_mesh, std::move(previous->second)).first->second;
    }

    std::shared_ptr<Ray_trace_geometry> geometry = std::make_shared<Ray_trace_geometry>();
    geometry->primitive    = primitive;
    geometry->render_shape = render_shape;
    geometry->buffer_mesh  = &buffer_mesh;
    std::shared_ptr<Ray_trace_geometry>& slot = m_geometries.emplace(&buffer_mesh, std::move(geometry)).first->second;
    Ray_trace_geometry& g = *slot.get();

    // The acceleration structure reads triangles straight from the mesh memory
    // pools: stream 0 starts with position at offset 0 for both the skinned and
    // non-skinned vertex formats, and indices are a uint32 triangle list. Indices
    // are relative to the range start (draws use base_vertex), so baking the range
    // byte offsets into the build addresses matches.
    //
    // When positions are quantized the build reads them as snorm16x3 and applies
    // the primitive's dequantization affine as a build transform, so the stored
    // geometry - and therefore the object space the hit shaders see, and every
    // normal derived from the instance transform - is exactly what it was before.
    //
    // Attribute fetch needs stream 1 (normal / texcoords / color); the
    // position-fetch fallback additionally needs stream 0 (the same range the
    // structure is built from).
    if (buffer_mesh.vertex_buffer_ranges.size() < 2) {
        return slot;
    }
    const erhe::primitive::Buffer_range& position_range  = buffer_mesh.vertex_buffer_ranges[0];
    const erhe::primitive::Buffer_range& attribute_range = buffer_mesh.vertex_buffer_ranges[1];
    const erhe::primitive::Buffer_range& index_range     = buffer_mesh.index_buffer_range;
    const erhe::primitive::Index_range&  triangles       = buffer_mesh.triangle_fill_indices;
    if ((triangles.index_count == 0) || (triangles.primitive_type != erhe::primitive::Primitive_type::triangles)) {
        return slot;
    }
    if ((position_range.count == 0) || (index_range.element_size != sizeof(uint32_t))) {
        return slot;
    }
    erhe::graphics::Buffer* position_buffer  = m_mesh_memory.get_vertex_buffer(position_range);
    erhe::graphics::Buffer* attribute_buffer = m_mesh_memory.get_vertex_buffer(attribute_range);
    erhe::graphics::Buffer* index_buffer     = m_mesh_memory.get_index_buffer(index_range);
    if ((position_buffer == nullptr) || (attribute_buffer == nullptr) || (index_buffer == nullptr)) {
        return slot;
    }

    // Position storage format and, when quantized, the affine that maps the
    // encoded values back into object space. Shared with the lightmap baker's
    // BLAS cache so the two builds cannot disagree.
    const erhe::scene_renderer::Blas_position_input position_input =
        erhe::scene_renderer::get_blas_position_input(m_graphics_device, m_mesh_memory, buffer_mesh);
    if (!position_input.has_position) {
        return slot;
    }
    if (!position_input.supported) {
        // Backstop only - Mesh_memory refuses to quantize on a device that cannot
        // ray trace the format, so this should be unreachable. Logged once per
        // geometry: the geometry stays in the working set, marked invalid.
        log_render->error(
            "Vertex positions are quantized but the device cannot use that format as acceleration "
            "structure build input; this mesh cannot be ray traced"
        );
        return slot;
    }

    const uint64_t position_base_address  = position_buffer->get_device_address();
    const uint64_t attribute_base_address = attribute_buffer->get_device_address();
    const uint64_t index_base_address     = index_buffer->get_device_address();
    // The stream-1 fetch is uint granular, so that stride still has to be a
    // multiple of 4. The position stride no longer does - it is carried in
    // bytes - which is what keeps a quantized (6 or 14 byte) stream 0 from
    // silently dropping the whole instance out of the TLAS.
    if ((position_base_address == 0) || (attribute_base_address == 0) || (index_base_address == 0) ||
        ((attribute_range.element_size % 4) != 0)) {
        return slot;
    }

    g.triangles = erhe::graphics::Acceleration_structure_triangles{
        .vertex_buffer      = position_buffer,
        .vertex_byte_offset = position_range.byte_offset,
        .vertex_byte_stride = position_range.element_size,
        .vertex_count       = position_range.count,
        .vertex_format      = position_input.vertex_format,
        .transform          = position_input.transform,
        .index_buffer       = index_buffer,
        .index_byte_offset  = index_range.byte_offset + (triangles.first_index * index_range.element_size),
        .index_count        = triangles.index_count,
        .opaque             = true
    };

    // Position encoding and its dequantization affine: the BLAS carries the
    // same affine as a build transform, so the structure stores dequantized
    // object-space positions - the fallback fetch reads the raw pool and has
    // to apply it itself to land in the same space.
    // Deliberately the STORAGE encoding, not the format the acceleration
    // structure build reads. Those two legitimately differ: a device without
    // the 3-component snorm AS format reads the same bytes as snorm16x4
    // (get_blas_position_input), while this record drives the fallback
    // position fetch, which reads three int16 lanes at position_stride_bytes.
    const erhe::dataformat::Vertex_position_encoding position_encoding =
        erhe::dataformat::get_vertex_position_encoding(&m_mesh_memory.get_vertex_input(buffer_mesh.vertex_input_key).vertex_format);
    const erhe::scene_renderer::Position_quantization quantization = buffer_mesh.position_quantization;

    g.record = Scene_tlas::Instance_record_data{
        .index_address         = index_base_address + index_range.byte_offset + (triangles.first_index * index_range.element_size),
        .vertex_address        = attribute_base_address + attribute_range.byte_offset,
        .position_address      = position_base_address + position_range.byte_offset,
        .vertex_stride_uints   = static_cast<uint32_t>(attribute_range.element_size / 4),
        .position_stride_bytes = static_cast<uint32_t>(position_range.element_size),
        .material_index        = 0u,
        .flags                 = 0u,
        .position_encoding     = static_cast<uint32_t>(position_encoding),
        .reserved0             = 0u,
        .position_scale        = quantization.scale,
        .position_offset       = quantization.offset
    };
    g.valid = true;
    return slot;
}

auto Ray_trace_scene::resolve_material(
    const erhe::primitive::Material*          material,
    const erhe::scene_renderer::Material_set& material_set,
    Instance&                                 instance
) const -> bool
{
    // The slot in the forward set the tracing dispatches bind. Slot 0 on a
    // miss, the same policy the bucket path takes.
    const uint32_t material_index = material_set.get_slot(material).value_or(0u);
    const bool     transmissive   = (material != nullptr) && (material->get_transmission() > 0.0f);
    const uint32_t flags          = transmissive ? 1u : 0u;
    const uint32_t mask           = transmissive
        ? Scene_tlas::c_instance_mask_all
        : (Scene_tlas::c_instance_mask_all | Scene_tlas::c_instance_mask_opaque);
    const bool changed =
        (instance.record.material_index != material_index) ||
        (instance.record.flags          != flags) ||
        (instance.mask                  != mask);
    instance.record.material_index = material_index;
    instance.record.flags          = flags;
    instance.mask                  = mask;
    return changed;
}

void Ray_trace_scene::rebuild_structure(const erhe::scene::Mesh_layer& content_layer, const erhe::scene_renderer::Material_set& material_set)
{
    ERHE_PROFILE_FUNCTION();
    ++m_structure_rebuilds;
    ++m_structure_serial;

    // Geometries are reused across the rebuild by Buffer_mesh, except those of
    // meshes whose renderable Buffer_mesh was replaced in place.
    std::swap(m_previous_geometries, m_geometries);
    m_geometries.clear();
    for (const std::shared_ptr<erhe::scene::Mesh>& mesh : m_replaced_scratch) {
        for (const erhe::scene::Mesh_primitive& mesh_primitive : mesh->get_primitives()) {
            if (!mesh_primitive.primitive) {
                continue;
            }
            const erhe::primitive::Buffer_mesh* buffer_mesh = mesh_primitive.primitive->get_renderable_mesh(erhe::primitive::Mesh_variant::original);
            if (buffer_mesh != nullptr) {
                m_previous_geometries.erase(buffer_mesh);
            }
        }
    }

    m_instances.clear();
    m_mesh_entries.clear();
    m_mesh_entry_index.clear();
    m_geometry_list.clear();
    m_changed_instances.clear();
    m_geometry_list_index.clear();

    for (const std::shared_ptr<erhe::scene::Mesh>& mesh : content_layer.meshes) {
        if (!mesh || !mesh->is_visible() || !mesh->is_active()) {
            continue;
        }
        // Skinned meshes are not in the acceleration structure, but they are
        // lit by the indirect diffuse volumes, so their (posed) bounds are part
        // of the content bounds.
        const bool skinned = static_cast<bool>(mesh->skin);
        const erhe::scene::Node* node = mesh.get();
        const glm::mat4 world_from_node = node->world_from_node();
        Mesh_entry entry{
            .mesh             = mesh.get(),
            .transform_serial = node->node_data.transforms.world_from_node_serial,
            .first_instance   = static_cast<uint32_t>(m_instances.size()),
            .instance_count   = 0,
            .skinned          = skinned,
            .world_aabb       = skinned ? erhe::math::Aabb{} : mesh->get_aabb_world()
        };
        if (!skinned) {
            const std::vector<erhe::scene::Mesh_primitive>& mesh_primitives = mesh->get_primitives();
            for (std::size_t primitive_index = 0, end = mesh_primitives.size(); primitive_index < end; ++primitive_index) {
                const erhe::scene::Mesh_primitive& mesh_primitive = mesh_primitives[primitive_index];
                if (!mesh_primitive.primitive) {
                    continue;
                }
                // Ray tracing pins to the original variant, unaffected by the mesh
                // optimizer: no structure references the optimized mesh, so
                // dropping that variant on a vertex edit needs no rebuild here,
                // and ray-traced results always reflect source order.
                const std::shared_ptr<erhe::primitive::Primitive_render_shape>& render_shape =
                    mesh_primitive.primitive->get_render_shape(erhe::primitive::Mesh_variant::original);
                if (!render_shape) {
                    continue;
                }
                const std::shared_ptr<Ray_trace_geometry>& geometry = get_or_create_geometry(mesh_primitive.primitive, render_shape);
                if (!geometry->valid) {
                    continue;
                }
                const std::pair<std::unordered_map<const Ray_trace_geometry*, uint32_t>::iterator, bool> list_entry =
                    m_geometry_list_index.emplace(geometry.get(), static_cast<uint32_t>(m_geometry_list.size()));
                if (list_entry.second) {
                    m_geometry_list.push_back(geometry);
                }
                Instance& instance = m_instances.emplace_back();
                instance.transform       = world_from_node;
                instance.geometry        = geometry.get();
                instance.geometry_index  = list_entry.first->second;
                instance.primitive_index = static_cast<uint32_t>(primitive_index);
                instance.record          = geometry->record;
                static_cast<void>(refresh_quantization(instance));
                static_cast<void>(resolve_material(mesh_primitive.material.get(), material_set, instance));
            }
        }
        entry.instance_count = static_cast<uint32_t>(m_instances.size()) - entry.first_instance;
        m_mesh_entry_index.emplace(mesh.get(), m_mesh_entries.size());
        m_mesh_entries.push_back(entry);
    }

    // What the previous structure named and this one does not is retired here;
    // the slots still holding a retired geometry keep it alive until they are
    // republished.
    m_previous_geometries.clear();
    m_valid_geometry_count = m_geometry_list.size();
    update_static_bounds();
}

void Ray_trace_scene::mark_changed(const std::size_t instance_index)
{
    Instance& instance = m_instances[instance_index];
    if (instance.pending_slots == 0) {
        m_changed_instances.push_back(instance_index);
    }
    instance.pending_slots = m_all_slots_mask;
}

void Ray_trace_scene::update_transforms()
{
    // Several transform hooks of one node within a frame (tools, physics
    // writeback, subtree propagation) enqueue the mesh several times; the
    // serial makes the update happen once. Serial 0 means "update needed".
    bool static_bounds_changed = false;
    for (const std::shared_ptr<erhe::scene::Mesh>& mesh : m_transform_scratch) {
        const std::unordered_map<const erhe::scene::Mesh*, std::size_t>::const_iterator i = m_mesh_entry_index.find(mesh.get());
        if (i == m_mesh_entry_index.end()) {
            continue; // not traced: hidden, inactive, or not content
        }
        Mesh_entry& entry = m_mesh_entries[i->second];
        const erhe::scene::Node* node = mesh.get();
        const glm::mat4 world_from_node = node->world_from_node();
        const uint64_t  serial          = node->node_data.transforms.world_from_node_serial;
        if ((serial != 0u) && (serial == entry.transform_serial)) {
            continue;
        }
        entry.transform_serial = serial;
        ++m_transform_updates;
        if (entry.skinned) {
            continue; // posed bounds are read every frame; no instances
        }
        entry.world_aabb = mesh->get_aabb_world();
        static_bounds_changed = true;
        for (uint32_t k = 0; k < entry.instance_count; ++k) {
            const std::size_t instance_index = entry.first_instance + k;
            Instance& instance = m_instances[instance_index];
            if (instance.transform != world_from_node) {
                instance.transform = world_from_node;
                mark_changed(instance_index);
            }
        }
    }
    if (static_bounds_changed) {
        update_static_bounds();
    }
}

void Ray_trace_scene::update_materials(const erhe::scene_renderer::Material_set& material_set)
{
    for (const Mesh_entry& entry : m_mesh_entries) {
        if (entry.instance_count == 0) {
            continue;
        }
        const std::vector<erhe::scene::Mesh_primitive>& mesh_primitives = entry.mesh->get_primitives();
        for (uint32_t k = 0; k < entry.instance_count; ++k) {
            const std::size_t instance_index = entry.first_instance + k;
            Instance& instance = m_instances[instance_index];
            ERHE_VERIFY(instance.primitive_index < mesh_primitives.size());
            const erhe::primitive::Material* material = mesh_primitives[instance.primitive_index].material.get();
            if (resolve_material(material, material_set, instance)) {
                mark_changed(instance_index);
            }
        }
    }
}

auto Ray_trace_scene::refresh_quantization(Instance& instance) const -> bool
{
    // The dequantization affine the build stored on the Buffer_mesh.
    const erhe::scene_renderer::Position_quantization quantization = instance.geometry->buffer_mesh->position_quantization;
    const bool changed =
        (instance.record.position_scale  != quantization.scale) ||
        (instance.record.position_offset != quantization.offset);
    instance.record.position_scale  = quantization.scale;
    instance.record.position_offset = quantization.offset;
    return changed;
}

void Ray_trace_scene::update_bounds()
{
    // A primitive's bounds grew in place: the meshes' world bounds and the
    // records' dequantization affine follow.
    for (Mesh_entry& entry : m_mesh_entries) {
        if (!entry.skinned) {
            entry.world_aabb = entry.mesh->get_aabb_world();
        }
    }
    update_static_bounds();
    for (std::size_t i = 0, end = m_instances.size(); i < end; ++i) {
        if (refresh_quantization(m_instances[i])) {
            mark_changed(i);
        }
    }
}

void Ray_trace_scene::update_static_bounds()
{
    m_static_bounds = erhe::math::Aabb{};
    for (const Mesh_entry& entry : m_mesh_entries) {
        if (!entry.skinned && entry.world_aabb.is_valid_3d()) {
            m_static_bounds.include(entry.world_aabb);
        }
    }
}

void Ray_trace_scene::prepare(
    const erhe::scene::Mesh_layer&            content_layer,
    const erhe::scene_renderer::Material_set& material_set,
    const uint64_t                            frame_index,
    const std::size_t                         frames_in_flight
)
{
    ERHE_PROFILE_FUNCTION();
    erhe::utility::Thread_roles::assert_main_thread_in_phase(erhe::utility::Frame_phase::m2_prepare);
    ERHE_VERIFY((frames_in_flight > 0) && (frames_in_flight <= 64));

    // The slot count is the device's frames in flight, fixed for the device's
    // lifetime: sized once, so the render phase indexes a vector that never
    // reallocates.
    if (m_slots.empty()) {
        m_slots.resize(frames_in_flight);
        m_all_slots_mask = (frames_in_flight == 64) ? ~uint64_t{0} : ((uint64_t{1} << frames_in_flight) - 1u);
    }
    ERHE_VERIFY(m_slots.size() == frames_in_flight);

    m_structure_rebuilds = 0;
    m_transform_updates  = 0;
    m_instance_copies    = 0;
    m_full_publishes     = 0;

    bool structure_dirty = false;
    bool materials_dirty = false;
    bool bounds_dirty    = false;
    {
        const std::lock_guard<std::mutex> lock{m_pending_mutex};
        m_prepared_once = true;
        structure_dirty = m_structure_dirty;
        materials_dirty = m_materials_dirty;
        bounds_dirty    = m_bounds_dirty;
        m_structure_dirty = false;
        m_materials_dirty = false;
        m_bounds_dirty    = false;
        std::swap(m_transform_scratch, m_pending_transforms);
        std::swap(m_replaced_scratch,  m_pending_replaced);
    }

    const std::size_t material_write_count = material_set.get_write_count();
    if (structure_dirty) {
        // A rebuild reads every current transform, material and bound.
        rebuild_structure(content_layer, material_set);
    } else {
        update_transforms();
        if (materials_dirty || (material_write_count != m_material_write_count)) {
            update_materials(material_set);
        }
        if (bounds_dirty) {
            update_bounds();
        }
    }
    m_material_write_count = material_write_count;
    m_material_live_count  = material_set.get_live_count();
    m_transform_scratch.clear();
    m_replaced_scratch.clear();

    const std::size_t slot_index = static_cast<std::size_t>(frame_index % frames_in_flight);
    Ray_trace_slot& slot = m_slots[slot_index];
    slot.frame_index = frame_index;
    slot.prepared    = true;
    publish(slot, slot_index);

    log_raytrace_frame->trace(
        "Ray_trace_scene: structure_rebuilds {} transform_updates {} instance_copies {} full_publishes {} (instances {}, geometries {})",
        m_structure_rebuilds, m_transform_updates, m_instance_copies, m_full_publishes, m_instances.size(), m_geometry_list.size()
    );
}

void Ray_trace_scene::publish(Ray_trace_slot& slot, const std::size_t slot_index)
{
    const uint64_t slot_bit = uint64_t{1} << slot_index;

    if (slot.structure_serial != m_structure_serial) {
        // assign() reuses the capacity the slot's copy already has.
        slot.geometries.assign(m_geometry_list.begin(), m_geometry_list.end());
        slot.instances.resize(m_instances.size());
        slot.records  .resize(m_instances.size());
        for (std::size_t i = 0, end = m_instances.size(); i < end; ++i) {
            const Instance& instance = m_instances[i];
            slot.instances[i] = Ray_trace_slot_instance{
                .transform      = instance.transform,
                .geometry_index = instance.geometry_index,
                .mask           = instance.mask
            };
            slot.records[i] = instance.record;
        }
        slot.structure_serial = m_structure_serial;
        for (const std::size_t instance_index : m_changed_instances) {
            m_instances[instance_index].pending_slots &= ~slot_bit;
        }
        ++m_full_publishes;
    } else {
        for (const std::size_t instance_index : m_changed_instances) {
            Instance& instance = m_instances[instance_index];
            if ((instance.pending_slots & slot_bit) == 0) {
                continue;
            }
            Ray_trace_slot_instance& published = slot.instances[instance_index];
            published.transform = instance.transform;
            published.mask      = instance.mask;
            ERHE_VERIFY(published.geometry_index == instance.geometry_index);
            slot.records[instance_index] = instance.record;
            instance.pending_slots &= ~slot_bit;
            ++m_instance_copies;
        }
    }
    // Drop the instances every slot has received (no allocation).
    m_changed_instances.erase(
        std::remove_if(
            m_changed_instances.begin(),
            m_changed_instances.end(),
            [this](const std::size_t instance_index) -> bool { return m_instances[instance_index].pending_slots == 0; }
        ),
        m_changed_instances.end()
    );

    // The bounds: the cached static union plus the skinned meshes' posed
    // bounds, which no notification reaches (joints move under animation), so
    // they are read every frame, as the shadow bounds gather does.
    erhe::math::Aabb bounds = m_static_bounds;
    for (const Mesh_entry& entry : m_mesh_entries) {
        if (!entry.skinned) {
            continue;
        }
        const erhe::math::Aabb mesh_bounds = entry.mesh->get_aabb_world();
        if (mesh_bounds.is_valid_3d()) {
            bounds.include(mesh_bounds);
        }
    }
    slot.content_bounds              = bounds;
    slot.forward_material_live_count = m_material_live_count;
}

auto Ray_trace_scene::get_slot(const uint64_t frame_index) const -> const Ray_trace_slot&
{
    erhe::utility::Thread_roles::assert_render_thread();
    erhe::utility::Thread_roles::assert_frame_phase(erhe::utility::Frame_phase::r_render);
    ERHE_VERIFY(!m_slots.empty());
    const Ray_trace_slot& slot = m_slots[frame_index % m_slots.size()];
    // The prepare phase of this frame published the slot (and the prepare
    // phase of no later frame has reused it yet).
    ERHE_VERIFY(slot.prepared && (slot.frame_index == frame_index));
    return slot;
}

auto Ray_trace_scene::get_geometry_count() const -> std::size_t
{
    return m_valid_geometry_count;
}

} // namespace editor
