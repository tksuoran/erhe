#include "erhe_graphics/fragment_outputs.hpp"

#include <sstream>

namespace erhe::graphics {

Fragment_outputs::Fragment_outputs(std::initializer_list<Fragment_output> outputs)
    : m_outputs{outputs}
{
}

auto Fragment_outputs::source() const -> std::string
{
    std::stringstream ss;

    for (const auto& output : m_outputs) {
        ss << "layout(location = " << output.location << ") ";
        ss << "out ";
        ss << glsl_type_c_str(output.type) << " ";
        ss << output.name << ";\n";
    }

    return ss.str();
}

} // namespace erhe::graphics
