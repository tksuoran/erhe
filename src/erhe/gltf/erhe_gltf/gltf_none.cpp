#include "gltf_none.hpp"

namespace erhe::gltf {

auto parse_gltf(const Gltf_parse_arguments&) -> Gltf_data
{
    return {};
}

auto query_gltf_device_options(erhe::graphics::Device&) -> Gltf_device_options
{
    return {};
}

auto scan_gltf(std::filesystem::path) -> Gltf_scan
{
    return {};
}

auto sniff_image_mime_type(const std::vector<std::byte>&) -> std::string
{
    return {};
}

auto export_gltf(const Gltf_export_arguments&) -> std::string
{
    return {};
}

auto export_gltf(const erhe::scene::Node&, bool, const erhe::scene::Physics_description*) -> std::string
{
    return {};
}

auto Gltf_export_document::get_byte_count() const -> std::size_t
{
    std::size_t byte_count = bytes.size();
    for (const Gltf_export_file& file : external_files) {
        byte_count += file.bytes.size();
    }
    return byte_count;
}

auto export_gltf_document(const Gltf_export_arguments&, const std::filesystem::path&) -> Gltf_export_document
{
    return {};
}

auto write_gltf_document(const std::filesystem::path&, const Gltf_export_document&) -> bool
{
    return false;
}

auto get_gltf_buffer_file_path(const std::filesystem::path&) -> std::filesystem::path
{
    return {};
}

}
