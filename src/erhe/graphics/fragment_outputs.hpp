#pragma once

#include "erhe/graphics/fragment_output.hpp"

#include <vector>

namespace erhe::graphics
{

class Fragment_outputs
{
public:
    explicit Fragment_outputs(const std::initializer_list<Fragment_output> outputs);

    [[nodiscard]] auto source() const -> std::string;

private:
    std::vector<Fragment_output> m_outputs;
};

} // namespace erhe::graphics
