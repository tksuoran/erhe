#pragma once

#include <cstddef>
#include <vector>

namespace erhe::scene {
    class Animation;
    class Animation_sampler;
}

namespace editor {

// Keyframe editing helpers shared by the Animation window and the MCP
// animation tools. All edits operate directly on Animation_sampler storage
// (timestamps + data) using the glTF layout:
//   STEP / LINEAR: one value block of component_count floats per key
//   CUBICSPLINE:   [in_tangent, value, out_tangent] blocks per key
// Channel::value_offset already points at the value element inside a key
// block (the glTF loader sets it to component_count for CUBICSPLINE), so
// value indexing is uniform across interpolation modes.

// Snapshot of one sampler's keyframe storage, used for undo/redo.
class Animation_sampler_state
{
public:
    std::size_t        sampler_index{0};
    std::vector<float> timestamps;
    std::vector<float> data;
};

[[nodiscard]] auto capture_sampler_state(const erhe::scene::Animation& animation, std::size_t sampler_index) -> Animation_sampler_state;
void restore_sampler_state(erhe::scene::Animation& animation, const Animation_sampler_state& state);

// Floats per key in sampler.data (0 when the sampler has no keys).
[[nodiscard]] auto get_sampler_value_stride(const erhe::scene::Animation_sampler& sampler) -> std::size_t;

// Index into sampler.data of the value of (key, component) for a channel.
[[nodiscard]] auto get_key_value_index(
    const erhe::scene::Animation& animation,
    std::size_t                   channel_index,
    std::size_t                   key_index,
    std::size_t                   component
) -> std::size_t;

[[nodiscard]] auto get_keyframe_value(
    const erhe::scene::Animation& animation,
    std::size_t                   channel_index,
    std::size_t                   key_index,
    std::size_t                   component
) -> float;

void set_keyframe_value(
    erhe::scene::Animation& animation,
    std::size_t             channel_index,
    std::size_t             key_index,
    std::size_t             component,
    float                   value
);

// Moves a keyframe in time, clamped between its neighbor keys so timestamps
// stay strictly increasing. Returns the applied (possibly clamped) time.
auto set_keyframe_time(
    erhe::scene::Animation& animation,
    std::size_t             sampler_index,
    std::size_t             key_index,
    float                   time
) -> float;

// Inserts a keyframe at the given time, evaluating every channel that uses
// the sampler so the curve shape is preserved (CUBICSPLINE tangents are
// estimated from the local derivative). If a key already exists at the time
// (within a small epsilon) no key is added. Returns the key index of the
// (new or existing) key.
auto insert_keyframe(
    erhe::scene::Animation& animation,
    std::size_t             sampler_index,
    float                   time
) -> std::size_t;

// Deletes a keyframe. Refuses to delete the last remaining key of a sampler
// (returns false).
auto delete_keyframe(
    erhe::scene::Animation& animation,
    std::size_t             sampler_index,
    std::size_t             key_index
) -> bool;

// Resets the cached seek position of every channel; must be called after any
// edit that changes key count or ordering.
void reset_channel_seek_state(erhe::scene::Animation& animation);

// True when the (channel, component) curve actually changes over time:
// any key value differs from the first key's value, or (CUBICSPLINE with
// more than one key) any tangent is non-zero. Constant curves - common in
// glTF exports that key every node - return false.
[[nodiscard]] auto is_component_animated(
    const erhe::scene::Animation& animation,
    std::size_t                   channel_index,
    std::size_t                   component
) -> bool;

// True when any component of the channel is animated.
[[nodiscard]] auto is_channel_animated(
    const erhe::scene::Animation& animation,
    std::size_t                   channel_index
) -> bool;

}
