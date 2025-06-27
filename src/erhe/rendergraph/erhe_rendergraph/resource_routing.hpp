#pragma once

namespace erhe::rendergraph {

class Rendergraph_node_key
{
public:
    constexpr static int none                 = 0;
    constexpr static int viewport_texture     = 1; // texture containing contents of viewport
    constexpr static int shadow_maps          = 2;
    constexpr static int depth_visualization  = 3; // shadow map converted to color
    constexpr static int texture_for_gui      = 4; // textures rendered with 3d shown in imgui (brdf slice)
    constexpr static int rendertarget_texture = 5; // imgui host texture renderd in 3d viewport
    //constexpr static int window               = 1;
    //constexpr static int viewport             = 3;
    //constexpr static int shadow_maps          = 4;
};

} // namespace erhe::rendergraph
