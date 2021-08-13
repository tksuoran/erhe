#include "erhe/graphics/fragment_output.hpp"

#include <utility>  // for move

namespace erhe::graphics
{

Fragment_output::Fragment_output(std::string                           name,
                                 const gl::Fragment_shader_output_type type,
                                 const unsigned int                    location)
    : m_name    {std::move(name)}
    , m_type    {type}
    , m_location{location}
{
}

auto Fragment_output::name() const
-> const std::string&
{
    return m_name;
}

auto Fragment_output::type() const
-> gl::Fragment_shader_output_type
{
    return m_type;
}

auto Fragment_output::location() const
-> unsigned int
{
    return m_location;
}

} // namespace erhe::graphics
