#pragma once

#include <string>
#include <string_view>

namespace editor {

// The side of the body a bone name marks (doc/plans/rigging/skeleton_editing.md
// R11).
enum class Bone_side : unsigned int {
    none  = 0,
    left  = 1,
    right = 2
};

// The side a bone name marks. Recognized at the end of the name, after an
// optional trailing index group (a '.' or '_' followed by digits only, such as
// ".001" or "_1", which the flip keeps):
// - the separator suffixes ".L" / ".R", "_L" / "_R", ".l" / ".r", "_l" / "_r"
//   (the separator is part of the suffix, so "L" alone is not a side);
// - "Left" / "Right" as a whole trailing word: the whole name, or preceded by
//   a character that is not a letter, or by a lowercase letter (camelCase,
//   "handLeft");
// - "left" / "right" as a whole trailing word: the whole name, or preceded by
//   a character that is neither a letter nor a digit ("hand_left").
[[nodiscard]] auto bone_side(std::string_view name) -> Bone_side;

// The name with its side flipped, in the same spelling family (".L" <-> ".R",
// "_l" <-> "_r", "Left" <-> "Right", "left" <-> "right"), the trailing index
// group kept. A name without a side is returned unchanged. The flip is an
// involution: flip_side_name(flip_side_name(n)) == n.
[[nodiscard]] auto flip_side_name(std::string_view name) -> std::string;

}
