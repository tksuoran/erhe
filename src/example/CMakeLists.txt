set(_target "example")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
add_executable(${_target})
erhe_target_sources_grouped(
    ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
    example.cpp
    example.hpp
    example_log.cpp
    example_log.hpp
    frame_controller.cpp
    frame_controller.hpp
    image_transfer.cpp
    image_transfer.hpp
    main.cpp
    mesh_memory.cpp
    mesh_memory.hpp
    programs.cpp
    programs.hpp
    res/models/Box.gltf
    res/shaders/depth.vert
    res/shaders/depth.frag
    res/shaders/standard.vert
    res/shaders/standard.frag
    erhe.ini
)
if (${ERHE_GLTF_LIBRARY} STREQUAL "cgltf")
    erhe_target_sources_grouped(
        ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
        gltf.cpp
        gltf.hpp
    )
endif ()
target_link_libraries(
    ${_target}
    PRIVATE
    erhe::bit
    erhe::file
    #erhe::gl
    #erhe::graphics
    IGLLibrary
    erhe::log
    erhe::math
    erhe::primitive
    erhe::profile
    erhe::renderer
    erhe::scene
    erhe::scene_renderer
    erhe::ui
    erhe::verify
    erhe::window
    mINI
)
if (DEFINED ERHE_PROFILE_TARGET)
    target_link_libraries(${_target} PRIVATE ${ERHE_PROFILE_TARGET})
endif ()
if (${ERHE_GLTF_LIBRARY} STREQUAL "cgltf")
    target_link_libraries(${_target} PRIVATE cgltf)
endif ()
target_include_directories(${_target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
if (${ERHE_USE_PRECOMPILED_HEADERS})
    target_precompile_headers(${_target} REUSE_FROM erhe_pch)
endif ()
set_target_properties(
    ${_target} PROPERTIES
    CXX_STANDARD                  20
    CXX_STANDARD_REQUIRED         YES
    CXX_EXTENSIONS                NO
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
)
erhe_target_settings(${_target})
set_property(TARGET ${_target} PROPERTY FOLDER "erhe-executables")
