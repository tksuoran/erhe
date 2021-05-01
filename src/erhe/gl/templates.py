"""Template strings."""
import util

AUTOGENERATION_WARNING                   = util.read("autogeneration_warning.inl")
ENUM_STRING_FUNCTIONS_HPP                = util.read("enum_string_functions.hpp")
ENUM_STRING_FUNCTIONS_CPP                = util.read("enum_string_functions.cpp")
ENUM_BASE_ZERO_FUNCTIONS_HPP             = util.read("enum_base_zero_functions.hpp")
ENUM_BASE_ZERO_FUNCTIONS_CPP             = util.read("enum_base_zero_functions.cpp")
ENUM_BASE_ZERO_FUNCTION_DECLARATION      = "std::size_t base_zero({WRAPPER_ENUM_TYPE_NAME} value);"
ENUM_BASE_ZERO_FUNCTION_DEFINITION       = util.read("enum_base_zero_function_definition.inl")
ENUM_BASE_ZERO_MAKE_ENTRY                = "        case {ENUM_VALUE}: return {ENUM_BASE_ZERO_VALUE};"
UNTYPED_ENUM_STRING_FUNCTION_DECLARATION = "const char* {WRAPPER_ENUM_STRING_FN_NAME}_string(unsigned int value);"
UNTYPED_ENUM_STRING_FUNCTION_DEFINITION  = util.read("untyped_enum_string_function_definition.inl")

ENUM_STRING_MAKE_ENTRY = {
    "basic":   "        case {ENUM_VALUE}: return \"{ENUM_STRING}\";",
    "bitmask": util.read("bitmask_enum_string_make_entry.inl")
}

ENUM_STRING_FUNCTION_DECLARATION = {
    "basic":   "const char* c_str({WRAPPER_ENUM_TYPE_NAME} value); // {GROUP_NAME}",
    "bitmask": "std::string to_string({WRAPPER_ENUM_TYPE_NAME} value); // {GROUP_NAME}"
}

ENUM_STRING_FUNCTION_DEFINITION = {
    "basic":   util.read("basic_enum_string_function_definition.inl"),
    "bitmask": util.read("bitmask_enum_string_function_definition.inl")
}

ENUM_HELPER_DEFINITION = {
    "basic":
        "GLenum glenum({WRAPPER_ENUM_TYPE_NAME} value) noexcept {{ return static_cast<GLenum>(value); }}",
    "bitmask":
        "GLbitfield glbitfield({WRAPPER_ENUM_TYPE_NAME} value) noexcept {{ return static_cast<GLbitfield>(value); }}"
}

WRAPPER_ENUMS_HPP                   = util.read("wrapper_enums.hpp")
WRAPPER_ENUM_DECLARATION            = util.read("wrapper_enum_declaration.inl")
DYNAMIC_LOAD_HPP                    = util.read("dynamic_load.hpp")
DYNAMIC_LOAD_CPP                    = util.read("dynamic_load.cpp")
COMMAND_INFO_CPP                    = util.read("command_info.cpp")
COMMAND_INFO_HPP                    = util.read("command_info.hpp")
DYNAMIC_LOAD_FUNCTION_DECLARATION   = 'extern {NATIVE_RETURN_TYPE} (* {COMMAND_NAME}) ({NATIVE_ARG_TYPE_LIST});'
DYNAMIC_LOAD_FUNCTION_DEFINITION    = '{NATIVE_RETURN_TYPE} (* {COMMAND_NAME}) ({NATIVE_ARGUMENTS}) = nullptr;'
DYNAMIC_LOAD_FUNCTION_GET_STATEMENT = (
    '{COMMAND_NAME} = ({NATIVE_RETURN_TYPE}(*)({NATIVE_ARG_TYPE_LIST}))get_proc_address(\"{COMMAND_NAME}\");'
)
WRAPPER_FUNCTION_DECLARATION        = '{WRAPPER_RETURN_TYPE} {WRAPPER_NAME}({WRAPPER_ARGUMENTS}); // {COMMAND_VERSION}'
WRAPPER_FUNCTION_DEFINITION         = util.read("wrapper_function_definition.inl")
WRAPPER_FUNCTIONS_HPP               = util.read("wrapper_functions.hpp")
WRAPPER_FUNCTIONS_CPP               = util.read("wrapper_functions.cpp")
