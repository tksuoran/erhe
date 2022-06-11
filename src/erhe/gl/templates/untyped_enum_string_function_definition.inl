
auto {WRAPPER_ENUM_STRING_FN_NAME}_string(const unsigned int value) -> const char* // {GROUP_NAME}
{{
    const auto typed_value = static_cast<{WRAPPER_ENUM_TYPE_NAME}>(value);
    return c_str(typed_value);
}}
