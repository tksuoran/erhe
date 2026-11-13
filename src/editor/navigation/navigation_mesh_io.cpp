#include "navigation/navigation_mesh_io.hpp"
#include "navigation/navigation_mesh.hpp"

#include <DetourAlloc.h>
#include <DetourNavMesh.h>

#include <fmt/format.h>

#include <algorithm>
#include <cstring>
#include <type_traits>

namespace editor {

namespace {

// "ERNV"; native byte order (little endian on every erhe target).
constexpr uint32_t c_magic   = 0x564e5245u;
constexpr uint32_t c_version = c_navigation_mesh_format_version;

class Byte_writer
{
public:
    explicit Byte_writer(std::vector<uint8_t>& bytes) : m_bytes{bytes} {}

    template <typename T>
    void write(const T& value)
    {
        static_assert(std::is_trivially_copyable_v<T>);
        const std::size_t offset = m_bytes.size();
        m_bytes.resize(offset + sizeof(T));
        std::memcpy(m_bytes.data() + offset, &value, sizeof(T));
    }

    void write_vec3(const glm::vec3& value)
    {
        write(value.x);
        write(value.y);
        write(value.z);
    }

    void write_string(const std::string& value)
    {
        write(static_cast<uint32_t>(value.size()));
        write_bytes(value.data(), value.size());
    }

    void write_bytes(const void* data, const std::size_t size)
    {
        const std::size_t offset = m_bytes.size();
        m_bytes.resize(offset + size);
        if (size > 0) {
            std::memcpy(m_bytes.data() + offset, data, size);
        }
    }

private:
    std::vector<uint8_t>& m_bytes;
};

class Byte_reader
{
public:
    explicit Byte_reader(const std::span<const uint8_t> bytes) : m_bytes{bytes} {}

    template <typename T>
    [[nodiscard]] auto read(T& value) -> bool
    {
        static_assert(std::is_trivially_copyable_v<T>);
        if ((m_bytes.size() - m_offset) < sizeof(T)) {
            return false;
        }
        std::memcpy(&value, m_bytes.data() + m_offset, sizeof(T));
        m_offset += sizeof(T);
        return true;
    }

    [[nodiscard]] auto read_vec3(glm::vec3& value) -> bool
    {
        return read(value.x) && read(value.y) && read(value.z);
    }

    [[nodiscard]] auto read_string(std::string& value) -> bool
    {
        uint32_t                 size = 0;
        std::span<const uint8_t> data;
        if (!read(size) || !read_span(size, data)) {
            return false;
        }
        value.assign(reinterpret_cast<const char*>(data.data()), data.size());
        return true;
    }

    [[nodiscard]] auto read_span(const std::size_t size, std::span<const uint8_t>& out) -> bool
    {
        if ((m_bytes.size() - m_offset) < size) {
            return false;
        }
        out = m_bytes.subspan(m_offset, size);
        m_offset += size;
        return true;
    }

    [[nodiscard]] auto at_end() const -> bool
    {
        return m_offset == m_bytes.size();
    }

private:
    std::span<const uint8_t> m_bytes;
    std::size_t              m_offset{0};
};

void write_settings(Byte_writer& writer, const Navigation_build_settings& settings)
{
    writer.write(settings.cell_size);
    writer.write(settings.cell_height);
    writer.write(settings.agent_height);
    writer.write(settings.agent_radius);
    writer.write(settings.agent_max_climb);
    writer.write(settings.agent_max_slope);
    writer.write(settings.region_min_size);
    writer.write(settings.region_merge_size);
    writer.write(static_cast<uint32_t>(settings.partitioning));
    writer.write(settings.edge_max_length);
    writer.write(settings.edge_max_error);
    writer.write(static_cast<int32_t>(settings.verts_per_poly));
    writer.write(settings.detail_sample_distance);
    writer.write(settings.detail_sample_max_error);
    writer.write(settings.tile_size);
    writer.write_vec3(settings.region_min);
    writer.write_vec3(settings.region_max);
    writer.write(settings.seed_search_radius);
    writer.write(settings.seed_search_height);
}

[[nodiscard]] auto read_settings(Byte_reader& reader, Navigation_build_settings& settings) -> bool
{
    uint32_t partitioning   = 0;
    int32_t  verts_per_poly = 0;
    const bool ok =
        reader.read(settings.cell_size) &&
        reader.read(settings.cell_height) &&
        reader.read(settings.agent_height) &&
        reader.read(settings.agent_radius) &&
        reader.read(settings.agent_max_climb) &&
        reader.read(settings.agent_max_slope) &&
        reader.read(settings.region_min_size) &&
        reader.read(settings.region_merge_size) &&
        reader.read(partitioning) &&
        reader.read(settings.edge_max_length) &&
        reader.read(settings.edge_max_error) &&
        reader.read(verts_per_poly) &&
        reader.read(settings.detail_sample_distance) &&
        reader.read(settings.detail_sample_max_error) &&
        reader.read(settings.tile_size) &&
        reader.read_vec3(settings.region_min) &&
        reader.read_vec3(settings.region_max) &&
        reader.read(settings.seed_search_radius) &&
        reader.read(settings.seed_search_height);
    if (!ok || (partitioning > static_cast<uint32_t>(Navigation_partitioning::layers))) {
        return false;
    }
    settings.partitioning   = static_cast<Navigation_partitioning>(partitioning);
    settings.verts_per_poly = verts_per_poly;
    return true;
}

void write_record(Byte_writer& writer, const Navigation_build_record& record)
{
    writer.write(record.seed_search_radius);
    writer.write(record.seed_search_height);
    writer.write(static_cast<uint32_t>(record.seeds.size()));
    for (const Navigation_record_seed& seed : record.seeds) {
        writer.write_string(seed.uid);
        writer.write_string(seed.name);
        writer.write_vec3(seed.point);
        writer.write_vec3(seed.result.seed);
        writer.write(static_cast<uint8_t>(seed.result.found ? 1 : 0));
        writer.write(seed.result.polygon);
        writer.write_vec3(seed.result.point);
        writer.write_string(seed.result.reason);
    }
    writer.write(static_cast<uint32_t>(record.input.size()));
    for (const Navigation_record_input& input : record.input) {
        writer.write_string(input.uid);
        for (int column = 0; column < 4; ++column) {
            for (int row = 0; row < 4; ++row) {
                writer.write(input.root_from_node[column][row]);
            }
        }
        writer.write(static_cast<uint32_t>(input.shape_hashes.size()));
        for (const Navigation_shape_hash& hash : input.shape_hashes) {
            writer.write(static_cast<uint8_t>(hash.known ? 1 : 0));
            writer.write_bytes(hash.bytes.data(), hash.bytes.size());
        }
    }
}

[[nodiscard]] auto read_record(Byte_reader& reader, Navigation_build_record& record) -> bool
{
    record = Navigation_build_record{};
    uint32_t seed_count = 0;
    if (!reader.read(record.seed_search_radius) || !reader.read(record.seed_search_height) || !reader.read(seed_count)) {
        return false;
    }
    for (uint32_t i = 0; i < seed_count; ++i) {
        Navigation_record_seed seed{};
        uint8_t                found = 0;
        const bool ok =
            reader.read_string(seed.uid) &&
            reader.read_string(seed.name) &&
            reader.read_vec3(seed.point) &&
            reader.read_vec3(seed.result.seed) &&
            reader.read(found) &&
            reader.read(seed.result.polygon) &&
            reader.read_vec3(seed.result.point) &&
            reader.read_string(seed.result.reason);
        if (!ok) {
            return false;
        }
        seed.result.found = (found != 0);
        record.seeds.push_back(std::move(seed));
    }
    uint32_t input_count = 0;
    if (!reader.read(input_count)) {
        return false;
    }
    for (uint32_t i = 0; i < input_count; ++i) {
        Navigation_record_input input{};
        if (!reader.read_string(input.uid)) {
            return false;
        }
        for (int column = 0; column < 4; ++column) {
            for (int row = 0; row < 4; ++row) {
                if (!reader.read(input.root_from_node[column][row])) {
                    return false;
                }
            }
        }
        uint32_t hash_count = 0;
        if (!reader.read(hash_count)) {
            return false;
        }
        for (uint32_t h = 0; h < hash_count; ++h) {
            Navigation_shape_hash    hash{};
            uint8_t                  known = 0;
            std::span<const uint8_t> bytes;
            if (!reader.read(known) || !reader.read_span(hash.bytes.size(), bytes)) {
                return false;
            }
            hash.known = (known != 0);
            std::copy(bytes.begin(), bytes.end(), hash.bytes.begin());
            input.shape_hashes.push_back(hash);
        }
        record.input.push_back(std::move(input));
    }
    return true;
}

[[nodiscard]] auto fail(std::string& out_error, std::string message) -> std::unique_ptr<Navigation_mesh>
{
    out_error = std::move(message);
    return {};
}

} // anonymous namespace

auto write_navigation_mesh(const Navigation_mesh& navigation_mesh, const Navigation_build_record& record) -> std::vector<uint8_t>
{
    std::vector<uint8_t> bytes;
    Byte_writer          writer{bytes};
    writer.write(c_magic);
    writer.write(c_version);
    write_settings(writer, navigation_mesh.get_settings());
    writer.write_vec3(navigation_mesh.get_bounds_min());
    writer.write_vec3(navigation_mesh.get_bounds_max());

    const dtNavMesh&       nav_mesh = *navigation_mesh.get_detour_nav_mesh();
    const dtNavMeshParams& params   = *nav_mesh.getParams();
    writer.write(params.orig[0]);
    writer.write(params.orig[1]);
    writer.write(params.orig[2]);
    writer.write(params.tileWidth);
    writer.write(params.tileHeight);
    writer.write(static_cast<int32_t>(params.maxTiles));
    writer.write(static_cast<int32_t>(params.maxPolys));

    const std::vector<const dtMeshTile*> tiles = get_tiles_in_grid_order(nav_mesh);
    writer.write(static_cast<uint32_t>(tiles.size()));
    for (const dtMeshTile* tile : tiles) {
        writer.write(static_cast<uint32_t>(tile->dataSize));
        writer.write_bytes(tile->data, static_cast<std::size_t>(tile->dataSize));
    }

    const std::span<const Navigation_polygon> polygons = navigation_mesh.get_polygons();
    writer.write(static_cast<uint32_t>(navigation_mesh.get_vertices().size()));
    for (const glm::vec3& vertex : navigation_mesh.get_vertices()) {
        writer.write_vec3(vertex);
    }
    writer.write(static_cast<uint32_t>(navigation_mesh.get_polygon_vertex_indices().size()));
    for (const uint32_t index : navigation_mesh.get_polygon_vertex_indices()) {
        writer.write(index);
    }
    writer.write(static_cast<uint32_t>(navigation_mesh.get_neighbours().size()));
    for (const uint32_t neighbour : navigation_mesh.get_neighbours()) {
        writer.write(neighbour);
    }
    writer.write(static_cast<uint32_t>(navigation_mesh.get_detail_triangle_vertices().size()));
    for (const glm::vec3& vertex : navigation_mesh.get_detail_triangle_vertices()) {
        writer.write_vec3(vertex);
    }
    writer.write(static_cast<uint32_t>(polygons.size()));
    for (const Navigation_polygon& polygon : polygons) {
        writer.write(polygon.tile);
        writer.write(polygon.first_vertex);
        writer.write(polygon.vertex_count);
        writer.write(polygon.first_neighbour);
        writer.write(polygon.neighbour_count);
        writer.write(polygon.first_detail_triangle);
        writer.write(polygon.detail_triangle_count);
        writer.write(polygon.area);
        writer.write(static_cast<uint8_t>(polygon.reachable ? 1 : 0));
    }
    write_record(writer, record);
    return bytes;
}

auto read_navigation_mesh(
    const std::span<const uint8_t> bytes,
    Navigation_build_record&       out_record,
    std::string&                   out_error
) -> std::unique_ptr<Navigation_mesh>
{
    Byte_reader reader{bytes};
    uint32_t magic   = 0;
    uint32_t version = 0;
    if (!reader.read(magic) || (magic != c_magic)) {
        return fail(out_error, "not a navigation mesh (magic)");
    }
    if (!reader.read(version) || (version != c_version)) {
        return fail(out_error, fmt::format("unsupported version {} (expected {})", version, c_version));
    }
    Navigation_build_settings settings{};
    glm::vec3 bounds_min{0.0f};
    glm::vec3 bounds_max{0.0f};
    if (!read_settings(reader, settings) || !reader.read_vec3(bounds_min) || !reader.read_vec3(bounds_max)) {
        return fail(out_error, "truncated settings");
    }

    dtNavMeshParams params{};
    int32_t max_tiles = 0;
    int32_t max_polys = 0;
    uint32_t tile_count = 0;
    if (
        !reader.read(params.orig[0]) || !reader.read(params.orig[1]) || !reader.read(params.orig[2]) ||
        !reader.read(params.tileWidth) || !reader.read(params.tileHeight) ||
        !reader.read(max_tiles) || !reader.read(max_polys) || !reader.read(tile_count)
    ) {
        return fail(out_error, "truncated Detour parameters");
    }
    params.maxTiles = max_tiles;
    params.maxPolys = max_polys;

    std::unique_ptr<dtNavMesh, Detour_nav_mesh_deleter> nav_mesh{dtAllocNavMesh()};
    if (!nav_mesh || dtStatusFailed(nav_mesh->init(&params))) {
        return fail(out_error, "Detour navigation mesh initialization failed");
    }
    for (uint32_t i = 0; i < tile_count; ++i) {
        uint32_t                 data_size = 0;
        std::span<const uint8_t> data;
        if (!reader.read(data_size) || !reader.read_span(data_size, data)) {
            return fail(out_error, fmt::format("truncated tile {}", i));
        }
        unsigned char* tile_data = static_cast<unsigned char*>(dtAlloc(data_size, DT_ALLOC_PERM));
        if (tile_data == nullptr) {
            return fail(out_error, fmt::format("out of memory for tile {}", i));
        }
        std::memcpy(tile_data, data.data(), data_size);
        const dtStatus status = nav_mesh->addTile(tile_data, static_cast<int>(data_size), DT_TILE_FREE_DATA, 0, nullptr);
        if (dtStatusFailed(status)) {
            dtFree(tile_data);
            return fail(out_error, fmt::format("Detour refused tile {} (status 0x{:x})", i, status));
        }
    }

    std::unique_ptr<Navigation_mesh> navigation_mesh = std::make_unique<Navigation_mesh>(std::move(nav_mesh), settings, bounds_min, bounds_max);
    navigation_mesh->fill_from_detour();

    // The stored polygon data must match what the tiles give; only the reachable flags are taken from it.
    uint32_t count = 0;
    glm::vec3 vertex{0.0f};
    uint32_t  index = 0;
    if (!reader.read(count) || (count != navigation_mesh->get_vertices().size())) {
        return fail(out_error, "vertex count does not match the tiles");
    }
    for (uint32_t i = 0; i < count; ++i) {
        if (!reader.read_vec3(vertex)) {
            return fail(out_error, "truncated vertices");
        }
    }
    const std::span<const uint32_t> polygon_vertex_indices = navigation_mesh->get_polygon_vertex_indices();
    if (!reader.read(count) || (count != polygon_vertex_indices.size())) {
        return fail(out_error, "polygon vertex index count does not match the tiles");
    }
    for (uint32_t i = 0; i < count; ++i) {
        if (!reader.read(index) || (index != polygon_vertex_indices[i])) {
            return fail(out_error, "polygon vertex indices do not match the tiles");
        }
    }
    const std::span<const uint32_t> neighbours = navigation_mesh->get_neighbours();
    if (!reader.read(count) || (count != neighbours.size())) {
        return fail(out_error, "neighbour count does not match the tiles");
    }
    for (uint32_t i = 0; i < count; ++i) {
        if (!reader.read(index) || (index != neighbours[i])) {
            return fail(out_error, "neighbours do not match the tiles");
        }
    }
    if (!reader.read(count) || (count != navigation_mesh->get_detail_triangle_vertices().size())) {
        return fail(out_error, "detail triangle vertex count does not match the tiles");
    }
    for (uint32_t i = 0; i < count; ++i) {
        if (!reader.read_vec3(vertex)) {
            return fail(out_error, "truncated detail triangle vertices");
        }
    }
    const std::span<const Navigation_polygon> polygons = navigation_mesh->get_polygons();
    if (!reader.read(count) || (count != polygons.size())) {
        return fail(out_error, "polygon count does not match the tiles");
    }
    std::vector<uint8_t> reachable_flags(count, uint8_t{0});
    for (uint32_t i = 0; i < count; ++i) {
        Navigation_polygon stored{};
        uint8_t            reachable = 0;
        const bool ok =
            reader.read(stored.tile) &&
            reader.read(stored.first_vertex) &&
            reader.read(stored.vertex_count) &&
            reader.read(stored.first_neighbour) &&
            reader.read(stored.neighbour_count) &&
            reader.read(stored.first_detail_triangle) &&
            reader.read(stored.detail_triangle_count) &&
            reader.read(stored.area) &&
            reader.read(reachable);
        if (!ok) {
            return fail(out_error, "truncated polygons");
        }
        const Navigation_polygon& polygon = polygons[i];
        if (
            (stored.tile                  != polygon.tile)                  ||
            (stored.first_vertex          != polygon.first_vertex)          ||
            (stored.vertex_count          != polygon.vertex_count)          ||
            (stored.first_neighbour       != polygon.first_neighbour)       ||
            (stored.neighbour_count       != polygon.neighbour_count)       ||
            (stored.first_detail_triangle != polygon.first_detail_triangle) ||
            (stored.detail_triangle_count != polygon.detail_triangle_count)
        ) {
            return fail(out_error, fmt::format("polygon {} does not match the tiles", i));
        }
        reachable_flags[i] = (reachable != 0) ? uint8_t{1} : uint8_t{0};
    }
    navigation_mesh->set_reachable(reachable_flags); // keeps the islands' reachable flags current
    if (!read_record(reader, out_record)) {
        return fail(out_error, "truncated build record");
    }
    if (!reader.at_end()) {
        return fail(out_error, "trailing bytes");
    }
    return navigation_mesh;
}

} // namespace editor
