#include "erhe_scene_renderer/draw_list_scene.hpp"
#include "erhe_scene_renderer/draw_indirect_buffer.hpp"
#include "erhe_scene_renderer/mesh_memory.hpp"
#include "erhe_scene_renderer/scene_renderer_log.hpp"
#include "erhe_scene_renderer/shader_variant_cache.hpp"

#include "erhe_graphics/draw_indirect.hpp"
#include "erhe_graphics/render_command_encoder.hpp"
#include "erhe_graphics/render_pass.hpp"
#include "erhe_graphics/render_pipeline.hpp"
#include "erhe_graphics/render_pipeline_state.hpp"
#include "erhe_graphics/scoped_debug_group.hpp"
#include "erhe_graphics/shader_stages.hpp"
#include "erhe_graphics/state/color_blend_state.hpp"
#include "erhe_graphics/state/vertex_input_state.hpp"
#include "erhe_item/item.hpp"
#include "erhe_primitive/buffer_mesh.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_primitive/primitive.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/skin.hpp"
#include "erhe_verify/verify.hpp"

#include <fmt/format.h>
#include <glm/glm.hpp>
#include <glm/gtx/matrix_operation.hpp>

#include <algorithm>
#include <cstring>
#include <sstream>

namespace erhe::scene_renderer {

namespace {

// Classification of one primitive for one purpose. Mirrors bucket_primitives()
// (mesh_memory.cpp): same skips, same key derivation minus environment.
class Primitive_classification
{
public:
    bool                                accepted   {false};
    const erhe::primitive::Buffer_mesh* buffer_mesh{nullptr};
    Draw_blending                       blending   {Draw_blending::opaque};
    Buffer_set                          buffer_set {};
    Shader_key                          key        {};
    erhe::primitive::Index_range        index_range{};
};

[[nodiscard]] auto classify_primitive(
    const Mesh_memory&                    mesh_memory,
    const erhe::scene::Mesh&              mesh,
    const erhe::scene::Mesh_primitive&    mesh_primitive,
    const erhe::primitive::Primitive_mode primitive_mode,
    const Draw_purpose                    purpose,
    const bool                            exclude_unlit_from_shadows
) -> Primitive_classification
{
    Primitive_classification result{};

    const erhe::primitive::Primitive* primitive = mesh_primitive.primitive.get();
    if (primitive == nullptr) {
        return result;
    }
    const erhe::primitive::Buffer_mesh* buffer_mesh = primitive->get_renderable_mesh();
    if (buffer_mesh == nullptr) {
        return result;
    }
    result.index_range = buffer_mesh->index_range(primitive_mode);
    if (result.index_range.index_count == 0) {
        return result;
    }

    const erhe::primitive::Material* material = mesh_primitive.material.get();

    // Blending classification (R1/R7): opaque materials -> opaque; anything
    // else, including no material (no blending mode), -> translucent. This is
    // exactly the opaque_primitives_only / translucent_primitives_only split
    // of Blending_mode_policy in bucket_primitives().
    const bool is_opaque =
        (material != nullptr) &&
        (material->data.blending_mode == erhe::primitive::Material_blending_mode::opaque);
    result.blending = is_opaque ? Draw_blending::opaque : Draw_blending::translucent;

    // Shadow lists take opaque casters only (Shadow_renderer uses
    // opaque_primitives_only today).
    if ((purpose == Draw_purpose::shadow) && !is_opaque) {
        return result;
    }

    // Unlit (KHR_materials_unlit) primitives are backdrop geometry, not
    // occluders: keep them out of the shadow lists when the editor setting
    // asks for it (Draw_list_scene::set_exclude_unlit_from_shadows, which
    // rebuilds the lists when the flag changes).
    if (
        (purpose == Draw_purpose::shadow) &&
        exclude_unlit_from_shadows &&
        (material != nullptr) &&
        (material->data.bxdf_model == erhe::primitive::Bxdf_model::unlit)
    ) {
        return result;
    }

    const Vertex_input_entry& vertex_input_entry = mesh_memory.get_vertex_input(buffer_mesh->vertex_input_key);
    const bool                skinned            = static_cast<bool>(mesh.skin);

    switch (purpose) {
        case Draw_purpose::color: {
            // Primitive-derived components only: derive from an empty
            // environment key. The pass environment is combined at
            // resolution time (R17/R21).
            result.key = Shader_key{}.derive(material, &vertex_input_entry.vertex_format, skinned);
            break;
        }
        case Draw_purpose::shadow: {
            // R4: shadow variants are position-only passes; the only
            // primitive-derived component that matters is skinning. Derive
            // with a null material so no material bits leak in, then keep
            // USE_SKINNING only.
            const Shader_key full = Shader_key{}.derive(nullptr, &vertex_input_entry.vertex_format, skinned);
            result.key = Shader_key{};
            result.key.set(Shader_bool::USE_SKINNING, full.get(Shader_bool::USE_SKINNING));
            break;
        }
        default: {
            ERHE_FATAL("bad Draw_purpose");
        }
    }

    result.buffer_set.vertex_input_key = bucket_vertex_input_key(*buffer_mesh, primitive_mode);
    result.buffer_set.index_buffer     = Pool_buffer_identity{
        buffer_mesh->index_buffer_range.pool_id,
        buffer_mesh->index_buffer_range.buffer_id
    };
    for (const erhe::primitive::Buffer_range& vertex_range : bucket_vertex_ranges(*buffer_mesh, primitive_mode)) {
        result.buffer_set.vertex_buffers.emplace_back(vertex_range.pool_id, vertex_range.buffer_id);
    }

    result.buffer_mesh = buffer_mesh;
    result.accepted    = true;
    return result;
}

[[nodiscard]] auto material_identity_hash(const erhe::primitive::Material* material) -> uint64_t
{
    // Material-derived Shader_key components only (no vertex format, no
    // skinning): the exact inputs whose change alters draw list identity.
    // derive() with a null vertex format drops every has_<attribute> gate,
    // which hides use_aniso_control (only visible together with the aniso
    // vertex attribute) - fold it in explicitly.
    uint64_t hash = Shader_key{}.derive(material, nullptr, false).get_hash();
    if ((material != nullptr) && material->data.use_aniso_control) {
        hash = erhe::hash::hash(static_cast<uint8_t>(1u), hash);
    }
    return hash;
}

[[nodiscard]] auto sample_negative_determinant(const erhe::scene::Mesh& mesh) -> bool
{
    // R10b: sample from the node's current world transform, not from
    // Item_flags::negative_determinant, which is maintained by
    // handle_node_transform_update() and lags behind at attach time.
    const erhe::scene::Node* node = mesh.get_node();
    if (node == nullptr) {
        return false;
    }
    return glm::determinant(node->world_from_node()) < 0.0f;
}

} // anonymous namespace

auto c_str(const Shadow_sub_variant sub_variant) -> const char*
{
    switch (sub_variant) {
        case Shadow_sub_variant::depth_only:          return "depth_only";
        case Shadow_sub_variant::depth_only_distance: return "depth_only_distance";
        case Shadow_sub_variant::cube:                return "cube";
        default:                                      return "?";
    }
}

auto Color_environment::operator==(const Color_environment& other) const -> bool
{
    for (std::size_t i = 0; i < 4; ++i) {
        if (light_partition.per_type_shadow   [i] != other.light_partition.per_type_shadow   [i]) { return false; }
        if (light_partition.per_type_nonshadow[i] != other.light_partition.per_type_nonshadow[i]) { return false; }
    }
    return
        (shadow_filter     == other.shadow_filter    ) &&
        (shadow_bias       == other.shadow_bias      ) &&
        (shadow_technique  == other.shadow_technique ) &&
        (shadow_depth_bits == other.shadow_depth_bits);
}

auto Color_environment::make_environment_key() const -> Shader_key
{
    // Mirrors the environment key built in Forward_renderer::render():
    // light counts per type, shadow axes, SHADER_DEBUG = 0 (draw lists never
    // carry the debug axis; those passes use the fallback).
    Shader_key key{};
    key.set(Shader_int::LIGHT_COUNT_DIRECTIONAL_NOT_SHADOWMAPPED, static_cast<uint32_t>(light_partition.per_type_nonshadow[0]));
    key.set(Shader_int::LIGHT_COUNT_DIRECTIONAL_SHADOWMAPPED,     static_cast<uint32_t>(light_partition.per_type_shadow   [0]));
    key.set(Shader_int::LIGHT_COUNT_SPOT_NOT_SHADOWMAPPED,        static_cast<uint32_t>(light_partition.per_type_nonshadow[1]));
    key.set(Shader_int::LIGHT_COUNT_SPOT_SHADOWMAPPED,            static_cast<uint32_t>(light_partition.per_type_shadow   [1]));
    key.set(Shader_int::LIGHT_COUNT_POINT_NOT_SHADOWMAPPED,       static_cast<uint32_t>(light_partition.per_type_nonshadow[2]));
    key.set(Shader_int::LIGHT_COUNT_POINT_SHADOWMAPPED,           static_cast<uint32_t>(light_partition.per_type_shadow   [2]));
    key.set(Shader_int::SHADER_DEBUG,                             0u);
    key.set(Shader_int::SHADOW_FILTER,                            shadow_filter);
    key.set(Shader_int::SHADOW_BIAS,                              shadow_bias);
    key.set(Shader_int::SHADOW_TECHNIQUE,                         shadow_technique);
    key.set(Shader_int::SHADOW_DEPTH_BITS,                        shadow_depth_bits);
    return key;
}

Draw_list_scene::Draw_list_scene(
    Mesh_memory&                    mesh_memory,
    Shader_variant_cache&           shader_variant_cache,
    const Primitive_interface&      primitive_interface,
    const std::span<const uint32_t> multiview_view_counts
)
    : m_mesh_memory            {mesh_memory}
    , m_shader_variant_cache   {shader_variant_cache}
    , m_primitive_interface    {primitive_interface}
    , m_primitive_record_stride{primitive_interface.primitive_struct.get_size_bytes()}
    , m_multiview_view_counts  {multiview_view_counts.begin(), multiview_view_counts.end()}
    , m_owner_thread_id        {std::this_thread::get_id()}
{
    ERHE_VERIFY(m_primitive_record_stride > 0);
}

Draw_list_scene::~Draw_list_scene() noexcept = default;

void Draw_list_scene::assert_main_thread() const
{
    ERHE_VERIFY(std::this_thread::get_id() == m_owner_thread_id);
}

// --- Object storage ---------------------------------------------------------

auto Draw_list_scene::allocate_object() -> uint32_t
{
    if (!m_free_object_indices.empty()) {
        const uint32_t index = m_free_object_indices.back();
        m_free_object_indices.pop_back();
        Draw_list_object& object = m_objects[index];
        ERHE_VERIFY(!object.alive);
        object.alive = true;
        return index;
    }
    const uint32_t index = static_cast<uint32_t>(m_objects.size());
    m_objects.emplace_back();
    m_objects.back().alive = true;
    return index;
}

void Draw_list_scene::release_object(const uint32_t object_index)
{
    Draw_list_object& object = m_objects[object_index];
    ERHE_VERIFY(object.alive);
    ERHE_VERIFY(object.locations.empty());
    object.info             = Draw_list_object_create_info{};
    object.transform_serial = 0u;
    object.joint_slot       = 0u;
    object.alive            = false;
    ++object.generation;
    m_free_object_indices.push_back(object_index);
}

auto Draw_list_scene::get_or_create_draw_list(const Draw_list_key& key) -> uint32_t
{
    const std::unordered_map<Draw_list_key, uint32_t, Draw_list_key_hash>::const_iterator it = m_draw_list_index_by_key.find(key);
    if (it != m_draw_list_index_by_key.end()) {
        return it->second;
    }
    const uint32_t index = static_cast<uint32_t>(m_draw_lists.size());
    m_draw_lists.emplace_back();
    m_draw_lists.back().key = key;
    m_draw_list_index_by_key.emplace(key, index);
    return index;
}

void Draw_list_scene::add_entries(const uint32_t object_index)
{
    ERHE_PROFILE_FUNCTION();

    Draw_list_object& object = m_objects[object_index];
    ERHE_VERIFY(object.alive);
    ERHE_VERIFY(object.locations.empty());
    const erhe::scene::Mesh* mesh = object.info.mesh.get();
    ERHE_VERIFY(mesh != nullptr);

    constexpr erhe::primitive::Primitive_mode primitive_mode = erhe::primitive::Primitive_mode::polygon_fill;
    constexpr Draw_purpose purposes[] = { Draw_purpose::color, Draw_purpose::shadow };

    const erhe::math::Aabb                          world_aabb = mesh->get_aabb_world();
    const std::vector<erhe::scene::Mesh_primitive>& primitives = mesh->get_primitives();
    if (primitives.size() > 0xffffu) {
        log_draw_list->error("Mesh '{}' has {} primitives, exceeds Draw_list_entry limit of 65535; not registered", mesh->get_name(), primitives.size());
        return;
    }
    for (std::size_t i = 0, count = primitives.size(); i < count; ++i) {
        for (const Draw_purpose purpose : purposes) {
            const Primitive_classification classification = classify_primitive(
                m_mesh_memory, *mesh, primitives[i], primitive_mode, purpose, m_exclude_unlit_from_shadows
            );
            if (!classification.accepted) {
                continue;
            }
            Draw_list_key key{};
            key.purpose              = purpose;
            key.mobility             = object.mobility;
            key.blending             = classification.blending;
            key.negative_determinant = object.negative_determinant;
            key.primitive_mode       = primitive_mode;
            key.layer_id             = object.layer_id;
            key.buffer_set           = classification.buffer_set;
            key.primitive_key        = classification.key;
            key.primitive_key_hash   = classification.key.get_hash();

            const uint32_t draw_list_index = get_or_create_draw_list(key);
            Draw_list&     draw_list       = m_draw_lists[draw_list_index];

            const erhe::primitive::Buffer_mesh& buffer_mesh = *classification.buffer_mesh;
            Draw_list_entry entry{};
            entry.object_index         = object_index;
            entry.mesh_primitive_index = static_cast<uint16_t>(i);
            entry.flag_bits            = object.flag_bits;
            entry.index_count          = static_cast<uint32_t>(classification.index_range.index_count);
            entry.first_index          = static_cast<uint32_t>(classification.index_range.first_index) + buffer_mesh.base_index();
            entry.base_vertex          = (primitive_mode == erhe::primitive::Primitive_mode::solid_wireframe)
                ? buffer_mesh.expanded_base_vertex()
                : buffer_mesh.base_vertex();
            entry.world_aabb           = world_aabb;

            object.locations.push_back(
                Draw_list_entry_location{
                    .draw_list_index = draw_list_index,
                    .entry_index     = static_cast<uint32_t>(draw_list.entries.size())
                }
            );
            const bool first_entry = draw_list.entries.empty();
            draw_list.entries.push_back(entry);
            ERHE_VERIFY(draw_list.primitive_records.size() == (draw_list.entries.size() - 1) * m_primitive_record_stride);
            draw_list.primitive_records.resize(draw_list.entries.size() * m_primitive_record_stride);
            write_entry_record(object, entry, get_record(object.locations.back()));

            // R17: resolve at registration (color: every enumerated view
            // config, once the environment is known; shadow sub-variants
            // resolve lazily on first use, R4a). A list that was empty has
            // no valid cached resolution to reuse only if it is brand new;
            // reused empty lists keep theirs.
            if (first_entry && (purpose == Draw_purpose::color) && draw_list.color_resolutions.empty()) {
                resolve_color_list(draw_list);
            }
        }
    }
}

void Draw_list_scene::remove_entry(const Draw_list_entry_location& location)
{
    Draw_list& draw_list = m_draw_lists[location.draw_list_index];
    ERHE_VERIFY(location.entry_index < draw_list.entries.size());
    const uint32_t last_index = static_cast<uint32_t>(draw_list.entries.size() - 1);
    if (location.entry_index != last_index) {
        // Swap-remove: the moved entry's owner must be told its new index.
        const Draw_list_entry& moved = draw_list.entries[last_index];
        Draw_list_object&      owner = m_objects[moved.object_index];
        bool patched = false;
        for (Draw_list_entry_location& owner_location : owner.locations) {
            if ((owner_location.draw_list_index == location.draw_list_index) && (owner_location.entry_index == last_index)) {
                owner_location.entry_index = location.entry_index;
                patched = true;
                break;
            }
        }
        ERHE_VERIFY(patched);
        draw_list.entries[location.entry_index] = moved;
        std::memcpy(
            draw_list.primitive_records.data() + static_cast<std::size_t>(location.entry_index) * m_primitive_record_stride,
            draw_list.primitive_records.data() + static_cast<std::size_t>(last_index)           * m_primitive_record_stride,
            m_primitive_record_stride
        );
    }
    draw_list.entries.pop_back();
    draw_list.primitive_records.resize(draw_list.entries.size() * m_primitive_record_stride);
}

// --- Primitive records ---------------------------------------------------------

auto Draw_list_scene::get_record(const Draw_list_entry_location& location) -> std::byte*
{
    Draw_list& draw_list = m_draw_lists[location.draw_list_index];
    ERHE_VERIFY(location.entry_index < draw_list.entries.size());
    ERHE_VERIFY(draw_list.primitive_records.size() == draw_list.entries.size() * m_primitive_record_stride);
    return draw_list.primitive_records.data() + static_cast<std::size_t>(location.entry_index) * m_primitive_record_stride;
}

namespace {

// Same math as Primitive_buffer::write_primitive(): the normal matrix is the
// transposed adjugate, mirrored for negative-determinant nodes (the node
// flag is maintained by Node::update_transform()).
void write_transform_fields(std::byte* record, const Primitive_struct& offsets, const erhe::scene::Node& node)
{
    const glm::mat4 world_from_node      = node.world_from_node();
    const bool      negative_determinant = (node.get_flag_bits() & erhe::Item_flags::negative_determinant) == erhe::Item_flags::negative_determinant;
    constexpr glm::mat4 invert_normal{
        -1.0f,  0.0f,  0.0f, 0.0f,
         0.0f, -1.0f,  0.0f, 0.0f,
         0.0f,  0.0f, -1.0f, 0.0f,
         0.0f,  0.0f,  0.0f, 1.0f
    };
    const glm::mat4 normal_transform_ = glm::transpose(glm::adjugate(world_from_node));
    const glm::mat4 normal_transform  = negative_determinant
        ? invert_normal * normal_transform_
        : normal_transform_;
    std::memcpy(record + offsets.world_from_node,  &world_from_node,  sizeof(glm::mat4));
    std::memcpy(record + offsets.normal_transform, &normal_transform, sizeof(glm::mat4));
}

void write_slot_fields(std::byte* record, const Primitive_struct& offsets, const erhe::scene::Mesh& mesh, const erhe::scene::Mesh_primitive& mesh_primitive)
{
    const erhe::primitive::Material* material         = mesh_primitive.material.get();
    const uint32_t                   material_index   = (material != nullptr) ? material->material_buffer_index : 0u;
    const std::shared_ptr<erhe::scene::Skin>& skin    = mesh.skin;
    const float                      skinning_factor  = skin ? 1.0f : 0.0f;
    const uint32_t                   base_joint_index = skin ? skin->skin_data.joint_buffer_index : 0u;
    std::memcpy(record + offsets.material_index,   &material_index,   sizeof(uint32_t));
    std::memcpy(record + offsets.skinning_factor,  &skinning_factor,  sizeof(float));
    std::memcpy(record + offsets.base_joint_index, &base_joint_index, sizeof(uint32_t));
}

} // anonymous namespace

void Draw_list_scene::write_entry_record(const Draw_list_object& object, const Draw_list_entry& entry, std::byte* record) const
{
    const Primitive_struct&  offsets = m_primitive_interface.offsets;
    const erhe::scene::Mesh* mesh    = object.info.mesh.get();
    ERHE_VERIFY(mesh != nullptr);
    const erhe::scene::Node* node = mesh->get_node();
    ERHE_VERIFY(node != nullptr);
    const std::vector<erhe::scene::Mesh_primitive>& mesh_primitives = mesh->get_primitives();
    ERHE_VERIFY(entry.mesh_primitive_index < mesh_primitives.size());
    const erhe::scene::Mesh_primitive& mesh_primitive = mesh_primitives[entry.mesh_primitive_index];

    std::memset(record, 0, m_primitive_record_stride);
    write_transform_fields(record, offsets, *node);
    // color / size: pass-dependent, patched by Primitive_buffer::update() per
    // draw; zero here.
    std::memcpy(record + offsets.lightmap_scale_offset, &mesh_primitive.lightmap_uv_scale_offset, sizeof(glm::vec4));
    write_slot_fields(record, offsets, *mesh, mesh_primitive);
    std::memcpy(record + offsets.base_vertex, &entry.base_vertex, sizeof(uint32_t));
}

void Draw_list_scene::write_object_transform(const uint32_t object_index)
{
    Draw_list_object&        object = m_objects[object_index];
    const erhe::scene::Mesh* mesh   = object.info.mesh.get();
    ERHE_VERIFY(mesh != nullptr);
    const erhe::scene::Node* node = mesh->get_node();
    ERHE_VERIFY(node != nullptr);
    const Primitive_struct& offsets = m_primitive_interface.offsets;
    for (const Draw_list_entry_location& location : object.locations) {
        write_transform_fields(get_record(location), offsets, *node);
    }
    object.transform_serial = node->node_data.transforms.world_from_node_serial;
}

void Draw_list_scene::write_object_gpu_slots(const uint32_t object_index)
{
    Draw_list_object&        object = m_objects[object_index];
    const erhe::scene::Mesh* mesh   = object.info.mesh.get();
    ERHE_VERIFY(mesh != nullptr);
    const Primitive_struct& offsets = m_primitive_interface.offsets;
    const std::vector<erhe::scene::Mesh_primitive>& mesh_primitives = mesh->get_primitives();
    for (const Draw_list_entry_location& location : object.locations) {
        const Draw_list_entry& entry = m_draw_lists[location.draw_list_index].entries[location.entry_index];
        ERHE_VERIFY(entry.mesh_primitive_index < mesh_primitives.size());
        write_slot_fields(get_record(location), offsets, *mesh, mesh_primitives[entry.mesh_primitive_index]);
    }
    object.joint_slot = mesh->skin ? mesh->skin->skin_data.joint_buffer_index : 0u;
}

void Draw_list_scene::refresh_object_records(const uint32_t object_index)
{
    Draw_list_object& object = m_objects[object_index];
    // Same detach race as register_object(): a queued refresh can flush after
    // the mesh lost its node, with the matching unregister still behind it in
    // the queue. The records cannot be rewritten without a node transform;
    // leave them as they are, the unregister removes the entries.
    {
        const erhe::scene::Mesh* refresh_mesh = object.info.mesh.get();
        if ((refresh_mesh == nullptr) || (refresh_mesh->get_node() == nullptr)) {
            return;
        }
    }
    for (const Draw_list_entry_location& location : object.locations) {
        const Draw_list_entry& entry = m_draw_lists[location.draw_list_index].entries[location.entry_index];
        write_entry_record(object, entry, get_record(location));
    }
    const erhe::scene::Mesh* mesh = object.info.mesh.get();
    const erhe::scene::Node* node = (mesh != nullptr) ? mesh->get_node() : nullptr;
    object.transform_serial = (node != nullptr) ? node->node_data.transforms.world_from_node_serial : 0u;
    object.joint_slot       = ((mesh != nullptr) && mesh->skin) ? mesh->skin->skin_data.joint_buffer_index : 0u;
    ++m_refresh_count;
}

void Draw_list_scene::update_object_transform(const uint32_t object_index)
{
    Draw_list_object&        object = m_objects[object_index];
    const erhe::scene::Mesh* mesh   = object.info.mesh.get();
    const erhe::scene::Node* node   = (mesh != nullptr) ? mesh->get_node() : nullptr;
    if (node == nullptr) {
        return;
    }
    // Several transform updates of one node within a frame (tools, physics
    // writeback, subtree propagation) enqueue several ops; the serial makes
    // the rewrite happen once. Serial 0 means "update needed": never skip.
    const uint64_t serial = node->node_data.transforms.world_from_node_serial;
    if ((serial != 0u) && (serial == object.transform_serial)) {
        return;
    }
    write_object_transform(object_index);
    ++m_transform_update_count;
}

void Draw_list_scene::sync_gpu_slots()
{
    ERHE_PROFILE_FUNCTION();

    // Material / joint GPU slots are assigned by the drawing renderer's
    // Material_buffer::update() / Joint_buffer::update() (R8a) right before
    // this is called from draw_color() / draw_shadow(). Compare the few
    // distinct watched materials and the skinned objects against the values
    // the records were written from; only a scene material-list / skin-list
    // change makes them differ.
    std::vector<const erhe::primitive::Material*> changed_materials;
    for (std::unordered_map<const erhe::primitive::Material*, Material_watch>::value_type& entry : m_material_watches) {
        const uint32_t slot = entry.first->material_buffer_index;
        if (slot != entry.second.slot) {
            entry.second.slot = slot;
            changed_materials.push_back(entry.first);
        }
    }
    if (!changed_materials.empty()) {
        for (std::size_t i = 0, end = m_objects.size(); i < end; ++i) {
            const Draw_list_object& object = m_objects[i];
            if (!object.alive) {
                continue;
            }
            bool uses_changed = false;
            for (const erhe::primitive::Material* material : object.materials) {
                for (const erhe::primitive::Material* changed : changed_materials) {
                    if (material == changed) {
                        uses_changed = true;
                        break;
                    }
                }
                if (uses_changed) {
                    break;
                }
            }
            if (uses_changed) {
                write_object_gpu_slots(static_cast<uint32_t>(i));
                ++m_slot_sync_count;
            }
        }
    }
    for (const uint32_t object_index : m_skinned_object_indices) {
        const Draw_list_object& object = m_objects[object_index];
        ERHE_VERIFY(object.alive);
        const erhe::scene::Mesh* mesh = object.info.mesh.get();
        const uint32_t joint_slot = ((mesh != nullptr) && mesh->skin) ? mesh->skin->skin_data.joint_buffer_index : 0u;
        if (joint_slot != object.joint_slot) {
            write_object_gpu_slots(object_index);
            ++m_slot_sync_count;
        }
    }
}

void Draw_list_scene::remove_entries(const uint32_t object_index)
{
    ERHE_PROFILE_FUNCTION();

    Draw_list_object& object = m_objects[object_index];
    // Remove in reverse so that swap-removes inside the same list never
    // move an entry that this object still has to remove.
    while (!object.locations.empty()) {
        const Draw_list_entry_location location = object.locations.back();
        object.locations.pop_back();
        remove_entry(location);
    }
}

// --- Main-thread API ---------------------------------------------------------

auto Draw_list_scene::register_object(const Draw_list_object_create_info& create_info) -> Draw_list_object_id
{
    ERHE_PROFILE_FUNCTION();
    assert_main_thread();

    ERHE_VERIFY(create_info.mesh);
    // Copy first: create_info may alias a registered object's own info, which
    // unregister_object() below resets.
    const Draw_list_object_create_info info = create_info;
    const erhe::scene::Mesh* mesh = info.mesh.get();

    const std::unordered_map<const erhe::scene::Mesh*, uint32_t>::const_iterator existing = m_object_index_by_mesh.find(mesh);
    if (existing != m_object_index_by_mesh.end()) {
        unregister_object(Draw_list_object_id{existing->second, m_objects[existing->second].generation});
    }

    // A draw list object exists only for a node-attached mesh: every record
    // field is written from the node's world transform. Registration arrives
    // through the pending queue (enqueue_register), so the mesh can have been
    // detached again between the enqueue and this flush - the queue then also
    // holds the matching unregister, but this register op is processed first.
    // The controller placeholder mesh does exactly that: it is attached at
    // Controller_visualization construction and detached in the same frame,
    // once the real controller render model has loaded. Decline it here; the
    // attach that gives the mesh a node enqueues a fresh registration.
    if (mesh->get_node() == nullptr) {
        log_draw_list->trace("Not registering mesh '{}': not attached to a node", mesh->get_name());
        return Draw_list_object_id{};
    }

    const uint32_t    object_index = allocate_object();
    Draw_list_object& object       = m_objects[object_index];
    object.info                 = info;
    object.mobility             = mesh->skin ? Draw_mobility::skinned : info.mobility;
    object.negative_determinant = sample_negative_determinant(*mesh);
    object.layer_id             = mesh->layer_id;
    object.flag_bits            = mesh->get_flag_bits();
    m_object_index_by_mesh.emplace(mesh, object_index);
    ++m_alive_object_count;

    add_entries(object_index);
    watch_object_materials(object_index);
    // add_entries() wrote the records from the live node / slots; remember
    // what they were written from for the transform dedup / slot sync.
    {
        const erhe::scene::Node* node = mesh->get_node();
        object.transform_serial = (node != nullptr) ? node->node_data.transforms.world_from_node_serial : 0u;
        object.joint_slot       = mesh->skin ? mesh->skin->skin_data.joint_buffer_index : 0u;
        if (object.mobility == Draw_mobility::skinned) {
            m_skinned_object_indices.push_back(object_index);
        }
    }

    return Draw_list_object_id{object_index, object.generation};
}

void Draw_list_scene::watch_object_materials(const uint32_t object_index)
{
    Draw_list_object& object = m_objects[object_index];
    object.materials.clear();
    for (const erhe::scene::Mesh_primitive& mesh_primitive : object.info.mesh->get_primitives()) {
        const erhe::primitive::Material* material = mesh_primitive.material.get();
        if (material == nullptr) {
            continue;
        }
        bool seen = false;
        for (const erhe::primitive::Material* known : object.materials) {
            if (known == material) {
                seen = true;
                break;
            }
        }
        if (seen) {
            continue;
        }
        object.materials.push_back(material);
        Material_watch& watch = m_material_watches[material];
        if (watch.use_count == 0) {
            watch.identity_hash = material_identity_hash(material);
            watch.slot          = material->material_buffer_index;
        }
        ++watch.use_count;
    }
}

void Draw_list_scene::unwatch_object_materials(const uint32_t object_index)
{
    Draw_list_object& object = m_objects[object_index];
    for (const erhe::primitive::Material* material : object.materials) {
        const std::unordered_map<const erhe::primitive::Material*, Material_watch>::iterator it = m_material_watches.find(material);
        if (it == m_material_watches.end()) {
            continue;
        }
        if (it->second.use_count > 0) {
            --it->second.use_count;
        }
        if (it->second.use_count == 0) {
            m_material_watches.erase(it);
        }
    }
    object.materials.clear();
}

void Draw_list_scene::check_material_changes()
{
    ERHE_PROFILE_FUNCTION();

    std::vector<const erhe::primitive::Material*> changed;
    for (std::unordered_map<const erhe::primitive::Material*, Material_watch>::value_type& entry : m_material_watches) {
        const uint64_t hash = material_identity_hash(entry.first);
        if (hash != entry.second.identity_hash) {
            entry.second.identity_hash = hash;
            changed.push_back(entry.first);
        }
    }
    if (changed.empty()) {
        return;
    }
    ++m_material_change_count;
    // Rare event: scan objects for users of the changed materials and
    // re-register them (their list identity may have changed).
    std::vector<Draw_list_object_id> to_reregister;
    for (std::size_t i = 0, end = m_objects.size(); i < end; ++i) {
        const Draw_list_object& object = m_objects[i];
        if (!object.alive) {
            continue;
        }
        bool uses_changed = false;
        for (const erhe::primitive::Material* material : object.materials) {
            for (const erhe::primitive::Material* changed_material : changed) {
                if (material == changed_material) {
                    uses_changed = true;
                    break;
                }
            }
            if (uses_changed) {
                break;
            }
        }
        if (uses_changed) {
            to_reregister.push_back(Draw_list_object_id{static_cast<uint32_t>(i), object.generation});
        }
    }
    log_draw_list->info("{} material(s) changed identity; re-registering {} object(s)", changed.size(), to_reregister.size());
    for (const Draw_list_object_id id : to_reregister) {
        static_cast<void>(reregister_object(id));
    }
}

void Draw_list_scene::unregister_object(const Draw_list_object_id id)
{
    ERHE_PROFILE_FUNCTION();
    assert_main_thread();

    if (!id.is_valid() || (id.index >= m_objects.size())) {
        return;
    }
    Draw_list_object& object = m_objects[id.index];
    if (!object.alive || (object.generation != id.generation)) {
        return;
    }
    remove_entries(id.index);
    unwatch_object_materials(id.index);
    m_object_index_by_mesh.erase(object.info.mesh.get());
    --m_alive_object_count;
    if (object.mobility == Draw_mobility::skinned) {
        const std::vector<uint32_t>::iterator it = std::find(m_skinned_object_indices.begin(), m_skinned_object_indices.end(), id.index);
        ERHE_VERIFY(it != m_skinned_object_indices.end());
        m_skinned_object_indices.erase(it);
    }
    release_object(id.index);
    // Empty draw lists are intentionally kept (see header).
}

void Draw_list_scene::unregister_object(const erhe::scene::Mesh* mesh)
{
    unregister_object(find_object(mesh));
}

auto Draw_list_scene::reregister_object(const Draw_list_object_id id) -> Draw_list_object_id
{
    assert_main_thread();
    const Draw_list_object* object = get_object(id);
    if (object == nullptr) {
        return Draw_list_object_id{};
    }
    const Draw_list_object_create_info create_info = object->info; // copy: unregister releases it
    unregister_object(id);
    return register_object(create_info);
}

void Draw_list_scene::set_object_flags(const Draw_list_object_id id, const uint64_t item_flag_bits)
{
    assert_main_thread();
    if (!id.is_valid() || (id.index >= m_objects.size())) {
        return;
    }
    Draw_list_object& object = m_objects[id.index];
    if (!object.alive || (object.generation != id.generation)) {
        return;
    }
    if (object.flag_bits == item_flag_bits) {
        return;
    }
    object.flag_bits = item_flag_bits;
    for (const Draw_list_entry_location& location : object.locations) {
        m_draw_lists[location.draw_list_index].entries[location.entry_index].flag_bits = item_flag_bits;
    }
}

void Draw_list_scene::set_object_flags(const erhe::scene::Mesh* mesh, const uint64_t item_flag_bits)
{
    set_object_flags(find_object(mesh), item_flag_bits);
}

void Draw_list_scene::set_exclude_unlit_from_shadows(const bool value)
{
    assert_main_thread();

    if (m_exclude_unlit_from_shadows == value) {
        return;
    }
    m_exclude_unlit_from_shadows = value;
    // Shadow list membership is baked into the cached entries; the flag only
    // takes effect once they are re-classified.
    log_draw_list->info("Draw_list_scene: exclude unlit from shadows = {}; rebuilding draw lists", value);
    rebuild_all();
}

void Draw_list_scene::rebuild_all()
{
    ERHE_PROFILE_FUNCTION();
    assert_main_thread();

    for (Draw_list_object& object : m_objects) {
        object.locations.clear();
    }
    // Full rebuild is the one place lists are dropped (also compacts away
    // lists left empty by unregistration).
    m_draw_lists.clear();
    m_draw_list_index_by_key.clear();
    m_skinned_object_indices.clear();
    for (std::size_t i = 0, end = m_objects.size(); i < end; ++i) {
        Draw_list_object& object = m_objects[i];
        if (!object.alive) {
            continue;
        }
        const erhe::scene::Mesh* mesh = object.info.mesh.get();
        object.mobility             = mesh->skin ? Draw_mobility::skinned : object.info.mobility;
        object.negative_determinant = sample_negative_determinant(*mesh);
        object.layer_id             = mesh->layer_id;
        object.flag_bits            = mesh->get_flag_bits();
        add_entries(static_cast<uint32_t>(i));
        unwatch_object_materials(static_cast<uint32_t>(i));
        watch_object_materials(static_cast<uint32_t>(i));
        const erhe::scene::Node* node = mesh->get_node();
        object.transform_serial = (node != nullptr) ? node->node_data.transforms.world_from_node_serial : 0u;
        object.joint_slot       = mesh->skin ? mesh->skin->skin_data.joint_buffer_index : 0u;
        if (object.mobility == Draw_mobility::skinned) {
            m_skinned_object_indices.push_back(static_cast<uint32_t>(i));
        }
    }
}

auto Draw_list_scene::find_object(const erhe::scene::Mesh* mesh) const -> Draw_list_object_id
{
    const std::unordered_map<const erhe::scene::Mesh*, uint32_t>::const_iterator it = m_object_index_by_mesh.find(mesh);
    if (it == m_object_index_by_mesh.end()) {
        return Draw_list_object_id{};
    }
    return Draw_list_object_id{it->second, m_objects[it->second].generation};
}

auto Draw_list_scene::get_object(const Draw_list_object_id id) const -> const Draw_list_object*
{
    if (!id.is_valid() || (id.index >= m_objects.size())) {
        return nullptr;
    }
    const Draw_list_object& object = m_objects[id.index];
    if (!object.alive || (object.generation != id.generation)) {
        return nullptr;
    }
    return &object;
}

auto Draw_list_scene::get_draw_lists() const -> const std::vector<Draw_list>&
{
    return m_draw_lists;
}

auto Draw_list_scene::get_object_count() const -> std::size_t
{
    return m_alive_object_count;
}

auto Draw_list_scene::get_entry_count() const -> std::size_t
{
    std::size_t count = 0;
    for (const Draw_list& draw_list : m_draw_lists) {
        count += draw_list.entries.size();
    }
    return count;
}

auto Draw_list_scene::describe() const -> std::string
{
    std::stringstream ss;
    std::size_t non_empty = 0;
    for (const Draw_list& draw_list : m_draw_lists) {
        if (!draw_list.entries.empty()) {
            ++non_empty;
        }
    }
    ss << fmt::format(
        "Draw_list_scene: {} objects, {} draw lists ({} non-empty), {} entries, {} pending, {} determinant flips\n",
        m_alive_object_count,
        m_draw_lists.size(),
        non_empty,
        get_entry_count(),
        get_pending_count(),
        m_determinant_flip_count
    );
    for (std::size_t i = 0, end = m_draw_lists.size(); i < end; ++i) {
        const Draw_list& draw_list = m_draw_lists[i];
        if (draw_list.entries.empty()) {
            continue;
        }
        ss << fmt::format("  [{}] entries={} {}\n", i, draw_list.entries.size(), draw_list.key.describe());
    }
    return ss.str();
}

// --- Any-thread API ------------------------------------------------------------

void Draw_list_scene::enqueue_register(const Draw_list_object_create_info& create_info)
{
    const std::lock_guard<std::mutex> lock{m_pending_mutex};
    m_pending.push_back(Pending_op{.kind = Pending_op::Kind::register_, .mesh = create_info.mesh, .mobility = create_info.mobility});
}

void Draw_list_scene::enqueue_unregister(const std::shared_ptr<erhe::scene::Mesh>& mesh)
{
    const std::lock_guard<std::mutex> lock{m_pending_mutex};
    m_pending.push_back(Pending_op{.kind = Pending_op::Kind::unregister, .mesh = mesh});
}

void Draw_list_scene::enqueue_reregister(const std::shared_ptr<erhe::scene::Mesh>& mesh)
{
    const std::lock_guard<std::mutex> lock{m_pending_mutex};
    m_pending.push_back(Pending_op{.kind = Pending_op::Kind::reregister, .mesh = mesh});
}

void Draw_list_scene::enqueue_set_flags(const std::shared_ptr<erhe::scene::Mesh>& mesh, const uint64_t item_flag_bits)
{
    const std::lock_guard<std::mutex> lock{m_pending_mutex};
    m_pending.push_back(Pending_op{.kind = Pending_op::Kind::set_flags, .mesh = mesh, .flag_bits = item_flag_bits});
}

void Draw_list_scene::enqueue_transform_update(const std::shared_ptr<erhe::scene::Mesh>& mesh)
{
    const std::lock_guard<std::mutex> lock{m_pending_mutex};
    m_pending.push_back(Pending_op{.kind = Pending_op::Kind::transform, .mesh = mesh});
}

void Draw_list_scene::enqueue_refresh(const std::shared_ptr<erhe::scene::Mesh>& mesh)
{
    const std::lock_guard<std::mutex> lock{m_pending_mutex};
    m_pending.push_back(Pending_op{.kind = Pending_op::Kind::refresh, .mesh = mesh});
}

auto Draw_list_scene::get_pending_count() const -> std::size_t
{
    const std::lock_guard<std::mutex> lock{m_pending_mutex};
    return m_pending.size();
}

void Draw_list_scene::flush_pending()
{
    ERHE_PROFILE_FUNCTION();
    assert_main_thread();

    std::vector<Pending_op> ops;
    {
        const std::lock_guard<std::mutex> lock{m_pending_mutex};
        ops.swap(m_pending);
    }
    for (const Pending_op& op : ops) {
        switch (op.kind) {
            case Pending_op::Kind::register_: {
                register_object(Draw_list_object_create_info{.mesh = op.mesh, .mobility = op.mobility});
                break;
            }
            case Pending_op::Kind::unregister: {
                unregister_object(op.mesh.get());
                break;
            }
            case Pending_op::Kind::reregister: {
                const Draw_list_object_id id = find_object(op.mesh.get());
                if (id.is_valid()) {
                    reregister_object(id);
                }
                break;
            }
            case Pending_op::Kind::set_flags: {
                const Draw_list_object_id id = find_object(op.mesh.get());
                if (!id.is_valid()) {
                    break; // flags set before attach / after detach: ignore
                }
                // R10b: the negative_determinant flag is maintained by
                // handle_node_transform_update(); a change relative to the
                // value sampled at registration means the object was
                // mirrored at runtime, which the initial scope does not
                // support (assert in debug, log otherwise).
                const bool observed_negative_determinant = (op.flag_bits & erhe::Item_flags::negative_determinant) != 0u;
                const Draw_list_object& object = m_objects[id.index];
                if (observed_negative_determinant != object.negative_determinant) {
                    ++m_determinant_flip_count;
                    log_draw_list->error(
                        "Runtime negative-determinant flip on registered mesh '{}' (registered {}, now {}) - not supported in initial scope",
                        op.mesh->get_name(),
                        object.negative_determinant,
                        observed_negative_determinant
                    );
#if !defined(NDEBUG)
                    ERHE_VERIFY(observed_negative_determinant == object.negative_determinant);
#endif
                }
                set_object_flags(id, op.flag_bits);
                break;
            }
            case Pending_op::Kind::transform: {
                const Draw_list_object_id id = find_object(op.mesh.get());
                if (id.is_valid()) {
                    update_object_transform(id.index);
                }
                break;
            }
            case Pending_op::Kind::refresh: {
                const Draw_list_object_id id = find_object(op.mesh.get());
                if (id.is_valid()) {
                    refresh_object_records(id.index);
                }
                break;
            }
            default: {
                ERHE_FATAL("bad Pending_op kind");
            }
        }
    }

    check_material_changes();
}

// --- Resolution ----------------------------------------------------------------

auto Draw_list_scene::get_object_mesh(const uint32_t object_index) const -> erhe::scene::Mesh*
{
    ERHE_VERIFY(object_index < m_objects.size());
    const Draw_list_object& object = m_objects[object_index];
    ERHE_VERIFY(object.alive);
    return object.info.mesh.get();
}

void Draw_list_scene::set_color_environment(const Color_environment& environment)
{
    if (m_color_environment_set && (m_color_environment == environment)) {
        return;
    }
    m_color_environment     = environment;
    m_color_environment_set = true;
    ++m_color_environment_change_count;
    // R18/R21: only cached resolutions change - never list contents.
    for (Draw_list& draw_list : m_draw_lists) {
        draw_list.color_resolutions.clear();
        draw_list.color_resolution_failed = false;
    }
}

auto Draw_list_scene::resolve_color_stages(Draw_list& draw_list, const uint16_t multiview_count) -> const erhe::graphics::Reloadable_shader_stages*
{
    for (const Draw_list_color_resolution& resolution : draw_list.color_resolutions) {
        if (resolution.multiview_count == multiview_count) {
            return resolution.stages;
        }
    }
    if (!m_color_environment_set) {
        return nullptr; // nothing to resolve against yet (before the first color draw)
    }

    // Combine (plan section 0.2): primitive-derived key from registration OR
    // environment bool bits (none today), environment int axes overwrite (the
    // primitive key never sets those axes when SHADER_DEBUG == 0), multiview
    // axis per view configuration, blending mode from the primitive key.
    Shader_key       key = draw_list.key.primitive_key;
    const Shader_key env = m_color_environment.make_environment_key();
    key.bool_mask |= env.bool_mask;
    for (std::size_t i = 0, end = key.int_values.size(); i < end; ++i) {
        if (env.int_values[i] != 0u) {
            key.int_values[i] = env.int_values[i];
        }
    }
    key.set(Shader_int::SHADER_MULTIVIEW_COUNT, static_cast<uint32_t>(multiview_count));

    const Vertex_input_entry& vertex_input = m_mesh_memory.get_vertex_input(draw_list.key.buffer_set.vertex_input_key);
    const erhe::graphics::Reloadable_shader_stages* stages = m_shader_variant_cache.get(key, &vertex_input.vertex_format);
    if (stages == nullptr) {
        if (!draw_list.color_resolution_failed) {
            log_draw_list->warn("No color shader variant for draw list: {}", draw_list.key.describe());
            draw_list.color_resolution_failed = true;
        }
    }
    draw_list.color_resolutions.push_back(Draw_list_color_resolution{.multiview_count = multiview_count, .stages = stages});
    return stages;
}

void Draw_list_scene::resolve_color_list(Draw_list& draw_list)
{
    for (const uint32_t view_count : m_multiview_view_counts) {
        static_cast<void>(resolve_color_stages(draw_list, static_cast<uint16_t>(view_count)));
    }
}

auto Draw_list_scene::resolve_shadow_stages(Draw_list& draw_list, const Shadow_sub_variant sub_variant) -> const erhe::graphics::Reloadable_shader_stages*
{
    const std::size_t slot = static_cast<std::size_t>(sub_variant);
    ERHE_VERIFY(slot < draw_list.shadow_resolutions.size());
    if (draw_list.shadow_resolutions[slot] != nullptr) {
        return draw_list.shadow_resolutions[slot];
    }
    // Shadow key: USE_SKINNING (from registration) plus the sub-variant's forced
    // bits, empty environment - as Shadow_renderer::draw_shadow_casters builds it.
    Shader_key key = draw_list.key.primitive_key;
    switch (sub_variant) {
        case Shadow_sub_variant::depth_only: {
            key.set(Shader_bool::VARIANT_DEPTH_ONLY, true);
            break;
        }
        case Shadow_sub_variant::depth_only_distance: {
            key.set(Shader_bool::VARIANT_DEPTH_ONLY,      true);
            key.set(Shader_bool::VARIANT_SHADOW_DISTANCE, true);
            break;
        }
        case Shadow_sub_variant::cube: {
            key.set(Shader_bool::VARIANT_SHADOW_CUBE, true);
            break;
        }
        default: {
            ERHE_FATAL("bad Shadow_sub_variant");
        }
    }
    const Vertex_input_entry& vertex_input = m_mesh_memory.get_vertex_input(draw_list.key.buffer_set.vertex_input_key);
    const erhe::graphics::Reloadable_shader_stages* stages = m_shader_variant_cache.get(key, &vertex_input.vertex_format);
    if (stages == nullptr) {
        if (!draw_list.shadow_resolution_failed) {
            log_draw_list->warn("No shadow shader variant ({}) for draw list: {}", c_str(sub_variant), draw_list.key.describe());
            draw_list.shadow_resolution_failed = true;
        }
        return nullptr;
    }
    ++m_lazy_resolution_count;
    draw_list.shadow_resolutions[slot] = stages;
    return stages;
}

// --- Drawing -------------------------------------------------------------------

namespace {

[[nodiscard]] auto layer_selected(const std::span<const erhe::scene::Layer_id> layers, const erhe::scene::Layer_id layer_id) -> bool
{
    if (layers.empty()) {
        return true;
    }
    for (const erhe::scene::Layer_id selected : layers) {
        if (selected == layer_id) {
            return true;
        }
    }
    return false;
}

} // anonymous namespace

void Draw_list_scene::draw_list_chunks(
    Draw_list&                               draw_list,
    erhe::graphics::Render_command_encoder&  render_encoder,
    erhe::graphics::Render_pipeline&         render_pipeline,
    Primitive_buffer&                        primitive_buffer,
    Draw_indirect_buffer&                    draw_indirect_buffer,
    const Primitive_interface_settings&      primitive_settings,
    const erhe::Item_filter&                 filter,
    Draw_statistics&                         statistics
)
{
    // P3a: chunk entries so no multi-draw exceeds the primitive block capacity
    // (ERHE_DRAW_ID indexes the primitives[] array).
    const std::size_t max_per_chunk = std::max<std::size_t>(std::size_t{1}, primitive_buffer.get_max_primitive_count());
    const std::size_t entry_count   = draw_list.entries.size();
    bool list_drew = false;

    erhe::graphics::Buffer* index_buffer = m_mesh_memory.get_index_buffer(draw_list.key.buffer_set.index_buffer);
    const erhe::dataformat::Format index_format = m_mesh_memory.get_index_format(draw_list.key.buffer_set.index_buffer);
    bool buffers_bound = false;

    for (std::size_t begin = 0; begin < entry_count; begin += max_per_chunk) {
        const std::size_t end = std::min(entry_count, begin + max_per_chunk);

        // Skip chunks with no passing entries without acquiring ring buffer
        // space (e.g. the "selected" passes when nothing is selected).
        bool any_passing = false;
        for (std::size_t i = begin; i < end; ++i) {
            if (filter(draw_list.entries[i].flag_bits)) {
                any_passing = true;
                break;
            }
        }
        if (!any_passing) {
            continue;
        }

        std::size_t primitive_count = 0;
        erhe::graphics::Ring_buffer_range primitive_range = primitive_buffer.update(draw_list, begin, end, *this, filter, primitive_settings, primitive_count);
        if (primitive_count == 0) {
            primitive_range.release();
            continue;
        }
        Draw_indirect_buffer_range draw_indirect_range = draw_indirect_buffer.update(draw_list, begin, end, filter);
        ERHE_VERIFY(draw_indirect_range.draw_indirect_count == primitive_count);

        if (!buffers_bound) {
            render_encoder.set_render_pipeline(render_pipeline);
            render_encoder.set_index_buffer(index_buffer);
            for (std::size_t stream_index = 0, stream_end = draw_list.key.buffer_set.vertex_buffers.size(); stream_index < stream_end; ++stream_index) {
                erhe::graphics::Buffer* vertex_buffer = m_mesh_memory.get_vertex_buffer(draw_list.key.buffer_set.vertex_buffers[stream_index]);
                render_encoder.set_vertex_buffer(vertex_buffer, 0, static_cast<uint32_t>(stream_index));
            }
            buffers_bound = true;
        }

        primitive_buffer.bind(render_encoder, primitive_range);
        draw_indirect_buffer.bind(render_encoder, draw_indirect_range.range);

        render_encoder.multi_draw_indexed_primitives_indirect(
            render_pipeline.get_create_info().base.input_assembly.primitive_topology,
            index_format,
            draw_indirect_range.range.get_byte_start_offset_in_buffer(),
            draw_indirect_range.draw_indirect_count,
            sizeof(erhe::graphics::Draw_indexed_primitives_indirect_command)
        );

        primitive_range.release();
        draw_indirect_range.range.release();

        statistics.entry_count     += primitive_count;
        statistics.draw_call_count += 1;
        list_drew = true;
    }
    if (list_drew) {
        statistics.draw_list_count += 1;
    }
}

auto Draw_list_scene::has_drawable_entries(
    const Draw_purpose                           purpose,
    const std::span<const erhe::scene::Layer_id> layers,
    const Draw_blending_selection                blending,
    const erhe::Item_filter&                     filter
) const -> bool
{
    for (const Draw_list& draw_list : m_draw_lists) {
        const Draw_list_key& key = draw_list.key;
        if ((key.purpose != purpose) || draw_list.entries.empty()) {
            continue;
        }
        if ((blending == Draw_blending_selection::opaque_only     ) && (key.blending != Draw_blending::opaque     )) { continue; }
        if ((blending == Draw_blending_selection::translucent_only) && (key.blending != Draw_blending::translucent)) { continue; }
        if (!layer_selected(layers, key.layer_id)) {
            continue;
        }
        for (const Draw_list_entry& entry : draw_list.entries) {
            if (filter(entry.flag_bits)) {
                return true;
            }
        }
    }
    return false;
}

auto Draw_list_scene::draw_color(const Draw_color_parameters& parameters) -> Draw_statistics
{
    ERHE_PROFILE_FUNCTION();
    assert_main_thread();

    Draw_statistics statistics{};
    ERHE_VERIFY(parameters.render_pass != nullptr);

    set_color_environment(parameters.environment);
    sync_gpu_slots();

    const Draw_blending classes_both[] = { Draw_blending::opaque, Draw_blending::translucent };
    const std::span<const Draw_blending> classes =
        (parameters.blending == Draw_blending_selection::opaque_only     ) ? std::span<const Draw_blending>{classes_both, 1} :
        (parameters.blending == Draw_blending_selection::translucent_only) ? std::span<const Draw_blending>{classes_both + 1, 1} :
                                                                             std::span<const Draw_blending>{classes_both, 2};

    for (const Draw_blending blending : classes) {
        for (std::size_t list_index = 0, list_end = m_draw_lists.size(); list_index < list_end; ++list_index) {
            Draw_list& draw_list = m_draw_lists[list_index];
            const Draw_list_key& key = draw_list.key;
            if ((key.purpose != Draw_purpose::color) || (key.blending != blending) || draw_list.entries.empty()) {
                continue;
            }
            if (!layer_selected(parameters.layers, key.layer_id)) {
                continue;
            }
            const std::size_t resolution_count_before = draw_list.color_resolutions.size();
            const erhe::graphics::Reloadable_shader_stages* stages = resolve_color_stages(draw_list, parameters.multiview_count);
            if (draw_list.color_resolutions.size() != resolution_count_before) {
                ++m_lazy_resolution_count;
            }
            if (stages == nullptr) {
                continue;
            }
            const erhe::graphics::Color_blend_state* color_blend = (parameters.color_blend_override != nullptr)
                ? parameters.color_blend_override
                : (blending == Draw_blending::opaque)
                    ? &erhe::graphics::Color_blend_state::color_blend_disabled
                    : &erhe::graphics::Color_blend_state::color_blend_premultiplied;
            const Vertex_input_entry& vertex_input = m_mesh_memory.get_vertex_input(key.buffer_set.vertex_input_key);
            erhe::graphics::Render_pipeline* render_pipeline = parameters.base_render_pipeline.get_pipeline_for(
                parameters.render_pass->get_descriptor(),
                color_blend,
                &stages->shader_stages,
                vertex_input.vertex_input.get(),
                &vertex_input.vertex_format,
                key.negative_determinant
            );
            if (render_pipeline == nullptr) {
                log_draw_list->warn("No render pipeline for draw list {}: {}", list_index, key.describe());
                continue;
            }
            // Keep the label cheap: Shader_key::describe() is a multi-line
            // define dump and would dominate the per-list CPU cost.
            erhe::graphics::Scoped_debug_group list_scope{
                parameters.render_encoder.get_command_buffer(),
                erhe::utility::Debug_label{
                    fmt::format("draw list {} {} {} layer={} entries={}", list_index, c_str(key.mobility), c_str(key.blending), key.layer_id, draw_list.entries.size())
                }
            };
            draw_list_chunks(
                draw_list,
                parameters.render_encoder,
                *render_pipeline,
                parameters.primitive_buffer,
                parameters.draw_indirect_buffer,
                parameters.primitive_settings,
                parameters.filter,
                statistics
            );
        }
    }
    return statistics;
}

auto Draw_list_scene::draw_shadow(const Draw_shadow_parameters& parameters) -> Draw_statistics
{
    ERHE_PROFILE_FUNCTION();
    assert_main_thread();

    Draw_statistics statistics{};
    ERHE_VERIFY(parameters.render_pass != nullptr);
    ERHE_VERIFY(parameters.color_blend != nullptr);
    sync_gpu_slots();

    for (std::size_t list_index = 0, list_end = m_draw_lists.size(); list_index < list_end; ++list_index) {
        Draw_list& draw_list = m_draw_lists[list_index];
        const Draw_list_key& key = draw_list.key;
        if ((key.purpose != Draw_purpose::shadow) || draw_list.entries.empty()) {
            continue;
        }
        if (!layer_selected(parameters.layers, key.layer_id)) {
            continue;
        }
        const erhe::graphics::Reloadable_shader_stages* stages = resolve_shadow_stages(draw_list, parameters.sub_variant);
        if (stages == nullptr) {
            continue;
        }
        const Vertex_input_entry& vertex_input = m_mesh_memory.get_vertex_input(key.buffer_set.vertex_input_key);
        erhe::graphics::Render_pipeline* render_pipeline = parameters.base_render_pipeline.get_pipeline_for(
            parameters.render_pass->get_descriptor(),
            parameters.color_blend,
            &stages->shader_stages,
            vertex_input.vertex_input.get(),
            &vertex_input.vertex_format,
            key.negative_determinant
        );
        if (render_pipeline == nullptr) {
            log_draw_list->warn("No shadow render pipeline for draw list {}: {}", list_index, key.describe());
            continue;
        }
        erhe::graphics::Scoped_debug_group list_scope{
            parameters.render_encoder.get_command_buffer(),
            erhe::utility::Debug_label{
                fmt::format("shadow draw list {} {} {} layer={} entries={}", list_index, c_str(key.mobility), c_str(parameters.sub_variant), key.layer_id, draw_list.entries.size())
            }
        };
        draw_list_chunks(
            draw_list,
            parameters.render_encoder,
            *render_pipeline,
            parameters.primitive_buffer,
            parameters.draw_indirect_buffer,
            Primitive_interface_settings{},
            parameters.filter,
            statistics
        );
    }
    return statistics;
}

} // namespace erhe::scene_renderer
