#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>

namespace erhe::graphics {

// The on-disk side of the Metal 4 pipeline archive (Pipeline_compiler,
// metal4_pipeline_compiler.hpp). The functions take no Metal objects, so
// they are tested without a device (erhe_graphics_tests).
//
// MTL::Device::newArchive crashes (SIGSEGV inside Metal) on a file that is
// not an archive, so an archive is opened only when a manifest written
// beside it names its exact size and content hash: a file that is absent,
// truncated, partially replaced or not written by erhe is ignored.

// <directory>/metal4_<FNV-1a 64 of the GPU name, hex>_<OS build>.mtl4archive:
// one archive per GPU and OS build (the OS build names the Metal compiler
// the GPU binaries came from), so an archive of another GPU or OS is never
// opened.
[[nodiscard]] auto make_metal4_pipeline_archive_path(
    const std::filesystem::path& directory,
    std::string_view             gpu_name,
    std::string_view             os_build
) -> std::filesystem::path;

// <archive path>.manifest
[[nodiscard]] auto get_metal4_pipeline_archive_manifest_path(const std::filesystem::path& archive_path) -> std::filesystem::path;

// "erhe metal4 pipeline archive 1\nsize <bytes>\nfnv1a64 <16 hex digits>\n"
[[nodiscard]] auto make_metal4_pipeline_archive_manifest(std::span<const uint8_t> archive_bytes) -> std::string;

// True when the archive and its manifest exist and the manifest matches the
// archive's bytes; otherwise false with the reason in out_reason.
[[nodiscard]] auto check_metal4_pipeline_archive(
    const std::filesystem::path& archive_path,
    std::string&                 out_reason
) -> bool;

// Moves a freshly serialized archive into place and writes its manifest:
// the old manifest goes first, so an interrupted install leaves an archive
// without a matching manifest (ignored at the next start), never a
// mismatched pair that would be opened. Returns false with out_error set
// on failure (the temporary file is removed).
[[nodiscard]] auto install_metal4_pipeline_archive(
    const std::filesystem::path& temporary_archive_path,
    const std::filesystem::path& archive_path,
    std::string&                 out_error
) -> bool;

// Removes the temporary files a crashed write left beside the archive
// (<archive or manifest file name>.<pid>.tmp) whose process no longer runs;
// the files of a live process (another editor writing at the same time)
// are kept. Returns the number removed.
[[nodiscard]] auto remove_stale_metal4_pipeline_archive_temporaries(const std::filesystem::path& archive_path) -> std::size_t;

} // namespace erhe::graphics
