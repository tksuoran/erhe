#include "erhe_utility/debug_label.hpp"

namespace erhe::utility {

std::mutex               Debug_label::s_mutex{};
std::vector<std::string> Debug_label::s_string_pool{};

Debug_label::Debug_label(std::string&& s)
{
    std::scoped_lock<std::mutex> lock{s_mutex};
    for (const auto& pooled_string : s_string_pool) {
        if (pooled_string == s) {
            m_string_view = std::string_view{pooled_string};
            return;
        }
    }
    s_string_pool.push_back(std::move(s));
    m_string_view = s_string_pool.back();
}

Debug_label::Debug_label(std::string_view s)
{
    std::scoped_lock<std::mutex> lock{s_mutex};
    for (const auto& pooled_string : s_string_pool) {
        if (pooled_string == s) {
            m_string_view = std::string_view{pooled_string};
            return;
        }
    }
    s_string_pool.emplace_back(s);
    m_string_view = s_string_pool.back();
}

} // namespace
