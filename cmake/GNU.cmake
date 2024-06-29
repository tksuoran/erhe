# TODO restore warnings that can be restored without issues with Jolt
#add_compile_options(-Wall;-Wextra;-Wno-unused;-Wno-unknown-pragmas;-Wno-sign-compare;-Wwrite-strings;-Wno-unused;-Wno-narrowing;-Wno-ignored-qualifiers;-Wno-inline)
add_compile_definitions(JPH_NO_FORCE_INLINE=1)

add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:-Woverloaded-virtual>")
add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:-Wno-empty-body>")
add_compile_options("$<$<CONFIG:RELEASE>:-O3>")
add_compile_options("$<$<CONFIG:DEBUG>:-O0;-g3>")

function (erhe_target_settings target)
endfunction()
