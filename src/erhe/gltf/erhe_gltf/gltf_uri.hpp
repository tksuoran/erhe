#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace erhe::gltf {

// `text` with every byte but RFC 3986's unreserved ones percent-encoded ('/' too).
[[nodiscard]] auto percent_encode_uri_segment(std::string_view text) -> std::string;

// The glTF uri of a file path: each '/'-separated segment of its UTF-8 generic
// form percent-encoded; an absolute path as a file: uri (doc/erhe/gltf.md "File uris").
[[nodiscard]] auto make_gltf_file_uri(const std::filesystem::path& path) -> std::string;

// The file path a glTF uri names, relative as the uri is; nullopt for a data
// uri, another scheme, no path, or a host other than localhost (off Windows).
// The uri splits before the path is percent-decoded: "%23" is no fragment.
[[nodiscard]] auto gltf_uri_to_file_path(std::string_view uri) -> std::optional<std::filesystem::path>;

} // namespace erhe::gltf
