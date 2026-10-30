#include "erhe_scene/pvs_database.hpp"

#include "erhe_hash/hash.hpp"

#include <fmt/format.h>

#include <bit>
#include <cmath>
#include <functional>
#include <unordered_set>

namespace erhe::scene {

auto c_str(const Pvs_state state) -> const char*
{
    switch (state) {
        case Pvs_state::absent: return "absent";
        case Pvs_state::ready:  return "ready";
        case Pvs_state::stale:  return "stale";
        default:                return "?";
    }
}

auto c_str(const Pvs_bake_method method) -> const char*
{
    switch (method) {
        case Pvs_bake_method::sampled:      return "sampled";
        case Pvs_bake_method::disocclusion: return "disocclusion";
        default:                            return "?";
    }
}

auto parse_pvs_bake_method(const std::string_view text) -> std::optional<Pvs_bake_method>
{
    if (text == "sampled") {
        return Pvs_bake_method::sampled;
    }
    if (text == "disocclusion") {
        return Pvs_bake_method::disocclusion;
    }
    return std::nullopt;
}

auto compute_pvs_transform_hash(const std::array<float, 12>& world_transform, const std::array<float, 6>& world_bounds) -> uint64_t
{
    uint64_t hash = erhe::hash::c_seed;
    for (const float value : world_transform) {
        hash = erhe::hash::hash(value, hash);
    }
    for (const float value : world_bounds) {
        hash = erhe::hash::hash(value, hash);
    }
    return hash;
}

auto make_pvs_world_transform(const glm::mat4& world_from_node) -> std::array<float, 12>
{
    std::array<float, 12> world_transform{};
    for (glm::length_t column = 0; column < 4; ++column) {
        for (glm::length_t row = 0; row < 3; ++row) {
            world_transform[static_cast<std::size_t>((column * 3) + row)] = world_from_node[column][row];
        }
    }
    return world_transform;
}

auto make_pvs_world_bounds(const erhe::math::Aabb& world_aabb) -> std::array<float, 6>
{
    return std::array<float, 6>{
        world_aabb.min.x, world_aabb.min.y, world_aabb.min.z,
        world_aabb.max.x, world_aabb.max.y, world_aabb.max.z
    };
}

auto compute_pvs_transform_hash(const glm::mat4& world_from_node, const erhe::math::Aabb& world_aabb) -> uint64_t
{
    return compute_pvs_transform_hash(make_pvs_world_transform(world_from_node), make_pvs_world_bounds(world_aabb));
}

namespace {

[[nodiscard]] auto get_words_per_cell_for(const std::size_t column_count) -> std::size_t
{
    return (column_count + 31) / 32;
}

[[nodiscard]] auto get_page_dims_for(const Pvs_header& header) -> glm::uvec3
{
    const uint32_t page_size = header.page_size;
    return (header.cell_counts + glm::uvec3{page_size - 1u}) / page_size;
}

// The mask of the padding bits of a cell's last word (bits past the last
// column), 0 when the column count is a multiple of 32.
[[nodiscard]] auto get_padding_mask(const std::size_t column_count) -> uint32_t
{
    const std::size_t used_bits = column_count % 32;
    if (used_bits == 0) {
        return 0u;
    }
    return ~((uint32_t{1} << used_bits) - 1u);
}

} // anonymous namespace

auto Pvs_database::validate(
    const Pvs_header&                     header,
    const std::span<const Pvs_column>     columns,
    const std::span<const Pvs_page_entry> pages,
    const std::span<const uint32_t>       bits
) -> std::string
{
    if (header.version != Pvs_header::current_version) {
        return fmt::format("unsupported version {}", header.version);
    }
    if (
        (header.bake_parameters.method != Pvs_bake_method::sampled) &&
        (header.bake_parameters.method != Pvs_bake_method::disocclusion)
    ) {
        return "unknown bake method";
    }
    if (!std::isfinite(header.cell_size) || (header.cell_size <= 0.0f)) {
        return "cell size is not positive and finite";
    }
    if (!std::isfinite(header.grid_origin.x) || !std::isfinite(header.grid_origin.y) || !std::isfinite(header.grid_origin.z)) {
        return "grid origin is not finite";
    }
    if ((header.cell_counts.x == 0) || (header.cell_counts.y == 0) || (header.cell_counts.z == 0)) {
        return "a cell count is zero";
    }
    if ((header.page_size == 0) || (header.page_size > Pvs_header::max_page_size)) {
        return fmt::format("page size {} is not 1 to {}", header.page_size, Pvs_header::max_page_size);
    }
    const uint64_t cell_count =
        static_cast<uint64_t>(header.cell_counts.x) *
        static_cast<uint64_t>(header.cell_counts.y) *
        static_cast<uint64_t>(header.cell_counts.z);
    if (cell_count >= 0xffffffffull) {
        return "too many cells";
    }
    if (columns.size() >= static_cast<std::size_t>(untracked_column)) {
        return "too many columns";
    }
    std::unordered_set<std::string> identities;
    for (std::size_t i = 0, end = columns.size(); i < end; ++i) {
        const Pvs_column& column = columns[i];
        if (column.node_uid.empty()) {
            return fmt::format("column {} has no node uid", i);
        }
        std::string identity = column.node_uid;
        identity.push_back('\0');
        identity += std::to_string(column.primitive_index);
        if (!identities.insert(std::move(identity)).second) {
            return fmt::format("column {} repeats the identity ('{}', {})", i, column.node_uid, column.primitive_index);
        }
        if (compute_pvs_transform_hash(column.world_transform, column.world_bounds) != column.transform_hash) {
            return fmt::format("column {} transform_hash does not match its transform and bounds", i);
        }
    }

    const glm::uvec3  page_dims      = get_page_dims_for(header);
    const uint64_t    page_count     = static_cast<uint64_t>(page_dims.x) * page_dims.y * page_dims.z;
    if (pages.size() != page_count) {
        return fmt::format("page table has {} entries, the grid has {} pages", pages.size(), page_count);
    }
    const uint32_t    page_size      = header.page_size;
    const std::size_t cells_per_page = static_cast<std::size_t>(page_size) * page_size * page_size;
    const std::size_t words_per_cell = get_words_per_cell_for(columns.size());
    const std::size_t words_per_page = cells_per_page * words_per_cell;
    const uint32_t    padding_mask   = get_padding_mask(columns.size());
    std::size_t       stored_count   = 0;
    std::unordered_set<uint32_t> offsets;
    for (std::size_t page = 0, end = pages.size(); page < end; ++page) {
        const Pvs_page_entry& entry = pages[page];
        if (entry.reserved != 0) {
            return fmt::format("page {} reserved word is not 0", page);
        }
        if (entry.word_offset == page_not_stored) {
            if (entry.cells_with_pvs != 0) {
                return fmt::format("page {} is not stored but names cells with a PVS", page);
            }
            continue;
        }
        ++stored_count;
        if (
            ((words_per_page > 0) && ((entry.word_offset % words_per_page) != 0)) ||
            ((static_cast<std::size_t>(entry.word_offset) + words_per_page) > bits.size()) ||
            ((words_per_page > 0) && !offsets.insert(entry.word_offset).second)
        ) {
            return fmt::format("page {} has an invalid bits offset {}", page, entry.word_offset);
        }
        const glm::uvec3 page_coordinate{
            static_cast<uint32_t>(page % page_dims.x),
            static_cast<uint32_t>((page / page_dims.x) % page_dims.y),
            static_cast<uint32_t>(page / (static_cast<std::size_t>(page_dims.x) * page_dims.y))
        };
        for (uint32_t local = 0; local < 64; ++local) {
            const bool has_pvs = ((entry.cells_with_pvs >> local) & 1u) != 0;
            if (local >= cells_per_page) {
                if (has_pvs) {
                    return fmt::format("page {} names local cell {}, past the page", page, local);
                }
                continue;
            }
            const glm::uvec3 cell =
                (page_coordinate * page_size) +
                glm::uvec3{local % page_size, (local / page_size) % page_size, local / (page_size * page_size)};
            const bool inside = (cell.x < header.cell_counts.x) && (cell.y < header.cell_counts.y) && (cell.z < header.cell_counts.z);
            if (has_pvs && !inside) {
                return fmt::format("page {} names local cell {}, outside the grid", page, local);
            }
            const std::span<const uint32_t> words = bits.subspan(entry.word_offset + (local * words_per_cell), words_per_cell);
            for (std::size_t word = 0; word < words_per_cell; ++word) {
                if (!has_pvs) {
                    if (words[word] != 0xffffffffu) {
                        return fmt::format("page {} local cell {} has no PVS but its words are not all ones", page, local);
                    }
                } else if ((word == (words_per_cell - 1)) && ((words[word] & padding_mask) != 0)) {
                    return fmt::format("page {} local cell {} sets bits past the last column", page, local);
                }
            }
        }
    }
    if (bits.size() != (stored_count * words_per_page)) {
        return fmt::format("bits hold {} words, {} stored pages need {}", bits.size(), stored_count, stored_count * words_per_page);
    }
    return {};
}

auto Pvs_database::Column_key_hash::operator()(const Column_key& key) const -> std::size_t
{
    const std::size_t uid_hash = std::hash<std::string>{}(key.node_uid);
    return uid_hash ^ (static_cast<std::size_t>(key.primitive_index) * 0x9e3779b97f4a7c15ull);
}

Pvs_database::Pvs_database(
    Pvs_header                     header,
    std::vector<Pvs_column>        columns,
    std::vector<Pvs_page_entry>    pages,
    std::vector<uint32_t>          bits,
    std::optional<Pvs_bits_source> bits_source
)
    : m_header      {std::move(header)}
    , m_columns     {std::move(columns)}
    , m_pages       {std::move(pages)}
    , m_bits        {std::move(bits)}
    , m_bits_source {std::move(bits_source)}
{
    m_column_lookup.reserve(m_columns.size());
    for (std::size_t i = 0, end = m_columns.size(); i < end; ++i) {
        const Pvs_column& column = m_columns[i];
        m_column_lookup.emplace(
            Column_key{.node_uid = column.node_uid, .primitive_index = column.primitive_index},
            static_cast<uint32_t>(i)
        );
    }
}

auto Pvs_database::get_column_count() const -> uint32_t
{
    return static_cast<uint32_t>(m_columns.size());
}

auto Pvs_database::get_cell_count() const -> uint32_t
{
    return m_header.cell_counts.x * m_header.cell_counts.y * m_header.cell_counts.z;
}

auto Pvs_database::get_words_per_cell() const -> uint32_t
{
    return static_cast<uint32_t>(get_words_per_cell_for(m_columns.size()));
}

auto Pvs_database::get_cells_per_page() const -> uint32_t
{
    return m_header.page_size * m_header.page_size * m_header.page_size;
}

auto Pvs_database::get_words_per_page() const -> uint32_t
{
    return get_cells_per_page() * get_words_per_cell();
}

auto Pvs_database::get_page_dims() const -> glm::uvec3
{
    return get_page_dims_for(m_header);
}

auto Pvs_database::get_page_count() const -> uint32_t
{
    return static_cast<uint32_t>(m_pages.size());
}

auto Pvs_database::get_stored_page_count() const -> uint32_t
{
    uint32_t count = 0;
    for (const Pvs_page_entry& entry : m_pages) {
        if (entry.word_offset != page_not_stored) {
            ++count;
        }
    }
    return count;
}

auto Pvs_database::get_cells_with_pvs() const -> uint32_t
{
    uint32_t count = 0;
    for (const Pvs_page_entry& entry : m_pages) {
        count += static_cast<uint32_t>(std::popcount(entry.cells_with_pvs));
    }
    return count;
}

auto Pvs_database::cell_index(const glm::uvec3& cell) const -> uint32_t
{
    const glm::uvec3& dims = m_header.cell_counts;
    return cell.x + (dims.x * (cell.y + (dims.y * cell.z)));
}

auto Pvs_database::find_cell(const glm::vec3& position) const -> std::optional<uint32_t>
{
    const glm::vec3 grid_position = (position - m_header.grid_origin) / m_header.cell_size;
    const glm::vec3 cell_float    = glm::floor(grid_position);
    const glm::vec3 counts        = glm::vec3{m_header.cell_counts};
    for (glm::length_t axis = 0; axis < 3; ++axis) {
        if (!std::isfinite(cell_float[axis]) || (cell_float[axis] < 0.0f) || (cell_float[axis] >= counts[axis])) {
            return std::nullopt;
        }
    }
    return cell_index(glm::uvec3{cell_float});
}

auto Pvs_database::page_of_cell(const uint32_t cell) const -> uint32_t
{
    const glm::uvec3& dims      = m_header.cell_counts;
    const uint32_t    page_size = m_header.page_size;
    const glm::uvec3  page_dims = get_page_dims();
    const glm::uvec3  page{
        (cell % dims.x) / page_size,
        ((cell / dims.x) % dims.y) / page_size,
        (cell / (dims.x * dims.y)) / page_size
    };
    return page.x + (page_dims.x * (page.y + (page_dims.y * page.z)));
}

auto Pvs_database::local_cell(const uint32_t cell) const -> uint32_t
{
    const glm::uvec3& dims      = m_header.cell_counts;
    const uint32_t    page_size = m_header.page_size;
    const glm::uvec3  local{
        (cell % dims.x) % page_size,
        ((cell / dims.x) % dims.y) % page_size,
        (cell / (dims.x * dims.y)) % page_size
    };
    return local.x + (page_size * (local.y + (page_size * local.z)));
}

auto Pvs_database::get_page_words(const uint32_t page) const -> std::span<const uint32_t>
{
    if (page >= m_pages.size()) {
        return {};
    }
    const uint32_t offset = m_pages[page].word_offset;
    if (offset == page_not_stored) {
        return {};
    }
    return std::span<const uint32_t>{m_bits}.subspan(offset, get_words_per_page());
}

auto Pvs_database::get_cell_words(const uint32_t cell) const -> std::span<const uint32_t>
{
    if (cell >= get_cell_count()) {
        return {};
    }
    const std::span<const uint32_t> page_words = get_page_words(page_of_cell(cell));
    if (page_words.empty()) {
        return {};
    }
    const uint32_t words_per_cell = get_words_per_cell();
    return page_words.subspan(static_cast<std::size_t>(local_cell(cell)) * words_per_cell, words_per_cell);
}

auto Pvs_database::has_cell_pvs(const uint32_t cell) const -> bool
{
    if (cell >= get_cell_count()) {
        return false;
    }
    const Pvs_page_entry& entry = m_pages[page_of_cell(cell)];
    return ((entry.cells_with_pvs >> local_cell(cell)) & 1u) != 0;
}

auto Pvs_database::is_column_visible(const uint32_t cell, const uint32_t column) const -> bool
{
    if (column >= m_columns.size()) {
        return false;
    }
    const std::span<const uint32_t> words = get_cell_words(cell);
    if (words.empty()) {
        return true; // page not stored, or the cell is outside the grid
    }
    return (words[column / 32] & (uint32_t{1} << (column % 32))) != 0;
}

auto Pvs_database::get_cell_pvs_size(const uint32_t cell) const -> uint32_t
{
    if (!has_cell_pvs(cell)) {
        return 0;
    }
    uint32_t count = 0;
    for (const uint32_t word : get_cell_words(cell)) {
        count += static_cast<uint32_t>(std::popcount(word));
    }
    return count;
}

auto Pvs_database::find_column(const std::string& node_uid, const uint32_t primitive_index) const -> uint32_t
{
    const auto it = m_column_lookup.find(Column_key{.node_uid = node_uid, .primitive_index = primitive_index});
    return (it != m_column_lookup.end()) ? it->second : untracked_column;
}

auto Pvs_database::get_state() const -> Pvs_state
{
    const std::lock_guard<std::mutex> lock{m_state_mutex};
    return m_state;
}

auto Pvs_database::mark_stale() -> bool
{
    const std::lock_guard<std::mutex> lock{m_state_mutex};
    if (m_state != Pvs_state::ready) {
        return false;
    }
    m_state = Pvs_state::stale;
    return true;
}

} // namespace erhe::scene
