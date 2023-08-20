#pragma once

#include <memory>

namespace erhe
{

class Item_host
{
public:
    virtual ~Item_host() noexcept;

    [[nodiscard]] virtual auto get_host_name() const -> const char* = 0;
};

} // namespace erhe
