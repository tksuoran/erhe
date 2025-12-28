set(ERHE_ADDITIONAL_GL_INCLUDES "${PROJECT_SOURCE_DIR}/src/khronos/khronos")

add_compile_options($<$<COMPILE_LANGUAGE:CXX>:/MP>)

function (erhe_target_settings_toolchain target)
    target_compile_definitions(${target} PUBLIC $<$<COMPILE_LANGUAGE:CXX>:NOMINMAX>)
    target_compile_definitions(${target} PUBLIC $<$<COMPILE_LANGUAGE:CXX>:_CRT_SECURE_NO_WARNINGS>)
    target_compile_definitions(${target} PUBLIC $<$<COMPILE_LANGUAGE:CXX>:_SILENCE_CXX20_U8PATH_DEPRECATION_WARNING>)
    target_compile_definitions(${target} PUBLIC $<$<COMPILE_LANGUAGE:CXX>:_SILENCE_CXX20_IS_POD_DEPRECATION_WARNING>)

    # Enable Just My Code - which cmake does not seem to set even though
    # Visual Studio has documented it to be enabled by default
    target_compile_options(${target} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/JMC>)

    # Set warning level 4 and enable warnings as errors
    target_compile_options(${target} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/W4>)
    target_compile_options(${target} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/WX>)

    # Disable unreachable warning (libfmt)
    target_compile_options(${target} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/wd4702>) # unreachable

    # Treat code included using angle brackets as external, and set warning level 3 for external code
    target_compile_options(${target} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/external:anglebrackets>)
    target_compile_options(${target} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/external:W3>)

    # Source code is UTF-8
    target_compile_options(${target} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/utf-8>)

    # Allow glm constexpr by disabling SIMD
    target_compile_definitions(${target} PUBLIC $<$<COMPILE_LANGUAGE:CXX>:GLM_FORCE_PURE>)
endfunction()
