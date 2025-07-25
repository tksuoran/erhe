#pragma once

namespace erhe::rendergraph {

class Rendergraph_node_key
{
public:
    constexpr static int none                 = 0;
    constexpr static int dependency           = 1; // simple dependency
    constexpr static int viewport_texture     = 2; // texture containing contents of viewport
    constexpr static int shadow_maps          = 3;
    constexpr static int depth_visualization  = 4; // shadow map converted to color
    constexpr static int texture_for_gui      = 5; // textures rendered with 3d shown in imgui (brdf slice)
    constexpr static int rendertarget_texture = 6; // imgui host texture rendered in 3d viewport
    constexpr static int wildcard             = 99; // imgui host texture rendered in 3d viewport
    //constexpr static int window               = 1;
    //constexpr static int viewport             = 3;
    //constexpr static int shadow_maps          = 4;
};

} // namespace erhe::rendergraph
