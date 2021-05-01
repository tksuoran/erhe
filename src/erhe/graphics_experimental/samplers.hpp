#error
#ifndef samplers_hpp_erhe_graphics
#define samplers_hpp_erhe_graphics

#include "erhe/graphics/shader_resource.hpp"

namespace erhe::graphics
{

class Sampler;

class Samplers
{
public:
    using Uniform_collection = std::unordered_map<std::string, Shader_resource>;

    auto sampler_uniforms()
    -> const Uniform_collection&;

    auto sampler(const std::string& key) const
    -> const Shader_resource*;

    void seal();

    auto add(const std::string& name, gl::Uniform_type type)
    -> Shader_resource&;

    auto add(const std::string& name, gl::Uniform_type type, size_t array_size)
    -> Shader_resource&;

    auto str() const
    -> std::string;

private:
    Uniform_collection        m_samplers;
    std::vector<unsigned int> m_textures;
};

} // namespace namespace graphics

#endif
