set(ERHE_ADDITIONAL_GL_INCLUDES "${PROJECT_SOURCE_DIR}/src/khronos/khronos")

# Globally use fastlink
add_link_options($<$<COMPILE_LANGUAGE:CXX>:/DEBUG:FASTLINK>)

add_definitions(-DNOMINMAX)
add_definitions(-D_CRT_SECURE_NO_WARNINGS)

add_compile_options($<$<COMPILE_LANGUAGE:CXX>:/MP>)
