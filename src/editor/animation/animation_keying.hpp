#pragma once

#include <memory>
#include <vector>

namespace erhe::scene {
    class Animation;
    class Node;
}

namespace editor {

class Operation;

// LightWave-style keying entry points shared by autokey
// (Transform_tool::record_transform_operation), the Animation window's
// Create Key / Delete Key buttons and the MCP keying tools.

// Autokey modes, after LightWave Layout's Auto Key. "Modified" keys only the
// paths (translation / rotation / scale) an edit actually changed; note that
// a glTF channel keys a whole path, so per-axis granularity (LightWave's
// X/Y/Z/H/P/B channels) does not apply.
enum class Autokey_mode : int {
    off            = 0,
    modified_paths = 1,
    all_paths      = 2
};

[[nodiscard]] auto c_str(Autokey_mode mode) -> const char*;

// One node to key, with the paths to write.
class Keying_request
{
public:
    std::shared_ptr<erhe::scene::Node> node;
    bool key_translation{false};
    bool key_rotation   {false};
    bool key_scale      {false};
};

// Keys each requested node's current TRS values at `time` into `animation`,
// creating missing channels (and their LINEAR samplers) on demand. The edits
// are applied immediately; the returned operation holds before/after
// snapshots for undo/redo (execute re-applies the already-current "after"
// state). Returns nullptr when the requests key nothing.
[[nodiscard]] auto key_nodes(
    const std::shared_ptr<erhe::scene::Animation>& animation,
    const std::vector<Keying_request>&             requests,
    float                                          time
) -> std::shared_ptr<Operation>;

// Deletes the keys at (approximately) `time` from every channel of
// `animation` that targets one of `nodes`. Same immediate-apply +
// snapshot-operation contract as key_nodes(); returns nullptr when no key
// was deleted.
[[nodiscard]] auto delete_keys(
    const std::shared_ptr<erhe::scene::Animation>&         animation,
    const std::vector<std::shared_ptr<erhe::scene::Node>>& nodes,
    float                                                  time
) -> std::shared_ptr<Operation>;

}
