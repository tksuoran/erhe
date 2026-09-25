#pragma once

#include "erhe_scene/trs_transform.hpp"

#include <cstdint>

namespace editor {

// Per-component transform channel locks (doc/plans/rigging/ik_settings.md
// section 2): `after` with every LOCAL component that `lock_flags` locks
// (Item_flags::lock_translation_x .. lock_scale_z) put back to its `before`
// value. Rotation masking is per Euler XYZ component of the local rotation,
// on the Euler branch of `after` closest to `before` - approximate for large
// deltas, exact in the locked component. Returns `after` unchanged when no
// channel-lock flag is set. Pure: the Transform tool (enforce_channel_locks)
// and the posing verbs (src/editor/rig/bone_pose.hpp) share it.
[[nodiscard]] auto apply_channel_locks(
    const erhe::scene::Trs_transform& before,
    const erhe::scene::Trs_transform& after,
    uint64_t                          lock_flags
) -> erhe::scene::Trs_transform;

} // namespace editor
