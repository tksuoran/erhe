set(ERHE_ADDITIONAL_GL_INCLUDES "${PROJECT_SOURCE_DIR}/src/khronos/khronos")

# Globally use fastlink
add_link_options($<$<COMPILE_LANGUAGE:CXX>:/DEBUG:FASTLINK>)

add_compile_options($<$<COMPILE_LANGUAGE:CXX>:/MP>)

function (target_strict_cxx target)
    target_compile_definitions(${target} PUBLIC $<$<COMPILE_LANGUAGE:CXX>:NOMINMAX>)
    target_compile_definitions(${target} PUBLIC $<$<COMPILE_LANGUAGE:CXX>:_CRT_SECURE_NO_WARNINGS>)
    target_compile_definitions(${target} PUBLIC $<$<COMPILE_LANGUAGE:CXX>:IMGUI_DEBUG_PARANOID>)
    target_compile_definitions(${target} PUBLIC $<$<COMPILE_LANGUAGE:CXX>:IMGUI_DISABLE_OBSOLETE_FUNCTIONS>)
    target_compile_options(${target} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/W4>)
    target_compile_options(${target} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/WX>)
    target_compile_options(${target} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/external:anglebrackets>)
    target_compile_options(${target} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/external:W3>)
endfunction()
