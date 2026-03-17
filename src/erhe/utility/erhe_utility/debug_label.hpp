#pragma once

#include <cstddef>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_set>

namespace erhe::utility {

class String_pool
{
public:
    static String_pool& instance()
    {
        static String_pool pool;
        return pool;
    }

    [[nodiscard]] auto intern(std::string_view sv) -> std::string_view
    {
        std::scoped_lock lock{m_mutex};
        auto it = m_storage.find(sv);
        if (it == m_storage.end()) {
            it = m_storage.emplace(sv).first;
        }
        return *it;          // string implicitly converts to string_view
    }

    [[nodiscard]] auto intern(std::string&& s) -> std::string_view
    {
        std::scoped_lock lock{m_mutex};
        auto it = m_storage.find(std::string_view{s});
        if (it != m_storage.end()) {
            return *it;
        }
        return *m_storage.insert(std::move(s)).first;
    }

private:
    String_pool() = default;

    struct Hash
    {
        using is_transparent = void;
        [[nodiscard]] auto operator()(std::string_view sv) const noexcept -> std::size_t
        {
            return std::hash<std::string_view>{}(sv);
        }
        [[nodiscard]] auto operator()(const std::string& s) const noexcept -> std::size_t
        {
            return std::hash<std::string_view>{}(s);
        }
    };

    struct Equal
    {
        using is_transparent = void;
        bool operator()(std::string_view a, std::string_view b) const noexcept
        {
            return a == b;
        }
    };

    std::mutex m_mutex;
    std::unordered_set<std::string, Hash, Equal> m_storage;
};

class Debug_label
{
public:
    Debug_label() = default;
    ~Debug_label() noexcept = default;
    explicit Debug_label(std::string&& s)
        : m_string_view{String_pool::instance().intern(std::move(s))}
    {
    }

    explicit Debug_label(std::string_view sv)
        : m_string_view{String_pool::instance().intern(sv)}
    {
    }

    template<std::size_t N>
    constexpr Debug_label(const char (&s)[N]) noexcept
        : m_string_view{s, N - 1}
    {
    }

    [[nodiscard]] constexpr auto string_view() const noexcept -> std::string_view
    {
        return m_string_view;
    }

    [[nodiscard]] constexpr auto empty() const noexcept -> bool { return m_string_view.empty(); }
    [[nodiscard]] constexpr auto data()  const noexcept -> const char* { return m_string_view.data(); }
    [[nodiscard]] constexpr auto size()  const noexcept -> std::size_t { return m_string_view.size(); }

private:
    std::string_view m_string_view;
};

} // namespace erhe::utility
