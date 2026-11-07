#include "erhe_asset_store/asset_store.hpp"
#include "erhe_asset_store/asset_store_log.hpp"
#include "erhe_file/file.hpp"

#include <fmt/format.h>
#include <sqlite3.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <condition_variable>
#include <iterator>
#include <system_error>
#include <utility>
#include <vector>

namespace erhe::asset_store {

namespace {

constexpr std::string_view c_record_format_version       = "format_version";
constexpr std::string_view c_record_source_relative_path = "source.relative_path";
constexpr std::string_view c_record_source_scene_id      = "source.scene_id";
constexpr std::string_view c_record_prepared_path_prefix = "prepared_path.";
constexpr std::string_view c_record_prepared_target      = "prepared.target";
constexpr std::string_view c_record_prepared_scene_key   = "prepared.scene_key";

constexpr std::size_t c_copy_chunk_size = 1024 * 1024;

// SQLite's largest page: payload overflow chains are 16 times shorter than at the default 4 KiB.
constexpr int c_page_size = 65536;

// 16 pages of 64 KiB per read connection; the default 2 MiB times read_connection_count is 16 MiB.
constexpr const char* c_read_cache_size_sql = "PRAGMA cache_size = -1024";

// Format version 2: payloads in a table of their own, so a metadata read reads no payload page.
constexpr const char* c_schema_sql =
    "CREATE TABLE IF NOT EXISTS asset ("
    "  name  TEXT PRIMARY KEY NOT NULL,"
    "  value TEXT NOT NULL"
    ") WITHOUT ROWID;"
    "CREATE TABLE IF NOT EXISTS item ("
    "  id           INTEGER PRIMARY KEY,"
    "  key          BLOB NOT NULL UNIQUE,"
    "  kind         INTEGER NOT NULL,"
    "  target       TEXT NOT NULL,"
    "  stamp        TEXT NOT NULL,"
    "  payload_size INTEGER NOT NULL,"
    "  meta         TEXT NOT NULL,"
    "  info         TEXT NOT NULL"
    ");"
    "CREATE TABLE IF NOT EXISTS item_payload ("
    "  id      INTEGER PRIMARY KEY,"
    "  payload BLOB NOT NULL"
    ");"
    // Covers the payload lookup of a read: the index pages alone, no item row.
    "CREATE INDEX IF NOT EXISTS item_location ON item (key, payload_size);";

enum class Statement : unsigned int
{
    contains = 0,
    get_info,
    get_item_row,
    get_meta,
    get_payload_location,
    for_each_item,
    item_headers,
    item_count,
    get_record,
    set_record,
    upsert_item,
    upsert_payload,
    insert_item,
    insert_payload_zeroblob,
    delete_item,
    delete_payload,
    set_item_info,
    page_count,
    page_size,
    count
};

// The id of an item row is the id of its item_payload row; both are written in one transaction.
constexpr std::array<const char*, static_cast<std::size_t>(Statement::count)> c_statement_sql{
    "SELECT 1 FROM item WHERE key = ?1",
    "SELECT kind, target, stamp, meta, info, payload_size FROM item WHERE key = ?1",
    "SELECT id, kind, target, stamp, payload_size, meta, info FROM item WHERE key = ?1",
    "SELECT meta FROM item WHERE key = ?1",
    "SELECT id, payload_size FROM item INDEXED BY item_location WHERE key = ?1",
    "SELECT key, kind, target, stamp, meta, info, payload_size FROM item ORDER BY key",
    "SELECT key, kind, target, stamp, payload_size FROM item",
    "SELECT count(*) FROM item",
    "SELECT value FROM asset WHERE name = ?1",
    "INSERT INTO asset (name, value) VALUES (?1, ?2) ON CONFLICT (name) DO UPDATE SET value = excluded.value",
    "INSERT INTO item (key, kind, target, stamp, payload_size, meta, info) VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7) "
    "ON CONFLICT (key) DO UPDATE SET kind = excluded.kind, target = excluded.target, stamp = excluded.stamp, "
    "payload_size = excluded.payload_size, meta = excluded.meta, info = excluded.info RETURNING id",
    "INSERT INTO item_payload (id, payload) VALUES (?1, ?2) ON CONFLICT (id) DO UPDATE SET payload = excluded.payload",
    "INSERT INTO item (key, kind, target, stamp, payload_size, meta, info) VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7) RETURNING id",
    "INSERT INTO item_payload (id, payload) VALUES (?1, zeroblob(?2))",
    "DELETE FROM item WHERE key = ?1 RETURNING id",
    "DELETE FROM item_payload WHERE id = ?1",
    "UPDATE item SET info = ?2 WHERE key = ?1",
    "PRAGMA page_count",
    "PRAGMA page_size"
};

class Connection
{
public:
    Connection() = default;
    ~Connection() noexcept
    {
        close_payload();
        for (sqlite3_stmt* statement : m_statements) {
            if (statement != nullptr) {
                sqlite3_finalize(statement);
            }
        }
        if (m_db != nullptr) {
            sqlite3_close_v2(m_db);
        }
    }
    Connection(const Connection&) = delete;
    auto operator=(const Connection&) -> Connection& = delete;

    [[nodiscard]] auto open(const std::filesystem::path& path, const int flags) -> int
    {
        const std::string path_string = erhe::file::to_string(path);
        const int result = sqlite3_open_v2(path_string.c_str(), &m_db, flags | SQLITE_OPEN_EXRESCODE, nullptr);
        if (result == SQLITE_OK) {
            sqlite3_extended_result_codes(m_db, 1);
        }
        return result;
    }

    [[nodiscard]] auto get_db() const -> sqlite3*
    {
        return m_db;
    }

    [[nodiscard]] auto error_message() const -> const char*
    {
        return (m_db != nullptr) ? sqlite3_errmsg(m_db) : "out of memory";
    }

    void apply_busy_timeout(const std::chrono::milliseconds timeout)
    {
        if (m_applied_timeout != timeout) {
            sqlite3_busy_timeout(m_db, static_cast<int>(timeout.count()));
            m_applied_timeout = timeout;
        }
    }

    [[nodiscard]] auto exec(const char* sql) -> int
    {
        return sqlite3_exec(m_db, sql, nullptr, nullptr, nullptr);
    }

    // Prepared on first use, reset and unbound on every call.
    [[nodiscard]] auto get_statement(const Statement statement) -> sqlite3_stmt*
    {
        const std::size_t index = static_cast<std::size_t>(statement);
        sqlite3_stmt*& slot = m_statements[index];
        if (slot == nullptr) {
            const int result = sqlite3_prepare_v3(m_db, c_statement_sql[index], -1, SQLITE_PREPARE_PERSISTENT, &slot, nullptr);
            if (result != SQLITE_OK) {
                log_asset_store->error("Preparing '{}' failed: {}", c_statement_sql[index], sqlite3_errmsg(m_db));
                slot = nullptr;
                return nullptr;
            }
        }
        sqlite3_reset(slot);
        sqlite3_clear_bindings(slot);
        return slot;
    }

    // The open payload of the last item read; its cursor keeps the overflow page list it has walked.
    [[nodiscard]] auto get_payload(const Key& key) const -> sqlite3_blob*
    {
        return ((m_payload_blob != nullptr) && (m_payload_key == key)) ? m_payload_blob : nullptr;
    }

    [[nodiscard]] auto has_payload(const Key& key) const -> bool
    {
        return get_payload(key) != nullptr;
    }

    void set_payload(const Key& key, sqlite3_blob* blob)
    {
        close_payload();
        m_payload_key  = key;
        m_payload_blob = blob;
    }

    void close_payload()
    {
        if (m_payload_blob != nullptr) {
            sqlite3_blob_close(m_payload_blob);
            m_payload_blob = nullptr;
        }
    }

private:
    sqlite3*                                                         m_db{nullptr};
    std::array<sqlite3_stmt*, static_cast<std::size_t>(Statement::count)> m_statements{};
    std::chrono::milliseconds                                        m_applied_timeout{-1};
    Key                                                              m_payload_key{};
    sqlite3_blob*                                                    m_payload_blob{nullptr}; // holds a read transaction while open
};

// Resets a statement at scope exit so it holds no read transaction open.
class Statement_scope
{
public:
    explicit Statement_scope(sqlite3_stmt* statement) : m_statement{statement} {}
    ~Statement_scope() noexcept
    {
        if (m_statement != nullptr) {
            sqlite3_reset(m_statement);
        }
    }
    Statement_scope(const Statement_scope&) = delete;
    auto operator=(const Statement_scope&) -> Statement_scope& = delete;

private:
    sqlite3_stmt* m_statement;
};

class Blob_handle
{
public:
    Blob_handle() = default;
    ~Blob_handle() noexcept
    {
        if (blob != nullptr) {
            sqlite3_blob_close(blob);
        }
    }
    Blob_handle(const Blob_handle&) = delete;
    auto operator=(const Blob_handle&) -> Blob_handle& = delete;

    sqlite3_blob* blob{nullptr};
};

auto bind_key(sqlite3_stmt* statement, const int index, const Key& key) -> int
{
    return sqlite3_bind_blob(statement, index, key.bytes.data(), static_cast<int>(Key::byte_count), SQLITE_STATIC);
}

auto bind_text(sqlite3_stmt* statement, const int index, const std::string_view text) -> int
{
    // A default-constructed view has no data pointer, which SQLite binds as NULL.
    const char* const data = (text.data() != nullptr) ? text.data() : "";
    return sqlite3_bind_text64(statement, index, data, static_cast<sqlite3_uint64>(text.size()), SQLITE_STATIC, SQLITE_UTF8);
}

auto column_text(sqlite3_stmt* statement, const int column) -> std::string
{
    const unsigned char* text   = sqlite3_column_text(statement, column);
    const int            length = sqlite3_column_bytes(statement, column);
    if ((text == nullptr) || (length <= 0)) {
        return {};
    }
    return std::string{reinterpret_cast<const char*>(text), static_cast<std::size_t>(length)};
}

auto column_key(sqlite3_stmt* statement, const int column) -> std::optional<Key>
{
    const void* blob   = sqlite3_column_blob(statement, column);
    const int   length = sqlite3_column_bytes(statement, column);
    if ((blob == nullptr) || (length != static_cast<int>(Key::byte_count))) {
        return {};
    }
    Key key{};
    std::copy_n(static_cast<const std::uint8_t*>(blob), Key::byte_count, key.bytes.begin());
    return key;
}

// Steps an INSERT / DELETE ... RETURNING id to its end; empty when no row was returned or the step failed.
auto step_returning_id(sqlite3_stmt* statement, bool& out_failed) -> std::optional<sqlite3_int64>
{
    out_failed = false;
    std::optional<sqlite3_int64> id;
    for (;;) {
        const int result = sqlite3_step(statement);
        if (result == SQLITE_ROW) {
            id = sqlite3_column_int64(statement, 0);
            continue;
        }
        out_failed = (result != SQLITE_DONE);
        return out_failed ? std::optional<sqlite3_int64>{} : id;
    }
}

enum class Remove_result : unsigned int
{
    failed = 0,
    absent,
    removed
};

// Deletes the item row of the key and its payload row.
auto remove_item_rows(Connection& connection, const Key& key) -> Remove_result
{
    sqlite3_stmt* item_statement    = connection.get_statement(Statement::delete_item);
    sqlite3_stmt* payload_statement = connection.get_statement(Statement::delete_payload);
    if ((item_statement == nullptr) || (payload_statement == nullptr)) {
        return Remove_result::failed;
    }
    std::optional<sqlite3_int64> id;
    {
        const Statement_scope scope{item_statement};
        bind_key(item_statement, 1, key);
        bool failed = false;
        id = step_returning_id(item_statement, failed);
        if (failed) {
            return Remove_result::failed;
        }
    }
    if (!id.has_value()) {
        return Remove_result::absent;
    }
    const Statement_scope scope{payload_statement};
    sqlite3_bind_int64(payload_statement, 1, id.value());
    return (sqlite3_step(payload_statement) == SQLITE_DONE) ? Remove_result::removed : Remove_result::failed;
}

enum class Version_check : unsigned int
{
    empty,
    matches,
    unread
};

class Version_result
{
public:
    Version_check check{Version_check::unread};
    std::string   reason; // unread: why, for the log and the caller
};

auto check_format_version(Connection& connection) -> Version_result
{
    const std::string reads = fmt::format("this build reads format version {}", Asset_store::format_version);
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(connection.get_db(), "SELECT count(*) FROM sqlite_schema", -1, &statement, nullptr) != SQLITE_OK) {
        return Version_result{.reason = fmt::format("it is not an SQLite file ({}); {}", connection.error_message(), reads)};
    }
    int result = sqlite3_step(statement);
    const std::int64_t table_count = (result == SQLITE_ROW) ? sqlite3_column_int64(statement, 0) : -1;
    sqlite3_finalize(statement);
    if (table_count < 0) {
        return Version_result{.reason = fmt::format("it is not an SQLite file ({}); {}", connection.error_message(), reads)};
    }
    if (table_count == 0) {
        return Version_result{.check = Version_check::empty, .reason = {}};
    }

    statement = nullptr;
    if (sqlite3_prepare_v2(connection.get_db(), "SELECT value FROM asset WHERE name = ?1", -1, &statement, nullptr) != SQLITE_OK) {
        return Version_result{.reason = fmt::format("it has no format version; {}", reads)};
    }
    bind_text(statement, 1, c_record_format_version);
    result = sqlite3_step(statement);
    const bool        has_value = (result == SQLITE_ROW);
    const std::string value     = has_value ? column_text(statement, 0) : std::string{};
    sqlite3_finalize(statement);
    if (value == std::to_string(Asset_store::format_version)) {
        return Version_result{.check = Version_check::matches, .reason = {}};
    }
    return Version_result{
        .reason = has_value ? fmt::format("it has format version {}; {}", value, reads) : fmt::format("it has no format version; {}", reads)
    };
}

void remove_container_files(const std::filesystem::path& path)
{
    std::error_code error_code;
    std::filesystem::remove(path, error_code);
    for (const char* suffix : {"-wal", "-shm", "-journal"}) {
        std::filesystem::path side_file = path;
        side_file += suffix;
        std::filesystem::remove(side_file, error_code);
    }
}

}

auto c_str(const Open_mode mode) -> const char*
{
    switch (mode) {
        case Open_mode::editor_read_write:   return "editor_read_write";
        case Open_mode::prepared_create:     return "prepared_create";
        case Open_mode::prepared_read_only:  return "prepared_read_only";
        default:                             return "?";
    }
}

enum class Payload_handles : unsigned int
{
    close_after_read = 0,
    keep_open,           // prepared_read_only until close_payload_handles()
    close_and_release    // prepared_read_only after it: the page cache is freed after each read too
};

class Asset_store_impl
{
public:
    // Prefers the free connection that holds the payload of `payload_key` open.
    [[nodiscard]] auto acquire_reader(const Key* const payload_key) -> Connection*
    {
        std::unique_lock<std::mutex> lock{m_pool_mutex};
        m_pool_condition.wait(lock, [this]() { return !m_free_readers.empty(); });
        std::vector<Connection*>::iterator chosen = std::prev(m_free_readers.end());
        if (payload_key != nullptr) {
            const std::vector<Connection*>::iterator holder = std::find_if(
                m_free_readers.begin(), m_free_readers.end(),
                [payload_key](const Connection* connection) { return connection->has_payload(*payload_key); }
            );
            if (holder != m_free_readers.end()) {
                chosen = holder;
            }
        }
        Connection* connection = *chosen;
        m_free_readers.erase(chosen);
        connection->apply_busy_timeout(m_busy_timeout);
        return connection;
    }

    void release_reader(Connection* connection)
    {
        {
            const std::lock_guard<std::mutex> lock{m_pool_mutex};
            if (m_payload_handles != Payload_handles::keep_open) {
                connection->close_payload();
            }
            if (m_payload_handles == Payload_handles::close_and_release) {
                sqlite3_db_release_memory(connection->get_db());
            }
            m_free_readers.push_back(connection);
        }
        m_pool_condition.notify_one();
    }

    void set_payload_handles(const Payload_handles payload_handles)
    {
        const std::lock_guard<std::mutex> lock{m_pool_mutex};
        if (m_payload_handles == payload_handles) {
            return;
        }
        m_payload_handles = payload_handles;
        if (payload_handles == Payload_handles::close_and_release) {
            // Leased connections close and free theirs at release.
            for (Connection* connection : m_free_readers) {
                connection->close_payload();
                sqlite3_db_release_memory(connection->get_db());
            }
        }
    }

    void add_reader(std::unique_ptr<Connection>&& connection)
    {
        const std::lock_guard<std::mutex> lock{m_pool_mutex};
        m_free_readers.push_back(connection.get());
        readers.push_back(std::move(connection));
    }

    [[nodiscard]] auto get_busy_timeout() -> std::chrono::milliseconds
    {
        const std::lock_guard<std::mutex> lock{m_pool_mutex};
        return m_busy_timeout;
    }

    void set_busy_timeout(const std::chrono::milliseconds timeout)
    {
        const std::lock_guard<std::mutex> lock{m_pool_mutex};
        m_busy_timeout = timeout;
    }

    std::filesystem::path                    path;
    Open_mode                                mode{Open_mode::prepared_read_only};
    std::unique_ptr<Connection>              writer;       // null in prepared_read_only
    std::mutex                               write_mutex;  // one Write_transaction at a time per store
    std::vector<std::unique_ptr<Connection>> readers;

private:
    std::mutex                m_pool_mutex;
    std::condition_variable   m_pool_condition;
    std::vector<Connection*>  m_free_readers;
    std::chrono::milliseconds m_busy_timeout{10000};
    Payload_handles           m_payload_handles{Payload_handles::close_after_read};
};

namespace {

class Read_lease
{
public:
    explicit Read_lease(Asset_store_impl& impl, const Key* const payload_key = nullptr)
        : m_impl      {impl}
        , m_connection{impl.acquire_reader(payload_key)}
    {
    }
    ~Read_lease() noexcept
    {
        m_impl.release_reader(m_connection);
    }
    Read_lease(const Read_lease&) = delete;
    auto operator=(const Read_lease&) -> Read_lease& = delete;

    [[nodiscard]] auto connection() const -> Connection&
    {
        return *m_connection;
    }

private:
    Asset_store_impl& m_impl;
    Connection*       m_connection;
};

auto open_writer(Asset_store_impl& impl, std::string& out_error) -> bool
{
    const bool wal = (impl.mode == Open_mode::editor_read_write);
    for (int attempt = 0; attempt < 2; ++attempt) {
        std::unique_ptr<Connection> connection = std::make_unique<Connection>();
        const int open_result = connection->open(impl.path, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE);
        if (open_result != SQLITE_OK) {
            out_error = fmt::format("opening it for writing failed: {}", connection->error_message());
            log_asset_store->error("Asset '{}': {}", erhe::file::to_string(impl.path), out_error);
            return false;
        }
        connection->apply_busy_timeout(impl.get_busy_timeout());

        const Version_result version = check_format_version(*connection);
        const Version_check  check   = version.check;
        if (check == Version_check::unread) {
            if (attempt > 0) {
                out_error = fmt::format("recreated, it is still unreadable: {}", version.reason);
                log_asset_store->error("Asset '{}': {}", erhe::file::to_string(impl.path), out_error);
                return false;
            }
            log_asset_store->warn("Asset '{}': {} - recreating it empty", erhe::file::to_string(impl.path), version.reason);
            connection.reset();
            remove_container_files(impl.path);
            continue;
        }

        // A new file takes its page size before the journal mode writes the header.
        if (check == Version_check::empty) {
            const std::string page_size_sql = "PRAGMA page_size = " + std::to_string(c_page_size);
            if (connection->exec(page_size_sql.c_str()) != SQLITE_OK) {
                out_error = fmt::format("setting the page size failed: {}", connection->error_message());
                log_asset_store->error("Asset '{}': {}", erhe::file::to_string(impl.path), out_error);
                return false;
            }
        }
        if (connection->exec(wal ? "PRAGMA journal_mode = WAL" : "PRAGMA journal_mode = DELETE") != SQLITE_OK) {
            out_error = fmt::format("setting the journal mode failed: {}", connection->error_message());
            log_asset_store->error("Asset '{}': {}", erhe::file::to_string(impl.path), out_error);
            return false;
        }
        if (connection->exec(wal ? "PRAGMA synchronous = NORMAL" : "PRAGMA synchronous = FULL") != SQLITE_OK) {
            out_error = fmt::format("setting the synchronous mode failed: {}", connection->error_message());
            log_asset_store->error("Asset '{}': {}", erhe::file::to_string(impl.path), out_error);
            return false;
        }

        if (check == Version_check::empty) {
            const std::string initialize_sql =
                std::string{"BEGIN IMMEDIATE;"} + c_schema_sql +
                "INSERT OR IGNORE INTO asset (name, value) VALUES ('" + std::string{c_record_format_version} + "', '" +
                std::to_string(Asset_store::format_version) + "');COMMIT;";
            if (connection->exec(initialize_sql.c_str()) != SQLITE_OK) {
                out_error = fmt::format("creating it failed: {}", connection->error_message());
                log_asset_store->error("Asset '{}': {}", erhe::file::to_string(impl.path), out_error);
                return false;
            }
        }
        impl.writer = std::move(connection);
        return true;
    }
    return false;
}

auto open_readers(Asset_store_impl& impl, std::string& out_error) -> bool
{
    for (std::size_t i = 0; i < Asset_store::read_connection_count; ++i) {
        std::unique_ptr<Connection> connection = std::make_unique<Connection>();
        if (connection->open(impl.path, SQLITE_OPEN_READONLY) != SQLITE_OK) {
            out_error = connection->error_message();
            log_asset_store->error("Cannot open asset '{}' for reading: {}", erhe::file::to_string(impl.path), out_error);
            return false;
        }
        connection->apply_busy_timeout(impl.get_busy_timeout());
        // Payload reads bypass the page cache but for the pages at the ends of each range (doc/erhe/asset_store.md "Reading payloads").
        if (connection->exec(c_read_cache_size_sql) != SQLITE_OK) {
            out_error = connection->error_message();
            log_asset_store->error("Setting the page cache size of '{}' failed: {}", erhe::file::to_string(impl.path), out_error);
            return false;
        }
        if (i == 0) {
            const Version_result version = check_format_version(*connection);
            if (version.check != Version_check::matches) {
                out_error = (version.check == Version_check::empty)
                    ? fmt::format("it holds no asset tables; this build reads format version {}", Asset_store::format_version)
                    : version.reason;
                log_asset_store->error("Asset '{}' cannot be read: {}", erhe::file::to_string(impl.path), out_error);
                return false;
            }
        }
        impl.add_reader(std::move(connection));
    }
    return true;
}

}

Asset_store::Asset_store(std::unique_ptr<Asset_store_impl>&& impl)
    : m_impl{std::move(impl)}
{
}

Asset_store::~Asset_store() noexcept
{
    // Readers close first so the writer's close checkpoints and removes the WAL files.
    m_impl->readers.clear();
    m_impl->writer.reset();
}

auto Asset_store::open(const std::filesystem::path& path, const Open_mode mode) -> std::unique_ptr<Asset_store>
{
    std::string error;
    return open(path, mode, error);
}

auto Asset_store::open(const std::filesystem::path& path, const Open_mode mode, std::string& out_error) -> std::unique_ptr<Asset_store>
{
    out_error.clear();
    std::unique_ptr<Asset_store_impl> impl = std::make_unique<Asset_store_impl>();
    impl->path = path;
    impl->mode = mode;

    if (mode == Open_mode::prepared_read_only) {
        std::error_code error_code;
        if (!std::filesystem::is_regular_file(path, error_code)) {
            out_error = "it is not a readable file";
            log_asset_store->error("Prepared asset '{}' is not a readable file", erhe::file::to_string(path));
            return {};
        }
    } else if (mode == Open_mode::prepared_create) {
        std::error_code error_code;
        if (std::filesystem::exists(path, error_code) || error_code) {
            out_error = "the path exists; prepared_create writes a new container only";
            log_asset_store->error("Prepared asset '{}' not created: {}", erhe::file::to_string(path), out_error);
            return {};
        }
        if (!open_writer(*impl, out_error)) {
            return {};
        }
    } else if (!open_writer(*impl, out_error)) {
        return {};
    }
    if (!open_readers(*impl, out_error)) {
        return {};
    }
    if (mode == Open_mode::prepared_read_only) {
        impl->set_payload_handles(Payload_handles::keep_open);
    }
    log_asset_store->debug("Opened asset '{}' ({})", erhe::file::to_string(path), c_str(mode));
    return std::unique_ptr<Asset_store>{new Asset_store{std::move(impl)}};
}

auto Asset_store::get_path() const -> const std::filesystem::path&
{
    return m_impl->path;
}

auto Asset_store::get_mode() const -> Open_mode
{
    return m_impl->mode;
}

void Asset_store::set_busy_timeout(const std::chrono::milliseconds timeout)
{
    m_impl->set_busy_timeout(timeout);
}

auto Asset_store::contains(const Key& key) const -> bool
{
    const Read_lease lease{*m_impl};
    sqlite3_stmt* statement = lease.connection().get_statement(Statement::contains);
    if (statement == nullptr) {
        return false;
    }
    const Statement_scope scope{statement};
    bind_key(statement, 1, key);
    return sqlite3_step(statement) == SQLITE_ROW;
}

auto Asset_store::get_info(const Key& key) const -> std::optional<Item_info>
{
    const Read_lease lease{*m_impl};
    sqlite3_stmt* statement = lease.connection().get_statement(Statement::get_info);
    if (statement == nullptr) {
        return {};
    }
    const Statement_scope scope{statement};
    bind_key(statement, 1, key);
    if (sqlite3_step(statement) != SQLITE_ROW) {
        return {};
    }
    const std::optional<Item_kind> kind = item_kind_from_value(static_cast<std::uint32_t>(sqlite3_column_int64(statement, 0)));
    if (!kind.has_value()) {
        log_asset_store->error("Item {} in '{}' has an unknown kind", key.to_hex(), erhe::file::to_string(m_impl->path));
        return {};
    }
    return Item_info{
        .key          = key,
        .kind         = kind.value(),
        .target       = column_text(statement, 1),
        .stamp        = column_text(statement, 2),
        .meta         = column_text(statement, 3),
        .info         = column_text(statement, 4),
        .payload_size = static_cast<std::uint64_t>(sqlite3_column_int64(statement, 5))
    };
}

auto Asset_store::read_meta(const Key& key) const -> std::optional<std::string>
{
    const Read_lease lease{*m_impl};
    sqlite3_stmt* statement = lease.connection().get_statement(Statement::get_meta);
    if (statement == nullptr) {
        return {};
    }
    const Statement_scope scope{statement};
    bind_key(statement, 1, key);
    if (sqlite3_step(statement) != SQLITE_ROW) {
        return {};
    }
    return column_text(statement, 0);
}

auto Asset_store::read_payload(const Key& key, const std::uint64_t offset, const std::span<std::byte> out) const -> bool
{
    const Read_lease lease{*m_impl, &key};
    Connection&   connection = lease.connection();
    sqlite3_blob* blob       = connection.get_payload(key);
    if (blob == nullptr) {
        sqlite3_stmt* statement = connection.get_statement(Statement::get_payload_location);
        if (statement == nullptr) {
            return false;
        }
        sqlite3_int64 rowid = 0;
        std::uint64_t size  = 0;
        {
            const Statement_scope scope{statement};
            bind_key(statement, 1, key);
            if (sqlite3_step(statement) != SQLITE_ROW) {
                return false;
            }
            rowid = sqlite3_column_int64(statement, 0);
            size  = static_cast<std::uint64_t>(sqlite3_column_int64(statement, 1));
        }
        // The blob handle holds its own read transaction; a row replaced after the lookup makes the open fail.
        Blob_handle handle;
        if (sqlite3_blob_open(connection.get_db(), "main", "item_payload", "payload", rowid, 0, &handle.blob) != SQLITE_OK) {
            log_asset_store->warn("Opening payload of item {} failed: {}", key.to_hex(), connection.error_message());
            return false;
        }
        if (static_cast<std::uint64_t>(sqlite3_blob_bytes(handle.blob)) != size) {
            log_asset_store->warn("Item {} changed during the read", key.to_hex());
            return false;
        }
        // The connection owns it from here; release closes it unless the store keeps payloads open.
        blob = std::exchange(handle.blob, nullptr);
        connection.set_payload(key, blob);
    }
    const std::uint64_t size = static_cast<std::uint64_t>(sqlite3_blob_bytes(blob));
    if ((offset > size) || (out.size() > (size - offset))) {
        log_asset_store->error(
            "Read of {} bytes at {} is outside item {} of {} bytes",
            out.size(), offset, key.to_hex(), size
        );
        return false;
    }
    if (out.empty()) {
        return true;
    }
    const int result = sqlite3_blob_read(blob, out.data(), static_cast<int>(out.size()), static_cast<int>(offset));
    if (result != SQLITE_OK) {
        log_asset_store->error("Reading payload of item {} failed: {}", key.to_hex(), connection.error_message());
        connection.close_payload();
        return false;
    }
    return true;
}

void Asset_store::close_payload_handles() const
{
    m_impl->set_payload_handles(Payload_handles::close_and_release);
}

auto Asset_store::for_each_item(const std::function<void(const Item_info& info)>& callback) const -> bool
{
    const Read_lease lease{*m_impl};
    sqlite3_stmt* statement = lease.connection().get_statement(Statement::for_each_item);
    if (statement == nullptr) {
        return false;
    }
    const Statement_scope scope{statement};
    for (;;) {
        const int result = sqlite3_step(statement);
        if (result == SQLITE_DONE) {
            return true;
        }
        if (result != SQLITE_ROW) {
            log_asset_store->error("Listing items of '{}' failed: {}", erhe::file::to_string(m_impl->path), lease.connection().error_message());
            return false;
        }
        const std::optional<Key>       key  = column_key(statement, 0);
        const std::optional<Item_kind> kind = item_kind_from_value(static_cast<std::uint32_t>(sqlite3_column_int64(statement, 1)));
        if (!key.has_value() || !kind.has_value()) {
            log_asset_store->error("Skipping malformed item row in '{}'", erhe::file::to_string(m_impl->path));
            continue;
        }
        const Item_info info{
            .key          = key.value(),
            .kind         = kind.value(),
            .target       = column_text(statement, 2),
            .stamp        = column_text(statement, 3),
            .meta         = column_text(statement, 4),
            .info         = column_text(statement, 5),
            .payload_size = static_cast<std::uint64_t>(sqlite3_column_int64(statement, 6))
        };
        callback(info);
    }
}

auto Asset_store::get_item_headers() const -> std::optional<std::vector<Item_info>>
{
    const Read_lease lease{*m_impl};
    sqlite3_stmt* statement = lease.connection().get_statement(Statement::item_headers);
    if (statement == nullptr) {
        return {};
    }
    const Statement_scope scope{statement};
    std::vector<Item_info> headers;
    for (;;) {
        const int result = sqlite3_step(statement);
        if (result == SQLITE_DONE) {
            break;
        }
        if (result != SQLITE_ROW) {
            log_asset_store->error("Listing items of '{}' failed: {}", erhe::file::to_string(m_impl->path), lease.connection().error_message());
            return {};
        }
        const std::optional<Key>       key  = column_key(statement, 0);
        const std::optional<Item_kind> kind = item_kind_from_value(static_cast<std::uint32_t>(sqlite3_column_int64(statement, 1)));
        if (!key.has_value() || !kind.has_value()) {
            log_asset_store->error("Skipping malformed item row in '{}'", erhe::file::to_string(m_impl->path));
            continue;
        }
        headers.push_back(Item_info{
            .key          = key.value(),
            .kind         = kind.value(),
            .target       = column_text(statement, 2),
            .stamp        = column_text(statement, 3),
            .meta         = {},
            .info         = {},
            .payload_size = static_cast<std::uint64_t>(sqlite3_column_int64(statement, 4))
        });
    }
    // Read in row order, a sequential walk of the table; sorted here.
    std::sort(headers.begin(), headers.end(), [](const Item_info& lhs, const Item_info& rhs) { return lhs.key < rhs.key; });
    return headers;
}

auto Asset_store::get_item_count() const -> std::optional<std::uint64_t>
{
    const Read_lease lease{*m_impl};
    sqlite3_stmt* statement = lease.connection().get_statement(Statement::item_count);
    if (statement == nullptr) {
        return {};
    }
    const Statement_scope scope{statement};
    if (sqlite3_step(statement) != SQLITE_ROW) {
        return {};
    }
    return static_cast<std::uint64_t>(sqlite3_column_int64(statement, 0));
}

auto Asset_store::get_record(const std::string_view name) const -> std::optional<std::string>
{
    const Read_lease lease{*m_impl};
    sqlite3_stmt* statement = lease.connection().get_statement(Statement::get_record);
    if (statement == nullptr) {
        return {};
    }
    const Statement_scope scope{statement};
    bind_text(statement, 1, name);
    if (sqlite3_step(statement) != SQLITE_ROW) {
        return {};
    }
    return column_text(statement, 0);
}

auto Asset_store::get_source_record() const -> std::optional<Source_record>
{
    std::optional<std::string> relative_path = get_record(c_record_source_relative_path);
    if (!relative_path.has_value()) {
        return {};
    }
    return Source_record{
        .relative_path = std::move(relative_path.value()),
        .scene_id      = get_record(c_record_source_scene_id).value_or(std::string{})
    };
}

auto Asset_store::get_prepared_path(const std::string_view target) const -> std::optional<std::string>
{
    const std::string name = std::string{c_record_prepared_path_prefix} + std::string{target};
    return get_record(name);
}

auto Asset_store::get_prepared_scene() const -> std::optional<Prepared_scene_record>
{
    std::optional<std::string> target    = get_record(c_record_prepared_target);
    std::optional<std::string> scene_key = get_record(c_record_prepared_scene_key);
    if (!target.has_value() || !scene_key.has_value()) {
        return {};
    }
    const std::optional<Key> key = Key::from_hex(scene_key.value());
    if (!key.has_value()) {
        log_asset_store->error("Asset '{}' has a malformed scene key record", erhe::file::to_string(m_impl->path));
        return {};
    }
    return Prepared_scene_record{.target = std::move(target.value()), .scene_key = key.value()};
}

auto Asset_store::get_stored_format_version() const -> std::optional<std::uint32_t>
{
    const std::optional<std::string> text = get_record(c_record_format_version);
    if (!text.has_value()) {
        return {};
    }
    std::uint32_t value = 0;
    const char* begin = text.value().data();
    const char* end   = begin + text.value().size();
    const std::from_chars_result result = std::from_chars(begin, end, value);
    if ((result.ec != std::errc{}) || (result.ptr != end)) {
        return {};
    }
    return value;
}

auto Asset_store::put(const Item_data& item) -> bool
{
    Write_transaction transaction{*this};
    return transaction.put(item) && transaction.commit();
}

auto Asset_store::remove(const Key& key) -> bool
{
    Write_transaction transaction{*this};
    const bool removed = transaction.remove(key);
    return transaction.commit() && removed;
}

auto Asset_store::copy_item_to(Asset_store& other, const Key& key) -> bool
{
    Write_transaction transaction{other};
    return transaction.copy_item_from(*this, key) && transaction.commit();
}

auto Asset_store::vacuum() -> bool
{
    const std::lock_guard<std::mutex> lock{m_impl->write_mutex};
    if (!m_impl->writer) {
        log_asset_store->error("Asset '{}' is read-only; VACUUM refused", erhe::file::to_string(m_impl->path));
        return false;
    }
    Connection& connection = *m_impl->writer;
    connection.apply_busy_timeout(m_impl->get_busy_timeout());
    if (connection.exec("VACUUM") != SQLITE_OK) {
        log_asset_store->error("VACUUM of '{}' failed: {}", erhe::file::to_string(m_impl->path), connection.error_message());
        return false;
    }
    if (m_impl->mode == Open_mode::editor_read_write) {
        if (connection.exec("PRAGMA wal_checkpoint(TRUNCATE)") != SQLITE_OK) {
            log_asset_store->error("Checkpoint of '{}' failed: {}", erhe::file::to_string(m_impl->path), connection.error_message());
            return false;
        }
    }
    return true;
}


auto Asset_store::get_database_size() const -> std::optional<std::uint64_t>
{
    const Read_lease lease{*m_impl};
    std::array<std::int64_t, 2> values{};
    const std::array<Statement, 2> statements{Statement::page_count, Statement::page_size};
    for (std::size_t i = 0; i < 2; ++i) {
        sqlite3_stmt* statement = lease.connection().get_statement(statements[i]);
        if (statement == nullptr) {
            return {};
        }
        const Statement_scope scope{statement};
        if (sqlite3_step(statement) != SQLITE_ROW) {
            return {};
        }
        values[i] = sqlite3_column_int64(statement, 0);
    }
    return static_cast<std::uint64_t>(values[0]) * static_cast<std::uint64_t>(values[1]);
}

Write_transaction::Write_transaction(Asset_store& store)
    : m_store{store}
    , m_lock{store.m_impl->write_mutex}
{
    Asset_store_impl& impl = *m_store.m_impl;
    if (!impl.writer) {
        log_asset_store->error("Asset '{}' is read-only; write refused", erhe::file::to_string(impl.path));
        m_lock.unlock();
        return;
    }
    const std::chrono::milliseconds timeout = impl.get_busy_timeout();
    impl.writer->apply_busy_timeout(timeout);
    const int result = impl.writer->exec("BEGIN IMMEDIATE");
    if (result != SQLITE_OK) {
        if ((result & 0xff) == SQLITE_BUSY) {
            log_asset_store->warn(
                "Asset '{}': no writer turn within {} ms; nothing is stored",
                erhe::file::to_string(impl.path), timeout.count()
            );
        } else {
            log_asset_store->error("Asset '{}': BEGIN failed: {}", erhe::file::to_string(impl.path), impl.writer->error_message());
        }
        m_lock.unlock();
        return;
    }
    m_active = true;
}

Write_transaction::~Write_transaction() noexcept
{
    rollback();
}

auto Write_transaction::is_active() const -> bool
{
    return m_active;
}

auto Write_transaction::fail(const char* operation) -> bool
{
    Asset_store_impl& impl = *m_store.m_impl;
    log_asset_store->error("Asset '{}': {} failed: {}", erhe::file::to_string(impl.path), operation, impl.writer->error_message());
    m_failed = true;
    return false;
}

auto Write_transaction::put(const Item_data& item) -> bool
{
    if (!m_active || m_failed) {
        return false;
    }
    Connection&   connection        = *m_store.m_impl->writer;
    sqlite3_stmt* item_statement    = connection.get_statement(Statement::upsert_item);
    sqlite3_stmt* payload_statement = connection.get_statement(Statement::upsert_payload);
    if ((item_statement == nullptr) || (payload_statement == nullptr)) {
        m_failed = true;
        return false;
    }
    std::optional<sqlite3_int64> id;
    {
        const Statement_scope scope{item_statement};
        bind_key (item_statement, 1, item.key);
        sqlite3_bind_int64(item_statement, 2, static_cast<sqlite3_int64>(item.kind));
        bind_text(item_statement, 3, item.target);
        bind_text(item_statement, 4, item.stamp);
        sqlite3_bind_int64(item_statement, 5, static_cast<sqlite3_int64>(item.payload.size()));
        bind_text(item_statement, 6, item.meta);
        bind_text(item_statement, 7, item.info);
        bool failed = false;
        id = step_returning_id(item_statement, failed);
        if (failed || !id.has_value()) {
            return fail("put");
        }
    }
    const Statement_scope scope{payload_statement};
    sqlite3_bind_int64(payload_statement, 1, id.value());
    if (item.payload.empty()) {
        sqlite3_bind_zeroblob(payload_statement, 2, 0);
    } else {
        sqlite3_bind_blob64(payload_statement, 2, item.payload.data(), static_cast<sqlite3_uint64>(item.payload.size()), SQLITE_STATIC);
    }
    if (sqlite3_step(payload_statement) != SQLITE_DONE) {
        return fail("put (payload)");
    }
    return true;
}

auto Write_transaction::remove(const Key& key) -> bool
{
    if (!m_active || m_failed) {
        return false;
    }
    const Remove_result result = remove_item_rows(*m_store.m_impl->writer, key);
    if (result == Remove_result::failed) {
        return fail("remove");
    }
    return result == Remove_result::removed;
}

auto Write_transaction::set_info(const Key& key, const std::string_view info) -> bool
{
    if (!m_active || m_failed) {
        return false;
    }
    sqlite3_stmt* statement = m_store.m_impl->writer->get_statement(Statement::set_item_info);
    if (statement == nullptr) {
        m_failed = true;
        return false;
    }
    const Statement_scope scope{statement};
    bind_key(statement, 1, key);
    bind_text(statement, 2, info);
    if (sqlite3_step(statement) != SQLITE_DONE) {
        return fail("set_info");
    }
    return sqlite3_changes(m_store.m_impl->writer->get_db()) > 0;
}

auto Write_transaction::copy_item_from(const Asset_store& source, const Key& key) -> bool
{
    if (!m_active || m_failed) {
        return false;
    }
    if (&source == &m_store) {
        log_asset_store->error("copy_item_from: source and destination are the same asset");
        return false;
    }

    const Read_lease source_lease{*source.m_impl};
    Connection& source_connection = source_lease.connection();
    // One read transaction keeps the row and its payload consistent across the chunked copy.
    if (source_connection.exec("BEGIN") != SQLITE_OK) {
        log_asset_store->error("copy_item_from: BEGIN on source failed: {}", source_connection.error_message());
        return false;
    }
    class End_read
    {
    public:
        explicit End_read(Connection& connection) : m_connection{connection} {}
        ~End_read() noexcept { static_cast<void>(m_connection.exec("COMMIT")); }
        End_read(const End_read&) = delete;
        auto operator=(const End_read&) -> End_read& = delete;
    private:
        Connection& m_connection;
    };
    const End_read end_read{source_connection};

    sqlite3_stmt* source_statement = source_connection.get_statement(Statement::get_item_row);
    if (source_statement == nullptr) {
        return false;
    }
    const Statement_scope source_scope{source_statement};
    bind_key(source_statement, 1, key);
    if (sqlite3_step(source_statement) != SQLITE_ROW) {
        log_asset_store->warn("copy_item_from: item {} is not in '{}'", key.to_hex(), erhe::file::to_string(source.get_path()));
        return false;
    }
    // Columns: id, kind, target, stamp, payload_size, meta, info.
    const sqlite3_int64 source_id = sqlite3_column_int64(source_statement, 0);
    const sqlite3_int64 size      = sqlite3_column_int64(source_statement, 4);

    Connection& connection = *m_store.m_impl->writer;
    if (remove_item_rows(connection, key) == Remove_result::failed) {
        return fail("copy (replace)");
    }
    sqlite3_int64 destination_id = 0;
    {
        sqlite3_stmt* statement = connection.get_statement(Statement::insert_item);
        if (statement == nullptr) {
            m_failed = true;
            return false;
        }
        const Statement_scope scope{statement};
        bind_key(statement, 1, key);
        for (int column = 1; column <= 6; ++column) {
            sqlite3_bind_value(statement, column + 1, sqlite3_column_value(source_statement, column));
        }
        bool failed = false;
        const std::optional<sqlite3_int64> id = step_returning_id(statement, failed);
        if (failed || !id.has_value()) {
            return fail("copy (insert)");
        }
        destination_id = id.value();
    }
    {
        sqlite3_stmt* statement = connection.get_statement(Statement::insert_payload_zeroblob);
        if (statement == nullptr) {
            m_failed = true;
            return false;
        }
        const Statement_scope scope{statement};
        sqlite3_bind_int64(statement, 1, destination_id);
        sqlite3_bind_int64(statement, 2, size);
        if (sqlite3_step(statement) != SQLITE_DONE) {
            return fail("copy (insert payload)");
        }
    }
    if (size == 0) {
        return true;
    }

    Blob_handle source_blob;
    Blob_handle destination_blob;
    if (sqlite3_blob_open(source_connection.get_db(), "main", "item_payload", "payload", source_id, 0, &source_blob.blob) != SQLITE_OK) {
        log_asset_store->error("copy_item_from: opening source payload failed: {}", source_connection.error_message());
        m_failed = true;
        return false;
    }
    if (sqlite3_blob_open(connection.get_db(), "main", "item_payload", "payload", destination_id, 1, &destination_blob.blob) != SQLITE_OK) {
        return fail("copy (open payload)");
    }
    std::vector<std::byte> buffer(std::min<std::size_t>(c_copy_chunk_size, static_cast<std::size_t>(size)));
    for (sqlite3_int64 offset = 0; offset < size; ) {
        const int chunk = static_cast<int>(std::min<sqlite3_int64>(static_cast<sqlite3_int64>(buffer.size()), size - offset));
        if (sqlite3_blob_read(source_blob.blob, buffer.data(), chunk, static_cast<int>(offset)) != SQLITE_OK) {
            log_asset_store->error("copy_item_from: reading source payload failed: {}", source_connection.error_message());
            m_failed = true;
            return false;
        }
        if (sqlite3_blob_write(destination_blob.blob, buffer.data(), chunk, static_cast<int>(offset)) != SQLITE_OK) {
            return fail("copy (write payload)");
        }
        offset += chunk;
    }
    return true;
}

auto Write_transaction::set_record(const std::string_view name, const std::string_view value) -> bool
{
    if (!m_active || m_failed) {
        return false;
    }
    sqlite3_stmt* statement = m_store.m_impl->writer->get_statement(Statement::set_record);
    if (statement == nullptr) {
        m_failed = true;
        return false;
    }
    const Statement_scope scope{statement};
    bind_text(statement, 1, name);
    bind_text(statement, 2, value);
    if (sqlite3_step(statement) != SQLITE_DONE) {
        return fail("set_record");
    }
    return true;
}

auto Write_transaction::set_source_record(const Source_record& record) -> bool
{
    return
        set_record(c_record_source_relative_path, record.relative_path) &&
        set_record(c_record_source_scene_id, record.scene_id);
}

auto Write_transaction::set_prepared_path(const std::string_view target, const std::string_view path) -> bool
{
    const std::string name = std::string{c_record_prepared_path_prefix} + std::string{target};
    return set_record(name, path);
}

auto Write_transaction::set_prepared_scene(const Prepared_scene_record& record) -> bool
{
    return
        set_record(c_record_prepared_target, record.target) &&
        set_record(c_record_prepared_scene_key, record.scene_key.to_hex());
}

auto Write_transaction::commit() -> bool
{
    if (!m_active) {
        return false;
    }
    if (m_failed) {
        rollback();
        return false;
    }
    Asset_store_impl& impl = *m_store.m_impl;
    if (impl.writer->exec("COMMIT") != SQLITE_OK) {
        log_asset_store->error("Asset '{}': COMMIT failed: {}", erhe::file::to_string(impl.path), impl.writer->error_message());
        rollback();
        return false;
    }
    m_active = false;
    m_lock.unlock();
    return true;
}

void Write_transaction::rollback()
{
    if (!m_active) {
        return;
    }
    Asset_store_impl& impl = *m_store.m_impl;
    if (sqlite3_get_autocommit(impl.writer->get_db()) == 0) {
        static_cast<void>(impl.writer->exec("ROLLBACK"));
    }
    m_active = false;
    m_failed = false;
    m_lock.unlock();
}

}
