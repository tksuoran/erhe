#pragma once

#include "erhe/gl/wrapper_enums.hpp"

#include <string>

namespace erhe::graphics
{

class Fragment_output
{
public:
    Fragment_output(std::string                     name,
                    gl::Fragment_shader_output_type type,
                    unsigned int                    location);

    auto name() const
    -> const std::string&;

    auto type() const
    -> gl::Fragment_shader_output_type;

    auto location() const
    -> unsigned int;

private:
    std::string                     m_name;
    gl::Fragment_shader_output_type m_type{gl::Fragment_shader_output_type::float_vec4};
    unsigned int                    m_location;
};

} // namespace erhe::graphics
