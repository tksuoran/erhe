set(ERHE_ADDITIONAL_GL_INCLUDES "${PROJECT_SOURCE_DIR}/src/khronos/khronos")

# Globally use fastlink
# or maybe not add_link_options($<$<COMPILE_LANGUAGE:CXX>:/DEBUG:FASTLINK>)

add_compile_options($<$<COMPILE_LANGUAGE:CXX>:/MP>)

function (erhe_target_settings target)
    target_compile_definitions(${target} PUBLIC $<$<COMPILE_LANGUAGE:CXX>:NOMINMAX>)
    target_compile_definitions(${target} PUBLIC $<$<COMPILE_LANGUAGE:CXX>:_CRT_SECURE_NO_WARNINGS>)
    target_compile_definitions(${target} PUBLIC $<$<COMPILE_LANGUAGE:CXX>:_SILENCE_CXX20_U8PATH_DEPRECATION_WARNING>)

    # Enable Just My Code - which cmake does not seem to set even though
    # Visual Studio has documented it to be enabled by default
    target_compile_options(${target} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/JMC>)

    # Set warning level 4 and enable warnings as errors
    target_compile_options(${target} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/W4>)
    target_compile_options(${target} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/WX>)

    # Treat code included using angle brackets as external, and set warning level 3 for external code
    target_compile_options(${target} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/external:anglebrackets>)
    target_compile_options(${target} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/external:W3>)

    # Source code is UTF-8
    target_compile_options(${target} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/utf-8>)

    # Allow glm constexpr by disabling SIMD
    target_compile_definitions(${target} PUBLIC $<$<COMPILE_LANGUAGE:CXX>:GLM_FORCE_PURE>)
endfunction()

function (erhe_disable_incremental_linking)
    #set(CMAKE_STATIC_LINKER_FLAGS_RELEASE        "/debug /incremental:no" PARENT_SCOPE)
    #set(CMAKE_STATIC_LINKER_FLAGS_RELWITHDEBINFO "/debug /incremental:no" PARENT_SCOPE)
    #set(CMAKE_SHARED_LINKER_FLAGS_RELEASE        "/debug /incremental:no" PARENT_SCOPE)
    #set(CMAKE_SHARED_LINKER_FLAGS_RELWITHDEBINFO "/debug /incremental:no" PARENT_SCOPE)
    set(CMAKE_EXE_LINKER_FLAGS_DEBUG             "/DEBUG /INCREMENTAL:NO" PARENT_SCOPE)
    set(CMAKE_EXE_LINKER_FLAGS_MINSIZEREL        "/DEBUG /INCREMENTAL:NO" PARENT_SCOPE)
    set(CMAKE_EXE_LINKER_FLAGS_RELEASE           "/DEBUG /INCREMENTAL:NO" PARENT_SCOPE)
    set(CMAKE_EXE_LINKER_FLAGS_RELWITHDEBINFO    "/DEBUG /INCREMENTAL:NO" PARENT_SCOPE)
endfunction (erhe_disable_incremental_linking)
