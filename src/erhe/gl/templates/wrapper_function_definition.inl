auto {WRAPPER_NAME}({WRAPPER_ARGUMENTS}) -> {WRAPPER_RETURN_TYPE}
{{
    log_gl->trace(
        "{NATIVE_NAME}({LOG_FORMAT_STRING})"
        {LOG_FORMAT_ENTRIES}
    );
    {CAPTURE_RESULT}{NATIVE_NAME}({ARGUMENT_LIST});
{WRAPPER_RETURN_STATEMENT}}}
