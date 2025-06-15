include(CheckCXXCompilerFlag)

add_compile_options(-Wall;-Wextra;-Wno-unused;-Wno-unknown-pragmas;-Wno-sign-compare;-Wwrite-strings;-Wno-unused;-Wno-narrowing)
check_cxx_compiler_flag(-Wno-unqualified-std-cast-call ERHE_CXX_HAS_NO_UNQUALIFIED_STD_CAST_CALL_FLAG)
if (ERHE_CXX_HAS_NO_UNQUALIFIED_STD_CAST_CALL_FLAG)
    add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:-Wno-deprecated-copy>")
endif ()
add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:-Woverloaded-virtual>")
add_compile_options("$<$<CONFIG:RELEASE>:-O3>")
add_compile_options("$<$<CONFIG:DEBUG>:-O0;-g3>")

# TODO For now, to enable sanitizers, uncomment lines here

#add_compile_options(-fsanitize=undefined)
#add_link_options(-fsanitize=undefined)

#add_compile_options(-fsanitize=address)
#add_link_options(-fsanitize=address)

# No implicit-conversion because Tracy uses moodycamel
#add_compile_options(-fsanitize=undefined,float-divide-by-zero,local-bounds -fno-sanitize-recover=all)
#add_link_options(-fsanitize=undefined,float-divide-by-zero,local-bounds -fno-sanitize-recover=all)

#add_compile_options(-fsanitize=thread)
#add_link_options(-fsanitize=thread)

set(ERHE_ADDITIONAL_GL_INCLUDES "${PROJECT_SOURCE_DIR}/src/khronos/khronos")

function (erhe_target_settings_toolchain target)
endfunction()
