#pragma once

#include <array>
#include <cstddef>
#include <memory>
#include <mutex>

namespace erhe::utility {

// What a share of a Memory_budget pays for, for the statistics.
enum class Memory_budget_kind : unsigned int {
    mesh  = 0, // vertex / index bytes waiting in the mesh loader queue
    image = 1, // decoded / encoded image bytes waiting for their upload
    count = 2
};

class Memory_budget;

// A share of a Memory_budget: holds `byte_count` bytes of it until it is
// destroyed or reset(). Move-only. An empty lease holds nothing.
class Memory_budget_lease
{
public:
    Memory_budget_lease() = default;
    ~Memory_budget_lease() noexcept;
    Memory_budget_lease(const Memory_budget_lease&) = delete;
    auto operator=(const Memory_budget_lease&) -> Memory_budget_lease& = delete;
    Memory_budget_lease(Memory_budget_lease&& other) noexcept;
    auto operator=(Memory_budget_lease&& other) noexcept -> Memory_budget_lease&;

    void reset();
    [[nodiscard]] auto is_empty     () const -> bool                { return m_budget == nullptr; }
    [[nodiscard]] auto get_byte_count() const -> std::size_t        { return m_byte_count; }
    [[nodiscard]] auto get_kind     () const -> Memory_budget_kind  { return m_kind; }

private:
    friend class Memory_budget;
    Memory_budget_lease(std::shared_ptr<Memory_budget> budget, std::size_t byte_count, Memory_budget_kind kind);

    std::shared_ptr<Memory_budget> m_budget;
    std::size_t                    m_byte_count{0};
    Memory_budget_kind             m_kind      {Memory_budget_kind::mesh};
};

// A byte budget shared by the producers of CPU memory that waits for a GPU
// upload: a producer takes a lease before it makes the bytes and the lease
// is dropped when the bytes have been copied into upload staging. The bytes
// leased at once stay within the capacity - except one request larger than
// the whole capacity, which is granted while nothing else is leased, so
// such an item still makes progress, alone. Thread-safe. Created through
// std::make_shared: a lease keeps its budget alive.
class Memory_budget : public std::enable_shared_from_this<Memory_budget>
{
public:
    explicit Memory_budget(std::size_t capacity_byte_count);

    // A lease of byte_count bytes, or an empty lease when they do not fit now.
    [[nodiscard]] auto try_acquire(std::size_t byte_count, Memory_budget_kind kind) -> Memory_budget_lease;
    // Whether try_acquire(byte_count) would succeed now (no reservation).
    [[nodiscard]] auto would_fit  (std::size_t byte_count) const -> bool;

    // A new capacity applies to requests from now on; leases already held stay.
    void set_capacity(std::size_t capacity_byte_count);

    class Statistics
    {
    public:
        std::size_t                                                       capacity_byte_count{0};
        std::size_t                                                       in_use_byte_count  {0};
        std::size_t                                                       high_water_byte_count{0};
        std::size_t                                                       oversize_grant_count{0}; // grants larger than the capacity
        std::size_t                                                       refused_count       {0};
        std::array<std::size_t, static_cast<std::size_t>(Memory_budget_kind::count)> in_use_by_kind    {};
        std::array<std::size_t, static_cast<std::size_t>(Memory_budget_kind::count)> high_water_by_kind{};
        std::array<std::size_t, static_cast<std::size_t>(Memory_budget_kind::count)> lease_count_by_kind{}; // leases granted
    };
    [[nodiscard]] auto get_statistics() const -> Statistics;
    // Resets the high-water marks and counters (for a per-load report).
    void reset_statistics();

private:
    friend class Memory_budget_lease;
    void release(std::size_t byte_count, Memory_budget_kind kind);

    mutable std::mutex m_mutex;
    Statistics         m_statistics;
};

} // namespace erhe::utility
