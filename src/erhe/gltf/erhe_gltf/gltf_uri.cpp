#include "erhe_gltf/gltf_uri.hpp"

#include "erhe_file/file.hpp"

#include <cstddef>

namespace erhe::gltf {

namespace {

[[nodiscard]] auto is_alpha(const char c) -> bool
{
    return ((c >= 'A') && (c <= 'Z')) || ((c >= 'a') && (c <= 'z'));
}

[[nodiscard]] auto is_digit(const char c) -> bool
{
    return (c >= '0') && (c <= '9');
}

[[nodiscard]] auto hex_value(const char c) -> int
{
    if (is_digit(c)) {
        return c - '0';
    }
    if ((c >= 'A') && (c <= 'F')) {
        return 10 + (c - 'A');
    }
    if ((c >= 'a') && (c <= 'f')) {
        return 10 + (c - 'a');
    }
    return -1;
}

// A '%' not followed by two hex digits stays as it is.
[[nodiscard]] auto percent_decode(const std::string_view text) -> std::string
{
    std::string decoded;
    decoded.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        if ((text[i] == '%') && ((i + 2) < text.size())) {
            const int high = hex_value(text[i + 1]);
            const int low  = hex_value(text[i + 2]);
            if ((high >= 0) && (low >= 0)) {
                decoded.push_back(static_cast<char>((high << 4) | low));
                i += 2;
                continue;
            }
        }
        decoded.push_back(text[i]);
    }
    return decoded;
}

[[nodiscard]] auto equals_ignoring_case(const std::string_view a, const std::string_view b) -> bool
{
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        const char ca = ((a[i] >= 'A') && (a[i] <= 'Z')) ? static_cast<char>(a[i] - 'A' + 'a') : a[i];
        const char cb = ((b[i] >= 'A') && (b[i] <= 'Z')) ? static_cast<char>(b[i] - 'A' + 'a') : b[i];
        if (ca != cb) {
            return false;
        }
    }
    return true;
}

// The scheme of an RFC 3986 uri: letters, digits, '+', '-', '.' up to a ':' before any '/', '?' or '#'.
[[nodiscard]] auto get_scheme_length(const std::string_view uri) -> std::size_t
{
    if (uri.empty() || !is_alpha(uri[0])) {
        return 0;
    }
    for (std::size_t i = 1; i < uri.size(); ++i) {
        const char c = uri[i];
        if (c == ':') {
            return i;
        }
        if (!is_alpha(c) && !is_digit(c) && (c != '+') && (c != '-') && (c != '.')) {
            return 0;
        }
    }
    return 0;
}

} // anonymous namespace

auto percent_encode_uri_segment(const std::string_view text) -> std::string
{
    static constexpr const char* hex = "0123456789ABCDEF";
    std::string encoded;
    encoded.reserve(text.size());
    for (const char c : text) {
        const unsigned char byte = static_cast<unsigned char>(c);
        const bool unreserved = is_alpha(c) || is_digit(c) || (c == '-') || (c == '.') || (c == '_') || (c == '~');
        if (unreserved) {
            encoded.push_back(c);
        } else {
            encoded.push_back('%');
            encoded.push_back(hex[(byte >> 4) & 0xFu]);
            encoded.push_back(hex[byte & 0xFu]);
        }
    }
    return encoded;
}

namespace {

// Each '/'-separated segment of `text` percent-encoded, the separators kept.
[[nodiscard]] auto encode_segments(const std::string_view text) -> std::string
{
    std::string uri;
    std::size_t start = 0;
    while (true) {
        const std::size_t slash = text.find('/', start);
        const std::string_view segment = text.substr(start, (slash == std::string_view::npos) ? std::string_view::npos : (slash - start));
        uri += percent_encode_uri_segment(segment);
        if (slash == std::string_view::npos) {
            break;
        }
        uri.push_back('/');
        start = slash + 1;
    }
    return uri;
}

[[nodiscard]] auto is_drive_prefix(const std::string_view text) -> bool
{
    return (text.size() >= 2) && is_alpha(text[0]) && (text[1] == ':') && ((text.size() == 2) || (text[2] == '/'));
}

} // anonymous namespace

auto make_gltf_file_uri(const std::filesystem::path& path) -> std::string
{
    const std::u8string u8_text = path.generic_u8string();
    const std::string_view text{reinterpret_cast<const char*>(u8_text.data()), u8_text.size()};
    if (text.starts_with("//")) { // UNC: the server is the host
        return "file:" + encode_segments(text);
    }
    if (text.starts_with("/")) {
        return "file://" + encode_segments(text);
    }
    if (is_drive_prefix(text)) {
        return "file:///" + std::string{text.substr(0, 2)} + encode_segments(text.substr(2));
    }
    return encode_segments(text);
}

auto gltf_uri_to_file_path(const std::string_view uri) -> std::optional<std::filesystem::path>
{
    std::string_view rest = uri;
    const std::size_t scheme_length = get_scheme_length(rest);
    // A one-letter "scheme" is a drive letter ("C:/models/a.glb").
    if (scheme_length > 1) {
        const std::string_view scheme = rest.substr(0, scheme_length);
        if (!equals_ignoring_case(scheme, "file")) {
            return std::nullopt;
        }
        rest = rest.substr(scheme_length + 1);
    }
    std::string host;
    if (rest.starts_with("//")) {
        const std::size_t authority_end = rest.find_first_of("/?#", 2);
        host = percent_decode(rest.substr(2, (authority_end == std::string_view::npos) ? std::string_view::npos : (authority_end - 2)));
        rest = (authority_end == std::string_view::npos) ? std::string_view{} : rest.substr(authority_end);
        if (equals_ignoring_case(host, "localhost")) {
            host.clear();
        }
    }
    const std::string_view path = rest.substr(0, rest.find_first_of("?#"));
    if (path.empty()) {
        return std::nullopt;
    }
    std::string decoded = percent_decode(path);
    if (!host.empty()) {
#if defined(_WIN32)
        return erhe::file::from_string("//" + host + decoded); // UNC
#else
        return std::nullopt; // a file on another host
#endif
    }
    // "file:///C:/models/a.glb" names "C:/models/a.glb".
    if ((scheme_length > 1) && (decoded.size() >= 3) && (decoded[0] == '/') && is_drive_prefix(std::string_view{decoded}.substr(1))) {
        decoded.erase(0, 1);
    }
    return erhe::file::from_string(decoded);
}

} // namespace erhe::gltf
