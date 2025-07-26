#pragma once

#include "erhe_dataformat/dataformat.hpp"
#include "erhe_graphics/enums.hpp"
#include "erhe_gl/wrapper_enums.hpp"

namespace erhe::graphics {

[[nodiscard]] auto to_gl_index_type(erhe::dataformat::Format format) -> gl::Draw_elements_type;

[[nodiscard]] auto to_gl(Blend_equation_mode    equation_mode         ) -> gl::Blend_equation_mode;
[[nodiscard]] auto to_gl(Blending_factor        blending_factor       ) -> gl::Blending_factor;
[[nodiscard]] auto to_gl(Cull_face_mode         cull_face_mode        ) -> gl::Cull_face_mode;
[[nodiscard]] auto to_gl(Front_face_direction   front_face_direction  ) -> gl::Front_face_direction;
[[nodiscard]] auto to_gl(Polygon_mode           polygon_mode          ) -> gl::Polygon_mode;
[[nodiscard]] auto to_gl(Shader_type            type                  ) -> gl::Shader_type;
[[nodiscard]] auto to_gl(Stencil_face_direction stencil_face_direction) -> gl::Stencil_face_direction;
[[nodiscard]] auto to_gl(Stencil_op             stencil_op            ) -> gl::Stencil_op;

[[nodiscard]] auto to_gl_depth_function  (Compare_operation compare_operation) -> gl::Depth_function;
[[nodiscard]] auto to_gl_stencil_function(Compare_operation compare_operation) -> gl::Stencil_function;

} // namespace erhe::graphics
