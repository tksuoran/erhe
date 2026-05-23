#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace erhe::utility {

// Owning label for debug-only labelling (Vulkan / GL / Metal scoped
// debug groups, named buffers, pipeline labels, etc.). Holds its own
// std::string so the contents survive the call site that constructed
// it. Earlier versions interned every label in a global String_pool
// for "cheap" pointer-equal sharing, but the pool grew without bound
// (one entry per Draw_list_renderer MDI span over a long session)
// and the RAII scope guards already provide cheap lifetimes, so the
// pool was solving a non-problem.
class Debug_label
{
public:
    Debug_label() = default;
    ~Debug_label() noexcept = default;

    explicit Debug_label(std::string s)
        : m_string{std::move(s)}
    {
    }

    explicit Debug_label(std::string_view sv)
        : m_string{sv}
    {
    }

    Debug_label(const char* s)
        : m_string{(s != nullptr) ? s : ""}
    {
    }

    template<std::size_t N>
    Debug_label(const char (&s)[N])
        : m_string{s, N - 1}
    {
    }

    [[nodiscard]] auto string_view() const noexcept -> std::string_view { return m_string; }
    [[nodiscard]] auto empty()       const noexcept -> bool             { return m_string.empty(); }
    [[nodiscard]] auto data()        const noexcept -> const char*      { return m_string.c_str(); }
    [[nodiscard]] auto size()        const noexcept -> std::size_t      { return m_string.size(); }

private:
    std::string m_string{};
};

} // namespace erhe::utility
