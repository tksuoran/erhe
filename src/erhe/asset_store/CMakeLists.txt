set(_target "erhe_asset_store")
add_library(${_target})
add_library(erhe::asset_store ALIAS ${_target})

erhe_target_sources_grouped(
    ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
    erhe_asset_store/asset_store.cpp
    erhe_asset_store/asset_store.hpp
    erhe_asset_store/asset_store_log.cpp
    erhe_asset_store/asset_store_log.hpp
    erhe_asset_store/item_kind.cpp
    erhe_asset_store/item_kind.hpp
    erhe_asset_store/key.cpp
    erhe_asset_store/key.hpp
)

target_include_directories(${_target} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# Third-party C sources from the download-only sqlite3 and xxhash packages
# (top-level CMakeLists.txt). Their headers stay private to this library.
set(_asset_store_third_party_sources
    "${sqlite3_SOURCE_DIR}/sqlite3.c"
    "${xxhash_SOURCE_DIR}/xxhash.c"
)
target_sources(${_target} PRIVATE ${_asset_store_third_party_sources})
set_source_files_properties(${_asset_store_third_party_sources} PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
if (MSVC)
    set_source_files_properties(${_asset_store_third_party_sources} PROPERTIES COMPILE_OPTIONS "/WX-;/W0")
else ()
    set_source_files_properties(${_asset_store_third_party_sources} PROPERTIES COMPILE_OPTIONS "-Wno-error;-w")
endif ()
target_include_directories(${_target} SYSTEM PRIVATE "${sqlite3_SOURCE_DIR}" "${xxhash_SOURCE_DIR}")

target_compile_definitions(${_target}
    PRIVATE
        SQLITE_THREADSAFE=1
        SQLITE_DQS=0
        SQLITE_DEFAULT_MEMSTATUS=0
        SQLITE_OMIT_DEPRECATED
        SQLITE_OMIT_LOAD_EXTENSION
        SQLITE_OMIT_SHARED_CACHE
        # Keeps the vendored xxHash symbols apart from other copies in the link.
        XXH_NAMESPACE=erhe_asset_store_
)

find_package(Threads REQUIRED)

target_link_libraries(${_target}
    PRIVATE
        erhe::file
        erhe::log
        erhe::verify
        Threads::Threads
)

erhe_target_settings(${_target} "erhe")

if (${ERHE_BUILD_TESTS} STREQUAL "ON")
    add_subdirectory(test)
endif ()
