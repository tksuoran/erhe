#pragma once

namespace editor {

// Which form of the mesh nodes the world-space bake admits a view draws
// (doc/editor/world_space_bake.md "Two forms", R8.8).
enum class Mesh_form : unsigned int
{
    source = 0,
    world_space
};

[[nodiscard]] auto c_str(Mesh_form form) -> const char*;

}
