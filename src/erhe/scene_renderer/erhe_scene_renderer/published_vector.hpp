#pragma once

#include "erhe_verify/verify.hpp"

#include <atomic>
#include <cstddef>
#include <memory>
#include <utility>
#include <vector>

namespace erhe::scene_renderer {

// Append-only sequence readable without a lock while another thread appends.
//
// Exists for the Buffer_pool containers: workers append pools / pool blocks
// during mesh builds (serialized by buffer_mesh_allocation_mutex()), while the
// main thread reads them with no lock from the render, flush and statistics
// paths. A std::vector there is a data race on the vector's own bookkeeping -
// and a mutex retrofit would be correct only if it enumerated every reader,
// the kind of hand-derived list that drifts (doc/erhe/gl_worker_thread_contexts.md
// Traps: re-derive from code, never trust a written list). This container is
// correct without that enumeration:
//
// - The slot array is allocated once, at construction, to a fixed capacity
//   and never reallocates; each element is heap-owned, so element addresses
//   are stable for the container's lifetime.
// - An append constructs the element, then publishes it with a release store
//   of the count. Readers acquire-load the count and touch only slots below
//   it, so a reader either sees a fully constructed element or does not see
//   it at all.
//
// Contract: appends must be externally serialized (one appender at a time);
// readers need nothing. Elements are never removed. Appending beyond the
// capacity is a loud ERHE_VERIFY failure, on in Release too - capacities are
// chosen where the container is declared, from bounds the owner knows.
template <typename T>
class Published_vector final
{
public:
    explicit Published_vector(const std::size_t capacity)
        : m_slots{capacity}
    {
    }

    Published_vector(const Published_vector&)            = delete;
    Published_vector& operator=(const Published_vector&) = delete;
    Published_vector(Published_vector&&)                 = delete;
    Published_vector& operator=(Published_vector&&)      = delete;

    // Serialized appender only.
    template <typename... Args>
    auto emplace_back(Args&&... args) -> T&
    {
        const std::size_t index = m_count.load(std::memory_order_relaxed);
        ERHE_VERIFY(index < m_slots.size());
        m_slots[index] = std::make_unique<T>(std::forward<Args>(args)...);
        T& element = *m_slots[index];
        m_count.store(index + 1, std::memory_order_release);
        return element;
    }

    [[nodiscard]] auto size() const -> std::size_t
    {
        return m_count.load(std::memory_order_acquire);
    }

    [[nodiscard]] auto at(const std::size_t index) -> T&
    {
        ERHE_VERIFY(index < size());
        return *m_slots[index];
    }

    [[nodiscard]] auto at(const std::size_t index) const -> const T&
    {
        ERHE_VERIFY(index < size());
        return *m_slots[index];
    }

    template <typename U>
    class Iterator final
    {
    public:
        Iterator(const std::unique_ptr<T>* const slot)
            : m_slot{slot}
        {
        }
        [[nodiscard]] auto operator*() const -> U&
        {
            return **m_slot;
        }
        auto operator++() -> Iterator&
        {
            ++m_slot;
            return *this;
        }
        [[nodiscard]] auto operator!=(const Iterator& other) const -> bool
        {
            return m_slot != other.m_slot;
        }

    private:
        const std::unique_ptr<T>* m_slot;
    };

    // begin() / end() pairs snapshot the published count when end() is
    // called; elements appended afterwards are simply not visited.
    [[nodiscard]] auto begin() -> Iterator<T>
    {
        return Iterator<T>{m_slots.data()};
    }
    [[nodiscard]] auto end() -> Iterator<T>
    {
        return Iterator<T>{m_slots.data() + size()};
    }
    [[nodiscard]] auto begin() const -> Iterator<const T>
    {
        return Iterator<const T>{m_slots.data()};
    }
    [[nodiscard]] auto end() const -> Iterator<const T>
    {
        return Iterator<const T>{m_slots.data() + size()};
    }

private:
    // Sized once at construction, never resized; only slots below m_count
    // are ever read.
    std::vector<std::unique_ptr<T>> m_slots;
    std::atomic<std::size_t>        m_count{0};
};

} // namespace erhe::scene_renderer
