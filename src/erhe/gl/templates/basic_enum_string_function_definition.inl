auto c_str(const {WRAPPER_ENUM_TYPE_NAME} value) -> const char* // {GROUP_NAME}
{{
    const auto ui_value = static_cast<unsigned int>(value);
    switch (ui_value)
    {{
{GROUP_ENUM_STRING_ENTRIES}
        default: return enum_string(ui_value);
    }}
}}
