{AUTOGENERATION_WARNING}

#include "erhe/gl/wrapper_functions.hpp"
#include "erhe/gl/enum_string_functions.hpp"
#include "erhe/gl/dynamic_load.hpp"
#include "erhe/gl/gl.hpp"

#include <spdlog/spdlog.h>

#include <cassert>
#include <memory>

namespace gl {{

extern void check_error();
extern std::shared_ptr<spdlog::logger> log_gl;

auto glbitfield(const ::GLbitfield value) -> ::GLbitfield
{{
    return value;
}}

{ENUM_HELPER_DEFINITIONS}

{WRAPPER_FUNCTION_DEFINITIONS}

}} // namespace gl
