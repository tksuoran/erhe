#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>
#include <mutex>

namespace erhe::utility {

class Debug_label
{
public:
    Debug_label() = default;

    explicit Debug_label(std::string&& s);

    explicit Debug_label(std::string_view s);

    template<std::size_t N>
    constexpr Debug_label(const char(&s)[N])
        : m_string_view{s, N - 1}
    {
    }

    [[nodiscard]] auto data() const -> const char*
    {
        return m_string_view.empty() ? "" : m_string_view.data();
    }

    [[nodiscard]] auto size() const -> std::size_t
    {
        return m_string_view.size();
    }

    [[nodiscard]] auto string_view() const -> std::string_view
    {
        return m_string_view;
    }

    [[nodiscard]] auto empty() const -> bool
    {
        return m_string_view.empty();
    }

private:
    std::string_view m_string_view;

    static std::mutex               s_mutex;
    static std::vector<std::string> s_string_pool;
};


} // namespace erhe::utility
