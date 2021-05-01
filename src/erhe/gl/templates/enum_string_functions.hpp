#pragma once
{AUTOGENERATION_WARNING}

#include "erhe/gl/wrapper_enums.hpp"

#include <string>

namespace gl
{{

typedef unsigned char GLboolean;
const char* c_str(GLboolean value);

const char* enum_string(unsigned int value);

unsigned int enum_value(const std::string& name);

{ENUM_STRING_FUNCTION_DECLARATIONS}

{UNTYPED_ENUM_STRING_FUNCTION_DECLARATIONS}

}} // namespace gl
