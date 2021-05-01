#error
#include "erhe/graphics/samplers.hpp"
#include <sstream>

namespace erhe::graphics
{

auto Samplers::sampler_uniforms()
-> const Samplers::Uniform_collection&
{
    return m_samplers;
}

auto Samplers::sampler(const std::string& key) const
-> const Shader_resource*
{
    auto i = m_samplers.find(key);
    if (i != m_samplers.cend())
    {
        return &(i->second);
    }
    return nullptr;
}

void Samplers::seal()
{
    m_textures.reserve(m_samplers.size());
}

auto Samplers::add(const std::string& name,
                   gl::Uniform_type   type)
-> Shader_resource&
{
    auto i = m_samplers.emplace(std::piecewise_construct,
                                std::forward_as_tuple(name),
                                std::forward_as_tuple(name, m_samplers.size(), type));
    return i.first->second;
}

auto Samplers::add(const std::string& name,
                   gl::Uniform_type   type,
                   size_t             array_size)
-> Shader_resource&
{
    auto i = m_samplers.emplace(std::piecewise_construct,
                                std::forward_as_tuple(name),
                                std::forward_as_tuple(name, m_samplers.size(), type, array_size));
    return i.first->second;
}

auto Samplers::str() const
-> std::string
{
    std::stringstream sb;
    for (const auto& i : m_samplers)
    {
        const auto& sampler_uniform = i.second;
        sb << "uniform " << glsl_token(sampler_uniform.basic_type()) << " " << sampler_uniform.name();

        if (sampler_uniform.is_array())
        {
            sb << "[" << sampler_uniform.array_size() << "]";
        }

        sb << ";\n";
    }
    return sb.str();
}

} // namespace erhe::graphics
