set(_target "hextiles")

add_executable(${_target})

erhe_target_sources_grouped(
    ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
    coordinate.cpp
    coordinate.hpp
    file_util.cpp
    file_util.hpp
    log.cpp
    log.hpp
    FastNoiseLite.h
    game.cpp
    game.hpp
    game_window.cpp
    game_window.hpp
    main.cpp
    map_renderer.cpp
    map_renderer.hpp
    map.cpp
    map.hpp
    map_window.cpp
    map_window.hpp
    menu_window.cpp
    menu_window.hpp
    on_load.cpp
    pixel_lookup.cpp
    pixel_lookup.hpp
    stream.cpp
    stream.hpp
    terrain_editor_window.cpp
    terrain_editor_window.hpp
    terrain_type.cpp
    terrain_type.hpp
    texture_util.cpp
    texture_util.hpp
    tile_shape.cpp
    tile_shape.hpp
    tiles.cpp
    tiles.hpp
    type_editor.cpp
    type_editor.hpp
    unit_editor_window.cpp
    unit_editor_window.hpp
    unit_type.cpp
    unit_type.hpp
    types.cpp
    types.hpp
    rendering.cpp
    rendering.hpp
    view_client.cpp
    view_client.hpp

    res/shaders/line.vert
    res/shaders/line.geom
    res/shaders/line.frag
    res/shaders/text.vert
    res/shaders/text.frag
    res/shaders/textured.vert
    res/shaders/textured.frag
    res/shaders/tile.vert
    res/shaders/tile.frag
)

target_link_libraries(
    ${_target}
    PRIVATE
    erhe::application
    erhe::components
    erhe::concurrency
    erhe::geometry
    erhe::gl
    erhe::graphics
    erhe::log
    erhe::toolkit
    erhe::ui
    imgui
    ghc_filesystem
    optional_lite
    nlohmann_json::nlohmann_json
)

if (${ERHE_SVG_LIBRARY} STREQUAL "lunasvg")
    target_link_libraries(${_target} PRIVATE lunasvg)
endif ()

if (${ERHE_AUDIO_LIBRARY} STREQUAL "miniaudio")
    target_link_libraries(${_target} PRIVATE miniaudio)
endif ()

if (${ERHE_PROFILE_LIBRARY} STREQUAL "tracy")
    target_link_libraries(${_target} PRIVATE TracyClient)
endif ()

if (${ERHE_PROFILE_LIBRARY} STREQUAL "superluminal")
    target_link_libraries(${_target} PRIVATE SuperluminalAPI)
endif ()

target_include_directories(${_target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

set_target_properties(
    ${_target} PROPERTIES
    CXX_STANDARD                  20
    CXX_STANDARD_REQUIRED         YES
    CXX_EXTENSIONS                NO
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
)

erhe_target_settings(${_target})
set_property(TARGET ${_target} PROPERTY FOLDER "erhe")
