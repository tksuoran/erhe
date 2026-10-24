#include "erhe_scene_renderer/draw_list_scene.hpp"
#include "erhe_scene_renderer/cull_lanes.hpp"
#include "erhe_scene_renderer/draw_indirect_buffer.hpp"
#include "erhe_scene_renderer/cull_frame_registry.hpp"
#include "erhe_scene_renderer/gpu_cull_classifier.hpp"
#include "erhe_scene_renderer/light_buffer.hpp"
#include "erhe_scene_renderer/mesh_memory.hpp"
#include "erhe_scene_renderer/scene_renderer_log.hpp"
#include "erhe_scene_renderer/shader_variant_cache.hpp"

#include "erhe_graphics/draw_indirect.hpp"
#include "erhe_graphics/gpu_timer.hpp"
#include "erhe_graphics/buffer.hpp"
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
#include "erhe_math/math_util.hpp"
#include "erhe_utility/thread_roles.hpp"
#include "erhe_verify/verify.hpp"

#include <iterator>

#include <fmt/format.h>
#include <glm/glm.hpp>
#include <glm/gtx/matrix_operation.hpp>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <sstream>
#include <utility>

namespace erhe::scene_renderer {

namespace {

// Classification of one primitive for one purpose. Mirrors bucket_primitives()
// (mesh_memory.cpp): same skips, same key derivation minus environment.
class Primitive_classification
{
public:
    bool                                accepted    {false};
    bool                                double_sided{false};
    const erhe::primitive::Buffer_mesh* buffer_mesh {nullptr};
    erhe::primitive::Mesh_variant       variant     {erhe::primitive::Mesh_variant::original};
    erhe::primitive::Blending_class     blending   {erhe::primitive::Blending_class::opaque};
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
    if (primitive->render_shape == nullptr) {
        return result;
    }
    // Registration bakes draw parameters, so the variant choice is made here
    // and recorded on the entry. Draw lists are content rendering, so they
    // prefer the optimized variant; the ID pass does not use draw lists.
    const std::pair<erhe::primitive::Mesh_variant, const erhe::primitive::Buffer_mesh*> resolved =
        primitive->get_resolved_renderable_mesh(erhe::primitive::Mesh_variant::optimized, primitive_mode);
    result.variant = resolved.first;
    const erhe::primitive::Buffer_mesh* buffer_mesh = resolved.second;
    if (buffer_mesh == nullptr) {
        return result;
    }
    result.index_range = buffer_mesh->index_range(primitive_mode);
    if (result.index_range.index_count == 0) {
        return result;
    }

    const erhe::primitive::Material* material = mesh_primitive.material.get();

    // Blending classification (R1/R7): the one classification
    // bucket_primitives() applies through Shader_key::get_blending_class()
    // (no material classifies as the opaque default material record).
    result.blending = erhe::primitive::get_blending_class(material);

    // glTF material.doubleSided or USD UsdGeomGprim.doubleSided, through the
    // one helper both the draw lists and the render buckets ask.
    result.double_sided = erhe::scene::is_double_sided(mesh, mesh_primitive);

    // Shadow lists take opaque casters only (Shadow_renderer uses
    // opaque_primitives_only): the position-only caster variants have no
    // alpha test, so alpha-tested and translucent primitives cast nothing.
    if ((purpose == Draw_purpose::shadow) && (result.blending != erhe::primitive::Blending_class::opaque)) {
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
        (material->get_bxdf_model() == erhe::primitive::Bxdf_model::unlit)
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
            // primitive-derived components that matter are the ones that
            // describe how to *get to* a position. Derive with a null material
            // so no material bits leak in, then keep exactly those axes:
            // skinning, and how a_position is encoded - without the latter a
            // quantized mesh would rasterize its caster from raw [-1,1]
            // positions while the lit pass decodes correctly.
            const Shader_key full = Shader_key{}.derive(nullptr, &vertex_input_entry.vertex_format, skinned);
            result.key = Shader_key{};
            result.key.set(Shader_bool::USE_SKINNING, full.get(Shader_bool::USE_SKINNING));
            // Derived from buffer_mesh->vertex_input_key, while the variant is
            // compiled against buffer_set.vertex_input_key (the *expanded* format
            // for solid wireframe). Those agree for every mode even with the
            // per-variant encoding split (doc/erhe/meshoptimizer_integration.md,
            // requirements 9-10): a draw resolved to the optimized variant is
            // fill-only, where the bound key IS buffer_mesh's own; every other
            // mode resolves to the original variant, whose content and expanded
            // formats are both float3 (encoding 0). A variant whose non-fill
            // modes could bind a differently-encoded format would have to derive
            // from the bound key instead.
            result.key.set(Shader_int::VERTEX_POSITION_ENCODING, full.get(Shader_int::VERTEX_POSITION_ENCODING));
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
    if ((material != nullptr) && material->get_use_aniso_control()) {
        hash = erhe::hash::hash(static_cast<uint8_t>(1u), hash);
    }
    return hash;
}

[[nodiscard]] auto sample_negative_determinant(const erhe::scene::Mesh& mesh) -> bool
{
    // R10b: sample from the node's current world transform, not from
    // Item_flags::negative_determinant, which is maintained by
    // handle_transform_update() and lags behind when a mesh is parented.
    const erhe::scene::Node* node = &mesh;
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
    // Equal when the keys are: the resolutions depend on the key only, and
    // in clustered mode the per type light counts never reach it, so a light
    // edit that keeps the key keeps every cached resolution.
    return make_environment_key() == other.make_environment_key();
}

auto Color_environment::make_environment_key() const -> Shader_key
{
    // Mirrors the environment key built in Forward_renderer::render():
    // light counts per type, shadow axes, SHADER_DEBUG = 0 (draw lists never
    // carry the debug axis; those passes use the fallback).
    Shader_key key{};
    set_light_partition_axes(key, light_partition);
    key.set(Shader_int::SHADER_DEBUG,                             0u);
    key.set(Shader_int::SHADOW_FILTER,                            shadow_filter);
    key.set(Shader_int::SHADOW_BIAS,                              shadow_bias);
    key.set(Shader_int::SHADOW_TECHNIQUE,                         shadow_technique);
    key.set(Shader_int::SHADOW_DEPTH_BITS,                        shadow_depth_bits);
    key.set(Shader_int::SPECULAR_ANTI_ALIASING,                   static_cast<uint32_t>(specular_anti_aliasing));
    key.set(Shader_bool::USE_DDGI,                                ddgi_enabled);
    return key;
}

Draw_list_scene::Draw_list_scene(const Draw_list_scene_create_info& create_info)
    : m_material_set           {create_info.material_set_create_info}
    , m_mesh_memory            {*create_info.mesh_memory}
    , m_shader_variant_cache   {*create_info.shader_variant_cache}
    , m_primitive_interface    {*create_info.primitive_interface}
    , m_primitive_record_stride{create_info.primitive_interface->primitive_struct.get_size_bytes()}
    , m_multiview_view_counts  {create_info.multiview_view_counts.begin(), create_info.multiview_view_counts.end()}
    , m_owner_thread_id        {std::this_thread::get_id()}
    , m_exclude_unlit_from_shadows{create_info.exclude_unlit_from_shadows}
    , m_gpu_cull_classifier    {create_info.gpu_cull_classifier}
    , m_cull_frame_registry    {*create_info.cull_frame_registry}
{
    ERHE_VERIFY(create_info.cull_frame_registry  != nullptr);
    ERHE_VERIFY(create_info.mesh_memory          != nullptr);
    ERHE_VERIFY(create_info.shader_variant_cache != nullptr);
    ERHE_VERIFY(create_info.primitive_interface  != nullptr);
    ERHE_VERIFY(m_primitive_record_stride > 0);
    // Shared shadow records need commands that name their record
    // (Record_indexing::first_instance); the device logs its indexing.
    m_record_upload = (m_primitive_interface.record_indexing == erhe::graphics::Record_indexing::first_instance)
        ? Record_upload::shared
        : Record_upload::per_pass;
    m_gpu_cull_check = make_gpu_cull_check();
    // Draw_list_content_changes::scene_instance: unique for the process, so a cache
    // keyed on content serials never mistakes another scene's serial for
    // this one's.
    {
        static std::mutex s_instance_mutex;
        static uint64_t   s_last_instance{0};
        const std::lock_guard<std::mutex> lock{s_instance_mutex};
        m_scene_instance = ++s_last_instance;
    }
}

Draw_list_scene::~Draw_list_scene() noexcept = default;

void Draw_list_scene::assert_render_phase()
{
    // Drawing reads the lists publish() published in M2; it runs in the
    // render phase, on the render thread (doc/editor/render_thread.md
    // section 2.7).
    erhe::utility::Thread_roles::assert_render_thread();
    erhe::utility::Thread_roles::assert_frame_phase(erhe::utility::Frame_phase::r_render);
}

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
    object.world_aabb       = erhe::math::Aabb{};
    object.has_world_aabb   = false;
    object.published_pose.clear();
    object.has_published_pose = false;
    object.alive            = false;
    ++object.generation;
    m_free_object_indices.push_back(object_index);
    mark_shadow_bounds_changed();
}

auto Draw_list_scene::get_or_create_draw_list(const Draw_list_key& key) -> uint32_t
{
    const std::unordered_map<Draw_list_key, uint32_t, Draw_list_key_hash>::const_iterator it = m_draw_list_index_by_key.find(key);
    if (it != m_draw_list_index_by_key.end()) {
        return it->second;
    }
    const uint32_t index = static_cast<uint32_t>(m_draw_lists.size());
    m_draw_lists.emplace_back();
    Draw_list& draw_list = m_draw_lists.back();
    draw_list.key      = key;
    draw_list.identity = ++m_draw_list_identity;
    draw_list.serial   = ++m_draw_list_serial;
    m_draw_list_index_by_key.emplace(key, index);
    return index;
}

auto Draw_list_scene::edit_draw_list(const uint32_t draw_list_index) -> Draw_list&
{
    ERHE_VERIFY(draw_list_index < m_draw_lists.size());
    Draw_list& draw_list = m_draw_lists[draw_list_index];
    draw_list.serial = ++m_draw_list_serial;
    return draw_list;
}

void Draw_list_scene::add_entries(const uint32_t object_index)
{
    ERHE_PROFILE_FUNCTION();

    Draw_list_object& object = m_objects[object_index];
    ERHE_VERIFY(object.alive);
    ERHE_VERIFY(object.locations.empty());
    mark_shadow_bounds_changed();
    // No entries carry bounds yet: write_object_bounds() below records the
    // new ones only.
    object.has_world_aabb = false;
    const erhe::scene::Mesh* mesh = object.info.mesh.get();
    ERHE_VERIFY(mesh != nullptr);

    constexpr erhe::primitive::Primitive_mode primitive_mode = erhe::primitive::Primitive_mode::polygon_fill;
    constexpr Draw_purpose purposes[] = { Draw_purpose::color, Draw_purpose::shadow };

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
            key.double_sided         = classification.double_sided;
            key.primitive_mode       = primitive_mode;
            key.layer_id             = object.layer_id;
            key.buffer_set           = classification.buffer_set;
            key.primitive_key        = classification.key;
            key.primitive_key_hash   = classification.key.get_hash();

            const uint32_t draw_list_index = get_or_create_draw_list(key);
            Draw_list&     draw_list       = edit_draw_list(draw_list_index);

            const erhe::primitive::Buffer_mesh& buffer_mesh = *classification.buffer_mesh;
            Draw_list_entry entry{};
            entry.object_index         = object_index;
            entry.mesh_primitive_index = static_cast<uint16_t>(i);
            entry.variant              = classification.variant;
            entry.flag_bits            = object.flag_bits;
            entry.index_count          = static_cast<uint32_t>(classification.index_range.index_count);
            entry.first_index          = static_cast<uint32_t>(classification.index_range.first_index) + buffer_mesh.base_index();
            entry.base_vertex          = (primitive_mode == erhe::primitive::Primitive_mode::solid_wireframe)
                ? buffer_mesh.expanded_base_vertex()
                : buffer_mesh.base_vertex();
            // world_aabb: write_object_bounds() below, once per object.

            object.locations.push_back(
                Draw_list_entry_location{
                    .draw_list_index = draw_list_index,
                    .entry_index     = static_cast<uint32_t>(draw_list.entries.size())
                }
            );
            draw_list.entries.push_back(entry);
            ERHE_VERIFY(draw_list.primitive_records.size() == (draw_list.entries.size() - 1) * m_primitive_record_stride);
            draw_list.primitive_records.resize(draw_list.entries.size() * m_primitive_record_stride);
            // The bounds: write_object_bounds() below, as for world_aabb.
            draw_list.bounds.resize(draw_list.entries.size());
            write_entry_record(object, entry, get_record(object.locations.back()));
            // The shader stages resolve in the render phase, at the list's
            // first draw (R17): the color environment they combine with is
            // the render phase's.
        }
    }
    write_object_bounds(object_index);
}

void Draw_list_scene::remove_entry(const Draw_list_entry_location& location)
{
    Draw_list& draw_list = edit_draw_list(location.draw_list_index);
    ERHE_VERIFY(location.entry_index < draw_list.entries.size());
    const uint32_t last_index = static_cast<uint32_t>(draw_list.entries.size() - 1);
    if (location.entry_index != last_index) {
        // Swap-remove: the moved entry's owner must be told its new index.
        const Draw_list_entry& moved = draw_list.entries[last_index];
        // The moved entry draws at another position of its list: a shadow
        // pass it reaches may resolve depth ties differently.
        Draw_list_object&      owner = m_objects[moved.object_index];
        if (draw_list.key.purpose == Draw_purpose::shadow) {
            record_changed_object_bounds(owner);
        }
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
        draw_list.bounds.move(last_index, location.entry_index);
        std::memcpy(
            draw_list.primitive_records.data() + static_cast<std::size_t>(location.entry_index) * m_primitive_record_stride,
            draw_list.primitive_records.data() + static_cast<std::size_t>(last_index)           * m_primitive_record_stride,
            m_primitive_record_stride
        );
    }
    draw_list.entries.pop_back();
    draw_list.primitive_records.resize(draw_list.entries.size() * m_primitive_record_stride);
    draw_list.bounds.resize(draw_list.entries.size());
}

// --- Primitive records ---------------------------------------------------------

auto Draw_list_scene::get_record(const Draw_list_entry_location& location) -> std::byte*
{
    Draw_list& draw_list = edit_draw_list(location.draw_list_index);
    ERHE_VERIFY(location.entry_index < draw_list.entries.size());
    ERHE_VERIFY(draw_list.primitive_records.size() == draw_list.entries.size() * m_primitive_record_stride);
    return draw_list.primitive_records.data() + static_cast<std::size_t>(location.entry_index) * m_primitive_record_stride;
}

auto Draw_list_scene::get_entry_material_index(const Draw_list_entry_location& location) const -> uint32_t
{
    ERHE_VERIFY(location.draw_list_index < m_draw_lists.size());
    const Draw_list& draw_list = m_draw_lists[location.draw_list_index];
    ERHE_VERIFY(location.entry_index < draw_list.entries.size());
    ERHE_VERIFY(draw_list.primitive_records.size() == draw_list.entries.size() * m_primitive_record_stride);
    const std::byte* record = draw_list.primitive_records.data() +
        static_cast<std::size_t>(location.entry_index) * m_primitive_record_stride;
    uint32_t material_index = 0;
    std::memcpy(&material_index, record + m_primitive_interface.offsets.material_index, sizeof(uint32_t));
    return material_index;
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

// The material slot comes from the OBJECT, not from the set: membership was
// established before any record write (R3, D1a), so the primitive's material
// is in the object's own list and the id it holds IS the slot. A miss means R3
// was violated for a registered object - a plan bug - and rendering the wrong
// material silently is exactly the class of failure this design removes, so it
// aborts rather than falling back. A primitive with no material names one of
// the set's reserved default slots: the vertex-colored one when its mesh
// authored vertex colors (they are then its albedo), the plain one otherwise.
void write_slot_fields(
    std::byte*                          record,
    const Primitive_struct&             offsets,
    const Draw_list_object&             object,
    const Material_set&                 material_set,
    const erhe::scene::Mesh&            mesh,
    const erhe::scene::Mesh_primitive&  mesh_primitive,
    const erhe::primitive::Buffer_mesh* buffer_mesh
)
{
    const erhe::primitive::Material* material       = mesh_primitive.material.get();
    uint32_t                         material_index = ((buffer_mesh != nullptr) && buffer_mesh->has_vertex_colors)
        ? Material_set::vertex_colored_default_material_slot_index
        : Material_set::default_material_slot_index;
    if (material != nullptr) {
        bool found = false;
        for (const Material_slot_id& id : object.material_slots) {
            if (material_set.get_material(id) == material) {
                material_index = id.index;
                found          = true;
                break;
            }
        }
        ERHE_VERIFY(found);
    }
    const std::shared_ptr<erhe::scene::Skin>& skin    = mesh.skin;
    const float                               skinning_factor  = skin ? 1.0f : 0.0f;
    const uint32_t                            base_joint_index = skin ? skin->skin_data.joint_buffer_index : 0u;
    std::memcpy(record + offsets.material_index,   &material_index,   sizeof(uint32_t));
    std::memcpy(record + offsets.skinning_factor,  &skinning_factor,  sizeof(float));
    std::memcpy(record + offsets.base_joint_index, &base_joint_index, sizeof(uint32_t));
}

// The dequantization affine is static per Buffer_mesh, so it belongs in the
// cached record next to base_vertex. write_entry_record() memsets the record
// and Primitive_buffer::update()'s fast path memcpys it verbatim, patching only
// color and size - so a quantized draw that is not written here would decode
// every position against scale = offset = 0.
//
// buffer_mesh may be null: classify_primitive() guarantees one at registration,
// but refresh_object_records() replays entries against a scene that may have
// been mutated since. Fall back to the identity affine rather than aborting -
// the same shape as the queued-op-vs-mutated-scene abort that
// write_entry_record() has produced before.
void write_position_quantization_fields(std::byte* record, const Primitive_struct& offsets, const erhe::primitive::Buffer_mesh* buffer_mesh)
{
    const Position_quantization quantization = (buffer_mesh != nullptr)
        ? get_position_quantization(buffer_mesh->bounding_box)
        : Position_quantization{};
    std::memcpy(record + offsets.position_scale,  &quantization.scale,  sizeof(glm::vec4));
    std::memcpy(record + offsets.position_offset, &quantization.offset, sizeof(glm::vec4));

    // Texcoord affine, same fallback rule.
    const erhe::primitive::Texcoord_quantization texcoord_quantization = (buffer_mesh != nullptr)
        ? erhe::primitive::get_texcoord_quantization(*buffer_mesh)
        : erhe::primitive::Texcoord_quantization{};
    std::memcpy(record + offsets.texcoord_scale,  &texcoord_quantization.scale,  sizeof(glm::vec4));
    std::memcpy(record + offsets.texcoord_offset, &texcoord_quantization.offset, sizeof(glm::vec4));
}

} // anonymous namespace

void Draw_list_scene::write_entry_record(const Draw_list_object& object, const Draw_list_entry& entry, std::byte* record) const
{
    const Primitive_struct&  offsets = m_primitive_interface.offsets;
    const erhe::scene::Mesh* mesh    = object.info.mesh.get();
    ERHE_VERIFY(mesh != nullptr);
    const erhe::scene::Node* node = mesh;
    const std::vector<erhe::scene::Mesh_primitive>& mesh_primitives = mesh->get_primitives();
    ERHE_VERIFY(entry.mesh_primitive_index < mesh_primitives.size());
    const erhe::scene::Mesh_primitive& mesh_primitive = mesh_primitives[entry.mesh_primitive_index];

    std::memset(record, 0, m_primitive_record_stride);
    write_transform_fields(record, offsets, *node);
    // color / size: pass-dependent, patched by Primitive_buffer::update() per
    // draw; zero here.
    std::memcpy(record + offsets.lightmap_scale_offset, &mesh_primitive.lightmap_uv_scale_offset, sizeof(glm::vec4));
    const erhe::primitive::Primitive*   primitive   = mesh_primitive.primitive.get();
    // The variant the entry's draw parameters were baked from, not a fresh
    // choice: the quantization AABB has to match the vertices being drawn.
    const erhe::primitive::Buffer_mesh* buffer_mesh = (primitive != nullptr) ? primitive->get_renderable_mesh(entry.variant) : nullptr;
    write_slot_fields(record, offsets, object, m_material_set, *mesh, mesh_primitive, buffer_mesh);
    std::memcpy(record + offsets.base_vertex, &entry.base_vertex, sizeof(uint32_t));
    write_position_quantization_fields(record, offsets, buffer_mesh);
}

void Draw_list_scene::write_object_bounds(const uint32_t object_index)
{
    Draw_list_object&        object = m_objects[object_index];
    const erhe::scene::Mesh* mesh   = object.info.mesh.get();
    ERHE_VERIFY(mesh != nullptr);
    // Skinned entries carry no bounds: they are never culled, and the shadow
    // fit bounds the object from its posed joints at gather time.
    const erhe::math::Aabb world_aabb = (object.mobility == Draw_mobility::skinned) ? erhe::math::Aabb{} : mesh->get_aabb_world();
    // A skinned object's geometry changed (a rebuilt primitive, a vertex
    // edit growing its joint boxes): the old posed bounds are recorded here
    // and the next publish records the new ones from the current joint
    // boxes (record_skinned_pose_changes()), whatever the pose.
    if (has_shadow_entries(object)) {
        if (object.has_world_aabb) {
            record_changed_object_bounds(object);
        }
        if (object.mobility != Draw_mobility::skinned) {
            record_changed_bounds(world_aabb);
        }
    }
    if (object.mobility == Draw_mobility::skinned) {
        object.has_published_pose = false;
    }
    for (const Draw_list_entry_location& location : object.locations) {
        Draw_list& draw_list = edit_draw_list(location.draw_list_index);
        draw_list.entries[location.entry_index].world_aabb = world_aabb;
        draw_list.bounds.set(location.entry_index, world_aabb);
    }
    object.world_aabb     = world_aabb;
    object.has_world_aabb = true;
    // The caster vertex shader's node-space input bound (Caster_vertex_extent).
    glm::vec3 node_abs_extent{0.0f};
    for (const erhe::scene::Mesh_primitive& mesh_primitive : mesh->get_primitives()) {
        if (!mesh_primitive.primitive) {
            continue;
        }
        const erhe::math::Aabb node_aabb = mesh_primitive.primitive->get_bounding_box();
        if (!node_aabb.is_valid_3d()) {
            continue;
        }
        node_abs_extent = glm::max(node_abs_extent, glm::max(glm::abs(node_aabb.min), glm::abs(node_aabb.max)));
    }
    object.node_abs_extent = node_abs_extent;
    mark_shadow_bounds_changed();
}

void Draw_list_scene::write_object_transform(const uint32_t object_index)
{
    Draw_list_object&        object = m_objects[object_index];
    const erhe::scene::Mesh* mesh   = object.info.mesh.get();
    ERHE_VERIFY(mesh != nullptr);
    const erhe::scene::Node* node = mesh;
    const Primitive_struct& offsets = m_primitive_interface.offsets;
    // The world bounds move with the node: keep the entries' culling AABB
    // current from the same hook (skinned entries are not culled).
    const erhe::math::Aabb world_aabb = (object.mobility == Draw_mobility::skinned) ? erhe::math::Aabb{} : mesh->get_aabb_world();
    if (has_shadow_entries(object)) {
        record_changed_object_bounds(object);
        if (object.mobility != Draw_mobility::skinned) {
            record_changed_bounds(world_aabb);
        }
    }
    object.world_aabb     = world_aabb;
    object.has_world_aabb = true;
    for (const Draw_list_entry_location& location : object.locations) {
        write_transform_fields(get_record(location), offsets, *node);
        Draw_list& draw_list = edit_draw_list(location.draw_list_index);
        draw_list.entries[location.entry_index].world_aabb = world_aabb;
        draw_list.bounds.set(location.entry_index, world_aabb);
    }
    object.transform_serial = node->node_data.transforms.world_from_node_serial;
    mark_shadow_bounds_changed();
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
        const erhe::scene::Mesh_primitive&  mesh_primitive = mesh_primitives[entry.mesh_primitive_index];
        const erhe::primitive::Primitive*   primitive      = mesh_primitive.primitive.get();
        const erhe::primitive::Buffer_mesh* buffer_mesh    = (primitive != nullptr) ? primitive->get_renderable_mesh(entry.variant) : nullptr;
        write_slot_fields(get_record(location), offsets, object, m_material_set, *mesh, mesh_primitive, buffer_mesh);
    }
    object.joint_slot = mesh->skin ? mesh->skin->skin_data.joint_buffer_index : 0u;
}

void Draw_list_scene::refresh_object_records(const uint32_t object_index)
{
    Draw_list_object& object = m_objects[object_index];
    // A registered object keeps its mesh (register_object() verifies it,
    // release_object() is the only reset), so a queued refresh that flushes
    // after the mesh left its scene still finds it; the unregister queued
    // behind it removes the entries.
    ERHE_VERIFY(object.info.mesh != nullptr);
    // Before the rewrite: this replays
    // entries against a scene that may have mutated since the refresh was
    // enqueued, so the records may be about to name materials the object did
    // not have. Syncing before the early return would instead release a
    // dropped material while the un-rewritten records still name it.
    sync_object_materials(object_index);
    for (const Draw_list_entry_location& location : object.locations) {
        const Draw_list_entry& entry = m_draw_lists[location.draw_list_index].entries[location.entry_index];
        write_entry_record(object, entry, get_record(location));
    }
    // The records now hold the node's current transform and claim its
    // serial below, so a transform op of the same flush skips
    // write_object_transform(): the bounds (and the node extent) follow the
    // records here, which also records the old and new bounds as a change.
    write_object_bounds(object_index);
    const erhe::scene::Mesh* mesh = object.info.mesh.get();
    const erhe::scene::Node* node = mesh;
    object.transform_serial = (node != nullptr) ? node->node_data.transforms.world_from_node_serial : 0u;
    object.joint_slot       = ((mesh != nullptr) && mesh->skin) ? mesh->skin->skin_data.joint_buffer_index : 0u;
    ++m_refresh_count;
}

void Draw_list_scene::update_object_transform(const uint32_t object_index)
{
    Draw_list_object&        object = m_objects[object_index];
    const erhe::scene::Mesh* mesh   = object.info.mesh.get();
    const erhe::scene::Node* node   = mesh;
    if (mesh == nullptr) {
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

void Draw_list_scene::sync_joint_slots()
{
    ERHE_PROFILE_FUNCTION();

    // Materials need no sync: a cached record's material_index is this
    // scene's own Material_set slot, assigned before the record was written
    // and held for as long as the record exists (R2, R3). What is left is the
    // joint half: Joint_poses::capture() assigns each skin's slot when it
    // captures the frame's poses, which the caller did before publish().
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
    mark_shadow_bounds_changed();
    if (has_shadow_entries(object)) {
        record_changed_object_bounds(object);
    }
    object.has_world_aabb     = false;
    object.has_published_pose = false;
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

    // A draw list object exists only for a mesh that is in a scene: every
    // record field is written from the mesh's world transform. Registration
    // arrives through the pending queue (enqueue_register), so the mesh can
    // have left the scene again between the enqueue and this flush - the queue
    // then also holds the matching unregister, but this register op is
    // processed first. The controller placeholder mesh does exactly that: it
    // is added at Controller_visualization construction and removed in the
    // same frame, once the real controller render model has loaded. Decline it
    // here; the add that puts the mesh back in a scene enqueues a fresh
    // registration.
    if (mesh->get_scene() == nullptr) {
        log_draw_list->trace("Not registering mesh '{}': not in a scene", mesh->get_name());
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

    // Membership first: add_entries() writes records that name these slots,
    // and a slot must be assigned before a record can name it (R3).
    sync_object_materials(object_index);
    add_entries(object_index);
    // add_entries() wrote the records from the live node / slots; remember
    // what they were written from for the transform dedup / slot sync.
    {
        const erhe::scene::Node* node = mesh;
        object.transform_serial = (node != nullptr) ? node->node_data.transforms.world_from_node_serial : 0u;
        object.joint_slot       = mesh->skin ? mesh->skin->skin_data.joint_buffer_index : 0u;
        if (object.mobility == Draw_mobility::skinned) {
            m_skinned_object_indices.push_back(object_index);
        }
    }

    return Draw_list_object_id{object_index, object.generation};
}

void Draw_list_scene::sync_object_materials(const uint32_t object_index)
{
    Draw_list_object& object = m_objects[object_index];

    // Already on the main thread, inside this scene's own flush, so the set's
    // direct (non-enqueued) form is the right one.
    std::vector<std::shared_ptr<erhe::primitive::Material>> materials;
    for (const erhe::scene::Mesh_primitive& mesh_primitive : object.info.mesh->get_primitives()) {
        if (mesh_primitive.material) {
            materials.push_back(mesh_primitive.material);
        }
    }
    m_material_set.sync_object_materials(
        static_cast<uint64_t>(object_index),
        std::span<const std::shared_ptr<erhe::primitive::Material>>{materials}
    );

    // The object's own resolution table, rebuilt from the set the diff just
    // settled. Distinct materials only, in first-use order.
    object.material_slots.clear();
    for (const std::shared_ptr<erhe::primitive::Material>& material : materials) {
        const Material_slot_id id = m_material_set.find(material.get());
        ERHE_VERIFY(id.is_valid());
        bool seen = false;
        for (const Material_slot_id& known : object.material_slots) {
            if (known == id) {
                seen = true;
                break;
            }
        }
        if (!seen) {
            object.material_slots.push_back(id);
        }
    }

    // Identity hashes for check_material_changes(); a material this object is
    // the first to use gets its baseline here, at the serial it was derived
    // at.
    for (const std::shared_ptr<erhe::primitive::Material>& material : materials) {
        m_material_identity_hashes.try_emplace(
            material.get(),
            Material_identity{
                .hash   = material_identity_hash(material.get()),
                .serial = material->get_change_serial()
            }
        );
    }
}

void Draw_list_scene::release_object_materials(const uint32_t object_index)
{
    Draw_list_object& object = m_objects[object_index];
    m_material_set.release_object_materials(static_cast<uint64_t>(object_index));
    object.material_slots.clear();
}

void Draw_list_scene::check_material_changes()
{
    ERHE_PROFILE_FUNCTION();

    // Prune first, and by set membership: a material that has left the set has
    // lost its last strong reference and may already be destroyed, so its key
    // must not be dereferenced. Looking it up compares pointers in a hash map
    // and dereferences nothing, which is what makes this safe to do here
    // rather than at the moment the reference was dropped.
    for (
        std::unordered_map<const erhe::primitive::Material*, Material_identity>::iterator i = m_material_identity_hashes.begin();
        i != m_material_identity_hashes.end();
    ) {
        i = m_material_set.find(i->first).is_valid() ? std::next(i) : m_material_identity_hashes.erase(i);
    }

    // A Shader_key derivation per watched material per frame is what this used
    // to cost. Every input the derivation reads is a property of the material,
    // and every write to one advances its change serial, so an entry whose
    // serial did not move still carries the hash the derivation would produce.
    std::vector<const erhe::primitive::Material*> changed;
    for (std::unordered_map<const erhe::primitive::Material*, Material_identity>::value_type& entry : m_material_identity_hashes) {
        const uint64_t serial = entry.first->get_change_serial();
        if (serial == entry.second.serial) {
            continue;
        }
        entry.second.serial = serial;
        const uint64_t hash = material_identity_hash(entry.first);
        if (hash != entry.second.hash) {
            entry.second.hash = hash;
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
        for (const Material_slot_id& id : object.material_slots) {
            const erhe::primitive::Material* material = m_material_set.get_material(id);
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
    release_object_materials(id.index);
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

auto Draw_list_scene::try_material_slot_update(const uint32_t object_index) -> bool
{
    ERHE_PROFILE_FUNCTION();

    Draw_list_object&        object = m_objects[object_index];
    const erhe::scene::Mesh* mesh   = object.info.mesh.get();
    if (mesh == nullptr) {
        return false;
    }

    // Same enumeration order as add_entries(), so the n-th acceptance
    // corresponds to the n-th entry location.
    constexpr erhe::primitive::Primitive_mode primitive_mode = erhe::primitive::Primitive_mode::polygon_fill;
    constexpr Draw_purpose purposes[] = { Draw_purpose::color, Draw_purpose::shadow };

    const std::vector<erhe::scene::Mesh_primitive>& primitives = mesh->get_primitives();
    std::size_t location_index = 0;
    for (std::size_t i = 0, count = primitives.size(); i < count; ++i) {
        for (const Draw_purpose purpose : purposes) {
            const Primitive_classification classification = classify_primitive(
                m_mesh_memory, *mesh, primitives[i], primitive_mode, purpose, m_exclude_unlit_from_shadows
            );
            if (!classification.accepted) {
                continue;
            }
            if (location_index >= object.locations.size()) {
                return false; // more entries than the object has: re-register
            }
            const Draw_list_entry_location& location  = object.locations[location_index];
            const Draw_list&                draw_list = m_draw_lists[location.draw_list_index];
            const Draw_list_entry&          entry     = draw_list.entries[location.entry_index];

            Draw_list_key key{};
            key.purpose              = purpose;
            key.mobility             = object.mobility;
            key.blending             = classification.blending;
            key.negative_determinant = object.negative_determinant;
            key.double_sided         = classification.double_sided;
            key.primitive_mode       = primitive_mode;
            key.layer_id             = object.layer_id;
            key.buffer_set           = classification.buffer_set;
            key.primitive_key        = classification.key;
            key.primitive_key_hash   = classification.key.get_hash();
            if (!(key == draw_list.key)) {
                return false;
            }
            // The variant decides base_vertex and the index range baked into
            // the entry, so a change there is a re-register even under an
            // equal key.
            if ((entry.variant != classification.variant) || (entry.mesh_primitive_index != static_cast<uint16_t>(i))) {
                return false;
            }
            ++location_index;
        }
    }
    if (location_index != object.locations.size()) {
        return false; // fewer entries than the object has: re-register
    }

    sync_object_materials(object_index);
    write_object_gpu_slots(object_index);
    return true;
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
    if (has_shadow_entries(object)) {
        // A skinned object's posed bounds are tested without the caster
        // filter (skinned_changed_bounds).
        if (object.mobility == Draw_mobility::skinned) {
            record_changed_object_bounds(object);
        } else {
            record_flags_change(object, object.flag_bits, item_flag_bits);
        }
    }
    object.flag_bits = item_flag_bits;
    for (const Draw_list_entry_location& location : object.locations) {
        edit_draw_list(location.draw_list_index).entries[location.entry_index].flag_bits = item_flag_bits;
    }
    mark_shadow_bounds_changed();
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
    //
    // ENQUEUED, not applied here (D1d): the next flush_pending() rebuilds,
    // ahead of its queued register / unregister / refresh ops and before
    // that frame's material set update, so every cached record and material
    // slot of the frame comes from one classification.
    log_draw_list->info("Draw_list_scene: exclude unlit from shadows = {}; queueing draw list rebuild", value);
    enqueue_rebuild_all();
}

void Draw_list_scene::rebuild_all()
{
    ERHE_PROFILE_FUNCTION();
    assert_main_thread();

    for (Draw_list_object& object : m_objects) {
        object.locations.clear();
        object.has_world_aabb     = false;
        object.has_published_pose = false;
    }
    // Every list is recreated: the dropped entries are not named.
    m_changes_unbounded = true;
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
        // Before add_entries(), and a diff rather than release-then-take: the
        // old sequence dropped every reference to zero for an instant, freeing
        // slots the records written one line earlier already named.
        sync_object_materials(static_cast<uint32_t>(i));
        add_entries(static_cast<uint32_t>(i));
        const erhe::scene::Node* node = mesh;
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

void Draw_list_scene::enqueue_bounds_update(const std::shared_ptr<erhe::scene::Mesh>& mesh)
{
    const std::lock_guard<std::mutex> lock{m_pending_mutex};
    m_pending.push_back(Pending_op{.kind = Pending_op::Kind::bounds, .mesh = mesh});
}

auto Draw_list_scene::get_pending_count() const -> std::size_t
{
    const std::lock_guard<std::mutex> lock{m_pending_mutex};
    return m_pending.size();
}

void Draw_list_scene::enqueue_material_update(const std::shared_ptr<erhe::scene::Mesh>& mesh)
{
    const std::lock_guard<std::mutex> lock{m_pending_mutex};
    m_pending.push_back(Pending_op{.kind = Pending_op::Kind::material_update, .mesh = mesh});
}

void Draw_list_scene::enqueue_rebuild_all()
{
    const std::lock_guard<std::mutex> lock{m_pending_mutex};
    m_pending.push_back(Pending_op{.kind = Pending_op::Kind::rebuild_all});
}

void Draw_list_scene::flush_pending()
{
    ERHE_PROFILE_FUNCTION();
    assert_main_thread();
    erhe::utility::Thread_roles::assert_main_thread_in_phase(erhe::utility::Frame_phase::m2_prepare);

    std::vector<Pending_op>& ops = m_flushing;
    {
        const std::lock_guard<std::mutex> lock{m_pending_mutex};
        ERHE_VERIFY(ops.empty());
        ops.swap(m_pending);
    }
    // A queued rebuild runs FIRST, ahead of this flush's register / unregister
    // / refresh ops, so it cannot undo them (D1d). Several enqueued rebuilds
    // collapse into one - the rebuild is over the whole object table either
    // way.
    {
        const bool rebuild_queued = std::any_of(
            ops.begin(),
            ops.end(),
            [](const Pending_op& op) { return op.kind == Pending_op::Kind::rebuild_all; }
        );
        if (rebuild_queued) {
            rebuild_all();
        }
    }
    for (const Pending_op& op : ops) {
        switch (op.kind) {
            case Pending_op::Kind::rebuild_all: {
                break; // already applied above
            }
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
            case Pending_op::Kind::material_update: {
                const Draw_list_object_id id = find_object(op.mesh.get());
                if (!id.is_valid()) {
                    break;
                }
                // The full re-register is always correct; the cheap path is
                // taken only when the object's entries provably belong where
                // they already are.
                if (try_material_slot_update(id.index)) {
                    ++m_material_slot_update_count;
                } else {
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
                // handle_transform_update(); a change relative to the
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
            case Pending_op::Kind::bounds: {
                const Draw_list_object_id id = find_object(op.mesh.get());
                if (id.is_valid() && (m_objects[id.index].info.mesh != nullptr)) {
                    write_object_bounds(id.index);
                }
                break;
            }
            default: {
                ERHE_FATAL("bad Pending_op kind");
            }
        }
    }
    ops.clear();

    check_material_changes();
}

// --- Publishing (prepare phase) -------------------------------------------------

void Draw_list_scene::publish(
    const uint64_t                    frame_index,
    const std::size_t                 frames_in_flight,
    const Cull_classification         classification,
    const std::span<const Joint_pose> joint_poses
)
{
    ERHE_PROFILE_FUNCTION();
    assert_main_thread();
    erhe::utility::Thread_roles::assert_main_thread_in_phase(erhe::utility::Frame_phase::m2_prepare);
    ERHE_VERIFY(frames_in_flight > 0);

    // The joint slots first: the rewrite bumps the serials of the lists it
    // touches, so the copy below carries the frame's base_joint_index.
    sync_joint_slots();

    // The slot count is the device's frames in flight, fixed for the
    // device's lifetime: sized once, so the render phase indexes a vector
    // that never reallocates.
    if (m_draw_list_slots.empty()) {
        m_draw_list_slots.resize(frames_in_flight);
    }
    ERHE_VERIFY(m_draw_list_slots.size() == frames_in_flight);

    Draw_list_slot& slot = m_draw_list_slots[frame_index % frames_in_flight];
    slot.frame_index = frame_index;
    slot.published   = true;

    // The GPU classification needs the device's support (doc/plans/
    // culling.md "Two complete paths"); without it the CPU path classifies.
    Cull_classification effective_classification = classification;
    if (
        (classification == Cull_classification::gpu) &&
        ((m_gpu_cull_classifier == nullptr) || !m_gpu_cull_classifier->is_supported())
    ) {
        if (!m_gpu_cull_refusal_logged) {
            log_draw_list->warn("GPU culling classification is not available on this device; the CPU path classifies");
            m_gpu_cull_refusal_logged = true;
        }
        effective_classification = Cull_classification::cpu;
    }
    slot.classification             = effective_classification;
    m_published_cull_classification = effective_classification;

    // A changed pose is a content change: it moves the content serial (no
    // list is copied for it).
    if (record_skinned_pose_changes(joint_poses)) {
        ++m_draw_list_serial;
    }

    // The changes since the previous publish (Draw_list_content_changes); the
    // accumulation restarts (capacity kept).
    slot.change_base_serial = m_published_draw_list_serial;
    slot.changes_unbounded  = m_changes_unbounded;
    slot.changed_bounds.assign(m_changed_bounds);
    slot.flag_changed_bounds.assign(m_flag_changed_bounds);
    slot.skinned_changed_bounds.assign(m_skinned_changed_bounds);
    slot.flag_changed_old_bits.assign(m_flag_changed_old_bits.begin(), m_flag_changed_old_bits.end());
    slot.flag_changed_new_bits.assign(m_flag_changed_new_bits.begin(), m_flag_changed_new_bits.end());
    m_changed_bounds.resize(0);
    m_flag_changed_bounds.resize(0);
    m_skinned_changed_bounds.resize(0);
    m_flag_changed_old_bits.clear();
    m_flag_changed_new_bits.clear();
    m_changes_unbounded          = false;
    m_published_draw_list_serial = m_draw_list_serial;

    if ((slot.serial != m_draw_list_serial) || (slot.lists.size() != m_draw_lists.size())) {
        copy_to_slot(slot);
    }
    if (effective_classification == Cull_classification::gpu) {
        publish_gpu_bounds(slot);
    }
}

void Draw_list_scene::copy_to_slot(Draw_list_slot& slot)
{
    slot.serial = m_draw_list_serial;

    // Lists are only ever appended, except by rebuild_all(), which recreates
    // them all (new identities and serials, so every one is copied below).
    // A grown slot list starts at serial 0, which no working list carries.
    slot.lists.resize(m_draw_lists.size());
    for (std::size_t i = 0, end = m_draw_lists.size(); i < end; ++i) {
        const Draw_list& source      = m_draw_lists[i];
        Draw_list&       destination = slot.lists[i];
        if (destination.serial == source.serial) {
            continue;
        }
        // The key is the list's identity: it changes only with the identity.
        if (destination.identity != source.identity) {
            destination.key      = source.key;
            destination.identity = source.identity;
        }
        // assign() reuses the capacity the slot's copy already has.
        destination.entries          .assign(source.entries.begin(),           source.entries.end());
        destination.primitive_records.assign(source.primitive_records.begin(), source.primitive_records.end());
        destination.bounds.assign(source.bounds);
        destination.serial = source.serial;
        ++m_published_list_copy_count;
    }

    // The lists back to back: the GPU bounds and mask layout
    // (publish_gpu_bounds(), cull_classify.comp).
    slot.entry_offsets.resize(slot.lists.size());
    uint32_t entry_offset = 0;
    for (std::size_t i = 0, end = slot.lists.size(); i < end; ++i) {
        slot.entry_offsets[i] = entry_offset;
        entry_offset += static_cast<uint32_t>(slot.lists[i].entries.size());
    }
    slot.entry_count = entry_offset;
}

auto Draw_list_scene::get_published_slot(const uint64_t frame_index) const -> const Draw_list_slot&
{
    ERHE_VERIFY(!m_draw_list_slots.empty());
    const Draw_list_slot& slot = m_draw_list_slots[frame_index % m_draw_list_slots.size()];
    ERHE_VERIFY(slot.published && (slot.frame_index == frame_index));
    return slot;
}

auto Draw_list_scene::get_published_content_changes(const uint64_t frame_index) const -> Draw_list_content_changes
{
    const Draw_list_slot& slot = get_published_slot(frame_index);
    return Draw_list_content_changes{
        .scene_instance = m_scene_instance,
        .serial         = slot.serial,
        .base_serial    = slot.change_base_serial,
        .unbounded             = slot.changes_unbounded,
        .changed_bounds        = &slot.changed_bounds,
        .flag_changed_bounds   = &slot.flag_changed_bounds,
        .flag_changed_old_bits = slot.flag_changed_old_bits,
        .flag_changed_new_bits = slot.flag_changed_new_bits,
        .skinned_changed_bounds = &slot.skinned_changed_bounds
    };
}

auto Draw_list_scene::has_shadow_entries(const Draw_list_object& object) const -> bool
{
    for (const Draw_list_entry_location& location : object.locations) {
        if (m_draw_lists[location.draw_list_index].key.purpose == Draw_purpose::shadow) {
            return true;
        }
    }
    return false;
}

void Draw_list_scene::record_changed_object_bounds(const Draw_list_object& object)
{
    // A skinned object's entries carry no bounds: what its shadow entries
    // drew is bounded by the pose they were last published with; never
    // published, they drew nothing.
    if (object.mobility == Draw_mobility::skinned) {
        if (object.has_published_pose) {
            record_skinned_changed_bounds(object.published_pose_aabb);
        }
        return;
    }
    // Entries whose bounds were never written carry the no-bounds sentinel.
    record_changed_bounds(object.has_world_aabb ? object.world_aabb : erhe::math::Aabb{});
}

void Draw_list_scene::record_skinned_changed_bounds(const erhe::math::Aabb& aabb)
{
    if (!reserve_changed_box()) {
        return;
    }
    const std::size_t count = m_skinned_changed_bounds.extent_x.size();
    m_skinned_changed_bounds.resize(count + 1);
    m_skinned_changed_bounds.set(count, aabb);
}

namespace {

// The world bounds of a skinned mesh posed by poses (its skin's joints in
// order): per primitive, each joint's bind-space box of the vertices it
// influences, posed by the joint's world_from_bind. A skinned vertex is a
// convex combination of its joints' posed positions, each inside its
// joint's posed box, so it lies inside the union's AABB (the primitive
// builder normalizes the weights, erhe::primitive::normalize_joint_weights()).
// Invalid (the no-bounds sentinel: every view) when a primitive has no joint
// boxes (built without joint attributes, or through the triangle-soup path)
// or a box names a joint the skin does not have.
[[nodiscard]] auto get_posed_aabb(const erhe::scene::Mesh& mesh, const std::span<const Joint_pose> poses) -> erhe::math::Aabb
{
    erhe::math::Aabb aabb;
    for (const erhe::scene::Mesh_primitive& mesh_primitive : mesh.get_primitives()) {
        const erhe::primitive::Primitive* primitive = mesh_primitive.primitive.get();
        if ((primitive == nullptr) || !primitive->render_shape) {
            return erhe::math::Aabb{};
        }
        const std::vector<erhe::math::Aabb>& joint_boxes = primitive->render_shape->get_renderable_mesh().joint_bounding_boxes;
        bool primitive_bounded = false;
        for (std::size_t i = 0, end = joint_boxes.size(); i < end; ++i) {
            if (!joint_boxes[i].is_valid_3d()) {
                continue; // the joint influences no vertex of the primitive
            }
            if (i >= poses.size()) {
                return erhe::math::Aabb{}; // a joint index past the skin's joints
            }
            aabb.include(joint_boxes[i].transformed_by(poses[i].world_from_bind));
            primitive_bounded = true;
        }
        if (!primitive_bounded) {
            return erhe::math::Aabb{};
        }
    }
    return aabb;
}

[[nodiscard]] auto same_pose(const std::vector<glm::mat4>& published, const std::span<const Joint_pose> poses) -> bool
{
    if (published.size() != poses.size()) {
        return false;
    }
    for (std::size_t i = 0, end = poses.size(); i < end; ++i) {
        if (std::memcmp(&published[i], &poses[i].world_from_bind, sizeof(glm::mat4)) != 0) {
            return false;
        }
    }
    return true;
}

} // anonymous namespace

auto Draw_list_scene::record_skinned_pose_changes(const std::span<const Joint_pose> joint_poses) -> bool
{
    ERHE_PROFILE_FUNCTION();

    // The poses are what the frame's shadow passes draw the skinned objects
    // with, and they are a function of the joints' world transforms and of
    // the skin's own data. Joint node transforms carry a change serial
    // (Node_transforms::world_from_node_serial), but edits of a skin's joint
    // list and inverse bind matrices have no change notification, so a
    // compare against the last published copy is the change detection
    // (the AGENTS.md exception for state without a notification). It is
    // O(joints of shadow-casting skins) memcmp per frame with no allocation,
    // next to the capture that already visits every joint each frame.
    bool changed = false;
    for (const uint32_t object_index : m_skinned_object_indices) {
        Draw_list_object& object = m_objects[object_index];
        if (!has_shadow_entries(object)) {
            continue;
        }
        const erhe::scene::Mesh* mesh = object.info.mesh.get();
        // A registered object has its mesh, and its mobility is skinned
        // exactly when the mesh had a skin at registration; skins are set
        // before a mesh is registered (imports, rigid skin creation).
        ERHE_VERIFY((mesh != nullptr) && mesh->skin);
        const erhe::scene::Skin& skin  = *mesh->skin;
        const std::size_t        first = skin.skin_data.joint_buffer_index;
        const std::size_t        count = skin.skin_data.joints.size();
        if ((first + count) > joint_poses.size()) {
            // No poses of this frame for the object (its skin is not among
            // the skins the frame captured): nothing bounds what its entries
            // draw, so every frame forgets every pass.
            if (!m_skinned_pose_missing_logged) {
                log_draw_list->warn(
                    "Draw_list_scene: skinned mesh '{}' has no captured joint poses this frame (skin '{}' not in the captured skins); the shadow map cache forgets every pass while it lasts",
                    mesh->get_name(),
                    skin.get_name()
                );
                m_skinned_pose_missing_logged = true;
            }
            record_changed_object_bounds(object);
            record_skinned_changed_bounds(erhe::math::Aabb{});
            object.has_published_pose = false;
            changed = true;
            continue;
        }
        const std::span<const Joint_pose> poses = joint_poses.subspan(first, count);
        if (object.has_published_pose && same_pose(object.published_pose, poses)) {
            continue;
        }
        record_changed_object_bounds(object); // the old pose, when published
        object.published_pose.resize(count);
        for (std::size_t i = 0; i < count; ++i) {
            object.published_pose[i] = poses[i].world_from_bind;
        }
        object.published_pose_aabb = get_posed_aabb(*mesh, poses);
        object.has_published_pose  = true;
        record_skinned_changed_bounds(object.published_pose_aabb);
        changed = true;
    }
    return changed;
}

auto Draw_list_scene::reserve_changed_box() -> bool
{
    // A bounded list: past this many boxes between two publishes (a scene
    // load, a large selection edit) the change names no boxes.
    constexpr std::size_t max_changed_boxes = 1024;
    if (m_changes_unbounded) {
        return false;
    }
    if ((m_changed_bounds.extent_x.size() + m_flag_changed_bounds.extent_x.size() + m_skinned_changed_bounds.extent_x.size()) >= max_changed_boxes) {
        m_changes_unbounded = true;
        m_changed_bounds.resize(0);
        m_flag_changed_bounds.resize(0);
        m_skinned_changed_bounds.resize(0);
        m_flag_changed_old_bits.clear();
        m_flag_changed_new_bits.clear();
        return false;
    }
    return true;
}

void Draw_list_scene::record_changed_bounds(const erhe::math::Aabb& aabb)
{
    if (!reserve_changed_box()) {
        return;
    }
    const std::size_t count = m_changed_bounds.extent_x.size();
    m_changed_bounds.resize(count + 1);
    m_changed_bounds.set(count, aabb);
}

void Draw_list_scene::record_flags_change(const Draw_list_object& object, const uint64_t old_flag_bits, const uint64_t new_flag_bits)
{
    if (!reserve_changed_box()) {
        return;
    }
    const std::size_t count = m_flag_changed_bounds.extent_x.size();
    m_flag_changed_bounds.resize(count + 1);
    m_flag_changed_bounds.set(count, object.has_world_aabb ? object.world_aabb : erhe::math::Aabb{});
    m_flag_changed_old_bits.push_back(old_flag_bits);
    m_flag_changed_new_bits.push_back(new_flag_bits);
}


auto Draw_list_scene::get_published_draw_lists(const uint64_t frame_index) const -> const std::vector<Draw_list>&
{
    // The prepare phase of this frame published the slot (and the prepare
    // phase of no later frame has reused it yet).
    return get_published_slot(frame_index).lists;
}

// --- Resolution (render phase) --------------------------------------------------

auto Draw_list_scene::get_resolutions(const std::size_t draw_list_index, const Draw_list& draw_list) -> Draw_list_resolutions&
{
    // Grows with the list count (lists are appended in the prepare phase);
    // capacity is kept, so a steady state allocates nothing.
    if (m_resolutions.size() <= draw_list_index) {
        m_resolutions.resize(draw_list_index + 1);
    }
    Draw_list_resolutions& resolutions = m_resolutions[draw_list_index];
    if (resolutions.identity != draw_list.identity) {
        resolutions.identity      = draw_list.identity;
        resolutions.color.clear();
        resolutions.shadow        = {nullptr, nullptr, nullptr};
        resolutions.color_failed  = false;
        resolutions.shadow_failed = false;
    }
    return resolutions;
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
    for (Draw_list_resolutions& resolutions : m_resolutions) {
        resolutions.color.clear();
        resolutions.color_failed = false;
    }
}

namespace {

// Whether the shader variant of key reads the pass-dependent fields of its
// primitive record (color and size, Primitive_buffer's per-pass patch):
// standard.vert under ERHE_VARIANT_POINTS, under ERHE_SOLID_WIREFRAME and
// under ERHE_SHADER_DEBUG != 0, standard.frag under ERHE_VARIANT_ID_RENDER.
// No draw-list pass may (doc/erhe/draw_list_renderer.md "Record upload"):
// the draw-list path takes polygon fill passes without shader debug only,
// and every pass of a frame over a list shares one record patch.
[[nodiscard]] auto reads_record_pass_fields(const Shader_key& key) -> bool
{
    return
        key.get(Shader_bool::VARIANT_POINTS)   ||
        key.get(Shader_bool::SOLID_WIREFRAME)  ||
        key.get(Shader_bool::VARIANT_ID_RENDER) ||
        (key.get(Shader_int::SHADER_DEBUG) != 0u);
}

} // anonymous namespace

auto Draw_list_scene::resolve_color_stages(
    Draw_list_resolutions& resolutions,
    const Draw_list&       draw_list,
    const uint16_t         multiview_count
) -> const erhe::graphics::Reloadable_shader_stages*
{
    for (const Draw_list_color_resolution& resolution : resolutions.color) {
        if (resolution.multiview_count == multiview_count) {
            return resolution.stages;
        }
    }
    if (!m_color_environment_set) {
        return nullptr; // nothing to resolve against yet (before the first color draw)
    }

    // Combine (plan section 0.2): primitive-derived key from registration OR
    // environment bool bits (USE_DDGI, LIGHT_SHADING_CLUSTERED), environment int axes overwrite (the
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
    // The specular anti-aliasing axis reaches only the keys whose shader
    // calls anisotropic_brdf().
    set_specular_anti_aliasing_axis(key, get_specular_anti_aliasing(env));
    key.set(Shader_int::SHADER_MULTIVIEW_COUNT, static_cast<uint32_t>(multiview_count));
    // The color passes share the frame's records of the list (Record
    // upload): exact only while the variant reads no pass-dependent field.
    ERHE_VERIFY(!reads_record_pass_fields(key));

    const Vertex_input_entry& vertex_input = m_mesh_memory.get_vertex_input(draw_list.key.buffer_set.vertex_input_key);
    const erhe::graphics::Reloadable_shader_stages* stages = m_shader_variant_cache.get(key, &vertex_input.vertex_format);
    if (stages == nullptr) {
        if (!resolutions.color_failed) {
            log_draw_list->warn("No color shader variant for draw list: {}", draw_list.key.describe());
            resolutions.color_failed = true;
        }
    }
    resolutions.color.push_back(Draw_list_color_resolution{.multiview_count = multiview_count, .stages = stages});
    return stages;
}

void Draw_list_scene::resolve_color_list(Draw_list_resolutions& resolutions, const Draw_list& draw_list)
{
    for (const uint32_t view_count : m_multiview_view_counts) {
        static_cast<void>(resolve_color_stages(resolutions, draw_list, static_cast<uint16_t>(view_count)));
    }
}

auto Draw_list_scene::resolve_shadow_stages(
    Draw_list_resolutions&   resolutions,
    const Draw_list&         draw_list,
    const Shadow_sub_variant sub_variant
) -> const erhe::graphics::Reloadable_shader_stages*
{
    const std::size_t slot = static_cast<std::size_t>(sub_variant);
    ERHE_VERIFY(slot < resolutions.shadow.size());
    if (resolutions.shadow[slot] != nullptr) {
        return resolutions.shadow[slot];
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
    // The shadow passes share the frame's records of the list (Record
    // upload): exact only while the variant reads no pass-dependent field.
    ERHE_VERIFY(!reads_record_pass_fields(key));
    const Vertex_input_entry& vertex_input = m_mesh_memory.get_vertex_input(draw_list.key.buffer_set.vertex_input_key);
    const erhe::graphics::Reloadable_shader_stages* stages = m_shader_variant_cache.get(key, &vertex_input.vertex_format);
    if (stages == nullptr) {
        if (!resolutions.shadow_failed) {
            log_draw_list->warn("No shadow shader variant ({}) for draw list: {}", c_str(sub_variant), draw_list.key.describe());
            resolutions.shadow_failed = true;
        }
        return nullptr;
    }
    ++m_lazy_resolution_count;
    resolutions.shadow[slot] = stages;
    return stages;
}

// --- Drawing -------------------------------------------------------------------

auto Draw_list_scene::is_layer_selected(const std::span<const erhe::scene::Layer_id> layers, const erhe::scene::Layer_id layer_id) -> bool
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

void Draw_list_scene::draw_list_gpu_chunks(
    const Draw_list&                        draw_list,
    erhe::graphics::Render_command_encoder& render_encoder,
    erhe::graphics::Render_pipeline&        render_pipeline,
    Primitive_buffer&                       primitive_buffer,
    const std::size_t                       draw_list_index,
    const uint32_t                          first_gpu_job,
    const uint64_t                          frame_index,
    Draw_statistics&                        statistics
)
{
    // Every chunk draws its job's commands with the GPU-written count (a
    // zero count draws nothing): no per-entry work on the CPU. The pass's
    // entry and culled counts come from the count readback
    // (get_retired_gpu_shadow_counts()).
    const std::size_t max_per_chunk = get_records_per_chunk(primitive_buffer);
    const std::size_t entry_count   = draw_list.entries.size();
    // The jobs were prepared as triangle draws (shadow casters).
    ERHE_VERIFY(render_pipeline.get_create_info().base.input_assembly.primitive_topology == erhe::graphics::Primitive_type::triangle);
    // The pass prepared its view's jobs (prepare_gpu_shadow_pass()); job j
    // of the frame is job j - first_job of the pass's set.
    ERHE_VERIFY(m_gpu_indirect_count_draw_set.draws != nullptr);
    ERHE_VERIFY(static_cast<std::size_t>(m_gpu_pass_cull_view) == static_cast<std::size_t>(m_gpu_shadow_jobs[first_gpu_job].view));

    // The prepared draw binds the job's index buffer and the command buffer
    // itself.
    render_encoder.set_render_pipeline(render_pipeline);
    for (std::size_t stream_index = 0, stream_end = draw_list.key.buffer_set.vertex_buffers.size(); stream_index < stream_end; ++stream_index) {
        erhe::graphics::Buffer* vertex_buffer = m_mesh_memory.get_vertex_buffer(draw_list.key.buffer_set.vertex_buffers[stream_index]);
        render_encoder.set_vertex_buffer(vertex_buffer, 0, static_cast<uint32_t>(stream_index));
    }

    for (std::size_t begin = 0, chunk = 0; begin < entry_count; begin += max_per_chunk, ++chunk) {
        const std::size_t end       = std::min(entry_count, begin + max_per_chunk);
        const std::size_t job_index = static_cast<std::size_t>(first_gpu_job) + chunk;
        ERHE_VERIFY(job_index < m_gpu_shadow_jobs.size());
        const Gpu_shadow_job& job = m_gpu_shadow_jobs[job_index];
        ERHE_VERIFY(job.entry_count == (end - begin));
        const erhe::graphics::Ring_buffer_range& shared_range = shared_records(draw_list, draw_list_index, begin, end, max_per_chunk, primitive_buffer, frame_index);
        primitive_buffer.bind(render_encoder, shared_range);
        ERHE_VERIFY((job_index >= m_gpu_pass_jobs.first_job) && (job_index < (m_gpu_pass_jobs.first_job + m_gpu_pass_jobs.job_count)));
        render_encoder.multi_draw_indexed_primitives_indirect_count(m_gpu_indirect_count_draw_set, static_cast<uint32_t>(job_index - m_gpu_pass_jobs.first_job));
        statistics.draw_call_count += 1;
    }
    statistics.draw_list_count += 1;
}

auto Draw_list_scene::get_records_per_chunk(const Primitive_buffer& primitive_buffer) -> std::size_t
{
    return std::max<std::size_t>(std::size_t{1}, primitive_buffer.get_max_primitive_count());
}

void Draw_list_scene::draw_list_chunks(
    const Draw_list&                         draw_list,
    erhe::graphics::Render_command_encoder&  render_encoder,
    erhe::graphics::Render_pipeline&         render_pipeline,
    Primitive_buffer&                        primitive_buffer,
    Draw_indirect_buffer&                    draw_indirect_buffer,
    const Primitive_interface_settings&      primitive_settings,
    const erhe::Item_filter&                 filter,
    const std::size_t                        draw_list_index,
    const Cull_view                          cull_view,
    const Draw_record_layout                 record_layout,
    const Draw_command_source                command_source,
    const uint64_t                           frame_index,
    Draw_statistics&                         statistics
)
{
    // P3a: chunk entries so no multi-draw exceeds the primitive block capacity
    // (ERHE_PRIMITIVE_INDEX indexes the primitives[] array of one chunk).
    const std::size_t max_per_chunk = get_records_per_chunk(primitive_buffer);
    const std::size_t entry_count   = draw_list.entries.size();
    bool list_drew = false;

    // GPU-generated commands (doc/plans/culling.md phase 6): the list's jobs
    // for this pass's view, one per chunk, cut with the same chunk size.
    uint32_t first_gpu_job = c_no_gpu_shadow_job;
    if (command_source == Draw_command_source::gpu_generated) {
        ERHE_VERIFY(record_layout == Draw_record_layout::chunk_entries);
        ERHE_VERIFY(cull_view != Cull_view::none);
        ERHE_VERIFY((m_gpu_command_buffer != nullptr) && (m_gpu_count_buffer != nullptr));
        // The jobs were cut with the chunk size of this same buffer.
        ERHE_VERIFY(&primitive_buffer == m_shadow_selection_primitive_buffer);
        const std::size_t lookup_index = (static_cast<std::size_t>(cull_view) * m_gpu_shadow_job_list_count) + draw_list_index;
        ERHE_VERIFY(draw_list_index < m_gpu_shadow_job_list_count);
        ERHE_VERIFY(lookup_index < m_gpu_shadow_job_lookup.size());
        first_gpu_job = m_gpu_shadow_job_lookup[lookup_index];
        // Every shadow list the selection's layers select has jobs for
        // every shadow view of the frame.
        ERHE_VERIFY(first_gpu_job != c_no_gpu_shadow_job);
        draw_list_gpu_chunks(draw_list, render_encoder, render_pipeline, primitive_buffer, draw_list_index, first_gpu_job, frame_index, statistics);
        return;
    }

    // One pass decision per entry, shared by the record and the command
    // writer so the records the commands name are the passing entries' (the
    // packed layout writes only those): the filter on the mirrored
    // flag bits, then the pass's culling view bit of the entry's mask, which
    // classify_cull_views() wrote (skinned entries and entries without
    // bounds have the bit of every view of their purpose set).
    const uint64_t* view_masks = nullptr;
    uint64_t        view_bit   = 0;
    if (cull_view != Cull_view::none) {
        const std::size_t view_index = static_cast<std::size_t>(cull_view);
        ERHE_VERIFY(draw_list_index < m_cull_masks.size());
        const std::vector<uint64_t>& masks = m_cull_masks[draw_list_index];
        ERHE_VERIFY(masks.size() >= (((view_index / 64) + 1) * entry_count));
        view_masks = masks.data() + ((view_index / 64) * entry_count);
        view_bit   = uint64_t{1} << (view_index % 64);
    }
    m_entry_passes.resize(entry_count);
    for (std::size_t i = 0; i < entry_count; ++i) {
        const Draw_list_entry& entry = draw_list.entries[i];
        bool passes = filter(entry.flag_bits);
        if (passes && (view_masks != nullptr) && ((view_masks[i] & view_bit) == 0)) {
            passes = false;
            ++statistics.culled_count;
        }
        m_entry_passes[i] = passes ? 1 : 0;
    }
    const std::span<const std::uint8_t> entry_passes{m_entry_passes};

    erhe::graphics::Buffer* index_buffer = m_mesh_memory.get_index_buffer(draw_list.key.buffer_set.index_buffer);
    const erhe::dataformat::Format index_format = m_mesh_memory.get_index_format(draw_list.key.buffer_set.index_buffer);
    bool buffers_bound = false;
    // The list's pipeline, index and vertex buffers, once, at its first draw.
    const auto bind_list_buffers = [&]() {
        if (buffers_bound) {
            return;
        }
        render_encoder.set_render_pipeline(render_pipeline);
        render_encoder.set_index_buffer(index_buffer);
        for (std::size_t stream_index = 0, stream_end = draw_list.key.buffer_set.vertex_buffers.size(); stream_index < stream_end; ++stream_index) {
            erhe::graphics::Buffer* vertex_buffer = m_mesh_memory.get_vertex_buffer(draw_list.key.buffer_set.vertex_buffers[stream_index]);
            render_encoder.set_vertex_buffer(vertex_buffer, 0, static_cast<uint32_t>(stream_index));
        }
        buffers_bound = true;
    };

    for (std::size_t begin = 0; begin < entry_count; begin += max_per_chunk) {
        const std::size_t end = std::min(entry_count, begin + max_per_chunk);

        // Skip chunks with no passing entries without acquiring ring buffer
        // space (e.g. the "selected" passes when nothing is selected).
        bool any_passing = false;
        for (std::size_t i = begin; i < end; ++i) {
            if (entry_passes[i] != 0) {
                any_passing = true;
                break;
            }
        }
        if (!any_passing) {
            continue;
        }


        // Packed: this pass's own records, one per passing entry, released
        // after the draw. Chunk entries: the chunk's shared records, which
        // stay acquired until the frame's release_frame_records().
        erhe::graphics::Ring_buffer_range        pass_primitive_range{};
        const erhe::graphics::Ring_buffer_range* primitive_range = &pass_primitive_range;
        std::size_t                              primitive_count = 0;
        if (record_layout == Draw_record_layout::packed) {
            pass_primitive_range = primitive_buffer.update(draw_list, begin, end, entry_passes, primitive_settings, primitive_count);
            if (primitive_count == 0) {
                pass_primitive_range.release();
                continue;
            }
        } else {
            primitive_range = &shared_records(draw_list, draw_list_index, begin, end, max_per_chunk, primitive_buffer, frame_index);
        }
        Draw_indirect_buffer_range draw_indirect_range = draw_indirect_buffer.update(draw_list, begin, end, entry_passes, record_layout);
        if (record_layout == Draw_record_layout::packed) {
            ERHE_VERIFY(draw_indirect_range.draw_indirect_count == primitive_count);
        } else {
            primitive_count = draw_indirect_range.draw_indirect_count;
            ERHE_VERIFY(primitive_count > 0);
        }

        bind_list_buffers();

        primitive_buffer.bind(render_encoder, *primitive_range);
        draw_indirect_buffer.bind(render_encoder, draw_indirect_range.range);

        render_encoder.multi_draw_indexed_primitives_indirect(
            render_pipeline.get_create_info().base.input_assembly.primitive_topology,
            index_format,
            draw_indirect_range.range.get_byte_start_offset_in_buffer(),
            draw_indirect_range.draw_indirect_count,
            sizeof(erhe::graphics::Draw_indexed_primitives_indirect_command)
        );

        if (record_layout == Draw_record_layout::packed) {
            pass_primitive_range.release();
        }
        draw_indirect_range.range.release();

        statistics.entry_count     += primitive_count;
        statistics.draw_call_count += 1;
        list_drew = true;
    }
    if (list_drew) {
        statistics.draw_list_count += 1;
    }
}

auto Draw_list_scene::shared_records(
    const Draw_list&  draw_list,
    const std::size_t draw_list_index,
    const std::size_t begin,
    const std::size_t end,
    const std::size_t max_per_chunk,
    Primitive_buffer& primitive_buffer,
    const uint64_t    frame_index
) -> const erhe::graphics::Ring_buffer_range&
{
    ERHE_VERIFY(m_record_upload == Record_upload::shared);
    // The ranges are released in the frame that acquired them
    // (release_frame_records()).
    if (!m_shared_records_open) {
        ERHE_VERIFY(m_shared_record_lists.empty());
        m_shared_records_open       = true;
        m_shared_record_frame_index = frame_index;
        m_cull_frame_registry.add_record_scene(*this, frame_index);
    } else {
        ERHE_VERIFY(m_shared_record_frame_index == frame_index);
    }
    if (m_shared_records.size() <= draw_list_index) {
        m_shared_records.resize(draw_list_index + 1);
    }
    Shared_list_records& shared      = m_shared_records[draw_list_index];
    const std::size_t    chunk_count = (draw_list.entries.size() + max_per_chunk - 1) / max_per_chunk;
    const std::size_t    chunk_index = begin / max_per_chunk;
    ERHE_VERIFY((begin % max_per_chunk) == 0);
    // Every pass of the frame over one list binds the ranges of one
    // Primitive_buffer (one chunk size).
    if (shared.primitive_buffer == nullptr) {
        shared.primitive_buffer = &primitive_buffer;
    } else {
        ERHE_VERIFY(shared.primitive_buffer == &primitive_buffer);
    }
    if (shared.chunks.size() < chunk_count) {
        shared.chunks.resize(chunk_count);
    }
    erhe::graphics::Ring_buffer_range& range = shared.chunks[chunk_index];
    if (range.get_buffer() == nullptr) {
        // The one record patch every pass of the frame shares: the default
        // settings' color and size. Exact for every draw-list pass, since no
        // draw-list shader variant reads a record's color or size
        // (reads_record_pass_fields(), verified where the variants resolve).
        range = primitive_buffer.update_records(draw_list, begin, end, Primitive_interface_settings{});
        if (!shared.in_release_list) {
            m_shared_record_lists.push_back(draw_list_index);
            shared.in_release_list = true;
        }
    }
    return range;
}

void Draw_list_scene::release_frame_records(const uint64_t frame_index)
{
    assert_render_phase();
    ERHE_VERIFY(m_shared_records_open);
    ERHE_VERIFY(m_shared_record_frame_index == frame_index);
    for (const std::size_t list_index : m_shared_record_lists) {
        Shared_list_records& shared = m_shared_records[list_index];
        for (erhe::graphics::Ring_buffer_range& range : shared.chunks) {
            if (range.get_buffer() != nullptr) {
                range.release();
                range = erhe::graphics::Ring_buffer_range{};
            }
        }
        shared.primitive_buffer = nullptr;
        shared.in_release_list  = false;
    }
    m_shared_record_lists.clear();
    m_shared_records_open = false;
}

namespace {

[[nodiscard]] auto same_sphere(const Cull_sphere& lhs, const Cull_sphere& rhs) -> bool
{
    // Bitwise per float (like the planes), never the vec3 object's bytes.
    const auto same_bits = [](const float a, const float b) -> bool {
        return std::bit_cast<uint32_t>(a) == std::bit_cast<uint32_t>(b);
    };
    return
        same_bits(lhs.center.x, rhs.center.x) &&
        same_bits(lhs.center.y, rhs.center.y) &&
        same_bits(lhs.center.z, rhs.center.z) &&
        same_bits(lhs.radius,   rhs.radius);
}

} // anonymous namespace

void Draw_list_scene::begin_cull_frame(const uint64_t frame_index)
{
    // A new frame starts a new view set; the frame's classifications write
    // the masks from zero.
    if (m_cull_frame_started && (m_cull_frame_index == frame_index)) {
        return;
    }
    // The previous frame's shared records were released in their frame
    // (release_frame_records()).
    ERHE_VERIFY(!m_shared_records_open);
    ERHE_VERIFY(m_shared_record_lists.empty());
    // The previous frame's views were classified in their frame
    // (classify_frame_cull_views()).
    ERHE_VERIFY(!m_cull_frame_started || m_cull_frame_classified);
    m_cull_frame_started    = true;
    m_cull_frame_classified = false;
    m_cull_frame_index      = frame_index;
    m_cull_views.clear();
    m_cull_view_survivors.clear();
    // The previous frame's GPU commands belong to that frame.
    m_gpu_command_buffer      = nullptr;
    m_gpu_command_byte_offset = 0;
    m_gpu_count_buffer        = nullptr;
    m_gpu_count_byte_offset   = 0;
    m_gpu_indirect_count_draw_set = erhe::graphics::Indirect_count_draw_set{};
    m_gpu_pass_cull_view          = Cull_view::none;
    m_gpu_pass_jobs               = Gpu_shadow_view_jobs{};
    m_gpu_shadow_jobs.clear();
    for (std::vector<uint64_t>& masks : m_cull_masks) {
        masks.clear();
    }
    // The classification of the frame: Cull_frame_registry::classify_frame()
    // classifies every scene that started a view set.
    m_cull_frame_registry.add_frame_scene(*this, frame_index);
}

auto Draw_list_scene::append_cull_view(const Draw_purpose purpose, const Cull_view_kind kind) -> Cull_view_tests&
{
    ERHE_VERIFY(m_cull_views.size() < static_cast<std::size_t>(Cull_view::none));
    Cull_view_tests& view = m_cull_views.emplace_back();
    view.purpose = purpose;
    view.kind    = kind;
    m_cull_view_survivors.push_back(0);
    return view;
}

auto Draw_list_scene::add_cull_view(
    const uint64_t                   frame_index,
    const Draw_purpose               purpose,
    const std::span<const glm::vec4> planes
) -> Cull_view
{
    assert_render_phase();
    ERHE_VERIFY(planes.size() <= 6);
    if (planes.empty()) {
        return Cull_view::none;
    }
    begin_cull_frame(frame_index);
    // Every view of the frame is added before its classification.
    ERHE_VERIFY(!m_cull_frame_classified);
    // The passes of one viewport share its camera: one view for all of them.
    for (std::size_t i = 0, end = m_cull_views.size(); i < end; ++i) {
        const Cull_view_tests& view = m_cull_views[i];
        if (
            (view.purpose == purpose) &&
            (view.kind == Cull_view_kind::planes) &&
            (view.plane_count == planes.size()) &&
            (std::memcmp(view.planes.data(), planes.data(), planes.size_bytes()) == 0)
        ) {
            return static_cast<Cull_view>(i);
        }
    }
    Cull_view_tests& view = append_cull_view(purpose, Cull_view_kind::planes);
    view.plane_count = static_cast<uint32_t>(planes.size());
    std::copy(planes.begin(), planes.end(), view.planes.begin());
    return static_cast<Cull_view>(m_cull_views.size() - 1);
}

auto Draw_list_scene::add_point_light_cull_views(
    const uint64_t     frame_index,
    const Draw_purpose purpose,
    const Cull_sphere& range_sphere
) -> std::array<Cull_view, 6>
{
    assert_render_phase();
    // A range sphere of zero radius would cull every caster (a point light
    // with range <= 0 has no shadow, Light::is_active()).
    ERHE_VERIFY(range_sphere.radius > 0.0f);
    begin_cull_frame(frame_index);
    // Every view of the frame is added before its classification.
    ERHE_VERIFY(!m_cull_frame_classified);
    std::array<Cull_view, 6> views{};
    // The six faces of a light are appended together, face 0 first.
    for (std::size_t i = 0, end = m_cull_views.size(); i < end; ++i) {
        const Cull_view_tests& view = m_cull_views[i];
        if (
            (view.purpose == purpose) &&
            (view.kind == Cull_view_kind::cube_face) &&
            (view.cube_face == 0) &&
            same_sphere(view.sphere, range_sphere)
        ) {
            ERHE_VERIFY((i + 6) <= end);
            for (std::size_t face = 0; face < 6; ++face) {
                views[face] = static_cast<Cull_view>(i + face);
            }
            return views;
        }
    }
    for (std::size_t face = 0; face < 6; ++face) {
        Cull_view_tests& view = append_cull_view(purpose, Cull_view_kind::cube_face);
        view.sphere    = range_sphere;
        view.cube_face = static_cast<uint32_t>(face);
        views[face]    = static_cast<Cull_view>(m_cull_views.size() - 1);
    }
    return views;
}

void Draw_list_scene::set_shadow_command_selection(const uint64_t frame_index, const Shadow_command_selection& selection)
{
    assert_render_phase();
    ERHE_VERIFY(selection.primitive_buffer != nullptr);
    // Given with the frame's views, before their classification.
    ERHE_VERIFY(!(m_cull_frame_started && (m_cull_frame_index == frame_index) && m_cull_frame_classified));
    if (m_shadow_selection_set && (m_shadow_selection_frame_index == frame_index)) {
        // Every shadow renderer of the frame draws with the same selection.
        ERHE_VERIFY(selection.filter == m_shadow_selection_filter);
        ERHE_VERIFY(std::ranges::equal(selection.layers, m_shadow_selection_layers));
        ERHE_VERIFY(selection.primitive_buffer == m_shadow_selection_primitive_buffer);
        return;
    }
    m_shadow_selection_set          = true;
    m_shadow_selection_frame_index  = frame_index;
    m_shadow_selection_filter       = selection.filter;
    m_shadow_selection_layers.assign(selection.layers.begin(), selection.layers.end());
    m_shadow_selection_primitive_buffer = selection.primitive_buffer;
}

auto Draw_list_scene::has_gpu_shadow_commands(const uint64_t frame_index) const -> bool
{
    return
        m_cull_frame_started &&
        (m_cull_frame_index == frame_index) &&
        m_cull_frame_classified &&
        (m_gpu_command_buffer != nullptr);
}

auto Draw_list_scene::get_retired_gpu_shadow_counts() const -> const Retired_gpu_shadow_counts*
{
    assert_render_phase();
    return m_has_retired_gpu_shadow_counts ? &m_retired_gpu_shadow_counts : nullptr;
}

auto Draw_list_scene::has_cull_view_survivors(const uint64_t frame_index, const Cull_view cull_view) -> bool
{
    assert_render_phase();
    if (cull_view == Cull_view::none) {
        return true;
    }
    verify_cull_view_classified(frame_index, cull_view);
    return m_cull_view_survivors[static_cast<std::size_t>(cull_view)] > 0;
}

auto Draw_list_scene::get_cull_view_tests(const uint64_t frame_index, const Cull_view cull_view) const -> Cull_view_tests
{
    if (cull_view == Cull_view::none) {
        return Cull_view_tests{};
    }
    verify_cull_view_classified(frame_index, cull_view);
    return m_cull_views[static_cast<std::size_t>(cull_view)];
}

void Draw_list_scene::verify_cull_view_classified(const uint64_t frame_index, const Cull_view cull_view) const
{
    if (cull_view == Cull_view::none) {
        return;
    }
    // A view of this frame's set, added before the frame's classification
    // (doc/plans/culling.md phase 6a): a view added at draw time is a defect.
    ERHE_VERIFY(m_cull_frame_started && (m_cull_frame_index == frame_index));
    ERHE_VERIFY(static_cast<std::size_t>(cull_view) < m_cull_views.size());
    ERHE_VERIFY(m_cull_frame_classified);
}

void Draw_list_scene::classify_frame_cull_views(const uint64_t frame_index, erhe::graphics::Command_buffer& command_buffer)
{
    assert_render_phase();
    if (!m_cull_frame_started || (m_cull_frame_index != frame_index)) {
        return;
    }
    ERHE_VERIFY(!m_cull_frame_classified);
    m_cull_frame_classified = true;
    decode_retired_gpu_shadow_counts(frame_index);
    classify_cull_views();
    if (get_published_slot(frame_index).classification == Cull_classification::gpu) {
        dispatch_gpu_cull_views(frame_index, command_buffer);
    }
}

void Draw_list_scene::classify_cull_views()
{
    ERHE_PROFILE_FUNCTION();

    const uint64_t    frame_index = m_cull_frame_index;
    const std::size_t first_view  = 0;
    const std::size_t end_view    = m_cull_views.size();
    const std::size_t group_count = (end_view + 63) / 64;

    // The purposes the views of the set cull for: a list of another
    // purpose is never drawn through them and is not classified.
    std::array<bool, 2> batch_has_purpose{false, false};
    for (std::size_t view_index = first_view; view_index < end_view; ++view_index) {
        batch_has_purpose[static_cast<std::size_t>(m_cull_views[view_index].purpose)] = true;
    }

    const Draw_list_slot&         slot       = get_published_slot(frame_index);
    const std::vector<Draw_list>& draw_lists = slot.lists;
    if (m_cull_masks.size() < draw_lists.size()) {
        m_cull_masks.resize(draw_lists.size());
    }
    for (std::size_t list_index = 0, list_end = draw_lists.size(); list_index < list_end; ++list_index) {
        const Draw_list&       draw_list   = draw_lists[list_index];
        const std::size_t      entry_count = draw_list.entries.size();
        std::vector<uint64_t>& masks       = m_cull_masks[list_index];
        // The masks start at zero (cleared when the frame's set started).
        if (masks.size() < (group_count * entry_count)) {
            masks.resize(group_count * entry_count, uint64_t{0});
        }
        const Draw_purpose purpose = draw_list.key.purpose;
        if ((entry_count == 0) || !batch_has_purpose[static_cast<std::size_t>(purpose)]) {
            continue;
        }
        const Draw_list_bounds& bounds = draw_list.bounds;
        ERHE_VERIFY(bounds.extent_x.size() == entry_count);

        // Skinned entries carry no bounds (they follow the joints): never
        // culled.
        if (draw_list.key.mobility == Draw_mobility::skinned) {
            for (std::size_t view_index = first_view; view_index < end_view; ++view_index) {
                if (m_cull_views[view_index].purpose != purpose) {
                    continue;
                }
                cull_lanes::set_view_bit_lanes(masks.data() + ((view_index / 64) * entry_count), entry_count, uint64_t{1} << (view_index % 64));
                m_cull_view_survivors[view_index] += entry_count;
            }
            continue;
        }

        // Views outside, entries inside: per view, each test is one
        // branch-free lane-wise loop over the SoA bounds (vectorized by the
        // compiler), narrowing the per-entry inside flags; entries with an
        // invalid AABB are kept in every view.
        m_cull_unbounded.resize(entry_count);
        m_cull_inside.resize(entry_count);
        m_cull_sphere_inside.resize(entry_count);
        m_cull_sphere_distance2.resize(entry_count);
        m_cull_sphere_gap_x.resize(entry_count);
        m_cull_sphere_gap_y.resize(entry_count);
        m_cull_sphere_gap_z.resize(entry_count);
        uint32_t* const              unbounded        = m_cull_unbounded.data();
        uint32_t* const   inside           = m_cull_inside.data();
        uint32_t* const   sphere_inside    = m_cull_sphere_inside.data();
        float* const      sphere_distance2 = m_cull_sphere_distance2.data();
        const cull_lanes::Sphere_gaps sphere_gaps{
            .x = m_cull_sphere_gap_x.data(),
            .y = m_cull_sphere_gap_y.data(),
            .z = m_cull_sphere_gap_z.data()
        };
        cull_lanes::mark_unbounded_lanes(bounds, entry_count, unbounded);

        // The range sphere result in sphere_inside and the gaps belong to
        // this view's sphere (the six faces of one point light follow each
        // other, so their shared sphere is tested once per list).
        const Cull_view_tests* sphere_view = nullptr;
        for (std::size_t view_index = first_view; view_index < end_view; ++view_index) {
            const Cull_view_tests& view = m_cull_views[view_index];
            if (view.purpose != purpose) {
                continue;
            }
            if (view.kind == Cull_view_kind::cube_face) {
                if ((sphere_view == nullptr) || !same_sphere(sphere_view->sphere, view.sphere)) {
                    cull_lanes::sphere_lanes(view.sphere, bounds, entry_count, sphere_gaps, sphere_distance2, sphere_inside);
                    sphere_view = &view;
                }
                // Face f looks along axis f / 2, toward + for even f.
                const uint32_t    axis      = view.cube_face / 2;
                const float       direction = ((view.cube_face % 2) == 0) ? 1.0f : -1.0f;
                const float*      gaps[3]   {sphere_gaps.x, sphere_gaps.y, sphere_gaps.z};
                const float*      centers[3]{bounds.center_x.data(), bounds.center_y.data(), bounds.center_z.data()};
                const float*      extents[3]{bounds.extent_x.data(), bounds.extent_y.data(), bounds.extent_z.data()};
                cull_lanes::cube_face_lanes(
                    centers[axis],
                    extents[axis],
                    view.sphere.center[static_cast<glm::length_t>(axis)],
                    direction,
                    gaps[(axis + 1) % 3],
                    gaps[(axis + 2) % 3],
                    sphere_inside,
                    entry_count,
                    inside
                );
            } else {
                std::fill(inside, inside + entry_count, uint32_t{1});
                for (uint32_t plane_index = 0; plane_index < view.plane_count; ++plane_index) {
                    cull_lanes::plane_lanes(view.planes[plane_index], bounds, entry_count, inside);
                }
            }
            m_cull_view_survivors[view_index] += cull_lanes::write_view_bit_lanes(inside, unbounded, entry_count, masks.data() + ((view_index / 64) * entry_count), static_cast<uint32_t>(view_index % 64));
        }
    }
}

auto Draw_list_scene::has_drawable_entries(
    const uint64_t                               frame_index,
    const Draw_purpose                           purpose,
    const std::span<const erhe::scene::Layer_id> layers,
    const Draw_blending_selection                blending,
    const erhe::Item_filter&                     filter
) const -> bool
{
    assert_render_phase();
    for (const Draw_list& draw_list : get_published_draw_lists(frame_index)) {
        const Draw_list_key& key = draw_list.key;
        if ((key.purpose != purpose) || draw_list.entries.empty()) {
            continue;
        }
        if (!is_blending_class_selected(blending, key.blending)) {
            continue;
        }
        if (!is_layer_selected(layers, key.layer_id)) {
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

auto Draw_list_scene::get_filtered_entry_count(
    const uint64_t                               frame_index,
    const Draw_purpose                           purpose,
    const std::span<const erhe::scene::Layer_id> layers,
    const erhe::Item_filter&                     filter
) const -> std::size_t
{
    assert_render_phase();
    std::size_t count = 0;
    for (const Draw_list& draw_list : get_published_draw_lists(frame_index)) {
        const Draw_list_key& key = draw_list.key;
        if ((key.purpose != purpose) || !is_layer_selected(layers, key.layer_id)) {
            continue;
        }
        for (const Draw_list_entry& entry : draw_list.entries) {
            if (filter(entry.flag_bits)) {
                ++count;
            }
        }
    }
    return count;
}

auto Draw_list_scene::draw_color(const Draw_color_parameters& parameters) -> Draw_statistics
{
    ERHE_PROFILE_FUNCTION();
    assert_render_phase();

    Draw_statistics statistics{};
    ERHE_VERIFY(parameters.render_pass != nullptr);

    // Shared records (doc/erhe/draw_list_renderer.md "Record upload"): the
    // color passes bind the frame's shared records of each chunk like the
    // shadow passes; the pass's color and size settings reach no draw-list
    // shader variant (reads_record_pass_fields(), verified at resolution).
    const Draw_record_layout color_record_layout = (m_record_upload == Record_upload::shared)
        ? Draw_record_layout::chunk_entries
        : Draw_record_layout::packed;

    set_color_environment(parameters.environment);
    const std::vector<Draw_list>& draw_lists = get_published_draw_lists(parameters.frame_index);
    verify_cull_view_classified(parameters.frame_index, parameters.cull_view);

    // Classes draw in erhe::primitive::blending_class_draw_order: every
    // discarding (alpha tested) list after every opaque one.
    for (const erhe::primitive::Blending_class blending : erhe::primitive::blending_class_draw_order) {
        if (!is_blending_class_selected(parameters.blending, blending)) {
            continue;
        }
        for (std::size_t list_index = 0, list_end = draw_lists.size(); list_index < list_end; ++list_index) {
            const Draw_list&     draw_list = draw_lists[list_index];
            const Draw_list_key& key       = draw_list.key;
            if ((key.purpose != Draw_purpose::color) || (key.blending != blending) || draw_list.entries.empty()) {
                continue;
            }
            if (!is_layer_selected(parameters.layers, key.layer_id)) {
                continue;
            }
            // R17 / R19: a list's first color draw (and the first after an
            // environment change) resolves every enumerated view
            // configuration; a configuration outside that set resolves
            // lazily here, once.
            Draw_list_resolutions& resolutions = get_resolutions(list_index, draw_list);
            if (resolutions.color.empty()) {
                resolve_color_list(resolutions, draw_list);
            }
            const std::size_t resolution_count_before = resolutions.color.size();
            const erhe::graphics::Reloadable_shader_stages* stages = resolve_color_stages(resolutions, draw_list, parameters.multiview_count);
            if (resolutions.color.size() != resolution_count_before) {
                ++m_lazy_resolution_count;
            }
            if (stages == nullptr) {
                continue;
            }
            const erhe::graphics::Color_blend_state* color_blend = (parameters.color_blend_override != nullptr)
                ? parameters.color_blend_override
                : (blending == erhe::primitive::Blending_class::translucent)
                    ? &erhe::graphics::Color_blend_state::color_blend_premultiplied
                    : &erhe::graphics::Color_blend_state::color_blend_disabled;
            const Vertex_input_entry& vertex_input = m_mesh_memory.get_vertex_input(key.buffer_set.vertex_input_key);
            erhe::graphics::Render_pipeline* render_pipeline = parameters.base_render_pipeline.get_pipeline_for(
                parameters.render_pass->get_descriptor(),
                color_blend,
                &stages->shader_stages,
                vertex_input.vertex_input.get(),
                &vertex_input.vertex_format,
                key.negative_determinant,
                key.double_sided,
                // The color passes can draw GPU-generated commands (as the
                // shadow passes do): on Metal their fragment functions read
                // every texture and sampler from argument buffers
                // (doc/erhe/metal_backend.md "Two Sampler Binding Paths").
                erhe::graphics::Indirect_count_draw_support::enabled
            );
            if (render_pipeline == nullptr) {
                log_draw_list->warn("No render pipeline for draw list {}: {}", list_index, key.describe());
                continue;
            }
            // Keep the label cheap: Shader_key::describe() is a multi-line
            // define dump and would dominate the per-list CPU cost.
            m_list_label.clear();
            fmt::format_to(std::back_inserter(m_list_label), "draw list {} {} {} layer={} entries={}", list_index, c_str(key.mobility), c_str(key.blending), key.layer_id, draw_list.entries.size());
            erhe::graphics::Scoped_debug_group list_scope{parameters.render_encoder.get_command_buffer(), std::string_view{m_list_label}};
            draw_list_chunks(
                draw_list,
                parameters.render_encoder,
                *render_pipeline,
                parameters.primitive_buffer,
                parameters.draw_indirect_buffer,
                parameters.primitive_settings,
                parameters.filter,
                list_index,
                parameters.cull_view,
                color_record_layout,
                Draw_command_source::cpu_written,
                parameters.frame_index,
                statistics
            );
        }
    }
    return statistics;
}

auto Draw_list_scene::draw_shadow(const Draw_shadow_parameters& parameters) -> Draw_statistics
{
    ERHE_PROFILE_FUNCTION();
    assert_render_phase();

    Draw_statistics statistics{};
    ERHE_VERIFY(parameters.render_pass != nullptr);
    ERHE_VERIFY(parameters.color_blend != nullptr);
    const std::vector<Draw_list>& draw_lists = get_published_draw_lists(parameters.frame_index);
    verify_cull_view_classified(parameters.frame_index, parameters.cull_view);

    // The frame's GPU command generation covers every culled shadow pass
    // (doc/plans/culling.md phase 6): its commands were generated for the
    // selection set_shadow_command_selection() recorded, which this pass
    // must draw.
    const bool gpu_commands =
        (m_gpu_command_buffer != nullptr) &&
        (parameters.cull_view != Cull_view::none);
    if (gpu_commands) {
        ERHE_VERIFY(m_shadow_selection_set && (m_shadow_selection_frame_index == parameters.frame_index));
        ERHE_VERIFY(parameters.filter == m_shadow_selection_filter);
        ERHE_VERIFY(std::ranges::equal(parameters.layers, m_shadow_selection_layers));
        ERHE_VERIFY(&parameters.primitive_buffer == m_shadow_selection_primitive_buffer);
    }
    const Draw_command_source command_source = gpu_commands ? Draw_command_source::gpu_generated : Draw_command_source::cpu_written;

    for (std::size_t list_index = 0, list_end = draw_lists.size(); list_index < list_end; ++list_index) {
        const Draw_list&     draw_list = draw_lists[list_index];
        const Draw_list_key& key       = draw_list.key;
        if ((key.purpose != Draw_purpose::shadow) || draw_list.entries.empty()) {
            continue;
        }
        if (!is_layer_selected(parameters.layers, key.layer_id)) {
            continue;
        }
        Draw_list_resolutions& resolutions = get_resolutions(list_index, draw_list);
        const erhe::graphics::Reloadable_shader_stages* stages = resolve_shadow_stages(resolutions, draw_list, parameters.sub_variant);
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
        m_list_label.clear();
        fmt::format_to(std::back_inserter(m_list_label), "shadow draw list {} {} {} layer={} entries={}", list_index, c_str(key.mobility), c_str(parameters.sub_variant), key.layer_id, draw_list.entries.size());
        erhe::graphics::Scoped_debug_group list_scope{parameters.render_encoder.get_command_buffer(), std::string_view{m_list_label}};
        draw_list_chunks(
            draw_list,
            parameters.render_encoder,
            *render_pipeline,
            parameters.primitive_buffer,
            parameters.draw_indirect_buffer,
            Primitive_interface_settings{},
            parameters.filter,
            list_index,
            parameters.cull_view,
            (m_record_upload == Record_upload::shared) ? Draw_record_layout::chunk_entries : Draw_record_layout::packed,
            command_source,
            parameters.frame_index,
            statistics
        );
    }
    return statistics;
}

void Draw_list_scene::prepare_shadow_bounds(
    const Shadow_bounds_selection& selection,
    const uint64_t                 frame_index,
    const std::size_t              frames_in_flight
)
{
    ERHE_PROFILE_FUNCTION();
    assert_main_thread();
    erhe::utility::Thread_roles::assert_main_thread_in_phase(erhe::utility::Frame_phase::m2_prepare);
    ERHE_VERIFY(frames_in_flight > 0);

    // The slot count is the device's frames in flight, fixed for the
    // device's lifetime: sized once, so the render phase indexes a vector
    // that never reallocates.
    if (m_shadow_bounds_slots.empty()) {
        m_shadow_bounds_slots.resize(frames_in_flight);
    }
    ERHE_VERIFY(m_shadow_bounds_slots.size() == frames_in_flight);

    Shadow_bounds_slot& slot = m_shadow_bounds_slots[frame_index % frames_in_flight];
    slot.frame_index = frame_index;
    slot.prepared    = true;

    const bool same_selection =
        (slot.caster_filter == selection.caster_filter) &&
        std::equal(slot.layers.begin(), slot.layers.end(), selection.layers.begin(), selection.layers.end());
    if (same_selection && (slot.serial == m_shadow_bounds_serial)) {
        if (update_skinned_shadow_bounds(slot)) {
            return;
        }
        // A skinned object's posed bounds became valid or invalid, which
        // adds or drops it: a change of what the gather reads, so every
        // slot gathers again and the extents identity moves.
        mark_shadow_bounds_changed();
    }
    gather_shadow_bounds(selection, slot);
}

auto Draw_list_scene::get_shadow_bounds(const uint64_t frame_index) const -> const Shadow_bounds&
{
    assert_render_phase();
    ERHE_VERIFY(!m_shadow_bounds_slots.empty());
    const Shadow_bounds_slot& slot = m_shadow_bounds_slots[frame_index % m_shadow_bounds_slots.size()];
    // The prepare phase of this frame filled the slot (and the prepare
    // phase of no later frame has reused it yet).
    ERHE_VERIFY(slot.prepared && (slot.frame_index == frame_index));
    return slot.bounds;
}

void Draw_list_scene::gather_shadow_bounds(const Shadow_bounds_selection& selection, Shadow_bounds_slot& slot)
{
    ERHE_PROFILE_FUNCTION();

    ++m_shadow_bounds_gather_count;
    slot.layers.assign(selection.layers.begin(), selection.layers.end());
    slot.caster_filter = selection.caster_filter;
    slot.serial        = m_shadow_bounds_serial;

    // The same state and selection gather the same extents (the slots of
    // the frames in flight each gather them): one identity for all of them.
    const bool same_extents =
        (m_extents_serial != 0) &&
        (m_extents_bounds_serial == m_shadow_bounds_serial) &&
        (m_extents_caster_filter == selection.caster_filter) &&
        std::equal(m_extents_layers.begin(), m_extents_layers.end(), selection.layers.begin(), selection.layers.end());
    if (!same_extents) {
        ++m_extents_serial;
        m_extents_bounds_serial = m_shadow_bounds_serial;
        m_extents_caster_filter = selection.caster_filter;
        m_extents_layers.assign(selection.layers.begin(), selection.layers.end());
    }
    slot.bounds.extents_identity = Caster_extents_identity{.scene_instance = m_scene_instance, .serial = m_extents_serial};

    Shadow_bounds& bounds = slot.bounds;
    bounds.caster_world_aabbs.clear();
    bounds.receiver_world_aabbs.clear();
    bounds.caster_vertex_extents.clear();
    slot.skinned_objects.clear();

    constexpr uint64_t visible_active = erhe::Item_flags::visible | erhe::Item_flags::active;
    for (uint32_t object_index = 0, end = static_cast<uint32_t>(m_objects.size()); object_index < end; ++object_index) {
        const Draw_list_object& object = m_objects[object_index];
        if (!object.alive || object.locations.empty()) {
            continue;
        }
        if (!is_layer_selected(selection.layers, object.layer_id)) {
            continue;
        }
        if ((object.flag_bits & visible_active) != visible_active) {
            continue;
        }
        // A caster holds at least one shadow entry: classification already
        // left out the primitives that never cast (unlit when excluded,
        // translucent), so a mesh made only of those is a receiver only.
        bool is_caster = false;
        if (selection.caster_filter(object.flag_bits)) {
            for (const Draw_list_entry_location& location : object.locations) {
                if (m_draw_lists[location.draw_list_index].key.purpose == Draw_purpose::shadow) {
                    is_caster = true;
                    break;
                }
            }
        }
        const erhe::scene::Mesh* mesh = object.info.mesh.get();
        ERHE_VERIFY(mesh != nullptr);
        // Every entry of an object carries the object's world AABB; a skinned
        // object's entries carry none (its bounds follow the joints), so it
        // is bounded here from its posed joints, and again by every
        // update_skinned_shadow_bounds().
        const bool                      skinned        = (object.mobility == Draw_mobility::skinned);
        const Draw_list_entry_location& first_location = object.locations.front();
        const erhe::math::Aabb aabb = skinned
            ? mesh->get_aabb_world()
            : m_draw_lists[first_location.draw_list_index].entries[first_location.entry_index].world_aabb;
        const bool valid = aabb.is_valid_3d();
        Shadow_bounds_skinned_object skinned_object{.object_index = object_index, .valid = valid};
        if (valid) {
            skinned_object.receiver_position = static_cast<uint32_t>(bounds.receiver_world_aabbs.size());
            bounds.receiver_world_aabbs.push_back(aabb);
            if (is_caster) {
                skinned_object.caster_position = static_cast<uint32_t>(bounds.caster_world_aabbs.size());
                bounds.caster_world_aabbs.push_back(aabb);
                const glm::mat4 world_from_node = mesh->world_from_node();
                bounds.caster_vertex_extents.push_back(
                    Caster_vertex_extent{
                        .world_from_node_linear = glm::mat3{world_from_node},
                        .world_translation      = glm::vec3{world_from_node[3]},
                        .node_abs_extent        = object.node_abs_extent
                    }
                );
            }
        }
        if (skinned) {
            slot.skinned_objects.push_back(skinned_object);
        }
    }
}

auto Draw_list_scene::update_skinned_shadow_bounds(Shadow_bounds_slot& slot) const -> bool
{
    ERHE_PROFILE_FUNCTION();

    Shadow_bounds& bounds = slot.bounds;
    for (const Shadow_bounds_skinned_object& skinned_object : slot.skinned_objects) {
        const Draw_list_object&  object = m_objects[skinned_object.object_index];
        const erhe::scene::Mesh* mesh   = object.info.mesh.get();
        ERHE_VERIFY(mesh != nullptr);
        const erhe::math::Aabb aabb = mesh->get_aabb_world();
        if (aabb.is_valid_3d() != skinned_object.valid) {
            return false;
        }
        if (skinned_object.receiver_position != Shadow_bounds_skinned_object::none) {
            bounds.receiver_world_aabbs[skinned_object.receiver_position] = aabb;
        }
        if (skinned_object.caster_position != Shadow_bounds_skinned_object::none) {
            bounds.caster_world_aabbs[skinned_object.caster_position] = aabb;
        }
    }
    return true;
}

} // namespace erhe::scene_renderer
