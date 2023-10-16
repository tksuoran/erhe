#pragma once

#include <string>
#include <string_view>

namespace erhe::graphics
{

class Scoped_debug_group final
{
public:
    explicit Scoped_debug_group(const std::string_view debug_label);
    ~Scoped_debug_group() noexcept;

private:
    std::string m_debug_label;
};

} // namespace erhe::graphics
