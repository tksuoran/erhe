std::string to_string({WRAPPER_ENUM_TYPE_NAME} value) // {GROUP_NAME}
{{
    std::stringstream ss;
    bool is_empty = true;
    auto ui_value = static_cast<unsigned int>(value);

{GROUP_ENUM_STRING_ENTRIES}

    static_cast<void>(is_empty);

    return ss.str();
}}
