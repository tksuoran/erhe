#pragma once

#include <cstddef>

#include "erhe/gl/wrapper_enums.hpp"

namespace gl_helpers
{

[[nodiscard]] auto size_of_type(const gl::Draw_elements_type type) -> std::size_t;
[[nodiscard]] auto size_of_type(const gl::Vertex_attrib_type type) -> std::size_t;
[[nodiscard]] auto is_indexed(const gl::Buffer_target type) -> bool;

void set_error_checking(const bool enable);
void check_error       ();
void initialize_logging();

} // namespace gl_helpers

