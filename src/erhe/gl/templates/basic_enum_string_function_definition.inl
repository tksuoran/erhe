const char* c_str({WRAPPER_ENUM_TYPE_NAME} value) // {GROUP_NAME}
{{
    auto ui_value = static_cast<unsigned int>(value);
    switch (ui_value)
    {{
{GROUP_ENUM_STRING_ENTRIES}
        default: return enum_string(ui_value);
    }}
}}
