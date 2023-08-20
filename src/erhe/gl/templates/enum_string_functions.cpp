{AUTOGENERATION_WARNING}

#include "erhe_gl/enum_string_functions.hpp"

#include <sstream>
#include <unordered_map>
#include <mutex>

#include <fmt/format.h>

namespace gl
{{

auto c_str(const GLboolean value) -> const char*
{{
    return value == 1 ? "true" : "false";
}}

namespace {{

std::unordered_map<std::string, unsigned int> enum_map
{{
{MAP_MAKE_ENTRIES}
}};

}}

auto enum_value(const std::string& name) -> unsigned int
{{
    const auto i = enum_map.find(name);
    if (i != enum_map.end()) {{
        return i->second;
    }}
    return 0;
}}

auto enum_string(const unsigned int value) -> const char*
{{
    switch (value) {{
{ALL_ENUM_STRING_CASES}
        default: {{
            return "?";
        }}
    }}
}}

{ENUM_STRING_FUNCTION_DEFINITIONS}

{UNTYPED_ENUM_STRING_FUNCTION_DEFINITIONS}

}} // namespace gl

