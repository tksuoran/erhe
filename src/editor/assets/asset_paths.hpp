#pragma once

#include <filesystem>
#include <string>

namespace editor {

// Shared path conventions for asset identity (asset-manager plan D1).
// Every asset-key / container-registry path conversion goes through these
// helpers so the normalization cannot drift between subsystems (it used to
// be ad-hoc in prefab_library.cpp / gltf.cpp).

// Runtime registry form: weakly_canonical absolute path. Falls back to the
// input path unchanged when canonicalization fails (nonexistent drive etc.)
// - the Prefab_library precedent.
[[nodiscard]] auto normalize_asset_path(const std::filesystem::path& path) -> std::filesystem::path;

// Portable display / serialization form: generic (forward-slash) string.
[[nodiscard]] auto asset_path_to_string(const std::filesystem::path& path) -> std::string;

}
