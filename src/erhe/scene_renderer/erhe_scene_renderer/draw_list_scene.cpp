#include "erhe_scene_renderer/draw_list_scene.hpp"
#include "erhe_scene_renderer/mesh_memory.hpp"
#include "erhe_scene_renderer/scene_renderer_log.hpp"

#include "erhe_item/item.hpp"
#include "erhe_primitive/buffer_mesh.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_primitive/primitive.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_verify/verify.hpp"

#include <fmt/format.h>
#include <glm/glm.hpp>

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
    const Draw_purpose                    purpose
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

Draw_list_scene::Draw_list_scene(
    Mesh_memory&                    mesh_memory,
    Shader_variant_cache&           shader_variant_cache,
    const std::span<const uint32_t> multiview_view_counts
)
    : m_mesh_memory          {mesh_memory}
    , m_shader_variant_cache {shader_variant_cache}
    , m_multiview_view_counts{multiview_view_counts.begin(), multiview_view_counts.end()}
    , m_owner_thread_id      {std::this_thread::get_id()}
{
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
    object.info  = Draw_list_object_create_info{};
    object.alive = false;
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
            const Primitive_classification classification = classify_primitive(m_mesh_memory, *mesh, primitives[i], primitive_mode, purpose);
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
            draw_list.entries.push_back(entry);
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
    }
    draw_list.entries.pop_back();
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

    return Draw_list_object_id{object_index, object.generation};
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
    m_object_index_by_mesh.erase(object.info.mesh.get());
    --m_alive_object_count;
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
            default: {
                ERHE_FATAL("bad Pending_op kind");
            }
        }
    }
}

} // namespace erhe::scene_renderer
