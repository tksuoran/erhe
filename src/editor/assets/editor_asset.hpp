#pragma once

#include "erhe_asset_store/item_kind.hpp"
#include "erhe_asset_store/key.hpp"

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>

namespace erhe::asset_store { class Asset_store; }

namespace editor {

// <source stem>.erhe_editor beside the source file (R2.4).
[[nodiscard]] auto get_default_editor_asset_path  (const std::filesystem::path& source_path) -> std::filesystem::path;
// <source stem>.<target>.erhe_prepared beside the source file (R2.4).
[[nodiscard]] auto get_default_prepared_asset_path(const std::filesystem::path& source_path, std::string_view target) -> std::filesystem::path;
// R2.5: <cache directory>/<source stem>-<hash of the normalized source path>.erhe_editor.
[[nodiscard]] auto get_fallback_editor_asset_path (const std::filesystem::path& source_path, const std::filesystem::path& cache_directory) -> std::filesystem::path;

// Where an editor asset was found or made (doc/editor/prepared_assets.md "Locations").
enum class Editor_asset_location : unsigned int
{
    record = 0,       // the path the source file records (R2.2)
    default_location, // beside the source file (R2.4)
    fallback          // under the editor's cache directory (R2.5)
};

[[nodiscard]] auto c_str(Editor_asset_location location) -> const char*;

// What the status reads of one stored item.
class Item_catalog_entry
{
public:
    erhe::asset_store::Item_kind kind{erhe::asset_store::Item_kind::editor_data};
    std::string                  target;
    std::uint64_t                payload_size{0};
};

// Every item of a store by key, as one listing read it (doc/editor/prepared_assets.md "Status").
class Item_catalog
{
public:
    std::unordered_map<erhe::asset_store::Key, Item_catalog_entry, erhe::asset_store::Key_hash> items;
};

// The items of a store, listed on first use after a change; null when the listing fails. Thread-safe.
[[nodiscard]] auto list_item_catalog(const erhe::asset_store::Asset_store& store) -> std::shared_ptr<const Item_catalog>;

// The store of one source file's derived data (doc/editor/prepared_assets.md).
class Editor_asset
{
public:
    Editor_asset(
        const std::filesystem::path&                    source_path,
        std::unique_ptr<erhe::asset_store::Asset_store> store,
        Editor_asset_location                           location
    );
    ~Editor_asset() noexcept;
    Editor_asset(const Editor_asset&) = delete;
    auto operator=(const Editor_asset&) -> Editor_asset& = delete;

    [[nodiscard]] auto get_source_path() const -> const std::filesystem::path&;
    [[nodiscard]] auto get_asset_path () const -> const std::filesystem::path&;
    [[nodiscard]] auto get_location   () const -> Editor_asset_location;
    [[nodiscard]] auto get_store      () -> erhe::asset_store::Asset_store&;
    // The store's items as the last listing read them, listed again after
    // invalidate_item_catalog(); null when the listing fails. Thread-safe.
    [[nodiscard]] auto get_item_catalog() -> std::shared_ptr<const Item_catalog>;
    // Called by every writer of the store (Export, the bakes, the drops) and
    // by a Refresh (another process may write the file, R5.6). Thread-safe.
    void invalidate_item_catalog();

private:
    std::filesystem::path                           m_source_path;
    std::unique_ptr<erhe::asset_store::Asset_store> m_store;
    Editor_asset_location                           m_location;
    std::mutex                                      m_catalog_mutex;
    std::shared_ptr<const Item_catalog>             m_catalog;
};

// The open editor assets, one per normalized source path (R10.1); thread-safe.
class Editor_asset_registry
{
public:
    explicit Editor_asset_registry(const std::filesystem::path& cache_directory);
    ~Editor_asset_registry() noexcept;
    Editor_asset_registry(const Editor_asset_registry&) = delete;
    auto operator=(const Editor_asset_registry&) -> Editor_asset_registry& = delete;

    // The editor asset of a source file, opened on first use from the path the
    // source records (R2.2), else its default location (R2.4), else the
    // fallback (R2.5); null (logged, naming the file and both locations) when
    // none can be written. record_path: absolute, empty when the source records none.
    [[nodiscard]] auto get(const std::filesystem::path& source_path, const std::filesystem::path& record_path) -> std::shared_ptr<Editor_asset>;
    // The editor asset of a source file when it is open; null otherwise.
    [[nodiscard]] auto find(const std::filesystem::path& source_path) -> std::shared_ptr<Editor_asset>;
    [[nodiscard]] auto get_cache_directory() const -> const std::filesystem::path&;
    // Editor_asset::invalidate_item_catalog() of every open editor asset.
    void invalidate_item_catalogs();
    // Why get() returned null: "'<source>' cannot be written at '<location>' nor at the fallback '<fallback>'".
    [[nodiscard]] auto describe_unwritable(const std::filesystem::path& source_path, const std::filesystem::path& record_path) const -> std::string;

private:
    std::mutex                                                     m_mutex;
    std::filesystem::path                                          m_cache_directory;
    std::map<std::filesystem::path, std::shared_ptr<Editor_asset>> m_assets;
    std::set<std::filesystem::path>                                m_fallback_logged;
};

// R2.2: the editor asset path a Save of the source at source_path writes into
// ERHE_scene, relative to the source's directory: the open editor asset's
// (a fallback one is never recorded, R2.6), else the scene's own record while
// it saves onto its source, else the default name.
[[nodiscard]] auto make_editor_asset_record(
    Editor_asset_registry*       registry,
    const std::filesystem::path& source_path,
    const std::filesystem::path& scene_source_path,
    const std::filesystem::path& scene_record_path
) -> std::string;

}
