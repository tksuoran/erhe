{AUTOGENERATION_WARNING}

#include "erhe/gl/enum_string_functions.hpp"

#include <sstream>
#include <unordered_map>
#include <mutex>

#include <fmt/format.h>

namespace gl
{{

const char* c_str(GLboolean value)
{{
    return value == 1 ? "true" : "false";
}}

namespace {{

std::unordered_map<std::string, unsigned int> enum_map {{
{MAP_MAKE_ENTRIES}
}};

}}

unsigned int enum_value(const std::string& name) {{
    auto i = enum_map.find(name);
    if (i != enum_map.end()) {{
        return i->second;
    }}
    return 0;
}}

const char* enum_string(unsigned int value)
{{
    switch (value)
    {{
{ALL_ENUM_STRING_CASES}
        default:
        {{
            return "?";
        }}
    }}
}}

{ENUM_STRING_FUNCTION_DEFINITIONS}

{UNTYPED_ENUM_STRING_FUNCTION_DEFINITIONS}

}} // namespace gl

