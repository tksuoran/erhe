#pragma once
{AUTOGENERATION_WARNING}

#include "erhe_gl/wrapper_enums.hpp"

#include <string>

namespace gl
{{

typedef unsigned char GLboolean;

auto c_str      (GLboolean value) -> const char*;
auto enum_string(unsigned int value) -> const char*;
auto enum_value (const std::string& name) -> unsigned int;

{ENUM_STRING_FUNCTION_DECLARATIONS}

{UNTYPED_ENUM_STRING_FUNCTION_DECLARATIONS}

}} // namespace gl
