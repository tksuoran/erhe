#pragma once

#include "erhe_gl/wrapper_enums.hpp"

#include <memory>
#include <string_view>
#include <string>

namespace glslang {
    class TShader;
    class TProgram;
}

namespace erhe::graphics {

class Device;

[[nodiscard]] auto glsl_to_glslang(
    Device&            graphics_device;
    gl::Shader_type    gl_shader_type,
    std::string_view   source,
    const std::string& name
) -> std::shared_ptr<glslang::TShader>;

} // namespace erhe::graphics
