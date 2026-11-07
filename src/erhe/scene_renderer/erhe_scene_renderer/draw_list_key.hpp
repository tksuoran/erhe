#pragma once

#include "erhe_hash/hash.hpp"
#include "erhe_primitive/enums.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene_renderer/mesh_memory.hpp"
#include "erhe_scene_renderer/shader_key.hpp"

#include <cstdint>
#include <string>

namespace erhe::scene_renderer {

// Which pass family a draw list serves (doc/erhe/draw_list_renderer.md, R6).
enum class Draw_purpose : uint8_t
{
    color  = 0, // main color fill
    shadow = 1  // shadow map casters (position-only variants)
};

// Mobility class of the registered object (R10a). Skinned is derived from
// mesh->skin and world_space from Item_flags::world_space at registration
// (the WORLD_SPACE_VERTICES variants draw these lists); static / dynamic is
// the caller's choice (everything else is dynamic in the initial scope).
enum class Draw_mobility : uint8_t
{
    static_     = 0,
    dynamic     = 1,
    skinned     = 2,
    world_space = 3
};

// The registration's mobility: skinned and world_space come from the mesh,
// otherwise the caller's choice.
[[nodiscard]] auto get_draw_mobility(const erhe::scene::Mesh& mesh, Draw_mobility requested) -> Draw_mobility;

// The Vertex_space the shader key of a list of this mobility is derived with.
[[nodiscard]] auto get_vertex_space(Draw_mobility mobility) -> Vertex_space;

[[nodiscard]] auto c_str(Draw_purpose  purpose ) -> const char*;
[[nodiscard]] auto c_str(Draw_mobility mobility) -> const char*;

// Identity of a draw list: every entry in a list shares all of these, so a
// list can be drawn with one pipeline / one buffer bind / one multi-draw.
// Mirrors Render_bucket identity (mesh_memory.hpp) plus purpose, mobility
// and layer id, minus the pass environment components, which are supplied
// at resolution time (R13, R21).
class Draw_list_key
{
public:
    Draw_purpose                    purpose             {Draw_purpose::color};
    Draw_mobility                   mobility            {Draw_mobility::dynamic};
    // Blending class of the primitive material (R7,
    // erhe::primitive::get_blending_class(const Material*)).
    erhe::primitive::Blending_class blending            {erhe::primitive::Blending_class::opaque};
    bool                            negative_determinant{false};
    // the Material double_sided property (glTF material.doubleSided): selects a
    // face-culling-disabled pipeline variant, so it has to partition lists.
    bool                            double_sided        {false};
    erhe::primitive::Primitive_mode primitive_mode      {erhe::primitive::Primitive_mode::polygon_fill};
    erhe::scene::Layer_id           layer_id            {0};
    Buffer_set                      buffer_set          {};
    // Primitive-derived shader key components only (no environment):
    //  - color : Shader_key{}.derive(material, vertex_format, vertex_space)
    //  - shadow: USE_SKINNING, WORLD_SPACE_VERTICES and VERTEX_POSITION_ENCODING only (R4)
    Shader_key                      primitive_key       {};
    uint64_t                        primitive_key_hash  {0};

    [[nodiscard]] auto operator==(const Draw_list_key& other) const -> bool;
    [[nodiscard]] auto get_hash () const -> uint64_t;
    [[nodiscard]] auto describe () const -> std::string;
};

class Draw_list_key_hash
{
public:
    [[nodiscard]] auto operator()(const Draw_list_key& key) const noexcept -> std::size_t
    {
        return static_cast<std::size_t>(key.get_hash());
    }
};

} // namespace erhe::scene_renderer
