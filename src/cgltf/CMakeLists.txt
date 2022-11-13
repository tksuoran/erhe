add_library(cgltf cgltf.c cgltf.h cgltf_write.c cgltf_write.h)
target_include_directories(cgltf PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
