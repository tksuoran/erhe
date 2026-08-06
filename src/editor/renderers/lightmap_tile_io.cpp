#include "renderers/lightmap_tile_io.hpp"

#include "erhe_scene/node.hpp"

#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include <cstring>
#include <fstream>

namespace editor {

namespace {

// 64-byte payload header (little-endian, tightly packed).
struct Payload_header
{
    uint32_t magic;
    uint32_t version;
    uint32_t width;
    uint32_t height;
    uint32_t format;
    uint32_t sweeps; // accumulation sweeps behind the payload; 0 = unknown (pre-field payloads)
    uint64_t bake_hash;
    float    bounds_min[3];
    float    bounds_max[3];
    uint32_t reserved1[2];
};
static_assert(sizeof(Payload_header) == 64);

} // anonymous namespace

auto Lightmap_tile_io::directory_for_scene(const std::filesystem::path& scene_path) -> std::filesystem::path
{
    if (scene_path.empty()) {
        return std::filesystem::path{"untitled.lightmap"};
    }
    std::filesystem::path directory = scene_path;
    directory += ".lightmap";
    return directory;
}

auto Lightmap_tile_io::payload_name(const int level, const int ix, const int iz) -> std::string
{
    return fmt::format("tile_L{}_{}_{}.lmt", level, ix, iz);
}

auto Lightmap_tile_io::delete_tile_set(const std::filesystem::path& directory, std::string* const error) -> int
{
    std::error_code ec;
    if (!std::filesystem::exists(directory, ec) || ec) {
        return 0; // nothing saved - the common quiet case
    }
    int removed = 0;
    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator{directory, ec}) {
        if (ec) {
            break;
        }
        if (!entry.is_regular_file(ec) || ec) {
            continue;
        }
        const std::string filename = entry.path().filename().string();
        const bool is_set_file =
            (filename == "manifest.json") ||
            (filename.starts_with("tile_") && filename.ends_with(".lmt"));
        if (!is_set_file) {
            continue; // only set files; leave anything else alone
        }
        std::error_code remove_ec;
        if (std::filesystem::remove(entry.path(), remove_ec) && !remove_ec) {
            ++removed;
        } else if (error != nullptr) {
            *error = fmt::format("cannot remove {}: {}", entry.path().string(), remove_ec.message());
            return -1;
        }
    }
    // Drop the directory itself when the set was all it held.
    std::error_code empty_ec;
    if (std::filesystem::is_empty(directory, empty_ec) && !empty_ec) {
        std::filesystem::remove(directory, empty_ec);
    }
    return removed;
}

auto Lightmap_tile_io::node_path(const erhe::scene::Node* const node) -> std::string
{
    if (node == nullptr) {
        return {};
    }
    std::vector<const erhe::scene::Node*> chain;
    for (const erhe::scene::Node* n = node; n != nullptr;) {
        chain.push_back(n);
        const std::shared_ptr<erhe::Hierarchy> parent = n->get_parent().lock();
        n = dynamic_cast<const erhe::scene::Node*>(parent.get());
    }
    std::string path;
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
        if (!path.empty()) {
            path += '/';
        }
        path += (*it)->get_name();
    }
    return path;
}

auto Lightmap_tile_io::node_index_path(const erhe::scene::Node* const node) -> std::string
{
    if (node == nullptr) {
        return {};
    }
    std::vector<std::size_t> indices;
    for (const erhe::scene::Node* n = node; n != nullptr;) {
        const std::shared_ptr<erhe::Hierarchy> parent = n->get_parent().lock();
        if (!parent) {
            break; // the scene root itself carries no index
        }
        const std::vector<std::shared_ptr<erhe::Hierarchy>>& siblings = parent->get_children();
        std::size_t index = 0;
        for (std::size_t i = 0; i < siblings.size(); ++i) {
            if (siblings[i].get() == n) {
                index = i;
                break;
            }
        }
        indices.push_back(index);
        n = dynamic_cast<const erhe::scene::Node*>(parent.get());
    }
    std::string path;
    for (auto it = indices.rbegin(); it != indices.rend(); ++it) {
        if (!path.empty()) {
            path += '/';
        }
        path += std::to_string(*it);
    }
    return path;
}

auto Lightmap_tile_io::write_manifest(const std::filesystem::path& directory, const Manifest& manifest, std::string* const error) -> bool
{
    std::error_code ec;
    std::filesystem::create_directories(directory, ec);
    if (ec) {
        if (error != nullptr) {
            *error = fmt::format("create_directories({}) failed: {}", directory.string(), ec.message());
        }
        return false;
    }
    nlohmann::json tiles = nlohmann::json::array();
    for (const Tile_entry& tile : manifest.tiles) {
        nlohmann::json regions = nlohmann::json::array();
        for (const Region_entry& region : tile.regions) {
            regions.push_back({
                {"node_path",       region.node_path},
                {"node_index_path", region.node_index_path},
                {"mesh_name",       region.mesh_name},
                {"primitive_index", region.primitive_index},
                {"piece_ordinal",   region.piece_ordinal},
                {"uv_scale_offset", {region.uv_scale_offset.x, region.uv_scale_offset.y, region.uv_scale_offset.z, region.uv_scale_offset.w}}
            });
        }
        tiles.push_back({
            {"id",               tile.id},
            {"level",            tile.level},
            {"ix",               tile.ix},
            {"iz",               tile.iz},
            {"texels_per_meter", tile.texels_per_meter},
            {"bounds_min",       {tile.bounds_min.x, tile.bounds_min.y, tile.bounds_min.z}},
            {"bounds_max",       {tile.bounds_max.x, tile.bounds_max.y, tile.bounds_max.z}},
            {"density_scale",    tile.density_scale},
            {"payload",          tile.payload},
            {"regions",          regions}
        });
    }
    const nlohmann::json root{
        {"version",   manifest.version},
        {"tile_size", manifest.tile_size},
        {"bake_hash", manifest.bake_hash},
        {"tiles",     tiles}
    };
    const std::filesystem::path file_path = directory / "manifest.json";
    std::ofstream stream{file_path, std::ios::binary | std::ios::trunc};
    if (!stream) {
        if (error != nullptr) {
            *error = fmt::format("cannot open {} for writing", file_path.string());
        }
        return false;
    }
    stream << root.dump(2);
    stream.flush();
    if (!stream) {
        if (error != nullptr) {
            *error = fmt::format("write to {} failed", file_path.string());
        }
        return false;
    }
    return true;
}

auto Lightmap_tile_io::read_manifest(const std::filesystem::path& directory, Manifest& out_manifest, std::string* const error) -> bool
{
    const std::filesystem::path file_path = directory / "manifest.json";
    std::ifstream stream{file_path, std::ios::binary};
    if (!stream) {
        if (error != nullptr) {
            *error = fmt::format("cannot open {}", file_path.string());
        }
        return false;
    }
    nlohmann::json root;
    try {
        stream >> root;
        out_manifest = Manifest{};
        out_manifest.version          = root.value("version", 0u);
        if (out_manifest.version != c_manifest_version) {
            if (error != nullptr) {
                *error = fmt::format("{}: unsupported manifest version {}", file_path.string(), out_manifest.version);
            }
            return false;
        }
        out_manifest.tile_size = root.value("tile_size", 0);
        out_manifest.bake_hash = root.value("bake_hash", static_cast<uint64_t>(0));
        for (const nlohmann::json& tile_json : root.value("tiles", nlohmann::json::array())) {
            Tile_entry tile;
            tile.id               = tile_json.value("id", 0);
            tile.level            = tile_json.value("level", 0);
            tile.ix               = tile_json.value("ix", 0);
            tile.iz               = tile_json.value("iz", 0);
            tile.texels_per_meter = tile_json.value("texels_per_meter", 0.0f);
            tile.density_scale    = tile_json.value("density_scale", 1.0f);
            tile.payload          = tile_json.value("payload", std::string{});
            const auto bounds_min = tile_json.value("bounds_min", std::vector<float>{0.0f, 0.0f, 0.0f});
            const auto bounds_max = tile_json.value("bounds_max", std::vector<float>{0.0f, 0.0f, 0.0f});
            if ((bounds_min.size() == 3) && (bounds_max.size() == 3)) {
                tile.bounds_min = glm::vec3{bounds_min[0], bounds_min[1], bounds_min[2]};
                tile.bounds_max = glm::vec3{bounds_max[0], bounds_max[1], bounds_max[2]};
            }
            for (const nlohmann::json& region_json : tile_json.value("regions", nlohmann::json::array())) {
                Region_entry region;
                region.node_path       = region_json.value("node_path", std::string{});
                region.node_index_path = region_json.value("node_index_path", std::string{});
                region.mesh_name       = region_json.value("mesh_name", std::string{});
                region.primitive_index = region_json.value("primitive_index", static_cast<std::size_t>(0));
                region.piece_ordinal   = region_json.value("piece_ordinal", -1);
                const auto scale_offset = region_json.value("uv_scale_offset", std::vector<float>{0.0f, 0.0f, 0.0f, 0.0f});
                if (scale_offset.size() == 4) {
                    region.uv_scale_offset = glm::vec4{scale_offset[0], scale_offset[1], scale_offset[2], scale_offset[3]};
                }
                tile.regions.push_back(std::move(region));
            }
            out_manifest.tiles.push_back(std::move(tile));
        }
    } catch (const std::exception& e) {
        if (error != nullptr) {
            *error = fmt::format("{}: {}", file_path.string(), e.what());
        }
        return false;
    }
    return true;
}

auto Lightmap_tile_io::write_tile_payload(
    const std::filesystem::path&    file_path,
    const int                       width,
    const int                       height,
    const std::span<const uint16_t> rgba16,
    const uint64_t                  bake_hash,
    const glm::vec3&                bounds_min,
    const glm::vec3&                bounds_max,
    const uint32_t                  sweeps,
    std::string* const              error
) -> bool
{
    const std::size_t expected = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u;
    if ((width <= 0) || (height <= 0) || (rgba16.size() != expected)) {
        if (error != nullptr) {
            *error = fmt::format("payload size mismatch: {}x{} needs {} values, got {}", width, height, expected, rgba16.size());
        }
        return false;
    }
    Payload_header header{};
    header.magic     = c_payload_magic;
    header.version   = c_payload_version;
    header.width     = static_cast<uint32_t>(width);
    header.height    = static_cast<uint32_t>(height);
    header.format    = static_cast<uint32_t>(Payload_format::rgba16f);
    header.sweeps    = sweeps;
    header.bake_hash = bake_hash;
    std::memcpy(header.bounds_min, &bounds_min, sizeof(float) * 3);
    std::memcpy(header.bounds_max, &bounds_max, sizeof(float) * 3);

    std::ofstream stream{file_path, std::ios::binary | std::ios::trunc};
    if (!stream) {
        if (error != nullptr) {
            *error = fmt::format("cannot open {} for writing", file_path.string());
        }
        return false;
    }
    stream.write(reinterpret_cast<const char*>(&header), sizeof(header));
    stream.write(reinterpret_cast<const char*>(rgba16.data()), static_cast<std::streamsize>(rgba16.size() * sizeof(uint16_t)));
    stream.flush();
    if (!stream) {
        if (error != nullptr) {
            *error = fmt::format("write to {} failed", file_path.string());
        }
        return false;
    }
    return true;
}

auto Lightmap_tile_io::read_tile_payload(
    const std::filesystem::path& file_path,
    int&                         out_width,
    int&                         out_height,
    std::vector<uint16_t>&       out_rgba16,
    uint64_t* const              out_bake_hash,
    uint32_t* const              out_sweeps,
    std::string* const           error
) -> bool
{
    std::ifstream stream{file_path, std::ios::binary};
    if (!stream) {
        if (error != nullptr) {
            *error = fmt::format("cannot open {}", file_path.string());
        }
        return false;
    }
    Payload_header header{};
    stream.read(reinterpret_cast<char*>(&header), sizeof(header));
    if (!stream || (header.magic != c_payload_magic) || (header.version != c_payload_version)) {
        if (error != nullptr) {
            *error = fmt::format("{}: not a lightmap tile payload (bad magic/version)", file_path.string());
        }
        return false;
    }
    if (header.format != static_cast<uint32_t>(Payload_format::rgba16f)) {
        if (error != nullptr) {
            *error = fmt::format("{}: unsupported payload format {}", file_path.string(), header.format);
        }
        return false;
    }
    if ((header.width == 0) || (header.height == 0) || (header.width > 16384u) || (header.height > 16384u)) {
        if (error != nullptr) {
            *error = fmt::format("{}: implausible payload dimensions {}x{}", file_path.string(), header.width, header.height);
        }
        return false;
    }
    const std::size_t value_count = static_cast<std::size_t>(header.width) * static_cast<std::size_t>(header.height) * 4u;
    out_rgba16.resize(value_count);
    stream.read(reinterpret_cast<char*>(out_rgba16.data()), static_cast<std::streamsize>(value_count * sizeof(uint16_t)));
    if (!stream) {
        if (error != nullptr) {
            *error = fmt::format("{}: truncated payload", file_path.string());
        }
        return false;
    }
    out_width  = static_cast<int>(header.width);
    out_height = static_cast<int>(header.height);
    if (out_bake_hash != nullptr) {
        *out_bake_hash = header.bake_hash;
    }
    if (out_sweeps != nullptr) {
        *out_sweeps = header.sweeps;
    }
    return true;
}

auto Lightmap_tile_io::float_to_half(const float value) -> uint16_t
{
    // Round-to-nearest-even fp32 -> fp16 with inf/nan passthrough and
    // denormal flush (the payload is HDR radiance; values below fp16
    // denormal range are visually black).
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(float));
    const uint32_t sign     = (bits >> 16) & 0x8000u;
    const uint32_t exponent = (bits >> 23) & 0xFFu;
    const uint32_t mantissa = bits & 0x7FFFFFu;
    if (exponent == 0xFFu) {
        return static_cast<uint16_t>(sign | 0x7C00u | (mantissa != 0 ? 0x200u : 0u));
    }
    const int new_exponent = static_cast<int>(exponent) - 127 + 15;
    if (new_exponent >= 0x1F) {
        return static_cast<uint16_t>(sign | 0x7C00u); // overflow -> inf
    }
    if (new_exponent <= 0) {
        return static_cast<uint16_t>(sign); // underflow / denormal -> zero
    }
    uint32_t half = sign | (static_cast<uint32_t>(new_exponent) << 10) | (mantissa >> 13);
    // Round to nearest even on the truncated bits.
    const uint32_t round_bits = mantissa & 0x1FFFu;
    if ((round_bits > 0x1000u) || ((round_bits == 0x1000u) && ((half & 1u) != 0u))) {
        ++half; // may carry into the exponent; that is correct rounding
    }
    return static_cast<uint16_t>(half);
}

}
