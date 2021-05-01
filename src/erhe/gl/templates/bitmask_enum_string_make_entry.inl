    if ((ui_value & {ENUM_VALUE}U) == {ENUM_VALUE}U)
    {{
        if (!is_empty)
        {{
            ss << " | ";
        }}
        ss << "{ENUM_STRING}";
        is_empty = false;
    }}
