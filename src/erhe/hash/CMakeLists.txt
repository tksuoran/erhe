set(_target "erhe_hash")
add_library(${_target})
add_library(erhe::hash ALIAS ${_target})

erhe_target_sources_grouped(
    ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
    erhe_hash/content_hash.cpp
    erhe_hash/content_hash.hpp
    erhe_hash/hash.cpp
    erhe_hash/hash.hpp
    erhe_hash/xxhash.hpp
)

target_include_directories(${_target} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# xxhash.c of the download-only xxhash package (top-level CMakeLists.txt) for
# Xxh3_128_hasher; its header stays private to this library.
set(_hash_third_party_sources "${xxhash_SOURCE_DIR}/xxhash.c")
target_sources(${_target} PRIVATE ${_hash_third_party_sources})
set_source_files_properties(${_hash_third_party_sources} PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
if (MSVC)
    set_source_files_properties(${_hash_third_party_sources} PROPERTIES COMPILE_OPTIONS "/WX-;/W0")
else ()
    set_source_files_properties(${_hash_third_party_sources} PROPERTIES COMPILE_OPTIONS "-Wno-error;-w")
endif ()
target_include_directories(${_target} SYSTEM PRIVATE "${xxhash_SOURCE_DIR}")
# Keeps the vendored xxHash symbols apart from erhe::asset_store's copy in the link.
target_compile_definitions(${_target} PRIVATE XXH_NAMESPACE=erhe_hash_)

target_link_libraries(${_target} PUBLIC glm::glm-header-only)

erhe_target_settings(${_target} "erhe")
