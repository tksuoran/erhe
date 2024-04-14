#pragma once

#include "erhe_graphics/shader_stages_prototype.hpp"

#include <memory>
#include <string>

namespace igl {
    class IDevice;
    class IShaderStages;
}

namespace erhe::graphics
{

class Shader_stages
{
public:
    explicit Shader_stages(Shader_stages_prototype&& prototype);

    // Reloads program by consuming prototype
    void reload    (Shader_stages_prototype&& prototype);
    void invalidate();

    [[nodiscard]] auto name    () const -> const std::string&;
    [[nodiscard]] auto get     () const -> const igl::IShaderStages*;
    [[nodiscard]] auto is_valid() const -> bool;

private:
    std::string                         m_name;
    std::unique_ptr<igl::IShaderStages> m_shader_stages;
    bool                                m_is_valid{false};
};

} // namespace erhe::graphics
