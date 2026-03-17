#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace erhe::codegen {

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
    std::size_t      offset;
    std::size_t      size;
    uint32_t         added_in;
    uint32_t         removed_in;
    const char*      short_desc;
    const char*      long_desc;
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
    std::span<const Field_info>     fields;
};

} // namespace erhe::codegen
