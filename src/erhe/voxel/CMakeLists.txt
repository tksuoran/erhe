# erhe_voxel
#
# Phase 1 (doc/openvdb-integration-plan.md): only the OpenVDB smoke test
# lives here. The erhe_voxel wrapper library arrives in Phase 2.

if (${ERHE_BUILD_TESTS} STREQUAL "ON")
    add_subdirectory(test)
endif ()
