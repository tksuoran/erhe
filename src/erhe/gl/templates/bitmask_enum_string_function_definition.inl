auto to_string(const {WRAPPER_ENUM_TYPE_NAME} value) -> std::string// {GROUP_NAME}
{{
    std::stringstream ss;
    bool is_empty = true;
    const auto ui_value = static_cast<unsigned int>(value);

{GROUP_ENUM_STRING_ENTRIES}

    static_cast<void>(is_empty);

    return ss.str();
}}
