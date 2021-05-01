const char* {WRAPPER_ENUM_STRING_FN_NAME}_string(unsigned int value) // {GROUP_NAME}
{{
    auto typed_value = static_cast<{WRAPPER_ENUM_TYPE_NAME}>(value);
    return c_str(typed_value);
}}
