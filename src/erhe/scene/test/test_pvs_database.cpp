// erhe::scene::Pvs_database (doc/erhe/scene.md "Potentially visible sets"):
// cell and page indexing, the identity-to-column lookup, the transform hash,
// validation, the stale transition, and the bits released and read back
// through a file source.

#include "erhe_scene/pvs_database.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <random>
#include <span>
#include <string>
#include <vector>

namespace {

[[nodiscard]] auto make_column(const std::string& uid, const uint32_t primitive_index, const float offset) -> erhe::scene::Pvs_column
{
    erhe::scene::Pvs_column column{};
    column.node_uid        = uid;
    column.primitive_index = primitive_index;
    column.world_transform = {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, offset, 2.0f, 3.0f};
    column.world_bounds    = {offset - 1.0f, 1.0f, 2.0f, offset + 1.0f, 3.0f, 4.0f};
    column.transform_hash  = erhe::scene::compute_pvs_transform_hash(column.world_transform, column.world_bounds);
    return column;
}

// Visible column of a cell's PVS: column (cell % 32) and 32 + (cell % 8).
[[nodiscard]] auto is_expected_visible(const uint32_t cell, const uint32_t column) -> bool
{
    return (column == (cell % 32)) || (column == (32 + (cell % 8)));
}

// 5 x 2 x 2 grid of 2 m cells at (-3, 0, -2), page size 4 (2 x 1 x 1
// pages), 40 columns (2 words per cell). Page 1 (cells x = 4) has no PVS
// and is not stored; cell 5 (x 0, y 1, z 0) of stored page 0 has none.
[[nodiscard]] auto make_database() -> std::unique_ptr<erhe::scene::Pvs_database>
{
    erhe::scene::Pvs_header header{};
    header.scene_id    = "test-scene";
    header.grid_origin = glm::vec3{-3.0f, 0.0f, -2.0f};
    header.cell_size   = 2.0f;
    header.cell_counts = glm::uvec3{5u, 2u, 2u};
    header.page_size   = 4;

    std::vector<erhe::scene::Pvs_column> columns;
    for (uint32_t i = 0; i < 40; ++i) {
        columns.push_back(make_column("node" + std::to_string(i / 2), i % 2, static_cast<float>(i)));
    }
    std::vector<erhe::scene::Pvs_page_entry> pages(2);
    std::vector<uint32_t> bits;
    pages[0].word_offset = 0;
    for (uint32_t local = 0; local < 64; ++local) {
        const uint32_t lx = local % 4;
        const uint32_t ly = (local / 4) % 4;
        const uint32_t lz = local / 16;
        const bool inside = (ly < 2) && (lz < 2);
        const uint32_t cell = lx + (5 * (ly + (2 * lz)));
        if (!inside || (cell == 5)) {
            bits.push_back(0xffffffffu);
            bits.push_back(0xffffffffu);
            continue;
        }
        pages[0].cells_with_pvs |= (uint64_t{1} << local);
        uint32_t words[2] = {0u, 0u};
        for (uint32_t column = 0; column < 40; ++column) {
            if (is_expected_visible(cell, column)) {
                words[column / 32] |= (uint32_t{1} << (column % 32));
            }
        }
        bits.push_back(words[0]);
        bits.push_back(words[1]);
    }
    EXPECT_EQ(erhe::scene::Pvs_database::validate(header, columns, pages, bits), std::string{});
    return std::make_unique<erhe::scene::Pvs_database>(std::move(header), std::move(columns), std::move(pages), std::move(bits));
}

} // anonymous namespace

TEST(Pvs_database, cell_and_page_indexing)
{
    const std::unique_ptr<erhe::scene::Pvs_database> database = make_database();
    EXPECT_EQ(database->get_cell_count(), 20u);
    EXPECT_EQ(database->get_page_dims(), glm::uvec3(2u, 1u, 1u));
    EXPECT_EQ(database->get_page_count(), 2u);
    EXPECT_EQ(database->get_stored_page_count(), 1u);
    EXPECT_EQ(database->get_cells_per_page(), 64u);
    EXPECT_EQ(database->get_words_per_page(), 128u);
    EXPECT_EQ(database->cell_index(glm::uvec3{0u, 0u, 0u}), 0u);
    EXPECT_EQ(database->cell_index(glm::uvec3{4u, 0u, 0u}), 4u);
    EXPECT_EQ(database->cell_index(glm::uvec3{0u, 1u, 0u}), 5u);
    EXPECT_EQ(database->cell_index(glm::uvec3{0u, 0u, 1u}), 10u);
    EXPECT_EQ(database->cell_index(glm::uvec3{4u, 1u, 1u}), 19u);

    EXPECT_EQ(database->page_of_cell(3),  0u);
    EXPECT_EQ(database->page_of_cell(4),  1u);
    EXPECT_EQ(database->page_of_cell(19), 1u);
    EXPECT_EQ(database->local_cell(0),  0u);
    EXPECT_EQ(database->local_cell(3),  3u);
    EXPECT_EQ(database->local_cell(4),  0u);
    EXPECT_EQ(database->local_cell(5),  4u);              // (0, 1, 0)
    EXPECT_EQ(database->local_cell(18), 3u + 4u + 16u);   // (3, 1, 1)
    EXPECT_EQ(database->local_cell(19), 0u + 4u + 16u);   // (4, 1, 1) -> (0, 1, 1) of page 1

    EXPECT_EQ(database->find_cell(glm::vec3{-3.0f, 0.0f, -2.0f}), std::optional<uint32_t>{0u});
    EXPECT_EQ(database->find_cell(glm::vec3{6.5f, 3.5f, 1.5f}),   std::optional<uint32_t>{19u});
    EXPECT_EQ(database->find_cell(glm::vec3{-0.5f, 2.5f, 0.0f}),  std::optional<uint32_t>{1u + (5u * (1u + (2u * 1u)))});
    EXPECT_FALSE(database->find_cell(glm::vec3{-3.5f, 0.0f, 0.0f}).has_value());
    EXPECT_FALSE(database->find_cell(glm::vec3{7.0f, 0.0f, 0.0f}).has_value());
    EXPECT_FALSE(database->find_cell(glm::vec3{0.0f, 4.0f, 0.0f}).has_value());
    EXPECT_FALSE(database->find_cell(glm::vec3{0.0f, 0.0f, 2.0f}).has_value());
}

TEST(Pvs_database, cell_words_and_visibility)
{
    const std::unique_ptr<erhe::scene::Pvs_database> database = make_database();
    EXPECT_EQ(database->get_column_count(), 40u);
    EXPECT_EQ(database->get_words_per_cell(), 2u);
    EXPECT_EQ(database->get_cells_with_pvs(), 15u);
    EXPECT_EQ(database->get_page_words(0).size(), 128u);
    EXPECT_TRUE(database->get_page_words(1).empty());

    for (uint32_t cell = 0; cell < 20; ++cell) {
        const bool unstored = ((cell % 5) == 4);
        const bool no_pvs   = unstored || (cell == 5);
        EXPECT_EQ(database->has_cell_pvs(cell), !no_pvs) << "cell " << cell;
        EXPECT_EQ(database->get_cell_words(cell).size(), unstored ? 0u : 2u) << "cell " << cell;
        EXPECT_EQ(database->get_cell_pvs_size(cell), no_pvs ? 0u : 2u) << "cell " << cell;
        for (uint32_t column = 0; column < 40; ++column) {
            const bool expected = no_pvs || is_expected_visible(cell, column);
            EXPECT_EQ(database->is_column_visible(cell, column), expected) << "cell " << cell << " column " << column;
        }
    }
    EXPECT_FALSE(database->is_column_visible(0, 40));
    EXPECT_TRUE (database->is_column_visible(20, 0)); // outside the grid: conservative
}

TEST(Pvs_database, column_lookup)
{
    const std::unique_ptr<erhe::scene::Pvs_database> database = make_database();
    EXPECT_EQ(database->find_column("node0", 0), 0u);
    EXPECT_EQ(database->find_column("node0", 1), 1u);
    EXPECT_EQ(database->find_column("node19", 1), 39u);
    EXPECT_EQ(database->find_column("node19", 2), erhe::scene::Pvs_database::untracked_column);
    EXPECT_EQ(database->find_column("node20", 0), erhe::scene::Pvs_database::untracked_column);
    EXPECT_EQ(database->find_column("", 0),       erhe::scene::Pvs_database::untracked_column);
}

TEST(Pvs_database, transform_hash_is_fnv1a_over_the_float_bits)
{
    const erhe::scene::Pvs_column column = make_column("a", 0, 1.5f);
    uint64_t expected = 0xcbf29ce484222325ull;
    const auto hash_floats = [&expected](const float* values, const std::size_t count) {
        for (std::size_t i = 0; i < count; ++i) {
            uint8_t bytes[4];
            std::memcpy(&bytes[0], &values[i], 4);
            for (const uint8_t byte : bytes) {
                expected = (expected ^ byte) * 0x100000001b3ull;
            }
        }
    };
    hash_floats(column.world_transform.data(), 12);
    hash_floats(column.world_bounds.data(), 6);
    EXPECT_EQ(column.transform_hash, expected);
    EXPECT_EQ(erhe::scene::compute_pvs_transform_hash(column.world_transform, column.world_bounds), expected);

    // Deterministic, and every float takes part: -0.0 differs from 0.0.
    erhe::scene::Pvs_column changed = column;
    changed.world_transform[1] = -0.0f;
    EXPECT_NE(erhe::scene::compute_pvs_transform_hash(changed.world_transform, changed.world_bounds), column.transform_hash);
    changed = column;
    changed.world_bounds[5] = std::nextafter(changed.world_bounds[5], 100.0f);
    EXPECT_NE(erhe::scene::compute_pvs_transform_hash(changed.world_transform, changed.world_bounds), column.transform_hash);
}

TEST(Pvs_database, validate_rejects_inconsistent_data)
{
    // 2 x 1 x 1 cells, page size 1: two pages, page 1 not stored.
    erhe::scene::Pvs_header header{};
    header.cell_counts = glm::uvec3{2u, 1u, 1u};
    header.page_size   = 1;
    const std::vector<erhe::scene::Pvs_column> columns{make_column("a", 0, 0.0f), make_column("b", 0, 1.0f)};
    const erhe::scene::Pvs_page_entry stored  {.word_offset = 0, .reserved = 0, .cells_with_pvs = 1};
    const erhe::scene::Pvs_page_entry unstored{};
    const std::vector<erhe::scene::Pvs_page_entry> pages{stored, unstored};
    const std::vector<uint32_t> bits{3u};
    EXPECT_EQ(erhe::scene::Pvs_database::validate(header, columns, pages, bits), std::string{});

    using erhe::scene::Pvs_page_entry;
    EXPECT_NE(erhe::scene::Pvs_database::validate(header, columns, pages, std::vector<uint32_t>{4u}), std::string{});      // padding bit
    EXPECT_NE(erhe::scene::Pvs_database::validate(header, columns, pages, std::vector<uint32_t>{3u, 0u}), std::string{});  // bits size
    EXPECT_NE(erhe::scene::Pvs_database::validate(header, columns, std::vector<Pvs_page_entry>{stored}, bits), std::string{}); // page table size
    EXPECT_NE(erhe::scene::Pvs_database::validate(header, columns, std::vector<Pvs_page_entry>{Pvs_page_entry{.word_offset = 1, .reserved = 0, .cells_with_pvs = 1}, unstored}, bits), std::string{});
    EXPECT_NE(erhe::scene::Pvs_database::validate(header, columns, std::vector<Pvs_page_entry>{Pvs_page_entry{.word_offset = 0, .reserved = 1, .cells_with_pvs = 1}, unstored}, bits), std::string{});
    EXPECT_NE(erhe::scene::Pvs_database::validate(header, columns, std::vector<Pvs_page_entry>{Pvs_page_entry{.word_offset = 0, .reserved = 0, .cells_with_pvs = 2}, unstored}, bits), std::string{}); // local cell past the page
    EXPECT_NE(erhe::scene::Pvs_database::validate(header, columns, std::vector<Pvs_page_entry>{stored, Pvs_page_entry{.word_offset = 0xffffffffu, .reserved = 0, .cells_with_pvs = 1}}, bits), std::string{});
    // A stored cell without a PVS holds all-ones words.
    EXPECT_NE(erhe::scene::Pvs_database::validate(header, columns, std::vector<Pvs_page_entry>{Pvs_page_entry{.word_offset = 0, .reserved = 0, .cells_with_pvs = 0}, unstored}, bits), std::string{});
    EXPECT_EQ(erhe::scene::Pvs_database::validate(header, columns, std::vector<Pvs_page_entry>{Pvs_page_entry{.word_offset = 0, .reserved = 0, .cells_with_pvs = 0}, unstored}, std::vector<uint32_t>{0xffffffffu}), std::string{});

    const std::vector<erhe::scene::Pvs_column> duplicate{make_column("a", 0, 0.0f), make_column("a", 0, 1.0f)};
    EXPECT_NE(erhe::scene::Pvs_database::validate(header, duplicate, pages, bits), std::string{});
    std::vector<erhe::scene::Pvs_column> bad_hash = columns;
    bad_hash[1].transform_hash ^= 1u;
    EXPECT_NE(erhe::scene::Pvs_database::validate(header, bad_hash, pages, bits), std::string{});
    erhe::scene::Pvs_header bad_header = header;
    bad_header.cell_size = 0.0f;
    EXPECT_NE(erhe::scene::Pvs_database::validate(bad_header, columns, pages, bits), std::string{});
    bad_header = header;
    bad_header.version = 2;
    EXPECT_NE(erhe::scene::Pvs_database::validate(bad_header, columns, pages, bits), std::string{});
    bad_header = header;
    bad_header.page_size = 5;
    EXPECT_NE(erhe::scene::Pvs_database::validate(bad_header, columns, pages, bits), std::string{});
    bad_header = header;
    bad_header.bake_parameters.method = static_cast<erhe::scene::Pvs_bake_method>(2);
    EXPECT_NE(erhe::scene::Pvs_database::validate(bad_header, columns, pages, bits), std::string{});
}

TEST(Pvs_database, bake_method_names_round_trip)
{
    using erhe::scene::Pvs_bake_method;
    for (const Pvs_bake_method method : {Pvs_bake_method::sampled, Pvs_bake_method::disocclusion}) {
        EXPECT_EQ(erhe::scene::parse_pvs_bake_method(erhe::scene::c_str(method)), method);
    }
    EXPECT_FALSE(erhe::scene::parse_pvs_bake_method("Sampled").has_value());
    EXPECT_FALSE(erhe::scene::parse_pvs_bake_method("").has_value());
}

TEST(Pvs_database, mark_stale_transitions_once)
{
    const std::unique_ptr<erhe::scene::Pvs_database> database = make_database();
    EXPECT_EQ(database->get_state(), erhe::scene::Pvs_state::ready);
    EXPECT_TRUE(database->mark_stale());
    EXPECT_EQ(database->get_state(), erhe::scene::Pvs_state::stale);
    EXPECT_FALSE(database->mark_stale());
    EXPECT_EQ(database->get_state(), erhe::scene::Pvs_state::stale);
}

namespace {

// A file holding the bits at a byte offset, between unrelated bytes, as a
// .glb holds them; removed by the destructor.
class Pvs_bits_file
{
public:
    explicit Pvs_bits_file(const std::vector<uint32_t>& bits, const std::string& name)
    {
        std::random_device random_device;
        path = std::filesystem::temp_directory_path() / ("erhe_scene_tests_" + name + "_" + std::to_string(random_device()) + ".bin");
        std::ofstream file{path, std::ios::binary};
        const std::string prefix(37, 'p');
        const std::string suffix(11, 's');
        file.write(prefix.data(), static_cast<std::streamsize>(prefix.size()));
        file.write(reinterpret_cast<const char*>(bits.data()), static_cast<std::streamsize>(bits.size() * sizeof(uint32_t)));
        file.write(suffix.data(), static_cast<std::streamsize>(suffix.size()));
        source = erhe::scene::Pvs_bits_source{
            .path        = path,
            .byte_offset = prefix.size(),
            .byte_length = bits.size() * sizeof(uint32_t)
        };
    }
    ~Pvs_bits_file() noexcept
    {
        std::error_code error;
        std::filesystem::remove(path, error);
    }
    Pvs_bits_file(const Pvs_bits_file&)            = delete;
    Pvs_bits_file& operator=(const Pvs_bits_file&) = delete;

    std::filesystem::path        path;
    erhe::scene::Pvs_bits_source source;
};

// make_database()'s data, with the bits also in a file.
[[nodiscard]] auto make_database_with_source(const erhe::scene::Pvs_database& model, const erhe::scene::Pvs_bits_source& source) -> std::unique_ptr<erhe::scene::Pvs_database>
{
    return std::make_unique<erhe::scene::Pvs_database>(
        model.get_header(), model.get_columns(), model.get_pages(), model.get_bits(), source
    );
}

} // anonymous namespace

TEST(Pvs_database, release_and_read_back_through_a_file_source)
{
    const std::unique_ptr<erhe::scene::Pvs_database> model = make_database();
    const std::vector<uint32_t> bits = model->get_bits();
    const Pvs_bits_file file{bits, "release"};
    const std::unique_ptr<erhe::scene::Pvs_database> database = make_database_with_source(*model, file.source);
    const uint64_t hash = database->get_bits_hash();
    EXPECT_EQ(hash, model->get_bits_hash());
    EXPECT_TRUE(database->bits_source_references(file.path));
    EXPECT_TRUE(database->has_resident_bits());

    // Released: nothing in memory, every read goes to the file.
    ASSERT_TRUE(database->release_bits());
    EXPECT_FALSE(database->has_resident_bits());
    EXPECT_TRUE(database->get_bits().empty());
    EXPECT_FALSE(database->release_bits()); // already released
    EXPECT_EQ(database->get_bits_hash(), hash);

    std::vector<uint32_t> page_words(database->get_words_per_page(), 0u);
    ASSERT_TRUE(database->read_page_words(0, std::span<uint32_t>{page_words}));
    EXPECT_EQ(page_words, (std::vector<uint32_t>{bits.begin(), bits.begin() + database->get_words_per_page()}));
    EXPECT_FALSE(database->read_page_words(1, std::span<uint32_t>{page_words})); // not stored
    std::vector<uint32_t> wrong_size(3, 0u);
    EXPECT_FALSE(database->read_page_words(0, std::span<uint32_t>{wrong_size}));

    std::vector<uint32_t> cell_words;
    for (uint32_t cell = 0; cell < database->get_cell_count(); ++cell) {
        if (!database->is_page_stored(database->page_of_cell(cell))) {
            EXPECT_FALSE(database->read_cell_words(cell, cell_words));
            continue;
        }
        ASSERT_TRUE(database->read_cell_words(cell, cell_words));
        ASSERT_EQ(cell_words.size(), database->get_words_per_cell());
        for (uint32_t column = 0; column < database->get_column_count(); ++column) {
            const bool visible = (cell_words[column / 32] & (uint32_t{1} << (column % 32))) != 0u;
            EXPECT_EQ(visible, database->has_cell_pvs(cell) ? is_expected_visible(cell, column) : true) << "cell " << cell << " column " << column;
        }
    }
    std::vector<uint32_t> all_bits;
    ASSERT_TRUE(database->read_bits(all_bits));
    EXPECT_EQ(all_bits, bits);

    // Resident again: the bits are in memory, the source is forgotten.
    ASSERT_TRUE(database->make_bits_resident());
    EXPECT_TRUE(database->has_resident_bits());
    EXPECT_EQ(database->get_bits(), bits);
    EXPECT_FALSE(database->get_bits_source().has_value());
    EXPECT_FALSE(database->bits_source_references(file.path));
    EXPECT_FALSE(database->release_bits()); // no source to read them back from
    EXPECT_EQ(database->get_bits_hash(), hash);
}

TEST(Pvs_database, release_needs_a_source_and_a_short_file_fails_the_reads)
{
    const std::unique_ptr<erhe::scene::Pvs_database> model = make_database();
    EXPECT_FALSE(model->release_bits()); // a bake's database: no source
    EXPECT_TRUE(model->has_resident_bits());

    // A source whose file ends before the bits do.
    const std::vector<uint32_t> bits = model->get_bits();
    const std::vector<uint32_t> short_bits{bits.begin(), bits.begin() + 4};
    Pvs_bits_file file{short_bits, "short"};
    file.source.byte_length = bits.size() * sizeof(uint32_t);
    const std::unique_ptr<erhe::scene::Pvs_database> database = make_database_with_source(*model, file.source);
    ASSERT_TRUE(database->release_bits());
    std::vector<uint32_t> page_words(database->get_words_per_page(), 0u);
    EXPECT_FALSE(database->read_page_words(0, std::span<uint32_t>{page_words}));
    std::vector<uint32_t> all_bits;
    EXPECT_FALSE(database->read_bits(all_bits));
    EXPECT_FALSE(database->make_bits_resident());
    EXPECT_FALSE(database->has_resident_bits());
}
