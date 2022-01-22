auto base_zero(const {WRAPPER_ENUM_TYPE_NAME} value) -> std::size_t // {GROUP_NAME}
{{
    const auto ui_value = static_cast<unsigned int>(value);
    switch (ui_value)
    {{
{GROUP_ENUM_TO_BASE_ZERO_ENTRIES}
        default: return std::numeric_limits<std::size_t>::max();
    }}
}}

auto {WRAPPER_ENUM_TYPE_NAME}_from_base_zero(const size_t ui_value) -> {WRAPPER_ENUM_TYPE_NAME} // {GROUP_NAME}
{{
    switch (ui_value)
    {{
{BASE_ZERO_TO_GROUP_ENUM_ENTRIES}
        default: return static_cast<{WRAPPER_ENUM_TYPE_NAME}>(0xffffu);
    }}
}}
