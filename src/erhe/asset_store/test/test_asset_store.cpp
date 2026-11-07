#include "erhe_asset_store/asset_store.hpp"

#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#if !defined(_WIN32)
#   include <signal.h>
#   include <sys/types.h>
#   include <sys/wait.h>
#   include <unistd.h>
#endif

namespace {

using erhe::asset_store::Asset_store;
using erhe::asset_store::Content_hasher;
using erhe::asset_store::Item_data;
using erhe::asset_store::Item_info;
using erhe::asset_store::Item_kind;
using erhe::asset_store::Key;
using erhe::asset_store::Key_builder;
using erhe::asset_store::Open_mode;
using erhe::asset_store::Prepared_scene_record;
using erhe::asset_store::Source_record;
using erhe::asset_store::Write_transaction;

// Counts failures reported from worker threads.
class Failure_count
{
public:
    void add()
    {
        const std::lock_guard<std::mutex> lock{m_mutex};
        ++m_count;
    }
    [[nodiscard]] auto get() -> int
    {
        const std::lock_guard<std::mutex> lock{m_mutex};
        return m_count;
    }

private:
    std::mutex m_mutex;
    int        m_count{0};
};

auto make_payload(const std::size_t size, const std::uint32_t seed) -> std::vector<std::byte>
{
    std::vector<std::byte> payload(size);
    std::uint32_t state = seed * 747796405u + 2891336453u;
    for (std::byte& value : payload) {
        state = (state * 1664525u) + 1013904223u;
        value = static_cast<std::byte>(state >> 24u);
    }
    return payload;
}

auto make_key(const std::uint64_t index) -> Key
{
    return Key_builder{Item_kind::local_mesh}.add_uint_setting("index", index).finish();
}

auto make_item(const Key& key, const std::vector<std::byte>& payload) -> Item_data
{
    return Item_data{
        .key     = key,
        .kind    = Item_kind::local_mesh,
        .target  = "pc",
        .stamp   = "stamp-1",
        .meta    = "{\"accessors\":[]}",
        .info    = "{\"nodes\":[]}",
        .payload = payload
    };
}

auto read_all(const Asset_store& store, const Key& key) -> std::optional<std::vector<std::byte>>
{
    const std::optional<Item_info> info = store.get_info(key);
    if (!info.has_value()) {
        return {};
    }
    std::vector<std::byte> bytes(static_cast<std::size_t>(info->payload_size));
    if (!store.read_payload(key, 0, bytes)) {
        return {};
    }
    return bytes;
}

class Asset_store_test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        const ::testing::TestInfo* test_info = ::testing::UnitTest::GetInstance()->current_test_info();
        m_directory =
            std::filesystem::temp_directory_path() / "erhe_asset_store_tests" /
            (std::string{test_info->name()} + "_" + std::to_string(static_cast<long long>(get_process_id())));
        std::filesystem::remove_all(m_directory);
        std::filesystem::create_directories(m_directory);
    }

    void TearDown() override
    {
        std::error_code error_code;
        std::filesystem::remove_all(m_directory, error_code);
    }

    [[nodiscard]] auto path(const char* name) const -> std::filesystem::path
    {
        return m_directory / name;
    }

    static auto get_process_id() -> long long
    {
#if defined(_WIN32)
        return 0;
#else
        return static_cast<long long>(getpid());
#endif
    }

    std::filesystem::path m_directory;
};

TEST_F(Asset_store_test, put_get_and_range_read)
{
    std::unique_ptr<Asset_store> store = Asset_store::open(path("a.erhe_editor"), Open_mode::editor_read_write);
    ASSERT_TRUE(store);
    EXPECT_EQ(store->get_stored_format_version(), Asset_store::format_version);

    const Key                    key     = make_key(1);
    const std::vector<std::byte> payload = make_payload(100000, 1);
    EXPECT_FALSE(store->contains(key));
    ASSERT_TRUE(store->put(make_item(key, payload)));
    EXPECT_TRUE(store->contains(key));

    const std::optional<Item_info> info = store->get_info(key);
    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(info->key, key);
    EXPECT_EQ(info->kind, Item_kind::local_mesh);
    EXPECT_EQ(info->target, "pc");
    EXPECT_EQ(info->stamp, "stamp-1");
    EXPECT_EQ(info->meta, "{\"accessors\":[]}");
    EXPECT_EQ(info->info, "{\"nodes\":[]}");
    EXPECT_EQ(info->payload_size, payload.size());
    EXPECT_EQ(store->read_meta(key), std::optional<std::string>{"{\"accessors\":[]}"});

    EXPECT_EQ(read_all(*store, key), std::optional<std::vector<std::byte>>{payload});
    const std::size_t ranges[4][2] = {{0, 10}, {12345, 1000}, {payload.size() - 17, 17}, {payload.size(), 0}};
    for (const std::size_t (&range)[2] : ranges) {
        std::vector<std::byte> out(range[1]);
        ASSERT_TRUE(store->read_payload(key, range[0], out));
        EXPECT_TRUE(std::equal(out.begin(), out.end(), payload.begin() + static_cast<std::ptrdiff_t>(range[0])));
    }
    std::vector<std::byte> past_end(2);
    EXPECT_FALSE(store->read_payload(key, payload.size() - 1, past_end));
    EXPECT_FALSE(store->read_payload(make_key(999), 0, past_end));
    EXPECT_FALSE(store->get_info(make_key(999)).has_value());

    // An empty payload and a put that replaces an item of the same key.
    const Key empty_key = make_key(2);
    ASSERT_TRUE(store->put(make_item(empty_key, {})));
    EXPECT_EQ(store->get_info(empty_key)->payload_size, 0u);
    EXPECT_TRUE(store->read_payload(empty_key, 0, {}));
    Item_data replacement = make_item(key, payload);
    replacement.info = "{\"nodes\":[\"a\"]}";
    ASSERT_TRUE(store->put(replacement));
    EXPECT_EQ(store->get_info(key)->info, "{\"nodes\":[\"a\"]}");
    EXPECT_EQ(store->get_item_count(), std::optional<std::uint64_t>{2});

    // Default-constructed (null) text views are stored as empty text.
    const Key no_text_key = make_key(3);
    Item_data no_text = make_item(no_text_key, payload);
    no_text.target = {};
    no_text.info   = {};
    ASSERT_TRUE(store->put(no_text));
    EXPECT_EQ(store->get_info(no_text_key)->target, "");
    EXPECT_EQ(store->get_info(no_text_key)->info, "");
    ASSERT_TRUE(store->remove(no_text_key));

    std::vector<Key> listed;
    ASSERT_TRUE(store->for_each_item([&listed](const Item_info& item) { listed.push_back(item.key); }));
    EXPECT_EQ(listed.size(), 2u);
    EXPECT_TRUE(std::is_sorted(listed.begin(), listed.end()));

    // Reopening finds the committed items.
    store.reset();
    store = Asset_store::open(path("a.erhe_editor"), Open_mode::editor_read_write);
    ASSERT_TRUE(store);
    EXPECT_EQ(read_all(*store, key), std::optional<std::vector<std::byte>>{payload});
}

TEST_F(Asset_store_test, parallel_range_reads)
{
    std::unique_ptr<Asset_store> store = Asset_store::open(path("a.erhe_editor"), Open_mode::editor_read_write);
    ASSERT_TRUE(store);
    std::vector<std::vector<std::byte>> payloads;
    for (std::uint32_t i = 0; i < 8; ++i) {
        payloads.push_back(make_payload(50000 + (i * 1000), i));
        ASSERT_TRUE(store->put(make_item(make_key(i), payloads.back())));
    }
    Failure_count failures;
    std::vector<std::thread> threads;
    for (std::uint32_t t = 0; t < 8; ++t) {
        threads.emplace_back([&, t]() {
            std::vector<std::byte> out(4096);
            for (std::uint32_t round = 0; round < 200; ++round) {
                const std::uint32_t      item   = (t + round) % 8;
                const std::size_t        offset = (round * 997u) % (payloads[item].size() - out.size());
                const bool               ok     = store->read_payload(make_key(item), offset, out);
                if (!ok || !std::equal(out.begin(), out.end(), payloads[item].begin() + static_cast<std::ptrdiff_t>(offset))) {
                    failures.add();
                }
            }
        });
    }
    for (std::thread& thread : threads) {
        thread.join();
    }
    EXPECT_EQ(failures.get(), 0);
}

// A prepared_read_only store keeps each read connection's payload open between reads until close_payload_handles().
TEST_F(Asset_store_test, read_only_store_keeps_payloads_open_until_closed)
{
    const std::filesystem::path  store_path = path("a.quest.erhe_prepared");
    const std::vector<std::byte> payload    = make_payload(700000, 7);
    {
        std::unique_ptr<Asset_store> writer = Asset_store::open(store_path, Open_mode::prepared_create);
        ASSERT_TRUE(writer);
        // New containers use 64 KiB pages: the empty schema is a few whole pages.
        const std::uint64_t empty_size = writer->get_database_size().value_or(0);
        EXPECT_GT(empty_size, 0u);
        EXPECT_EQ(empty_size % 65536u, 0u);
        ASSERT_TRUE(writer->put(make_item(make_key(1), payload)));
    }
    std::unique_ptr<Asset_store> reader = Asset_store::open(store_path, Open_mode::prepared_read_only);
    ASSERT_TRUE(reader);
    const std::size_t ranges[5][2] = {{0, 140000}, {140000, 245000}, {600000, 100000}, {385000, 215000}, {12345, 1}};
    for (const std::size_t (&range)[2] : ranges) {
        std::vector<std::byte> out(range[1]);
        ASSERT_TRUE(reader->read_payload(make_key(1), range[0], out));
        EXPECT_TRUE(std::equal(out.begin(), out.end(), payload.begin() + static_cast<std::ptrdiff_t>(range[0])));
    }
    std::vector<std::byte> past_end(2);
    EXPECT_FALSE(reader->read_payload(make_key(1), payload.size() - 1, past_end));

    // A prepared asset is written once: nothing opens it for writing again.
    EXPECT_FALSE(Asset_store::open(store_path, Open_mode::prepared_create));

    reader->close_payload_handles();
    // Reads after the close hold nothing open.
    EXPECT_EQ(read_all(*reader, make_key(1)), std::optional<std::vector<std::byte>>{payload});
    std::vector<std::byte> range(1000);
    ASSERT_TRUE(reader->read_payload(make_key(1), 5000, range));
    EXPECT_TRUE(std::equal(range.begin(), range.end(), payload.begin() + 5000));
}

TEST_F(Asset_store_test, uncommitted_transaction_rolls_back)
{
    std::unique_ptr<Asset_store> store = Asset_store::open(path("a.erhe_editor"), Open_mode::editor_read_write);
    ASSERT_TRUE(store);
    const std::vector<std::byte> payload = make_payload(1000, 3);
    {
        Write_transaction transaction{*store};
        ASSERT_TRUE(transaction.is_active());
        ASSERT_TRUE(transaction.put(make_item(make_key(1), payload)));
        ASSERT_TRUE(transaction.put(make_item(make_key(2), payload)));
        // Readers do not see an uncommitted write.
        EXPECT_FALSE(store->contains(make_key(1)));
    }
    EXPECT_FALSE(store->contains(make_key(1)));
    EXPECT_FALSE(store->contains(make_key(2)));

    {
        Write_transaction transaction{*store};
        ASSERT_TRUE(transaction.put(make_item(make_key(1), payload)));
        ASSERT_TRUE(transaction.put(make_item(make_key(2), payload)));
        ASSERT_TRUE(transaction.commit());
    }
    EXPECT_TRUE(store->contains(make_key(1)));
    EXPECT_TRUE(store->contains(make_key(2)));
}

#if !defined(_WIN32)
TEST_F(Asset_store_test, process_killed_mid_write_leaves_no_partial_item)
{
    const std::filesystem::path store_path = path("a.erhe_editor");
    const Key committed_key = make_key(1);
    const Key partial_key   = make_key(2);
    const std::vector<std::byte> committed_payload = make_payload(200000, 1);
    {
        std::unique_ptr<Asset_store> store = Asset_store::open(store_path, Open_mode::editor_read_write);
        ASSERT_TRUE(store);
        ASSERT_TRUE(store->put(make_item(committed_key, committed_payload)));
    }

    const pid_t child = fork();
    ASSERT_GE(child, 0);
    if (child == 0) {
        // Large enough to spill uncommitted pages out of the page cache into the WAL file.
        std::unique_ptr<Asset_store> store = Asset_store::open(store_path, Open_mode::editor_read_write);
        if (!store) {
            _exit(2);
        }
        const std::vector<std::byte> partial_payload = make_payload(32 * 1024 * 1024, 2);
        Write_transaction transaction{*store};
        if (!transaction.is_active() || !transaction.put(make_item(partial_key, partial_payload))) {
            _exit(3);
        }
        kill(getpid(), SIGKILL);
        _exit(4);
    }
    int status = 0;
    ASSERT_EQ(waitpid(child, &status, 0), child);
    ASSERT_TRUE(WIFSIGNALED(status)) << "child exit status " << (WIFEXITED(status) ? WEXITSTATUS(status) : -1);
    EXPECT_EQ(WTERMSIG(status), SIGKILL);

    std::filesystem::path wal_path = store_path;
    wal_path += "-wal";
    EXPECT_TRUE(std::filesystem::exists(wal_path)); // the killed writer left its WAL behind

    std::unique_ptr<Asset_store> store = Asset_store::open(store_path, Open_mode::editor_read_write);
    ASSERT_TRUE(store);
    EXPECT_FALSE(store->contains(partial_key));
    EXPECT_FALSE(store->get_info(partial_key).has_value());
    EXPECT_EQ(store->get_item_count(), std::optional<std::uint64_t>{1});
    EXPECT_EQ(read_all(*store, committed_key), std::optional<std::vector<std::byte>>{committed_payload});

    // The store stays writable after the recovery.
    ASSERT_TRUE(store->put(make_item(partial_key, committed_payload)));
    EXPECT_TRUE(store->contains(partial_key));
}
#endif

TEST_F(Asset_store_test, two_writers)
{
    const std::filesystem::path store_path = path("a.erhe_editor");
    std::unique_ptr<Asset_store> first  = Asset_store::open(store_path, Open_mode::editor_read_write);
    std::unique_ptr<Asset_store> second = Asset_store::open(store_path, Open_mode::editor_read_write);
    ASSERT_TRUE(first);
    ASSERT_TRUE(second);

    // Writers through separate connections take turns within the busy timeout.
    constexpr std::uint64_t item_count = 40;
    Failure_count failures;
    std::thread first_thread([&]() {
        for (std::uint64_t i = 0; i < item_count; ++i) {
            if (!first->put(make_item(make_key(i), make_payload(20000, static_cast<std::uint32_t>(i))))) {
                failures.add();
            }
        }
    });
    std::thread second_thread([&]() {
        for (std::uint64_t i = item_count; i < (2 * item_count); ++i) {
            if (!second->put(make_item(make_key(i), make_payload(20000, static_cast<std::uint32_t>(i))))) {
                failures.add();
            }
        }
    });
    first_thread.join();
    second_thread.join();
    EXPECT_EQ(failures.get(), 0);
    EXPECT_EQ(first->get_item_count(), std::optional<std::uint64_t>{2 * item_count});
    EXPECT_EQ(second->get_item_count(), std::optional<std::uint64_t>{2 * item_count});

    // A writer that does not get its turn within the timeout stores nothing.
    second->set_busy_timeout(std::chrono::milliseconds{100});
    {
        Write_transaction holding{*first};
        ASSERT_TRUE(holding.is_active());
        ASSERT_TRUE(holding.put(make_item(make_key(1000), make_payload(10, 0))));
        const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
        Write_transaction waiting{*second};
        const std::chrono::steady_clock::duration waited = std::chrono::steady_clock::now() - start;
        EXPECT_FALSE(waiting.is_active());
        EXPECT_FALSE(waiting.put(make_item(make_key(1001), make_payload(10, 0))));
        EXPECT_GE(waited, std::chrono::milliseconds{90});
        EXPECT_LT(waited, std::chrono::seconds{5});
        ASSERT_TRUE(holding.commit());
    }
    EXPECT_TRUE(second->put(make_item(make_key(1001), make_payload(10, 0))));
    EXPECT_TRUE(first->contains(make_key(1000)));
    EXPECT_TRUE(first->contains(make_key(1001)));
}

TEST_F(Asset_store_test, format_version_handling)
{
    const std::filesystem::path store_path = path("a.erhe_editor");
    {
        std::unique_ptr<Asset_store> store = Asset_store::open(store_path, Open_mode::editor_read_write);
        ASSERT_TRUE(store);
        ASSERT_TRUE(store->put(make_item(make_key(1), make_payload(100, 1))));
        Write_transaction transaction{*store};
        ASSERT_TRUE(transaction.set_record("format_version", "999"));
        ASSERT_TRUE(transaction.commit());
    }
    // A prepared asset of an unread version fails to open.
    EXPECT_FALSE(Asset_store::open(store_path, Open_mode::prepared_read_only));
    // An editor asset of an unread version is recreated empty.
    {
        std::unique_ptr<Asset_store> store = Asset_store::open(store_path, Open_mode::editor_read_write);
        ASSERT_TRUE(store);
        EXPECT_EQ(store->get_stored_format_version(), Asset_store::format_version);
        EXPECT_EQ(store->get_item_count(), std::optional<std::uint64_t>{0});
    }
    EXPECT_TRUE(Asset_store::open(store_path, Open_mode::prepared_read_only));

    // A file that is not a container at all.
    const std::filesystem::path garbage_path = path("garbage.erhe_editor");
    {
        std::ofstream garbage{garbage_path, std::ios::binary};
        garbage << "this is not an SQLite database, only some text long enough to look like a header";
    }
    EXPECT_FALSE(Asset_store::open(garbage_path, Open_mode::prepared_read_only));
    {
        std::unique_ptr<Asset_store> store = Asset_store::open(garbage_path, Open_mode::editor_read_write);
        ASSERT_TRUE(store);
        EXPECT_EQ(store->get_stored_format_version(), Asset_store::format_version);
    }

    // A missing prepared asset fails to open and is not created.
    EXPECT_FALSE(Asset_store::open(path("missing.pc.erhe_prepared"), Open_mode::prepared_read_only));
    EXPECT_FALSE(std::filesystem::exists(path("missing.pc.erhe_prepared")));
}

TEST_F(Asset_store_test, records)
{
    std::unique_ptr<Asset_store> store = Asset_store::open(path("a.erhe_editor"), Open_mode::editor_read_write);
    ASSERT_TRUE(store);
    EXPECT_FALSE(store->get_source_record().has_value());
    EXPECT_FALSE(store->get_prepared_scene().has_value());
    const Key scene_key = Key_builder{Item_kind::scene}.add_code_version(1).finish();
    {
        Write_transaction transaction{*store};
        ASSERT_TRUE(transaction.set_source_record(Source_record{.relative_path = "a.glb", .scene_id = "1234"}));
        ASSERT_TRUE(transaction.set_prepared_path("pc", "a.pc.erhe_prepared"));
        ASSERT_TRUE(transaction.set_prepared_path("quest", "a.quest.erhe_prepared"));
        ASSERT_TRUE(transaction.set_prepared_scene(Prepared_scene_record{.target = "pc", .scene_key = scene_key}));
        ASSERT_TRUE(transaction.commit());
    }
    const std::optional<Source_record> source = store->get_source_record();
    ASSERT_TRUE(source.has_value());
    EXPECT_EQ(source->relative_path, "a.glb");
    EXPECT_EQ(source->scene_id, "1234");
    EXPECT_EQ(store->get_prepared_path("pc"), std::optional<std::string>{"a.pc.erhe_prepared"});
    EXPECT_EQ(store->get_prepared_path("quest"), std::optional<std::string>{"a.quest.erhe_prepared"});
    EXPECT_FALSE(store->get_prepared_path("other").has_value());
    const std::optional<Prepared_scene_record> prepared = store->get_prepared_scene();
    ASSERT_TRUE(prepared.has_value());
    EXPECT_EQ(prepared->target, "pc");
    EXPECT_EQ(prepared->scene_key, scene_key);
}

TEST_F(Asset_store_test, copy_item_to)
{
    std::unique_ptr<Asset_store> editor = Asset_store::open(path("a.erhe_editor"), Open_mode::editor_read_write);
    ASSERT_TRUE(editor);
    const std::vector<std::byte> large_payload = make_payload((4 * 1024 * 1024) + 123, 7);
    const std::vector<std::byte> small_payload = make_payload(10, 8);
    ASSERT_TRUE(editor->put(make_item(make_key(1), large_payload)));
    ASSERT_TRUE(editor->put(make_item(make_key(2), small_payload)));
    ASSERT_TRUE(editor->put(make_item(make_key(3), {})));

    const std::filesystem::path prepared_path = path("a.pc.erhe_prepared");
    {
        std::unique_ptr<Asset_store> prepared = Asset_store::open(prepared_path, Open_mode::prepared_create);
        ASSERT_TRUE(prepared);
        EXPECT_TRUE(editor->copy_item_to(*prepared, make_key(1)));
        EXPECT_TRUE(editor->copy_item_to(*prepared, make_key(3)));
        EXPECT_FALSE(editor->copy_item_to(*prepared, make_key(99)));
        // Copying again replaces the item in place.
        EXPECT_TRUE(editor->copy_item_to(*prepared, make_key(1)));
        EXPECT_FALSE(editor->copy_item_to(*editor, make_key(1)));
    }

    // At rest a prepared asset is one file.
    std::size_t file_count = 0;
    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator{m_directory}) {
        if (entry.path().filename().string().starts_with("a.pc.erhe_prepared")) {
            ++file_count;
        }
    }
    EXPECT_EQ(file_count, 1u);

    std::unique_ptr<Asset_store> prepared = Asset_store::open(prepared_path, Open_mode::prepared_read_only);
    ASSERT_TRUE(prepared);
    EXPECT_EQ(prepared->get_item_count(), std::optional<std::uint64_t>{2});
    EXPECT_FALSE(prepared->contains(make_key(2)));
    const std::optional<Item_info> source_info = editor->get_info(make_key(1));
    const std::optional<Item_info> copied_info = prepared->get_info(make_key(1));
    ASSERT_TRUE(source_info.has_value());
    ASSERT_TRUE(copied_info.has_value());
    EXPECT_EQ(copied_info->kind, source_info->kind);
    EXPECT_EQ(copied_info->target, source_info->target);
    EXPECT_EQ(copied_info->stamp, source_info->stamp);
    EXPECT_EQ(copied_info->meta, source_info->meta);
    EXPECT_EQ(copied_info->info, source_info->info);
    EXPECT_EQ(read_all(*prepared, make_key(1)), std::optional<std::vector<std::byte>>{large_payload});
    EXPECT_EQ(prepared->get_info(make_key(3))->payload_size, 0u);

    // A read-only asset refuses writes.
    EXPECT_FALSE(prepared->put(make_item(make_key(5), small_payload)));
    Write_transaction transaction{*prepared};
    EXPECT_FALSE(transaction.is_active());
}

TEST_F(Asset_store_test, set_info_replaces_the_info_alone)
{
    std::unique_ptr<Asset_store> store = Asset_store::open(path("info.erhe_editor"), Open_mode::editor_read_write);
    ASSERT_TRUE(store);
    const std::vector<std::byte> payload = make_payload(64, 3);
    ASSERT_TRUE(store->put(make_item(make_key(1), payload)));
    const std::optional<Item_info> before = store->get_info(make_key(1));
    ASSERT_TRUE(before.has_value());
    {
        Write_transaction transaction{*store};
        ASSERT_TRUE(transaction.is_active());
        EXPECT_TRUE(transaction.set_info(make_key(1), "{\"nodes\":[1]}"));
        EXPECT_FALSE(transaction.set_info(make_key(2), "{}"));
        EXPECT_TRUE(transaction.commit());
    }
    const std::optional<Item_info> after = store->get_info(make_key(1));
    ASSERT_TRUE(after.has_value());
    EXPECT_EQ(after->info, "{\"nodes\":[1]}");
    EXPECT_NE(before->info, after->info);
    EXPECT_EQ(after->meta, before->meta);
    EXPECT_EQ(after->stamp, before->stamp);
    EXPECT_EQ(read_all(*store, make_key(1)), std::optional<std::vector<std::byte>>{payload});
    EXPECT_FALSE(store->contains(make_key(2)));
}

TEST_F(Asset_store_test, remove_and_size_after_vacuum)
{
    for (const Open_mode mode : {Open_mode::editor_read_write, Open_mode::prepared_create}) {
        const std::filesystem::path store_path = path((mode == Open_mode::editor_read_write) ? "a.erhe_editor" : "a.pc.erhe_prepared");
        std::unique_ptr<Asset_store> store = Asset_store::open(store_path, mode);
        ASSERT_TRUE(store);
        constexpr std::uint64_t item_count = 8;
        constexpr std::size_t   item_size  = 1024 * 1024;
        for (std::uint64_t i = 0; i < item_count; ++i) {
            ASSERT_TRUE(store->put(make_item(make_key(i), make_payload(item_size, static_cast<std::uint32_t>(i)))));
        }
        const std::uint64_t full_size = store->get_database_size().value_or(0);
        EXPECT_GE(full_size, item_count * item_size);

        for (std::uint64_t i = 1; i < item_count; ++i) {
            EXPECT_TRUE(store->remove(make_key(i)));
        }
        EXPECT_FALSE(store->remove(make_key(1)));
        EXPECT_FALSE(store->contains(make_key(1)));
        EXPECT_TRUE(store->contains(make_key(0)));
        // Removed pages stay in the file until VACUUM.
        EXPECT_EQ(store->get_database_size().value_or(0), full_size);

        ASSERT_TRUE(store->vacuum());
        const std::uint64_t vacuumed_size = store->get_database_size().value_or(0);
        EXPECT_LT(vacuumed_size, 2 * item_size);
        EXPECT_GE(vacuumed_size, item_size);
        EXPECT_LT(std::filesystem::file_size(store_path), 2 * item_size);
        EXPECT_EQ(read_all(*store, make_key(0)), std::optional<std::vector<std::byte>>{make_payload(item_size, 0)});
    }
}

TEST_F(Asset_store_test, item_headers_in_key_order_without_meta_and_info)
{
    std::unique_ptr<Asset_store> store = Asset_store::open(path("headers.erhe_editor"), Open_mode::editor_read_write);
    ASSERT_TRUE(store);
    constexpr std::uint64_t item_count = 16;
    for (std::uint64_t i = 0; i < item_count; ++i) {
        Item_data item = make_item(make_key(i), {});
        const std::vector<std::byte> payload = make_payload(100 + static_cast<std::size_t>(i), static_cast<std::uint32_t>(i));
        item.payload = payload;
        item.kind    = ((i % 2) == 0) ? Item_kind::local_mesh : Item_kind::raytrace_bvh;
        ASSERT_TRUE(store->put(item));
    }
    const std::optional<std::vector<Item_info>> headers = store->get_item_headers();
    ASSERT_TRUE(headers.has_value());
    ASSERT_EQ(headers->size(), item_count);
    for (std::size_t i = 0; i < headers->size(); ++i) {
        const Item_info& header = headers->at(i);
        if (i > 0) {
            EXPECT_LT(headers->at(i - 1).key, header.key);
        }
        const std::optional<Item_info> info = store->get_info(header.key);
        ASSERT_TRUE(info.has_value());
        EXPECT_EQ(header.kind, info->kind);
        EXPECT_EQ(header.target, info->target);
        EXPECT_EQ(header.stamp, info->stamp);
        EXPECT_EQ(header.payload_size, info->payload_size);
        EXPECT_TRUE(header.meta.empty());
        EXPECT_TRUE(header.info.empty());
        EXPECT_FALSE(info->meta.empty());
    }
}

// The payload table: a replaced item gets its new payload and size, and VACUUM keeps every item's payload with its row.
TEST_F(Asset_store_test, payload_rows_follow_their_items)
{
    for (const Open_mode mode : {Open_mode::editor_read_write, Open_mode::prepared_create}) {
        const std::filesystem::path store_path = path((mode == Open_mode::editor_read_write) ? "rows.erhe_editor" : "rows.pc.erhe_prepared");
        std::unique_ptr<Asset_store> store = Asset_store::open(store_path, mode);
        ASSERT_TRUE(store);
        constexpr std::uint64_t item_count = 10;
        for (std::uint64_t i = 0; i < item_count; ++i) {
            ASSERT_TRUE(store->put(make_item(make_key(i), make_payload(70000 + (1000 * static_cast<std::size_t>(i)), static_cast<std::uint32_t>(i)))));
        }
        const std::vector<std::byte> replaced = make_payload(123, 77);
        ASSERT_TRUE(store->put(make_item(make_key(4), replaced)));
        EXPECT_EQ(store->get_info(make_key(4))->payload_size, replaced.size());
        EXPECT_EQ(read_all(*store, make_key(4)), std::optional<std::vector<std::byte>>{replaced});

        for (const std::uint64_t i : {std::uint64_t{0}, std::uint64_t{3}, std::uint64_t{7}}) {
            EXPECT_TRUE(store->remove(make_key(i)));
        }
        ASSERT_TRUE(store->vacuum());
        ASSERT_TRUE(store->put(make_item(make_key(100), make_payload(5000, 100))));
        EXPECT_EQ(store->get_item_count(), std::optional<std::uint64_t>{item_count - 2});
        for (std::uint64_t i = 0; i < item_count; ++i) {
            if ((i == 0) || (i == 3) || (i == 7)) {
                EXPECT_FALSE(store->contains(make_key(i)));
                continue;
            }
            const std::vector<std::byte> expected = (i == 4) ? replaced : make_payload(70000 + (1000 * static_cast<std::size_t>(i)), static_cast<std::uint32_t>(i));
            EXPECT_EQ(read_all(*store, make_key(i)), std::optional<std::vector<std::byte>>{expected});
        }
        EXPECT_EQ(read_all(*store, make_key(100)), std::optional<std::vector<std::byte>>{make_payload(5000, 100)});
    }
}

// A container of the previous format version: refused as a prepared asset with the reason, never written again.
TEST_F(Asset_store_test, previous_format_version)
{
    const std::filesystem::path store_path = path("old.quest.erhe_prepared");
    {
        std::unique_ptr<Asset_store> store = Asset_store::open(store_path, Open_mode::prepared_create);
        ASSERT_TRUE(store);
        ASSERT_TRUE(store->put(make_item(make_key(1), make_payload(100, 1))));
        Write_transaction transaction{*store};
        ASSERT_TRUE(transaction.set_record("format_version", std::to_string(Asset_store::format_version - 1)));
        ASSERT_TRUE(transaction.commit());
    }
    std::string error;
    EXPECT_FALSE(Asset_store::open(store_path, Open_mode::prepared_read_only, error));
    EXPECT_NE(error.find("format version " + std::to_string(Asset_store::format_version - 1)), std::string::npos) << error;
    EXPECT_NE(error.find("reads format version " + std::to_string(Asset_store::format_version)), std::string::npos) << error;

    // prepared_create writes new containers only; an Export writes a new file and renames it over this one.
    EXPECT_FALSE(Asset_store::open(store_path, Open_mode::prepared_create, error));
    EXPECT_NE(error.find("the path exists"), std::string::npos) << error;
}

}
