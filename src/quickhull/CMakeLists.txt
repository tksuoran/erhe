set(_target "quickhull")
add_library(${_target})
add_library(quickhull::quickhull ALIAS ${_target})
erhe_target_sources_grouped(
    ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
    quickhull/ConvexHull.hpp
    quickhull/HalfEdgeMesh.hpp
    quickhull/MathUtils.hpp
    quickhull/QuickHull.cpp
    quickhull/QuickHull.hpp
    quickhull/Structs/Mesh.hpp
    quickhull/Structs/Plane.hpp
    quickhull/Structs/Pool.hpp
    quickhull/Structs/Ray.hpp
    quickhull/Structs/Vector3.hpp
    quickhull/Structs/VertexDataSource.hpp    
)
target_include_directories(${_target} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
