#include "level/foliage_index.hpp"

#include "erhe_verify/verify.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace editor {

namespace {

constexpr std::uint64_t c_cell_bits = 21;
constexpr std::uint64_t c_cell_mask = (std::uint64_t{1} << c_cell_bits) - 1;

[[nodiscard]] auto pack_cell_key(const int x, const int y, const int z) -> std::uint64_t
{
    // Offset by 2^20 so the 21-bit fields are unsigned.
    const std::uint64_t ux = static_cast<std::uint64_t>(x - Foliage_index::cell_coordinate_min) & c_cell_mask;
    const std::uint64_t uy = static_cast<std::uint64_t>(y - Foliage_index::cell_coordinate_min) & c_cell_mask;
    const std::uint64_t uz = static_cast<std::uint64_t>(z - Foliage_index::cell_coordinate_min) & c_cell_mask;
    return (ux << (2 * c_cell_bits)) | (uy << c_cell_bits) | uz;
}

[[nodiscard]] auto is_finite(const glm::vec3 v) -> bool
{
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

// Clamps infinities too; value is never NaN here.
[[nodiscard]] auto clamped_cell_coordinate(const float value) -> int
{
    const float cell = std::floor(value / Foliage_index::cell_size);
    if (cell <= static_cast<float>(Foliage_index::cell_coordinate_min)) {
        return Foliage_index::cell_coordinate_min;
    }
    if (cell >= static_cast<float>(Foliage_index::cell_coordinate_max)) {
        return Foliage_index::cell_coordinate_max;
    }
    return static_cast<int>(cell);
}

class Cell_box
{
public:
    glm::ivec3 low;
    glm::ivec3 high;
};

// The cells of every point the squared-distance test can accept: the margin covers its float rounding.
[[nodiscard]] auto get_query_cell_box(const glm::vec3 center, const float distance) -> Cell_box
{
    const float widened = distance + (distance * (1.0f / 1048576.0f));
    Cell_box box{};
    for (int axis = 0; axis < 3; ++axis) {
        const float low  = std::nextafter(center[axis] - widened, -std::numeric_limits<float>::infinity());
        const float high = std::nextafter(center[axis] + widened,  std::numeric_limits<float>::infinity());
        box.low [axis] = clamped_cell_coordinate(low);
        box.high[axis] = clamped_cell_coordinate(high);
    }
    return box;
}

} // anonymous namespace

auto Foliage_index::get_cell_coordinate(const float value) -> int
{
    ERHE_VERIFY(std::isfinite(value));
    return clamped_cell_coordinate(value);
}

auto Foliage_index::get_cell_key(const glm::vec3 position) -> std::uint64_t
{
    return pack_cell_key(
        get_cell_coordinate(position.x),
        get_cell_coordinate(position.y),
        get_cell_coordinate(position.z)
    );
}

auto Foliage_index::get_slot(const Handle handle) -> Slot&
{
    ERHE_VERIFY(handle < m_slots.size());
    Slot& slot = m_slots[handle];
    ERHE_VERIFY(slot.in_use);
    return slot;
}

auto Foliage_index::get_slot(const Handle handle) const -> const Slot&
{
    ERHE_VERIFY(handle < m_slots.size());
    const Slot& slot = m_slots[handle];
    ERHE_VERIFY(slot.in_use);
    return slot;
}

void Foliage_index::link(const std::uint32_t slot_index)
{
    Slot& slot = m_slots[slot_index];
    // try_emplace allocates only for a cell used for the first time.
    std::uint32_t& head = m_cell_heads.try_emplace(slot.cell_key, no_slot).first->second;
    slot.previous = no_slot;
    slot.next     = head;
    if (head != no_slot) {
        m_slots[head].previous = slot_index;
    }
    head = slot_index;
}

void Foliage_index::unlink(const std::uint32_t slot_index)
{
    Slot& slot = m_slots[slot_index];
    if (slot.previous != no_slot) {
        m_slots[slot.previous].next = slot.next;
    } else {
        const std::unordered_map<std::uint64_t, std::uint32_t>::iterator i = m_cell_heads.find(slot.cell_key);
        ERHE_VERIFY(i != m_cell_heads.end());
        ERHE_VERIFY(i->second == slot_index);
        i->second = slot.next;
    }
    if (slot.next != no_slot) {
        m_slots[slot.next].previous = slot.previous;
    }
    slot.previous = no_slot;
    slot.next     = no_slot;
}

auto Foliage_index::insert(const Foliage_index_entry& entry) -> Handle
{
    ERHE_VERIFY(is_finite(entry.position));
    ERHE_VERIFY(std::isfinite(entry.radius) && (entry.radius >= 0.0f));

    std::uint32_t slot_index = no_slot;
    if (m_free_head != no_slot) {
        slot_index  = m_free_head;
        m_free_head = m_slots[slot_index].next;
    } else {
        ERHE_VERIFY(m_slots.size() < static_cast<std::size_t>(no_slot));
        slot_index = static_cast<std::uint32_t>(m_slots.size());
        m_slots.emplace_back();
    }
    Slot& slot = m_slots[slot_index];
    slot.entry    = entry;
    slot.cell_key = get_cell_key(entry.position);
    slot.in_use   = true;
    link(slot_index);
    ++m_count;
    if (!m_max_radius_stale) {
        m_max_radius = std::max(m_max_radius, entry.radius);
    }
    return slot_index;
}

void Foliage_index::remove(const Handle handle)
{
    Slot& slot = get_slot(handle);
    const float radius = slot.entry.radius;
    unlink(handle);
    slot.in_use = false;
    slot.entry  = Foliage_index_entry{};
    slot.next   = m_free_head;
    m_free_head = handle;
    --m_count;
    if (m_count == 0) {
        m_max_radius       = 0.0f;
        m_max_radius_stale = false;
    } else if (radius >= m_max_radius) {
        m_max_radius_stale = true;
    }
}

void Foliage_index::move(const Handle handle, const glm::vec3 position)
{
    ERHE_VERIFY(is_finite(position));
    Slot& slot = get_slot(handle);
    slot.entry.position = position;
    const std::uint64_t cell_key = get_cell_key(position);
    if (cell_key == slot.cell_key) {
        return;
    }
    unlink(handle);
    slot.cell_key = cell_key;
    link(handle);
}

void Foliage_index::set_radius(const Handle handle, const float radius)
{
    ERHE_VERIFY(std::isfinite(radius) && (radius >= 0.0f));
    Slot& slot = get_slot(handle);
    const float old_radius = slot.entry.radius;
    slot.entry.radius = radius;
    if (m_max_radius_stale) {
        return;
    }
    if (radius >= m_max_radius) {
        m_max_radius = radius;
    } else if (old_radius >= m_max_radius) {
        m_max_radius_stale = true;
    }
}

void Foliage_index::clear()
{
    m_slots.clear();
    // The cells stay, emptied, so a refill allocates no map nodes for them.
    for (std::pair<const std::uint64_t, std::uint32_t>& cell : m_cell_heads) {
        cell.second = no_slot;
    }
    m_free_head        = no_slot;
    m_count            = 0;
    m_max_radius       = 0.0f;
    m_max_radius_stale = false;
}

auto Foliage_index::get_entry(const Handle handle) const -> const Foliage_index_entry&
{
    return get_slot(handle).entry;
}

auto Foliage_index::get_count() const -> std::size_t
{
    return m_count;
}

auto Foliage_index::get_max_radius() const -> float
{
    if (m_max_radius_stale) {
        float max_radius = 0.0f;
        for (const Slot& slot : m_slots) {
            if (slot.in_use) {
                max_radius = std::max(max_radius, slot.entry.radius);
            }
        }
        m_max_radius       = max_radius;
        m_max_radius_stale = false;
    }
    return m_max_radius;
}

auto Foliage_index::get_query_path(const glm::vec3 center, const float distance) const -> Foliage_query_path
{
    ERHE_VERIFY(is_finite(center));
    ERHE_VERIFY(std::isfinite(distance) && (distance >= 0.0f));
    const Cell_box box = get_query_cell_box(center, distance);
    const std::uint64_t count_x = static_cast<std::uint64_t>(box.high.x - box.low.x) + 1;
    const std::uint64_t count_y = static_cast<std::uint64_t>(box.high.y - box.low.y) + 1;
    const std::uint64_t count_z = static_cast<std::uint64_t>(box.high.z - box.low.z) + 1;
    const std::uint64_t cell_count = (count_x * count_y) * count_z; // at most 2^63
    return (cell_count < static_cast<std::uint64_t>(m_count)) ? Foliage_query_path::cell_walk : Foliage_query_path::dense_scan;
}

template <typename Visit>
void Foliage_index::visit_within(const glm::vec3 center, const float distance, Visit&& visit) const
{
    ERHE_VERIFY(is_finite(center));
    ERHE_VERIFY(std::isfinite(distance));
    if ((distance < 0.0f) || (m_count == 0)) {
        return;
    }
    const float distance_squared = distance * distance;
    const auto  test = [&](const std::uint32_t slot_index, const Slot& slot) {
        const glm::vec3 d = slot.entry.position - center;
        if (glm::dot(d, d) <= distance_squared) {
            visit(slot_index);
        }
    };

    if (get_query_path(center, distance) == Foliage_query_path::dense_scan) {
        const std::uint32_t slot_count = static_cast<std::uint32_t>(m_slots.size());
        for (std::uint32_t slot_index = 0; slot_index < slot_count; ++slot_index) {
            const Slot& slot = m_slots[slot_index];
            if (slot.in_use) {
                test(slot_index, slot);
            }
        }
        return;
    }

    const Cell_box box = get_query_cell_box(center, distance);
    for (int x = box.low.x; x <= box.high.x; ++x) {
        for (int y = box.low.y; y <= box.high.y; ++y) {
            for (int z = box.low.z; z <= box.high.z; ++z) {
                const std::unordered_map<std::uint64_t, std::uint32_t>::const_iterator i = m_cell_heads.find(pack_cell_key(x, y, z));
                if (i == m_cell_heads.end()) {
                    continue;
                }
                for (std::uint32_t slot_index = i->second; slot_index != no_slot; slot_index = m_slots[slot_index].next) {
                    test(slot_index, m_slots[slot_index]);
                }
            }
        }
    }
}

void Foliage_index::find_within(const glm::vec3 center, const float distance, std::vector<Handle>& out_handles) const
{
    out_handles.clear();
    visit_within(center, distance, [&out_handles](const std::uint32_t slot_index) { out_handles.push_back(slot_index); });
}

auto Foliage_index::count_within(const glm::vec3 center, const float distance) const -> std::size_t
{
    std::size_t count = 0;
    visit_within(center, distance, [&count](const std::uint32_t) { ++count; });
    return count;
}

} // namespace editor
