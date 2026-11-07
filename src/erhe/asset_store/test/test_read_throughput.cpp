#include "erhe_asset_store/asset_store.hpp"
#include "erhe_file/positional_file_reader.hpp"
#include "erhe_log/log.hpp"

#include <gtest/gtest.h>
#include <spdlog/spdlog.h>

#include <array>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#if defined(__linux__)
#   include <fcntl.h>
#   include <unistd.h>
#endif

// The payload and metadata read benchmarks of doc/erhe/asset_store.md "Reading payloads";
// disabled by default, run with --gtest_also_run_disabled_tests.
namespace {

using erhe::asset_store::Asset_store;
using erhe::asset_store::Item_data;
using erhe::asset_store::Item_kind;
using erhe::asset_store::Key;
using erhe::asset_store::Key_builder;
using erhe::asset_store::Open_mode;
using erhe::asset_store::Write_transaction;

constexpr std::size_t c_item_count  = 3000;
constexpr std::size_t c_range_count = 6;

// About 300 KB on average, as the mesh items of Bistro.
auto get_item_size(const std::size_t index) -> std::size_t
{
    return 100000u + static_cast<std::size_t>((static_cast<std::uint32_t>(index) * 2654435761u) % 400000u);
}

// The views of a mesh item, back to back: indices, vertex streams, points, LOD levels.
auto get_ranges(const std::size_t size) -> std::array<std::array<std::size_t, 2>, c_range_count>
{
    constexpr std::array<double, c_range_count> fractions{0.20, 0.35, 0.05, 0.15, 0.15, 0.10};
    std::array<std::array<std::size_t, 2>, c_range_count> ranges{};
    std::size_t offset = 0;
    for (std::size_t i = 0; i < c_range_count; ++i) {
        const std::size_t length = (i + 1 == c_range_count)
            ? (size - offset)
            : (static_cast<std::size_t>(static_cast<double>(size) * fractions[i]) & ~static_cast<std::size_t>(3));
        ranges[i] = {offset, length};
        offset += length;
    }
    return ranges;
}

auto get_key(const std::size_t index) -> Key
{
    return Key_builder{Item_kind::local_mesh}.add_uint_setting("index", index).finish();
}

enum class Read_pattern : unsigned int
{
    file_ranges,  // the file path: one positional read per range
    store_ranges, // one read_payload() per range
    store_whole   // one read_payload() of the whole item
};

auto c_str(const Read_pattern pattern) -> const char*
{
    switch (pattern) {
        case Read_pattern::file_ranges:  return "file, 6 ranges per item";
        case Read_pattern::store_ranges: return "store, 6 ranges per item";
        case Read_pattern::store_whole:  return "store, whole item";
        default:                         return "?";
    }
}

class Read_throughput_test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_directory = std::filesystem::temp_directory_path() / "erhe_asset_store_read_throughput";
        std::filesystem::remove_all(m_directory);
        std::filesystem::create_directories(m_directory);
        // One logger for every test of the fixture.
        m_log = spdlog::get("erhe.asset_store.bench");
        if (!m_log) {
            m_log = erhe::log::make_logger("erhe.asset_store.bench");
        }
        m_log->set_level(spdlog::level::info);
    }

    void TearDown() override
    {
        std::error_code error_code;
        std::filesystem::remove_all(m_directory, error_code);
    }

    std::filesystem::path           m_directory;
    std::shared_ptr<spdlog::logger> m_log;
};

// Drops the file's pages from the OS page cache, so the next reads come from the disk.
// posix_fadvise exists on Linux and Android only; elsewhere the cold pass is skipped.
auto evict_from_page_cache(const std::filesystem::path& path) -> bool
{
#if defined(__linux__)
    const int fd = ::open(path.c_str(), O_RDONLY);
    if (fd < 0) {
        return false;
    }
    const int result = ::posix_fadvise(fd, 0, 0, POSIX_FADV_DONTNEED);
    ::close(fd);
    return result == 0;
#else
    static_cast<void>(path);
    return false;
#endif
}

TEST_F(Read_throughput_test, DISABLED_mesh_item_reads)
{
    const std::filesystem::path store_path = m_directory / "bench.quest.erhe_prepared";
    const std::filesystem::path file_path  = m_directory / "bench.bin";
    std::vector<std::uint64_t> file_offsets(c_item_count + 1, 0);
    {
        std::unique_ptr<Asset_store> store = Asset_store::open(store_path, Open_mode::prepared_create);
        ASSERT_TRUE(store);
        std::ofstream file{file_path, std::ios::binary};
        Write_transaction transaction{*store};
        ASSERT_TRUE(transaction.is_active());
        std::vector<std::byte> payload;
        for (std::size_t i = 0; i < c_item_count; ++i) {
            payload.resize(get_item_size(i));
            std::uint32_t state = (static_cast<std::uint32_t>(i) * 747796405u) + 1u;
            for (std::byte& value : payload) {
                state = (state * 1664525u) + 1013904223u;
                value = static_cast<std::byte>(state >> 24u);
            }
            ASSERT_TRUE(transaction.put(Item_data{.key = get_key(i), .kind = Item_kind::local_mesh, .target = "quest", .stamp = "s", .meta = "{}", .info = "{}", .payload = payload}));
            file.write(reinterpret_cast<const char*>(payload.data()), static_cast<std::streamsize>(payload.size()));
            file_offsets[i + 1] = file_offsets[i] + payload.size();
        }
        ASSERT_TRUE(transaction.commit());
    }
    std::unique_ptr<Asset_store> store = Asset_store::open(store_path, Open_mode::prepared_read_only);
    ASSERT_TRUE(store);
    erhe::file::Positional_file_reader file_reader;
    ASSERT_TRUE(file_reader.open(file_path));
    m_log->info(
        "{} items, {} MB payload, store file {} MB",
        c_item_count, file_offsets.back() / 1000000u, std::filesystem::file_size(store_path) / 1000000u
    );

    for (const std::size_t thread_count : {std::size_t{1}, std::size_t{4}, std::size_t{8}}) {
        for (const Read_pattern pattern : {Read_pattern::file_ranges, Read_pattern::store_ranges, Read_pattern::store_whole}) {
            std::mutex  mutex;
            std::size_t next_item = 0;
            std::size_t failures  = 0;
            const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
            std::vector<std::thread> threads;
            for (std::size_t t = 0; t < thread_count; ++t) {
                threads.emplace_back([&]() {
                    std::vector<std::byte> out;
                    std::size_t local_failures = 0;
                    for (;;) {
                        std::size_t item = 0;
                        {
                            const std::lock_guard<std::mutex> lock{mutex};
                            item = next_item++;
                        }
                        if (item >= c_item_count) {
                            break;
                        }
                        const std::size_t size = get_item_size(item);
                        const Key         key  = get_key(item);
                        if (pattern == Read_pattern::store_whole) {
                            out.resize(size);
                            local_failures += store->read_payload(key, 0, out) ? 0 : 1;
                            continue;
                        }
                        for (const std::array<std::size_t, 2>& range : get_ranges(size)) {
                            out.resize(range[1]);
                            const bool ok = (pattern == Read_pattern::file_ranges)
                                ? file_reader.read_at(file_offsets[item] + range[0], out)
                                : store->read_payload(key, range[0], out);
                            local_failures += ok ? 0 : 1;
                        }
                    }
                    const std::lock_guard<std::mutex> lock{mutex};
                    failures += local_failures;
                });
            }
            for (std::thread& thread : threads) {
                thread.join();
            }
            const double milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
            EXPECT_EQ(failures, 0u);
            m_log->info("{} thread(s), {:<26}: {:8.1f} ms", thread_count, c_str(pattern), milliseconds);
        }
    }
}


// The format stamp check of a prepared run: the kind and stamp of every item (doc/erhe/asset_store.md "Container").
TEST_F(Read_throughput_test, DISABLED_item_info_reads)
{
    constexpr std::size_t c_info_item_count = 8000;
    const std::filesystem::path store_path = m_directory / "info.quest.erhe_prepared";
    const std::string meta(1200, 'm');
    const std::string info(300, 'i');
    {
        std::unique_ptr<Asset_store> store = Asset_store::open(store_path, Open_mode::prepared_create);
        ASSERT_TRUE(store);
        Write_transaction transaction{*store};
        ASSERT_TRUE(transaction.is_active());
        std::vector<std::byte> payload;
        for (std::size_t i = 0; i < c_info_item_count; ++i) {
            payload.assign(get_item_size(i) / 2, std::byte{static_cast<unsigned char>(i)});
            ASSERT_TRUE(transaction.put(Item_data{.key = get_key(i), .kind = Item_kind::local_mesh, .target = "quest", .stamp = "s", .meta = meta, .info = info, .payload = payload}));
        }
        ASSERT_TRUE(transaction.commit());
    }
    m_log->info("{} items, store file {} MB", c_info_item_count, std::filesystem::file_size(store_path) / 1000000u);
    for (const bool cold : {true, false}) {
        if (cold && !evict_from_page_cache(store_path)) {
            m_log->info("page cache eviction unavailable; skipping the cold pass");
            continue;
        }
        const std::chrono::steady_clock::time_point open_start = std::chrono::steady_clock::now();
        std::unique_ptr<Asset_store> store = Asset_store::open(store_path, Open_mode::prepared_read_only);
        ASSERT_TRUE(store);
        const std::chrono::steady_clock::time_point lookup_start = std::chrono::steady_clock::now();
        std::size_t found = 0;
        for (std::size_t i = 0; i < c_info_item_count; ++i) {
            found += store->get_info(get_key(i)).has_value() ? 1 : 0;
        }
        const std::chrono::steady_clock::time_point lookup_end = std::chrono::steady_clock::now();
        EXPECT_EQ(found, c_info_item_count);
        store.reset();
        if (cold) {
            ASSERT_TRUE(evict_from_page_cache(store_path));
        }
        store = Asset_store::open(store_path, Open_mode::prepared_read_only);
        ASSERT_TRUE(store);
        const std::chrono::steady_clock::time_point scan_start = std::chrono::steady_clock::now();
        std::size_t listed = 0;
        EXPECT_TRUE(store->for_each_item([&listed](const erhe::asset_store::Item_info&) { ++listed; }));
        const std::chrono::steady_clock::time_point scan_end = std::chrono::steady_clock::now();
        EXPECT_EQ(listed, c_info_item_count);
        store.reset();
        if (cold) {
            ASSERT_TRUE(evict_from_page_cache(store_path));
        }
        store = Asset_store::open(store_path, Open_mode::prepared_read_only);
        ASSERT_TRUE(store);
        const std::chrono::steady_clock::time_point headers_start = std::chrono::steady_clock::now();
        const std::optional<std::vector<erhe::asset_store::Item_info>> headers = store->get_item_headers();
        const std::chrono::steady_clock::time_point headers_end = std::chrono::steady_clock::now();
        ASSERT_TRUE(headers.has_value());
        EXPECT_EQ(headers->size(), c_info_item_count);
        m_log->info(
            "{}: open {:6.1f} ms, {} get_info {:7.1f} ms, for_each_item {:7.1f} ms, get_item_headers {:7.1f} ms",
            cold ? "cold" : "warm",
            std::chrono::duration<double, std::milli>(lookup_start - open_start).count(),
            c_info_item_count,
            std::chrono::duration<double, std::milli>(lookup_end - lookup_start).count(),
            std::chrono::duration<double, std::milli>(scan_end - scan_start).count(),
            std::chrono::duration<double, std::milli>(headers_end - headers_start).count()
        );
    }
}

}
