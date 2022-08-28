set(_target "hextiles")

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
add_executable(${_target})

erhe_target_sources_grouped(
    ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
    erhe.ini
    coordinate.cpp
    coordinate.hpp
    file_util.cpp
    file_util.hpp
    hextiles_log.cpp
    hextiles_log.hpp
    hextiles_on_load.cpp
    hextiles_view_client.cpp
    hextiles_view_client.hpp
    game/game.cpp
    game/game.hpp
    game/game_context.hpp
    game/game_window.cpp
    game/game_window.hpp
    game/player.cpp
    game/player.hpp
    game/unit.hpp
    main.cpp
    map.cpp
    map.hpp
    map_window.cpp
    map_window.hpp
    menu_window.cpp
    menu_window.hpp
    new_game_window.cpp
    new_game_window.hpp
    pixel_lookup.cpp
    pixel_lookup.hpp
    stream.cpp
    stream.hpp
    terrain_type.cpp
    terrain_type.hpp
    texture_util.cpp
    texture_util.hpp
    tile_renderer.cpp
    tile_renderer.hpp
    tile_shape.cpp
    tile_shape.hpp
    tiles.cpp
    tiles.hpp
    unit_type.cpp
    unit_type.hpp
    types.cpp
    types.hpp
    rendering.cpp
    rendering.hpp
    map_editor/map_editor.cpp
    map_editor/map_editor.hpp
    map_editor/map_tool_window.cpp
    map_editor/map_tool_window.hpp
    map_editor/terrain_palette_window.cpp
    map_editor/terrain_palette_window.hpp
    map_generator/biome.cpp
    map_generator/biome.hpp
    map_generator/fbm_noise.cpp
    map_generator/fbm_noise.hpp
    map_generator/map_generator.cpp
    map_generator/map_generator.hpp
    map_generator/terrain_variation.cpp
    map_generator/terrain_variation.hpp
    map_generator/variations.cpp
    map_generator/variations.hpp
    type_editors/terrain_editor_window.cpp
    type_editors/terrain_editor_window.hpp
    type_editors/terrain_replacement_rule_editor_window.cpp
    type_editors/terrain_replacement_rule_editor_window.hpp
    type_editors/terrain_group_editor_window.cpp
    type_editors/terrain_group_editor_window.hpp
    type_editors/type_editor.cpp
    type_editors/type_editor.hpp
    type_editors/unit_editor_window.cpp
    type_editors/unit_editor_window.hpp

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
