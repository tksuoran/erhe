#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace erhe::codegen {

enum class Field_type : uint8_t
{
    bool_,
    int_,
    unsigned_int,
    int8,
    uint8,
    int16,
    uint16,
    int32,
    uint32,
    int64,
    uint64,
    float_,
    double_,
    string,
    vec2,
    vec3,
    vec4,
    ivec2,
    mat4,
    vector,
    array,
    optional,
    struct_ref,
    enum_ref,
};

struct Numeric_limits
{
    double ui_min;
    double ui_max;
    double hard_min;
    double hard_max;
    bool   has_ui_min;
    bool   has_ui_max;
    bool   has_hard_min;
    bool   has_hard_max;
};

struct Enum_value_info
{
    const char* name;
    int64_t     value;
    const char* short_desc;
    const char* long_desc;
};

struct Enum_info
{
    const char*                      name;
    const char*                      underlying_type_name;
    std::span<const Enum_value_info> values;
};

struct Field_info
{
    const char*      name;
    const char*      type_name;
    Field_type       field_type;
    std::size_t      offset;
    std::size_t      size;
    uint32_t         added_in;
    uint32_t         removed_in;
    const char*      short_desc;
    const char*      long_desc;
    const char*      path;
    const char*      default_value;
    Numeric_limits   numeric_limits;
    bool             is_numeric;
    bool             is_enum;
    const Enum_info* enum_info;
};

struct Struct_info
{
    const char*                     name;
    uint32_t                        version;
    const char*                     short_desc;
    const char*                     long_desc;
    std::span<const Field_info>     fields;
};

} // namespace erhe::codegen
