#pragma once

#include <string_view>

namespace erhe::graphics
{

class Scoped_debug_group final
{
public:
    Scoped_debug_group(const std::string_view debug_label);
    ~Scoped_debug_group();

private:
};

} // namespace erhe::graphics
