#ifndef fragment_outputs_hpp_erhe_graphics
#define fragment_outputs_hpp_erhe_graphics

#include "erhe/graphics/fragment_output.hpp"
#include <vector>

namespace erhe::graphics
{

class Shader_stages;

class Fragment_outputs
{
public:
    void clear();

    void add(const std::string&              name,
             gl::Fragment_shader_output_type type,
             unsigned int                    location);

    auto source() const
    -> std::string;

private:
    std::vector<Fragment_output> m_outputs;
};

} // namespace erhe::graphics

#endif
