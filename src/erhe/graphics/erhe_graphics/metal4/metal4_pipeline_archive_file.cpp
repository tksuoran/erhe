#include "erhe_graphics/metal4/metal4_pipeline_archive_file.hpp"

#include <fmt/format.h>

#include <fstream>
#include <iterator>
#include <system_error>
#include <vector>

#include <cerrno>
#include <csignal>
#include <cstdlib>

#include <unistd.h>

namespace erhe::graphics {

namespace {

constexpr std::string_view c_manifest_header = "erhe metal4 pipeline archive 1\n";

auto fnv1a_64(const std::span<const uint8_t> bytes) -> uint64_t
{
    uint64_t hash = 0xcbf29ce484222325ull;
    for (const uint8_t byte : bytes) {
        hash ^= static_cast<uint64_t>(byte);
        hash *= 0x100000001b3ull;
    }
    return hash;
}

auto read_file(const std::filesystem::path& path, std::vector<uint8_t>& out_bytes) -> bool
{
    std::error_code ec;
    const std::uintmax_t size = std::filesystem::file_size(path, ec);
    if (ec) {
        return false;
    }
    std::ifstream file{path, std::ios::binary};
    if (!file) {
        return false;
    }
    out_bytes.resize(static_cast<std::size_t>(size));
    file.read(reinterpret_cast<char*>(out_bytes.data()), static_cast<std::streamsize>(size));
    return static_cast<std::uintmax_t>(file.gcount()) == size;
}

auto read_text_file(const std::filesystem::path& path, std::string& out_text) -> bool
{
    std::ifstream file{path, std::ios::binary};
    if (!file) {
        return false;
    }
    out_text.assign(std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{});
    return !file.bad();
}

} // anonymous namespace

auto make_metal4_pipeline_archive_path(
    const std::filesystem::path& directory,
    const std::string_view       gpu_name,
    const std::string_view       os_build
) -> std::filesystem::path
{
    const std::span<const uint8_t> name_bytes{reinterpret_cast<const uint8_t*>(gpu_name.data()), gpu_name.size()};
    return directory / fmt::format("metal4_{:016x}_{}.mtl4archive", fnv1a_64(name_bytes), os_build);
}

auto get_metal4_pipeline_archive_manifest_path(const std::filesystem::path& archive_path) -> std::filesystem::path
{
    std::filesystem::path manifest_path = archive_path;
    manifest_path += ".manifest";
    return manifest_path;
}

auto make_metal4_pipeline_archive_manifest(const std::span<const uint8_t> archive_bytes) -> std::string
{
    return fmt::format("{}size {}\nfnv1a64 {:016x}\n", c_manifest_header, archive_bytes.size(), fnv1a_64(archive_bytes));
}

auto check_metal4_pipeline_archive(const std::filesystem::path& archive_path, std::string& out_reason) -> bool
{
    std::error_code ec;
    if (!std::filesystem::exists(archive_path, ec)) {
        out_reason = "no archive";
        return false;
    }
    std::string manifest;
    if (!read_text_file(get_metal4_pipeline_archive_manifest_path(archive_path), manifest)) {
        out_reason = "no manifest";
        return false;
    }
    std::vector<uint8_t> archive_bytes;
    if (!read_file(archive_path, archive_bytes)) {
        out_reason = "archive not readable";
        return false;
    }
    if (manifest != make_metal4_pipeline_archive_manifest(archive_bytes)) {
        out_reason = "archive does not match its manifest";
        return false;
    }
    return true;
}

auto install_metal4_pipeline_archive(
    const std::filesystem::path& temporary_archive_path,
    const std::filesystem::path& archive_path,
    std::string&                 out_error
) -> bool
{
    std::error_code ec;
    std::vector<uint8_t> archive_bytes;
    if (!read_file(temporary_archive_path, archive_bytes) || archive_bytes.empty()) {
        out_error = "serialized archive not readable";
        std::filesystem::remove(temporary_archive_path, ec);
        return false;
    }
    const std::string manifest = make_metal4_pipeline_archive_manifest(archive_bytes);

    const std::filesystem::path manifest_path = get_metal4_pipeline_archive_manifest_path(archive_path);
    std::filesystem::path temporary_manifest_path = manifest_path;
    temporary_manifest_path += fmt::format(".{}.tmp", static_cast<long>(getpid()));
    {
        std::ofstream file{temporary_manifest_path, std::ios::binary | std::ios::trunc};
        file.write(manifest.data(), static_cast<std::streamsize>(manifest.size()));
        file.close();
        if (!file) {
            out_error = fmt::format("{} not writable", temporary_manifest_path.string());
            std::filesystem::remove(temporary_manifest_path, ec);
            std::filesystem::remove(temporary_archive_path, ec);
            return false;
        }
    }

    std::filesystem::remove(manifest_path, ec);
    // The rename replaces the directory entry only: an archive a running
    // process opened from the old file keeps that file.
    std::filesystem::rename(temporary_archive_path, archive_path, ec);
    if (ec) {
        out_error = ec.message();
        std::filesystem::remove(temporary_archive_path, ec);
        std::filesystem::remove(temporary_manifest_path, ec);
        return false;
    }
    std::filesystem::rename(temporary_manifest_path, manifest_path, ec);
    if (ec) {
        out_error = ec.message();
        std::filesystem::remove(temporary_manifest_path, ec);
        return false;
    }
    return true;
}

auto remove_stale_metal4_pipeline_archive_temporaries(const std::filesystem::path& archive_path) -> std::size_t
{
    const std::filesystem::path directory = archive_path.parent_path().empty() ? std::filesystem::path{"."} : archive_path.parent_path();
    const std::string archive_prefix  = archive_path.filename().string() + ".";
    const std::string manifest_prefix = get_metal4_pipeline_archive_manifest_path(archive_path).filename().string() + ".";
    constexpr std::string_view suffix = ".tmp";
    std::error_code ec;
    std::size_t removed = 0;
    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator{directory, ec}) {
        const std::string name = entry.path().filename().string();
        if ((name.size() <= suffix.size()) || (name.compare(name.size() - suffix.size(), suffix.size(), suffix) != 0)) {
            continue;
        }
        std::size_t prefix_size = 0;
        if (name.rfind(manifest_prefix, 0) == 0) {
            prefix_size = manifest_prefix.size();
        } else if (name.rfind(archive_prefix, 0) == 0) {
            prefix_size = archive_prefix.size();
        } else {
            continue;
        }
        const std::string pid_text = name.substr(prefix_size, name.size() - prefix_size - suffix.size());
        if (pid_text.empty() || (pid_text.find_first_not_of("0123456789") != std::string::npos)) {
            continue;
        }
        const long pid = std::strtol(pid_text.c_str(), nullptr, 10);
        if (pid <= 0) {
            continue;
        }
        const bool alive = (kill(static_cast<pid_t>(pid), 0) == 0) || (errno == EPERM);
        if (alive) {
            continue;
        }
        if (std::filesystem::remove(entry.path(), ec)) {
            ++removed;
        }
    }
    return removed;
}

} // namespace erhe::graphics
