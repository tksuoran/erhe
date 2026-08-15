#pragma once

#include "erhe_scene_renderer/draw_list_entry.hpp"
#include "erhe_scene_renderer/draw_list_key.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace erhe::graphics {
    class Reloadable_shader_stages;
}

namespace erhe::scene_renderer {

// Shadow caster sub-variant (doc/draw_list_renderer_requirements.md R4a):
// which forced VARIANT bits the shadow pass draws with. Selected by the
// caller per shadow pass, exactly as Shadow_renderer chooses today.
enum class Shadow_sub_variant : uint8_t
{
    depth_only          = 0, // directional / spot, depth technique
    depth_only_distance = 1, // directional / spot, distance technique (VARIANT_SHADOW_DISTANCE)
    cube                = 2, // point light cube faces (VARIANT_SHADOW_CUBE)
    count               = 3
};

[[nodiscard]] auto c_str(Shadow_sub_variant sub_variant) -> const char*;

enum class Draw_blending_selection : uint8_t
{
    opaque_only      = 0,
    translucent_only = 1,
    both             = 2  // opaque lists first, then translucent (R7)
};

class Draw_statistics
{
public:
    std::size_t draw_list_count{0}; // lists that produced at least one draw
    std::size_t entry_count    {0}; // entries drawn (after flag filtering)
    std::size_t draw_call_count{0}; // multi-draw submissions (chunks)
};

// Resolved shader stages for one color view configuration (R19).
class Draw_list_color_resolution
{
public:
    uint16_t                                        multiview_count{0}; // key value: 0 single view, N >= 2 multiview
    const erhe::graphics::Reloadable_shader_stages* stages         {nullptr};
};

// A group of entries that share one Draw_list_key and can therefore be
// drawn with one pipeline, one buffer bind and one multi-draw
// (doc/draw_list_renderer_requirements.md R13/R14). Owned by
// Draw_list_scene; entries are appended on registration and swap-removed on
// unregistration (Draw_list_scene patches the moved entry's owner record).
//
// Resolved shader stages are cached per list (R17): color per view
// configuration (invalidated wholesale by an environment configuration
// change, R18), shadow per sub-variant (never invalidated - the shadow key
// has no environment). Lazily filled on first use for configurations not
// resolved at registration (R19 / R4a one-off paths).
class Draw_list
{
public:
    Draw_list_key                                     key;
    std::vector<Draw_list_entry>                      entries;

    std::vector<Draw_list_color_resolution>           color_resolutions;
    std::array<
        const erhe::graphics::Reloadable_shader_stages*,
        static_cast<std::size_t>(Shadow_sub_variant::count)
    >                                                 shadow_resolutions{nullptr, nullptr, nullptr};
    // Set when a resolution attempt failed (no variant); avoids re-logging
    // and re-trying every frame.
    bool                                              color_resolution_failed {false};
    bool                                              shadow_resolution_failed{false};
};

} // namespace erhe::scene_renderer
