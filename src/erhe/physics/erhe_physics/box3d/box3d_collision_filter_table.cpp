#include "erhe_physics/box3d/box3d_collision_filter_table.hpp"
#include "erhe_physics/collision_filter.hpp"
#include "erhe_physics/physics_log.hpp"

namespace erhe::physics {

auto Box3d_collision_filter_table::intern_system(const std::string& name) -> int
{
    for (std::size_t i = 0, end = m_system_names.size(); i < end; ++i) {
        if (m_system_names[i] == name) {
            return static_cast<int>(i);
        }
    }
    if (m_system_names.size() >= 64) {
        log_physics->error("collision system limit (64) exceeded; system '{}' ignored", name);
        return -1;
    }
    m_system_names.push_back(name);
    return static_cast<int>(m_system_names.size() - 1);
}

auto Box3d_collision_filter_table::make_bitset(const std::vector<std::string>& names) -> uint64_t
{
    uint64_t bits = 0;
    for (const std::string& name : names) {
        const int index = intern_system(name);
        if (index >= 0) {
            bits = bits | (uint64_t{1} << static_cast<unsigned int>(index));
        }
    }
    return bits;
}

auto Box3d_collision_filter_table::compile(const Collision_filter& filter) -> Compiled_collision_filter
{
    Compiled_collision_filter compiled{};
    compiled.membership    = make_bitset(filter.get_collision_systems());
    compiled.is_allow_list = !filter.get_collide_with_systems().empty();
    compiled.mask          = compiled.is_allow_list
        ? make_bitset(filter.get_collide_with_systems())
        : make_bitset(filter.get_not_collide_with_systems());
    return compiled;
}

auto Box3d_collision_filter_table::get_or_compile(const std::shared_ptr<Collision_filter>& filter) -> int
{
    if (!filter) {
        return no_collision_filter;
    }
    const auto existing = m_filter_to_compiled.find(filter.get());
    if (existing != m_filter_to_compiled.end()) {
        // A stale entry means the filter that occupied this address has been
        // destroyed and a different one now lives there; recompile in place.
        if (!existing->second.filter.expired()) {
            return existing->second.index;
        }
        m_compiled_filters[static_cast<std::size_t>(existing->second.index)] = compile(*filter);
        existing->second.filter = filter;
        return existing->second.index;
    }
    const int index = static_cast<int>(m_compiled_filters.size());
    m_compiled_filters.push_back(compile(*filter));
    m_filter_to_compiled.emplace(filter.get(), Filter_cache_entry{index, filter});
    return index;
}

auto Box3d_collision_filter_table::recompile(const std::shared_ptr<Collision_filter>& filter) -> int
{
    if (!filter) {
        return no_collision_filter;
    }
    const auto existing = m_filter_to_compiled.find(filter.get());
    if (existing == m_filter_to_compiled.end()) {
        return get_or_compile(filter);
    }
    m_compiled_filters[static_cast<std::size_t>(existing->second.index)] = compile(*filter);
    existing->second.filter = filter;
    return existing->second.index;
}

auto Box3d_collision_filter_table::accepts(const Compiled_collision_filter& filter, const uint64_t other_membership) -> bool
{
    return filter.is_allow_list
        ? ((filter.mask & other_membership) != 0)
        : ((filter.mask & other_membership) == 0);
}

auto Box3d_collision_filter_table::should_collide(const int filter_index_a, const int filter_index_b) const -> bool
{
    const bool has_a = (filter_index_a != no_collision_filter);
    const bool has_b = (filter_index_b != no_collision_filter);
    if (!has_a && !has_b) {
        return true;
    }

    // A body with no filter belongs to no collision system, so its membership
    // bitset is zero. That matches the Jolt backend: an allow-list filter then
    // rejects it (it is in none of the allowed systems) and a deny-list filter
    // accepts it (it is in none of the denied systems).
    const uint64_t membership_a = has_a ? m_compiled_filters[static_cast<std::size_t>(filter_index_a)].membership : uint64_t{0};
    const uint64_t membership_b = has_b ? m_compiled_filters[static_cast<std::size_t>(filter_index_b)].membership : uint64_t{0};

    if (has_a && !accepts(m_compiled_filters[static_cast<std::size_t>(filter_index_a)], membership_b)) {
        return false;
    }
    if (has_b && !accepts(m_compiled_filters[static_cast<std::size_t>(filter_index_b)], membership_a)) {
        return false;
    }
    return true;
}

auto Box3d_collision_filter_table::get_compiled_filter(const int filter_index) const -> const Compiled_collision_filter&
{
    return m_compiled_filters[static_cast<std::size_t>(filter_index)];
}

} // namespace erhe::physics
