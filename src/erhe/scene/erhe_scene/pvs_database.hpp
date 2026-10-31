#pragma once

#include "erhe_math/aabb.hpp"

#include <glm/glm.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace erhe::scene {

// A potentially visible set database (doc/plans/pvs.md "Database";
// doc/erhe/scene.md "Potentially visible sets"): per cell of a uniform grid,
// one bit per column (a baked draw-list entry). Cells are grouped into
// pages of page_size^3 cells; the page is the storage (and later the GPU
// residency and streaming) unit. Plain data: no GPU objects, no per-frame
// API. Persisted as the asset-root glTF extension ERHE_pvs
// (doc/gltf_extensions/ERHE_pvs.md).

// absent: the scene has no database. ready: the database is valid. stale: a
// baked entry moved, changed its bounds or was removed; only a rebake
// returns to ready.
enum class Pvs_state : unsigned int {
    absent = 0,
    ready  = 1,
    stale  = 2
};

[[nodiscard]] auto c_str(Pvs_state state) -> const char*;

// How a database was baked (doc/plans/pvs.md "Build order"). sampled: the
// union of the id captures from random viewpoints inside each cell, not
// conservative, the ground truth the verification compares against.
// disocclusion: the conservative disocclusion-buffer bake.
enum class Pvs_bake_method : unsigned int {
    sampled      = 0,
    disocclusion = 1
};

// "sampled" / "disocclusion".
[[nodiscard]] auto c_str(Pvs_bake_method method) -> const char*;
// The method a c_str() name spells, nullopt for any other text.
[[nodiscard]] auto parse_pvs_bake_method(std::string_view text) -> std::optional<Pvs_bake_method>;

// The parameters the bake ran with (doc/plans/pvs.md "Bake").
class Pvs_bake_parameters
{
public:
    Pvs_bake_method method{Pvs_bake_method::disocclusion};
    uint32_t face_resolution {1024};
    uint32_t tile_size       {8};
    uint32_t layer_count     {64};
    float    face_fov        {2.0943951f}; // radians (120 degrees)
    uint32_t samples_per_face{1};
    // disocclusion: rule 3 (silhouette sides of the cones, doc/plans/pvs.md
    // "Open"), opt-in.
    bool     silhouette_tests{false};
};

class Pvs_header
{
public:
    static constexpr uint32_t current_version = 1;
    // A page holds at most 64 cells (Pvs_page_entry::cells_with_pvs).
    static constexpr uint32_t max_page_size   = 4;

    uint32_t            version              {current_version};
    std::string         scene_id;
    glm::vec3           grid_origin          {0.0f, 0.0f, 0.0f};
    float               cell_size            {1.0f};
    glm::uvec3          cell_counts          {0u, 0u, 0u};
    uint32_t            page_size            {4}; // cells per page axis, 1 to max_page_size
    Pvs_bake_parameters bake_parameters;
    float               bake_duration_seconds{0.0f};
};

// One baked entry: its identity (the mesh node's glTF uid and the mesh
// primitive index) and what the bake saw of it.
class Pvs_column
{
public:
    std::string           node_uid;
    uint32_t              primitive_index{0};
    // world_from_node as 4 columns of 3 rows (the x, y, z of each glm::mat4
    // column, translation last).
    std::array<float, 12> world_transform{};
    // World AABB: min x, y, z, then max x, y, z.
    std::array<float, 6>  world_bounds{};
    // compute_pvs_transform_hash(world_transform, world_bounds).
    uint64_t              transform_hash{0};
};

// One page of the page table (16 bytes, the layout of the ERHE_pvs `pages`
// bufferView).
class Pvs_page_entry
{
public:
    // First word of the page's bits, or Pvs_database::page_not_stored: no
    // cell of the page has a PVS and every cell reads as everything visible.
    uint32_t word_offset   {0xffffffffu};
    uint32_t reserved      {0};
    // Bit local_cell set when that cell of the page has a baked PVS.
    uint64_t cells_with_pvs{0};
};
static_assert(sizeof(Pvs_page_entry) == 16);

// Where the bits are in a file: the absolute byte range of the ERHE_pvs
// `bits` bufferView, recorded by the parser so a streamer can read single
// pages by range without the glTF parser.
class Pvs_bits_source
{
public:
    std::filesystem::path path;
    uint64_t              byte_offset{0};
    uint64_t              byte_length{0};
};

// 64-bit FNV-1a (erhe::hash::hash()) over the bits of the 12 transform
// floats followed by the 6 bounds floats, in that order.
[[nodiscard]] auto compute_pvs_transform_hash(
    const std::array<float, 12>& world_transform,
    const std::array<float, 6>&  world_bounds
) -> uint64_t;

// The Pvs_column::world_transform values of a node's world_from_node: the
// x, y, z of each of the four columns, translation last.
[[nodiscard]] auto make_pvs_world_transform(const glm::mat4& world_from_node) -> std::array<float, 12>;
// The Pvs_column::world_bounds values of a world AABB: min x, y, z, then
// max x, y, z.
[[nodiscard]] auto make_pvs_world_bounds(const erhe::math::Aabb& world_aabb) -> std::array<float, 6>;
// compute_pvs_transform_hash() of the two above: what the bake records for
// a baked entry and what the staleness rule (doc/plans/pvs.md D4) compares
// a registered entry's state with.
[[nodiscard]] auto compute_pvs_transform_hash(const glm::mat4& world_from_node, const erhe::math::Aabb& world_aabb) -> uint64_t;

class Pvs_database
{
public:
    // Pvs_page_entry::word_offset of a page that is not stored.
    static constexpr uint32_t page_not_stored  = 0xffffffffu;
    // find_column() result for an identity that has no column.
    static constexpr uint32_t untracked_column = 0xffffffffu;

    // A database is created ready. The arguments must pass validate();
    // a caller holding unchecked data (a parser) calls validate() first.
    Pvs_database(
        Pvs_header                     header,
        std::vector<Pvs_column>        columns,
        std::vector<Pvs_page_entry>    pages,
        std::vector<uint32_t>          bits,
        std::optional<Pvs_bits_source> bits_source = std::nullopt
    );
    Pvs_database(const Pvs_database&)            = delete;
    Pvs_database& operator=(const Pvs_database&) = delete;
    Pvs_database(Pvs_database&&)                 = delete;
    Pvs_database& operator=(Pvs_database&&)      = delete;

    // Empty when the data is consistent, else the first problem found:
    // version, bake method, grid (positive finite cell size, non-zero counts), page size,
    // column identities (non-empty uids, no duplicate identity), transform
    // hashes, page table (size, reserved words, offsets, masks naming only
    // cells of the page inside the grid), bits size, zero padding bits in
    // cells with a PVS, all-ones words in stored cells without one.
    [[nodiscard]] static auto validate(
        const Pvs_header&               header,
        std::span<const Pvs_column>     columns,
        std::span<const Pvs_page_entry> pages,
        std::span<const uint32_t>       bits
    ) -> std::string;

    [[nodiscard]] auto get_header     () const -> const Pvs_header&                  { return m_header; }
    [[nodiscard]] auto get_columns    () const -> const std::vector<Pvs_column>&     { return m_columns; }
    [[nodiscard]] auto get_pages      () const -> const std::vector<Pvs_page_entry>& { return m_pages; }
    // The in-memory copy of the bits (empty after release_bits()). Main
    // thread, like release_bits() and make_bits_resident(); other threads
    // read pages with read_page_words().
    [[nodiscard]] auto get_bits       () const -> const std::vector<uint32_t>&       { return m_bits; }
    // Where the bits are in a file (nullopt: the bits are in memory only).
    // Main thread.
    [[nodiscard]] auto get_bits_source() const -> const std::optional<Pvs_bits_source>& { return m_bits_source; }

    // --- The bits in memory and on disk (doc/plans/pvs.md "Residency and
    // streaming") ---
    // True while the in-memory copy of the bits is held (a bake, or an open
    // until release_bits()). Any thread; never waits for a file read.
    [[nodiscard]] auto has_resident_bits() const -> bool;
    // 64-bit FNV-1a (erhe::hash::hash()) of the bits' bytes, computed once
    // by the constructor (the bits are in memory then): equal for equal
    // bakes and across a save and reload, whether or not the bits are in
    // memory now.
    [[nodiscard]] auto get_bits_hash() const -> uint64_t { return m_bits_hash; }
    // Drops the in-memory copy of the bits when they can be read from the
    // bits source on disk; returns true when it did. Main thread.
    auto release_bits() -> bool;
    // Reads the bits back from the bits source into memory and forgets the
    // source (a save is about to replace that file); true when the bits are
    // in memory afterwards. Main thread.
    auto make_bits_resident() -> bool;
    // True when the bits source is the file at path.
    [[nodiscard]] auto bits_source_references(const std::filesystem::path& path) const -> bool;
    // Reads one stored page's words (get_words_per_page()) into out, from
    // the in-memory copy or else by range from the bits source; false when
    // the page is not stored, out has another size or the read fails. Any
    // thread (the page streamer's worker): the reads are serialized with
    // release_bits() and make_bits_resident().
    [[nodiscard]] auto read_page_words(uint32_t page, std::span<uint32_t> out) const -> bool;
    // Reads the cell's words (get_words_per_cell()) into out (resized), as
    // read_page_words() does; false when the cell's page is not stored or
    // the read fails. Any thread.
    [[nodiscard]] auto read_cell_words(uint32_t cell, std::vector<uint32_t>& out) const -> bool;
    // Reads every stored page's words, in storage order (the layout of
    // get_bits()), into out; false when a read fails. Any thread.
    [[nodiscard]] auto read_bits(std::vector<uint32_t>& out) const -> bool;

    [[nodiscard]] auto get_column_count     () const -> uint32_t;
    [[nodiscard]] auto get_cell_count       () const -> uint32_t;
    // ceil(column_count / 32): the words of one cell's PVS.
    [[nodiscard]] auto get_words_per_cell   () const -> uint32_t;
    [[nodiscard]] auto get_cells_per_page   () const -> uint32_t;
    [[nodiscard]] auto get_words_per_page   () const -> uint32_t;
    // ceil(cell_counts / page_size) per axis.
    [[nodiscard]] auto get_page_dims        () const -> glm::uvec3;
    [[nodiscard]] auto get_page_count       () const -> uint32_t;
    [[nodiscard]] auto get_stored_page_count() const -> uint32_t;
    // True when the page has a stored range of bits (some cell of it has a
    // PVS); false for a page out of range.
    [[nodiscard]] auto is_page_stored       (uint32_t page) const -> bool;
    [[nodiscard]] auto get_cells_with_pvs   () const -> uint32_t;

    // x + dims.x * (y + dims.y * z); the cell must be inside the grid.
    [[nodiscard]] auto cell_index (const glm::uvec3& cell) const -> uint32_t;
    // The cell holding a world position, nullopt outside the grid.
    [[nodiscard]] auto find_cell  (const glm::vec3& position) const -> std::optional<uint32_t>;
    // px + pdims.x * (py + pdims.y * pz) of the cell's page.
    [[nodiscard]] auto page_of_cell(uint32_t cell) const -> uint32_t;
    // lx + page_size * (ly + page_size * lz) of the cell inside its page.
    [[nodiscard]] auto local_cell  (uint32_t cell) const -> uint32_t;

    // The page's words (get_words_per_page()) in the in-memory copy, empty
    // when the page is not stored or the bits are not in memory
    // (release_bits()). Main thread.
    [[nodiscard]] auto get_page_words(uint32_t page) const -> std::span<const uint32_t>;
    // The cell's words inside its stored page (all ones for a cell without
    // a PVS) in the in-memory copy, empty when the page is not stored or the
    // bits are not in memory. Main thread.
    [[nodiscard]] auto get_cell_words(uint32_t cell) const -> std::span<const uint32_t>;
    // True when the cell has a baked PVS (the page table; no bits read).
    [[nodiscard]] auto has_cell_pvs  (uint32_t cell) const -> bool;
    // Conservative: true when the cell has no PVS (or is outside the grid),
    // else the cell's bit of the column. False for a column out of range.
    // Reads the in-memory copy: needs has_resident_bits(). Main thread.
    [[nodiscard]] auto is_column_visible(uint32_t cell, uint32_t column) const -> bool;
    // Number of columns in the cell's PVS, 0 for a cell without one. Reads
    // the in-memory copy: needs has_resident_bits(). Main thread.
    [[nodiscard]] auto get_cell_pvs_size(uint32_t cell) const -> uint32_t;
    // Number of set bits of a cell's words (read_cell_words()).
    [[nodiscard]] static auto count_pvs_words(std::span<const uint32_t> words) -> uint32_t;

    // The column of an identity, untracked_column when it has none. The
    // lookup map is built once, by the constructor.
    [[nodiscard]] auto find_column(const std::string& node_uid, uint32_t primitive_index) const -> uint32_t;

    [[nodiscard]] auto get_state() const -> Pvs_state;
    // Ready -> stale. Returns true when this call changed the state. Any
    // thread.
    auto mark_stale() -> bool;

private:
    // Reads out.size() words of the bits source from first_word; caller
    // holds m_bits_mutex.
    [[nodiscard]] auto read_source_words(std::size_t first_word, std::span<uint32_t> out) const -> bool;

    class Column_key
    {
    public:
        std::string node_uid;
        uint32_t    primitive_index{0};

        [[nodiscard]] auto operator==(const Column_key& other) const -> bool = default;
    };
    class Column_key_hash
    {
    public:
        [[nodiscard]] auto operator()(const Column_key& key) const -> std::size_t;
    };

    Pvs_header                                                m_header;
    std::vector<Pvs_column>                                   m_columns;
    std::vector<Pvs_page_entry>                               m_pages;
    // m_bits and m_bits_source; held across a file read, so a read never
    // straddles release_bits() or make_bits_resident(). m_bits_state_mutex
    // (taken inside it, or alone) guards m_bits_in_memory, so a query never
    // waits for a read.
    mutable std::mutex                                        m_bits_mutex;
    mutable std::mutex                                        m_bits_state_mutex;
    bool                                                      m_bits_in_memory{true};
    uint64_t                                                  m_bits_hash{0};
    std::vector<uint32_t>                                     m_bits;
    std::optional<Pvs_bits_source>                            m_bits_source;
    // The first word of each stored page in storage order is its
    // Pvs_page_entry::word_offset; the bits' word count.
    std::size_t                                               m_bit_word_count{0};
    std::unordered_map<Column_key, uint32_t, Column_key_hash> m_column_lookup;
    mutable std::mutex                                        m_state_mutex;
    Pvs_state                                                 m_state{Pvs_state::ready};
};

} // namespace erhe::scene
