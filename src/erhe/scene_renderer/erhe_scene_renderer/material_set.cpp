#include "erhe_scene_renderer/material_set.hpp"

#include "erhe_scene_renderer/material_buffer.hpp"

#include "erhe_graphics/command_buffer.hpp"
#include "erhe_graphics/compute_command_encoder.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/multi_copy_buffer.hpp"
#include "erhe_graphics/render_command_encoder.hpp"
#include "erhe_graphics/texture_heap.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_verify/verify.hpp"

#include <algorithm>

namespace erhe::scene_renderer {

// The GPU half, held apart only so that a membership-only set costs nothing
// and needs no device (R16). Buffer and heap are always reset and rewritten
// together: a heap handle baked into a record is meaningless in any other
// heap, and, with the record persisting, meaningless once this heap drops the
// allocation.
class Material_set::Gpu
{
public:
    Gpu(const Material_set_create_info& create_info)
        : material_buffer{*create_info.graphics_device, *create_info.material_interface}
        , texture_heap{
            *create_info.graphics_device,
            *create_info.fallback_texture,
            *create_info.fallback_sampler,
            create_info.bind_group_layout,
            create_info.max_textures
        }
        , buffer{
            *create_info.graphics_device,
            erhe::graphics::Multi_copy_buffer_create_info{
                .initial_copy_byte_count = create_info.initial_material_count *
                    create_info.material_interface->material_struct.get_size_bytes(),
                .copy_count              = 0,
                .buffer_target           = create_info.material_interface->material_block.get_binding_target(),
                .binding_point           = create_info.material_interface->material_block.get_binding_point(),
                .debug_label             = create_info.debug_label
            }
        }
    {
    }

    Material_buffer                                   material_buffer;
    erhe::graphics::Texture_heap                      texture_heap;
    erhe::graphics::Multi_copy_buffer                 buffer;
    // Slot-ordered view of the table, holes null. A member so the per-write
    // allocation is amortized.
    std::vector<const erhe::primitive::Material*>     slot_materials;
    bool                                              force_dirty {true};
    std::size_t                                       write_count {0};
    std::size_t                                       written_byte_count{0};
};

// Reserves default_material_slot_index and
// vertex_colored_default_material_slot_index. The slots are alive - so they
// are covered by every GPU write and by get_slot_count() - carry no Material,
// and are reachable by no lookup: they are in neither m_index_by_material nor
// m_free_slots, which is what keeps allocate_slot() from ever handing them out
// and release_slot_if_unreferenced() from ever freeing them.
void Material_set::reserve_default_slots()
{
    ERHE_VERIFY(m_materials.empty());
    for (uint32_t index = 0; index < first_material_slot_index; ++index) {
        Material_slot& slot = m_materials.emplace_back();
        slot.alive = true;
    }
}

Material_set::Material_set()
{
    reserve_default_slots();
}

Material_set::Material_set(const Material_set_create_info& create_info)
{
    reserve_default_slots();
    ERHE_VERIFY(create_info.graphics_device    != nullptr);
    ERHE_VERIFY(create_info.material_interface != nullptr);
    ERHE_VERIFY(create_info.fallback_texture   != nullptr);
    ERHE_VERIFY(create_info.fallback_sampler   != nullptr);
    m_gpu = std::make_unique<Gpu>(create_info);
}

Material_set::~Material_set() noexcept = default;

auto Material_set::has_gpu() const -> bool
{
    return m_gpu != nullptr;
}

auto Material_set::get_write_count() const -> std::size_t
{
    return m_gpu ? m_gpu->write_count : 0;
}

auto Material_set::get_written_byte_count() const -> std::size_t
{
    return m_gpu ? m_gpu->written_byte_count : 0;
}

void Material_set::invalidate()
{
    if (m_gpu) {
        m_gpu->force_dirty = true;
    }
}

void Material_set::update(erhe::graphics::Command_buffer& command_buffer)
{
    ERHE_PROFILE_FUNCTION();
    ERHE_VERIFY(m_gpu);

    // Hash every live member, always. This is what catches a Material_data
    // field written straight through the object - the colour picker drag, the
    // MCP edit_material tool - with no notification of any kind, which is the
    // failure a version counter cannot see.
    bool dirty = m_gpu->force_dirty || m_membership_dirty;
    for (Material_slot& slot : m_materials) {
        if (!slot.alive || (slot.material == nullptr)) {
            continue;
        }
        const uint64_t content_hash = m_gpu->material_buffer.get_content_hash(slot.material.get());
        if (content_hash != slot.content_hash) {
            slot.content_hash = content_hash;
            dirty = true;
        }
    }
    if (!dirty) {
        return;
    }

    m_membership_dirty = false;
    m_gpu->force_dirty = false;

    const std::size_t slot_count = get_slot_count();
    ERHE_VERIFY(slot_count >= first_material_slot_index); // the reserved default slots are always live
    m_gpu->slot_materials.clear();
    m_gpu->slot_materials.resize(slot_count, nullptr);
    for (std::size_t i = 0; i < slot_count; ++i) {
        const Material_slot& slot = m_materials[i];
        if (slot.alive) {
            m_gpu->slot_materials[i] = slot.material.get();
        }
    }

    // The heap is repopulated by the record write that follows, and by nothing
    // else, so it is reset here and only here.
    m_gpu->texture_heap.reset_heap(command_buffer);

    const std::size_t entry_byte_count = m_gpu->material_buffer.get_record_byte_count();
    const std::size_t byte_count       = slot_count * entry_byte_count;

    const std::span<std::byte> gpu_data = m_gpu->buffer.begin_write(byte_count);
    m_gpu->material_buffer.write_records(gpu_data, m_gpu->texture_heap, m_gpu->slot_materials);
    m_gpu->buffer.commit(byte_count);
    ++m_gpu->write_count;
    m_gpu->written_byte_count += byte_count;
}

auto Material_set::bind(erhe::graphics::Render_command_encoder& encoder) -> bool
{
    ERHE_VERIFY(m_gpu);
    const bool buffer_bound = m_gpu->buffer.bind(encoder);
    m_gpu->texture_heap.bind(encoder);
    return buffer_bound;
}

auto Material_set::bind(erhe::graphics::Compute_command_encoder& encoder) -> bool
{
    ERHE_VERIFY(m_gpu);
    const bool buffer_bound = m_gpu->buffer.bind(encoder);
    m_gpu->texture_heap.bind(encoder);
    return buffer_bound;
}

void Material_set::unbind(erhe::graphics::Command_buffer& command_buffer)
{
    ERHE_VERIFY(m_gpu);
    m_gpu->texture_heap.unbind(command_buffer);
}

auto Material_set::allocate_slot(const std::shared_ptr<erhe::primitive::Material>& material) -> uint32_t
{
    ERHE_VERIFY(material);
    const auto i = m_index_by_material.find(material.get());
    if (i != m_index_by_material.end()) {
        return i->second;
    }

    uint32_t index = 0;
    if (!m_free_slots.empty()) {
        index = m_free_slots.back();
        m_free_slots.pop_back();
    } else {
        index = static_cast<uint32_t>(m_materials.size());
        m_materials.emplace_back();
    }

    Material_slot& slot = m_materials[index];
    // The generation was bumped when the slot was freed, so a Material_slot_id
    // issued for the previous occupant no longer validates.
    slot.material   = material;
    slot.in_library = false;
    slot.use_count  = 0;
    slot.alive        = true;
    // A reused slot must not carry the previous occupant's hash into the
    // first update after the reuse.
    slot.content_hash = 0;
    m_index_by_material.emplace(material.get(), index);
    m_membership_dirty = true;
    return index;
}

void Material_set::release_slot_if_unreferenced(const uint32_t index)
{
    ERHE_VERIFY(index < m_materials.size());
    Material_slot& slot = m_materials[index];
    if (!slot.alive || slot.in_library || (slot.use_count > 0)) {
        return;
    }
    m_index_by_material.erase(slot.material.get());
    slot.material.reset();
    slot.alive = false;
    ++slot.generation;
    m_free_slots.push_back(index);
    m_membership_dirty = true;
}

void Material_set::sync_library(const std::span<const std::shared_ptr<erhe::primitive::Material>> materials)
{
    // Reconciled by difference, not by rebuild: the slot a material already
    // holds must survive a library update that did not mention it, or every
    // cached record naming that slot would have to be rewritten.
    for (const std::shared_ptr<erhe::primitive::Material>& material : materials) {
        if (!material) {
            continue;
        }
        const uint32_t index = allocate_slot(material);
        m_materials[index].in_library = true;
    }

    for (std::size_t i = default_material_slot_index + 1, end = m_materials.size(); i < end; ++i) {
        Material_slot& slot = m_materials[i];
        if (!slot.alive || !slot.in_library) {
            continue;
        }
        const erhe::primitive::Material* material = slot.material.get();
        const bool still_in_library = std::any_of(
            materials.begin(),
            materials.end(),
            [material](const std::shared_ptr<erhe::primitive::Material>& entry) { return entry.get() == material; }
        );
        if (!still_in_library) {
            slot.in_library = false;
            release_slot_if_unreferenced(static_cast<uint32_t>(i));
        }
    }
}

auto Material_set::add_ref(const std::shared_ptr<erhe::primitive::Material>& material) -> Material_slot_id
{
    if (!material) {
        return Material_slot_id{};
    }
    const uint32_t index = allocate_slot(material);
    Material_slot& slot = m_materials[index];
    ++slot.use_count;
    return Material_slot_id{.index = index, .generation = slot.generation};
}

void Material_set::release(const erhe::primitive::Material* material)
{
    if (material == nullptr) {
        return;
    }
    const auto i = m_index_by_material.find(material);
    if (i == m_index_by_material.end()) {
        return;
    }
    const uint32_t index = i->second;
    Material_slot& slot = m_materials[index];
    ERHE_VERIFY(slot.use_count > 0);
    --slot.use_count;
    release_slot_if_unreferenced(index);
}

void Material_set::sync_object_materials(
    const uint64_t                                                    object_key,
    const std::span<const std::shared_ptr<erhe::primitive::Material>> materials
)
{
    // Apply the DIFFERENCE, so a material shared between the old and the new
    // list keeps a positive count throughout and never loses its slot in
    // between - records naming it stay valid across the reassignment.
    std::vector<std::shared_ptr<erhe::primitive::Material>> next;
    for (const std::shared_ptr<erhe::primitive::Material>& material : materials) {
        if (!material) {
            continue;
        }
        const bool already_listed = std::any_of(
            next.begin(),
            next.end(),
            [&material](const std::shared_ptr<erhe::primitive::Material>& entry) { return entry == material; }
        );
        if (!already_listed) {
            next.push_back(material);
        }
    }

    std::vector<std::shared_ptr<erhe::primitive::Material>>& previous = m_object_materials[object_key];
    for (const std::shared_ptr<erhe::primitive::Material>& material : next) {
        const bool held = std::any_of(
            previous.begin(),
            previous.end(),
            [&material](const std::shared_ptr<erhe::primitive::Material>& entry) { return entry == material; }
        );
        if (!held) {
            static_cast<void>(add_ref(material));
        }
    }
    for (const std::shared_ptr<erhe::primitive::Material>& material : previous) {
        const bool kept = std::any_of(
            next.begin(),
            next.end(),
            [&material](const std::shared_ptr<erhe::primitive::Material>& entry) { return entry == material; }
        );
        if (!kept) {
            release(material.get());
        }
    }
    previous = std::move(next);
}

void Material_set::release_object_materials(const uint64_t object_key)
{
    const auto i = m_object_materials.find(object_key);
    if (i == m_object_materials.end()) {
        return;
    }
    // Move the list out first: release() can drop the last reference and
    // destroy the Material, and the map entry being iterated is what holds it.
    const std::vector<std::shared_ptr<erhe::primitive::Material>> materials = std::move(i->second);
    m_object_materials.erase(i);
    for (const std::shared_ptr<erhe::primitive::Material>& material : materials) {
        release(material.get());
    }
}

void Material_set::enqueue_object_materials(
    const uint64_t                                                    object_key,
    const std::span<const std::shared_ptr<erhe::primitive::Material>> materials
)
{
    const std::lock_guard<std::mutex> lock{m_pending_mutex};
    m_pending.push_back(
        Pending_op{
            .object_key = object_key,
            .materials  = std::vector<std::shared_ptr<erhe::primitive::Material>>{materials.begin(), materials.end()},
            .release    = false
        }
    );
}

void Material_set::enqueue_release_object(const uint64_t object_key)
{
    const std::lock_guard<std::mutex> lock{m_pending_mutex};
    m_pending.push_back(Pending_op{.object_key = object_key, .materials = {}, .release = true});
}

void Material_set::flush_pending()
{
    std::vector<Pending_op> pending;
    {
        const std::lock_guard<std::mutex> lock{m_pending_mutex};
        pending.swap(m_pending);
    }
    // In enqueue order: a register / unregister pair for one object means
    // different things in either order.
    for (const Pending_op& op : pending) {
        if (op.release) {
            release_object_materials(op.object_key);
        } else {
            sync_object_materials(op.object_key, op.materials);
        }
    }
}

auto Material_set::get_pending_count() const -> std::size_t
{
    const std::lock_guard<std::mutex> lock{m_pending_mutex};
    return m_pending.size();
}

auto Material_set::find(const erhe::primitive::Material* material) const -> Material_slot_id
{
    if (material == nullptr) {
        return Material_slot_id{};
    }
    const auto i = m_index_by_material.find(material);
    if (i == m_index_by_material.end()) {
        return Material_slot_id{};
    }
    return Material_slot_id{.index = i->second, .generation = m_materials[i->second].generation};
}

auto Material_set::get_slot(const erhe::primitive::Material* material) const -> std::optional<uint32_t>
{
    const Material_slot_id id = find(material);
    if (!id.is_valid()) {
        return {};
    }
    return id.index;
}

auto Material_set::is_valid(const Material_slot_id& id) const -> bool
{
    if (!id.is_valid() || (id.index >= m_materials.size())) {
        return false;
    }
    const Material_slot& slot = m_materials[id.index];
    return slot.alive && (slot.generation == id.generation);
}

auto Material_set::get_material(const Material_slot_id& id) const -> const erhe::primitive::Material*
{
    if (!is_valid(id)) {
        return nullptr;
    }
    return m_materials[id.index].material.get();
}

auto Material_set::get_slot_count() const -> std::size_t
{
    for (std::size_t i = m_materials.size(); i > 0; --i) {
        if (m_materials[i - 1].alive) {
            return i;
        }
    }
    return 0;
}

auto Material_set::get_live_count() const -> std::size_t
{
    return m_index_by_material.size();
}

auto Material_set::get_materials() const -> std::span<const Material_slot>
{
    return std::span<const Material_slot>{m_materials};
}

auto Material_set::is_membership_dirty() const -> bool
{
    return m_membership_dirty;
}

void Material_set::clear_membership_dirty()
{
    m_membership_dirty = false;
}

} // namespace erhe::scene_renderer
