#pragma once

#include "erhe_hash/hash.hpp"
#include "erhe_primitive/enums.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene_renderer/mesh_memory.hpp"
#include "erhe_scene_renderer/shader_key.hpp"

#include <cstdint>
#include <string>

namespace erhe::scene_renderer {

// Which pass family a draw list serves (doc/draw_list_renderer_requirements.md, R6).
enum class Draw_purpose : uint8_t
{
    color  = 0, // main color fill
    shadow = 1  // shadow map casters (position-only variants)
};

// Mobility class of the registered object (R10a). Skinned is derived from
// mesh->skin at registration; static / dynamic is the caller's choice
// (everything non-skinned is dynamic in the initial scope).
enum class Draw_mobility : uint8_t
{
    static_  = 0,
    dynamic  = 1,
    skinned  = 2
};

// Blending classification of the primitive material (R7). A primitive with
// no material has no blending mode and is classified translucent, mirroring
// today's Blending_mode_policy handling in bucket_primitives().
enum class Draw_blending : uint8_t
{
    opaque      = 0,
    translucent = 1
};

[[nodiscard]] auto c_str(Draw_purpose  purpose ) -> const char*;
[[nodiscard]] auto c_str(Draw_mobility mobility) -> const char*;
[[nodiscard]] auto c_str(Draw_blending blending) -> const char*;

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
    Draw_blending                   blending            {Draw_blending::opaque};
    bool                            negative_determinant{false};
    erhe::primitive::Primitive_mode primitive_mode      {erhe::primitive::Primitive_mode::polygon_fill};
    erhe::scene::Layer_id           layer_id            {0};
    Buffer_set                      buffer_set          {};
    // Primitive-derived shader key components only (no environment):
    //  - color : Shader_key{}.derive(material, vertex_format, skinned)
    //  - shadow: USE_SKINNING only (R4)
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
