include(CheckCXXCompilerFlag)

add_compile_options(-Wall;-Wextra;-Wno-unused;-Wno-unknown-pragmas;-Wno-sign-compare;-Wwrite-strings;-Wno-unused;-Wno-narrowing)
check_cxx_compiler_flag(-Wno-unqualified-std-cast-call ERHE_CXX_HAS_NO_UNQUALIFIED_STD_CAST_CALL_FLAG)
if (ERHE_CXX_HAS_NO_UNQUALIFIED_STD_CAST_CALL_FLAG)
    add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:-Wno-deprecated-copy>")
endif ()
add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:-Woverloaded-virtual>")
add_compile_options("$<$<CONFIG:RELEASE>:-O3>")
add_compile_options("$<$<CONFIG:DEBUG>:-O0;-g3>")

add_compile_options(-fsanitize=undefined)
add_link_options(-fsanitize=undefined)

if (WIN32)
    set(ERHE_ADDITIONAL_GL_INCLUDES "${PROJECT_SOURCE_DIR}/src/khronos/khronos")
endif ()

function (erhe_target_settings target)
    if (WIN32)
        target_compile_definitions(${target} PUBLIC $<$<COMPILE_LANGUAGE:CXX>:NOMINMAX>)
        target_compile_definitions(${target} PUBLIC $<$<COMPILE_LANGUAGE:CXX>:_CRT_SECURE_NO_WARNINGS>)
    endif ()
endfunction()

