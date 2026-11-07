#pragma once

#include "erhe_asset_store/item_kind.hpp"
#include "erhe_asset_store/key.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace erhe::asset_store {

enum class Open_mode : unsigned int
{
    editor_read_write,   // WAL; creates when absent, recreates on an unread format version
    prepared_create,     // rollback journal; creates a new container, fails when the path exists
    prepared_read_only   // fails on a missing file or an unread format version
};

[[nodiscard]] auto c_str(Open_mode mode) -> const char*;

// What put() stores; the views must stay valid for the call.
class Item_data
{
public:
    Key                        key;
    Item_kind                  kind{Item_kind::editor_data};
    std::string_view           target;
    std::string_view           stamp;
    std::string_view           meta;
    std::string_view           info;
    std::span<const std::byte> payload;
};

class Item_info
{
public:
    Key           key;
    Item_kind     kind{Item_kind::editor_data};
    std::string   target;
    std::string   stamp;
    std::string   meta;
    std::string   info;
    std::uint64_t payload_size{0};
};

// R2.1: the source asset an editor or prepared asset is made from.
class Source_record
{
public:
    std::string relative_path;
    std::string scene_id;
};

// The target of a prepared asset and the key of its Scene item.
class Prepared_scene_record
{
public:
    std::string target;
    Key         scene_key;
};

class Asset_store_impl;

class Asset_store
{
public:
    static constexpr std::uint32_t format_version = 2;
    static constexpr std::size_t   read_connection_count = 8;

    // Null when the container cannot be opened (logged).
    [[nodiscard]] static auto open(const std::filesystem::path& path, Open_mode mode) -> std::unique_ptr<Asset_store>;
    // As above; out_error gets the reason of a failed open, for the caller's message.
    [[nodiscard]] static auto open(const std::filesystem::path& path, Open_mode mode, std::string& out_error) -> std::unique_ptr<Asset_store>;

    ~Asset_store() noexcept;
    Asset_store(const Asset_store&) = delete;
    auto operator=(const Asset_store&) -> Asset_store& = delete;

    [[nodiscard]] auto get_path() const -> const std::filesystem::path&;
    [[nodiscard]] auto get_mode() const -> Open_mode;
    void set_busy_timeout(std::chrono::milliseconds timeout);

    // Reads; all thread-safe, served by a pool of read connections.
    [[nodiscard]] auto contains    (const Key& key) const -> bool;
    [[nodiscard]] auto get_info    (const Key& key) const -> std::optional<Item_info>;
    [[nodiscard]] auto read_meta   (const Key& key) const -> std::optional<std::string>;
    [[nodiscard]] auto read_payload(const Key& key, std::uint64_t offset, std::span<std::byte> out) const -> bool;
    // A prepared_read_only store keeps the last payload of each read connection open until this call, and frees each read's page cache after it (doc/erhe/asset_store.md "Reading payloads").
    void close_payload_handles() const;
    auto for_each_item(const std::function<void(const Item_info& info)>& callback) const -> bool;
    // Key, kind, target, stamp and payload size of every item (meta and info empty), in key order; one read of the item table.
    [[nodiscard]] auto get_item_headers() const -> std::optional<std::vector<Item_info>>;
    [[nodiscard]] auto get_item_count() const -> std::optional<std::uint64_t>;

    [[nodiscard]] auto get_record             (std::string_view name) const -> std::optional<std::string>;
    [[nodiscard]] auto get_source_record      () const -> std::optional<Source_record>;
    [[nodiscard]] auto get_prepared_path      (std::string_view target) const -> std::optional<std::string>;
    [[nodiscard]] auto get_prepared_scene     () const -> std::optional<Prepared_scene_record>;
    [[nodiscard]] auto get_stored_format_version() const -> std::optional<std::uint32_t>;

    // Writes; each runs as its own Write_transaction.
    auto put         (const Item_data& item) -> bool;
    auto remove      (const Key& key) -> bool;
    auto copy_item_to(Asset_store& other, const Key& key) -> bool;

    // R8.13: gives the space of removed items back to the file system.
    auto vacuum() -> bool;
    [[nodiscard]] auto get_database_size() const -> std::optional<std::uint64_t>;

private:
    friend class Write_transaction;
    explicit Asset_store(std::unique_ptr<Asset_store_impl>&& impl);

    std::unique_ptr<Asset_store_impl> m_impl;
};

// R5.3: commit() makes every write visible at once; destruction without commit rolls back.
class Write_transaction
{
public:
    explicit Write_transaction(Asset_store& store);
    ~Write_transaction() noexcept;
    Write_transaction(const Write_transaction&) = delete;
    auto operator=(const Write_transaction&) -> Write_transaction& = delete;

    // False when the store is read-only or the writer turn was not granted within the busy timeout.
    [[nodiscard]] auto is_active() const -> bool;

    auto put               (const Item_data& item) -> bool;
    auto remove            (const Key& key) -> bool;
    // Replaces the info of a held item; false when the store holds no item of the key.
    auto set_info          (const Key& key, std::string_view info) -> bool;
    auto copy_item_from    (const Asset_store& source, const Key& key) -> bool;
    auto set_record        (std::string_view name, std::string_view value) -> bool;
    auto set_source_record (const Source_record& record) -> bool;
    auto set_prepared_path (std::string_view target, std::string_view path) -> bool;
    auto set_prepared_scene(const Prepared_scene_record& record) -> bool;

    // False (and rolled back) when any write of the transaction failed.
    auto commit() -> bool;
    void rollback();

private:
    auto fail(const char* operation) -> bool;

    Asset_store&                 m_store;
    std::unique_lock<std::mutex> m_lock;
    bool                         m_active{false};
    bool                         m_failed{false};
};

}
