#pragma once

#include <cstdint>

namespace erhe::scene_renderer {

// What a set of caster vertex extents was gathered from: two sets with the
// same identity are equal element for element (Draw_list_scene gathers them
// from the same scene state and selection; Shadow_bounds::extents_identity).
// serial 0: unknown (a set the caller gathers per frame): nothing derived
// from it is reused.
class Caster_extents_identity
{
public:
    [[nodiscard]] auto is_known() const -> bool { return serial != 0; }
    [[nodiscard]] auto operator==(const Caster_extents_identity& other) const -> bool = default;

    uint64_t scene_instance{0};
    uint64_t serial        {0};
};

} // namespace erhe::scene_renderer
