#include "erhe_utility/memory_budget.hpp"

#include "erhe_verify/verify.hpp"

#include <algorithm>
#include <utility>

namespace erhe::utility {

Memory_budget_lease::Memory_budget_lease(std::shared_ptr<Memory_budget> budget, const std::size_t byte_count, const Memory_budget_kind kind)
    : m_budget    {std::move(budget)}
    , m_byte_count{byte_count}
    , m_kind      {kind}
{
}

Memory_budget_lease::~Memory_budget_lease() noexcept
{
    reset();
}

Memory_budget_lease::Memory_budget_lease(Memory_budget_lease&& other) noexcept
    : m_budget    {std::move(other.m_budget)}
    , m_byte_count{other.m_byte_count}
    , m_kind      {other.m_kind}
{
    other.m_budget.reset();
    other.m_byte_count = 0;
}

auto Memory_budget_lease::operator=(Memory_budget_lease&& other) noexcept -> Memory_budget_lease&
{
    if (this != &other) {
        reset();
        m_budget     = std::move(other.m_budget);
        m_byte_count = other.m_byte_count;
        m_kind       = other.m_kind;
        other.m_budget.reset();
        other.m_byte_count = 0;
    }
    return *this;
}

void Memory_budget_lease::reset()
{
    if (m_budget) {
        m_budget->release(m_byte_count, m_kind);
        m_budget.reset();
        m_byte_count = 0;
    }
}

Memory_budget::Memory_budget(const std::size_t capacity_byte_count)
{
    m_statistics.capacity_byte_count = std::max<std::size_t>(1, capacity_byte_count);
}

auto Memory_budget::would_fit(const std::size_t byte_count) const -> bool
{
    const std::lock_guard<std::mutex> lock{m_mutex};
    return
        (m_statistics.in_use_byte_count == 0) ||
        ((m_statistics.in_use_byte_count + byte_count) <= m_statistics.capacity_byte_count);
}

auto Memory_budget::try_acquire(const std::size_t byte_count, const Memory_budget_kind kind) -> Memory_budget_lease
{
    const std::size_t kind_index = static_cast<std::size_t>(kind);
    ERHE_VERIFY(kind_index < static_cast<std::size_t>(Memory_budget_kind::count));
    {
        const std::lock_guard<std::mutex> lock{m_mutex};
        Statistics& s = m_statistics;
        const bool fits = (s.in_use_byte_count == 0) || ((s.in_use_byte_count + byte_count) <= s.capacity_byte_count);
        if (!fits) {
            ++s.refused_count;
            return Memory_budget_lease{};
        }
        if (byte_count > s.capacity_byte_count) {
            ++s.oversize_grant_count;
        }
        s.in_use_byte_count += byte_count;
        s.high_water_byte_count = std::max(s.high_water_byte_count, s.in_use_byte_count);
        s.in_use_by_kind[kind_index] += byte_count;
        s.high_water_by_kind[kind_index] = std::max(s.high_water_by_kind[kind_index], s.in_use_by_kind[kind_index]);
        ++s.lease_count_by_kind[kind_index];
    }
    return Memory_budget_lease{shared_from_this(), byte_count, kind};
}

void Memory_budget::release(const std::size_t byte_count, const Memory_budget_kind kind)
{
    const std::size_t kind_index = static_cast<std::size_t>(kind);
    const std::lock_guard<std::mutex> lock{m_mutex};
    ERHE_VERIFY(m_statistics.in_use_byte_count >= byte_count);
    ERHE_VERIFY(m_statistics.in_use_by_kind[kind_index] >= byte_count);
    m_statistics.in_use_byte_count -= byte_count;
    m_statistics.in_use_by_kind[kind_index] -= byte_count;
}

void Memory_budget::set_capacity(const std::size_t capacity_byte_count)
{
    const std::lock_guard<std::mutex> lock{m_mutex};
    m_statistics.capacity_byte_count = std::max<std::size_t>(1, capacity_byte_count);
}

auto Memory_budget::get_statistics() const -> Statistics
{
    const std::lock_guard<std::mutex> lock{m_mutex};
    return m_statistics;
}

void Memory_budget::reset_statistics()
{
    const std::lock_guard<std::mutex> lock{m_mutex};
    Statistics& s = m_statistics;
    s.high_water_byte_count = s.in_use_byte_count;
    s.high_water_by_kind    = s.in_use_by_kind;
    s.oversize_grant_count  = 0;
    s.refused_count         = 0;
    s.lease_count_by_kind   = {};
}

} // namespace erhe::utility
