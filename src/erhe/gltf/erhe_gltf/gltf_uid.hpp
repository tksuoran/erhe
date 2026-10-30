#pragma once

#include <random>
#include <string>

namespace erhe::gltf {

// One generated glTF 2.1 uid: 16 characters drawn uniformly from [0-9A-Za-z]
// (the uid explainer's recommended charset, about 95 bits). The generator the
// glTF writer uses for every object it exports without a uid
// (doc/editor/scene_serialization.md); a caller that assigns uids ahead of an
// export (the PVS bake) uses it too, and rejects a result that collides with
// an identifier it already has, as the writer does.
[[nodiscard]] auto generate_gltf_uid(std::mt19937_64& rng) -> std::string;

} // namespace erhe::gltf
