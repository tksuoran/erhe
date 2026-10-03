#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <string_view>

namespace erhe::graphics {

// Null-terminated stack copy of a debug label for the backend label calls
// (vkCmdBeginDebugUtilsLabelEXT, glPushDebugGroup, pushDebugGroup), so a
// debug scope needs no heap copy of its label. Longer labels are truncated.
class Debug_label_buffer
{
public:
    static constexpr std::size_t c_capacity = 256;

    explicit Debug_label_buffer(const std::string_view label)
        : m_size{std::min(label.size(), c_capacity - 1)}
    {
        std::copy_n(label.data(), m_size, m_chars.data());
        m_chars[m_size] = '\0';
    }

    [[nodiscard]] auto c_str() const -> const char*      { return m_chars.data(); }
    [[nodiscard]] auto size () const -> std::size_t      { return m_size; }
    [[nodiscard]] auto view () const -> std::string_view { return std::string_view{m_chars.data(), m_size}; }

private:
    std::array<char, c_capacity> m_chars{};
    std::size_t                  m_size{0};
};

} // namespace erhe::graphics
