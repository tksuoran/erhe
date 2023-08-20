#pragma once

#include <atomic>
#include <cstddef>

namespace erhe
{

template <class T>
class Unique_id
{
public:
    using id_type = std::size_t;

    static constexpr id_type null_id{0};

    constexpr Unique_id()
        : m_id{allocate_id()}
    {
    }

    Unique_id(const Unique_id&) = delete;

    Unique_id(Unique_id&& other) noexcept
        : m_id{other.get_id()}
    {
        other.reset();
    }

    auto operator=(Unique_id&& other) noexcept -> Unique_id&
    {
        m_id = other.get_id();
        other.reset();
        return *this;
    }

    ~Unique_id() noexcept = default;

    void reset()
    {
        m_id = null_id;
    }

    [[nodiscard]] auto get_id() const -> id_type
    {
        return m_id;
    }

private:
    [[nodiscard]] static auto allocate_id() -> id_type
    {
        static std::atomic<id_type> counter{1};

        return counter++;
    }

    id_type m_id{null_id};
};

}
