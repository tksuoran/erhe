std::size_t base_zero({WRAPPER_ENUM_TYPE_NAME} value) // {GROUP_NAME}
{{
    auto ui_value = static_cast<unsigned int>(value);
    switch (ui_value)
    {{
{GROUP_ENUM_BASE_ZERO_ENTRIES}
        default: return std::numeric_limits<std::size_t>::max();
    }}
}}
