// erhe::utility::Memory_budget: leases within the capacity, one oversize
// request granted alone, release on lease destruction / move, statistics.

#include "erhe_utility/memory_budget.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <utility>

namespace {

using erhe::utility::Memory_budget;
using erhe::utility::Memory_budget_kind;
using erhe::utility::Memory_budget_lease;

constexpr std::size_t mesh  = static_cast<std::size_t>(Memory_budget_kind::mesh);
constexpr std::size_t image = static_cast<std::size_t>(Memory_budget_kind::image);

TEST(Memory_budget, leases_stay_within_capacity)
{
    const std::shared_ptr<Memory_budget> budget = std::make_shared<Memory_budget>(100);
    Memory_budget_lease a = budget->try_acquire(60, Memory_budget_kind::mesh);
    ASSERT_FALSE(a.is_empty());
    EXPECT_TRUE (budget->would_fit(40));
    EXPECT_FALSE(budget->would_fit(41));
    Memory_budget_lease refused = budget->try_acquire(41, Memory_budget_kind::image);
    EXPECT_TRUE(refused.is_empty());
    Memory_budget_lease b = budget->try_acquire(40, Memory_budget_kind::image);
    ASSERT_FALSE(b.is_empty());

    const Memory_budget::Statistics full = budget->get_statistics();
    EXPECT_EQ(full.in_use_byte_count, 100u);
    EXPECT_EQ(full.in_use_by_kind[mesh], 60u);
    EXPECT_EQ(full.in_use_by_kind[image], 40u);
    EXPECT_EQ(full.refused_count, 1u);

    a.reset();
    EXPECT_EQ(budget->get_statistics().in_use_byte_count, 40u);
    {
        Memory_budget_lease moved = std::move(b);
        EXPECT_TRUE(b.is_empty()); // NOLINT(bugprone-use-after-move): a moved-from lease is empty by contract
        EXPECT_EQ(budget->get_statistics().in_use_byte_count, 40u);
    }
    const Memory_budget::Statistics empty = budget->get_statistics();
    EXPECT_EQ(empty.in_use_byte_count, 0u);
    EXPECT_EQ(empty.high_water_byte_count, 100u);
    EXPECT_EQ(empty.lease_count_by_kind[mesh], 1u);
    EXPECT_EQ(empty.lease_count_by_kind[image], 1u);
}

TEST(Memory_budget, oversize_request_is_granted_alone)
{
    const std::shared_ptr<Memory_budget> budget = std::make_shared<Memory_budget>(100);
    Memory_budget_lease small = budget->try_acquire(1, Memory_budget_kind::mesh);
    EXPECT_TRUE(budget->try_acquire(500, Memory_budget_kind::image).is_empty());
    small.reset();
    Memory_budget_lease big = budget->try_acquire(500, Memory_budget_kind::image);
    ASSERT_FALSE(big.is_empty());
    EXPECT_EQ(budget->get_statistics().oversize_grant_count, 1u);
    // Nothing else while the oversize lease is held.
    EXPECT_TRUE(budget->try_acquire(1, Memory_budget_kind::mesh).is_empty());
}

TEST(Memory_budget, lease_keeps_budget_alive_and_reset_statistics)
{
    Memory_budget_lease lease;
    {
        const std::shared_ptr<Memory_budget> budget = std::make_shared<Memory_budget>(10);
        lease = budget->try_acquire(10, Memory_budget_kind::mesh);
        budget->reset_statistics();
        const Memory_budget::Statistics statistics = budget->get_statistics();
        EXPECT_EQ(statistics.high_water_byte_count, 10u); // reset to what is in use
        EXPECT_EQ(statistics.lease_count_by_kind[mesh], 0u);
    }
    EXPECT_FALSE(lease.is_empty());
    lease.reset(); // the budget dies with its last lease
    EXPECT_TRUE(lease.is_empty());
}

} // anonymous namespace
