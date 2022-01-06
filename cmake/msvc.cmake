set(ERHE_ADDITIONAL_GL_INCLUDES "${PROJECT_SOURCE_DIR}/src/khronos/khronos")

# Globally use fastlink
# or maybe not add_link_options($<$<COMPILE_LANGUAGE:CXX>:/DEBUG:FASTLINK>)

add_compile_options($<$<COMPILE_LANGUAGE:CXX>:/MP>)

function (erhe_target_settings target)
    target_compile_definitions(${target} PUBLIC $<$<COMPILE_LANGUAGE:CXX>:NOMINMAX>)
    target_compile_definitions(${target} PUBLIC $<$<COMPILE_LANGUAGE:CXX>:_CRT_SECURE_NO_WARNINGS>)

    # Defines for Dear ImGui for development purposes
    target_compile_definitions(${target} PUBLIC $<$<COMPILE_LANGUAGE:CXX>:IMGUI_DEBUG_PARANOID>)
    target_compile_definitions(${target} PUBLIC $<$<COMPILE_LANGUAGE:CXX>:IMGUI_DISABLE_OBSOLETE_FUNCTIONS>)

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

    # Attempts to disable incremental linking for RelWithDebInfo.
    # This does not work yet.

    #target_link_options(
    #    ${target} PUBLIC
    #    $<$<AND:$<CONFIG:RelWithDebInfo>,$<COMPILE_LANGUAGE:CXX>>:/incremental:no>)
    #foreach(t EXE SHARED MODULE)
    #    string(APPEND CMAKE_${t}_LINKER_FLAGS_DEBUG_INIT " /debug /pdbtype:sept ${MSVC_INCREMENTAL_YES_FLAG}")
    #    string(APPEND CMAKE_${t}_LINKER_FLAGS_RELWITHDEBINFO_INIT " /debug /pdbtype:sept ${MSVC_INCREMENTAL_YES_FLAG}")

    foreach(FLAG_TYPE EXE MODULE SHARED)
        string(REPLACE "INCREMENTAL:YES" "INCREMENTAL:NO" FLAG_TMP "${CMAKE_${FLAG_TYPE}_LINKER_FLAGS_DEBUG}")
        string(REPLACE "incremental:yes" "incremental:no" FLAG_TMP "${CMAKE_${FLAG_TYPE}_LINKER_FLAGS_DEBUG}")
        #string(REPLACE "/EDITANDCONTINUE" "" FLAG_TMP "${CMAKE_${FLAG_TYPE}_LINKER_FLAGS_DEBUG}")
        set(CMAKE_${FLAG_TYPE}_LINKER_FLAGS_DEBUG "/incremental:NO ${FLAG_TMP}" CACHE STRING "Overriding default debug ${FLAG_TYPE} linker flags." FORCE)
        mark_as_advanced(CMAKE_${FLAG_TYPE}_LINKER_FLAGS_DEBUG)
    endforeach()
endfunction()
