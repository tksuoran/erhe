#pragma once

#include <string>
#include <string_view>

namespace erhe::gltf {

// A glTF 2.1 uid derived from seed: 16 characters of [0-9A-Za-z], the same
// for the same seed in every session (doc/editor/scene_serialization.md
// "Unique IDs"). The caller makes the seed name what the uid identifies.
[[nodiscard]] auto make_derived_gltf_uid(std::string_view seed) -> std::string;

} // namespace erhe::gltf
