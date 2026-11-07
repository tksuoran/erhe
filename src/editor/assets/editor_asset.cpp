#include "assets/editor_asset.hpp"

#include "assets/asset_paths.hpp"
#include "editor_log.hpp"

#include "erhe_asset_store/asset_store.hpp"
#include "erhe_asset_store/key.hpp"
#include "erhe_file/file.hpp"
#include "erhe_verify/verify.hpp"

#include <fmt/format.h>

namespace editor {

auto get_default_editor_asset_path(const std::filesystem::path& source_path) -> std::filesystem::path
{
    std::filesystem::path path = source_path.parent_path() / source_path.stem();
    path += ".erhe_editor";
    return path;
}

auto get_default_prepared_asset_path(const std::filesystem::path& source_path, const std::string_view target) -> std::filesystem::path
{
    std::filesystem::path path = source_path.parent_path() / source_path.stem();
    path += ".";
    path += std::string{target};
    path += ".erhe_prepared";
    return path;
}

auto get_fallback_editor_asset_path(const std::filesystem::path& source_path, const std::filesystem::path& cache_directory) -> std::filesystem::path
{
    const std::string text = asset_path_to_string(normalize_asset_path(source_path));
    const erhe::asset_store::Key hash = erhe::asset_store::Content_hasher::hash(std::as_bytes(std::span<const char>{text.data(), text.size()}));
    return cache_directory / (erhe::file::to_string(source_path.stem()) + "-" + hash.to_hex().substr(0, 16) + ".erhe_editor");
}

auto c_str(const Editor_asset_location location) -> const char*
{
    switch (location) {
        case Editor_asset_location::record:           return "record";
        case Editor_asset_location::default_location: return "default";
        case Editor_asset_location::fallback:         return "fallback";
        default:                                      return "?";
    }
}

Editor_asset::Editor_asset(
    const std::filesystem::path&                    source_path,
    std::unique_ptr<erhe::asset_store::Asset_store> store,
    const Editor_asset_location                     location
)
    : m_source_path{source_path}
    , m_store      {std::move(store)}
    , m_location   {location}
{
    ERHE_VERIFY(m_store);
}

Editor_asset::~Editor_asset() noexcept = default;

auto Editor_asset::get_source_path() const -> const std::filesystem::path&
{
    return m_source_path;
}

auto Editor_asset::get_asset_path() const -> const std::filesystem::path&
{
    return m_store->get_path();
}

auto Editor_asset::get_location() const -> Editor_asset_location
{
    return m_location;
}

auto Editor_asset::get_store() -> erhe::asset_store::Asset_store&
{
    return *m_store;
}

auto list_item_catalog(const erhe::asset_store::Asset_store& store) -> std::shared_ptr<const Item_catalog>
{
    std::shared_ptr<Item_catalog> catalog = std::make_shared<Item_catalog>();
    const bool listed = store.for_each_item([&catalog](const erhe::asset_store::Item_info& info) {
        catalog->items.insert_or_assign(info.key, Item_catalog_entry{.kind = info.kind, .target = info.target, .payload_size = info.payload_size});
    });
    if (!listed) {
        return {};
    }
    return catalog;
}

auto Editor_asset::get_item_catalog() -> std::shared_ptr<const Item_catalog>
{
    const std::lock_guard<std::mutex> lock{m_catalog_mutex};
    if (!m_catalog) {
        m_catalog = list_item_catalog(*m_store);
    }
    return m_catalog;
}

void Editor_asset::invalidate_item_catalog()
{
    const std::lock_guard<std::mutex> lock{m_catalog_mutex};
    m_catalog.reset();
}

Editor_asset_registry::Editor_asset_registry(const std::filesystem::path& cache_directory)
    : m_cache_directory{cache_directory}
{
}

Editor_asset_registry::~Editor_asset_registry() noexcept = default;

auto Editor_asset_registry::get_cache_directory() const -> const std::filesystem::path&
{
    return m_cache_directory;
}

void Editor_asset_registry::invalidate_item_catalogs()
{
    const std::lock_guard<std::mutex> lock{m_mutex};
    for (const auto& [source_path, asset] : m_assets) {
        static_cast<void>(source_path);
        asset->invalidate_item_catalog();
    }
}

auto Editor_asset_registry::describe_unwritable(const std::filesystem::path& source_path, const std::filesystem::path& record_path) const -> std::string
{
    const std::filesystem::path normalized = normalize_asset_path(source_path);
    const std::filesystem::path primary    = record_path.empty() ? get_default_editor_asset_path(normalized) : record_path;
    return fmt::format(
        "'{}' cannot be written at '{}' nor at the fallback '{}'",
        erhe::file::to_string(normalized), erhe::file::to_string(primary),
        erhe::file::to_string(get_fallback_editor_asset_path(normalized, m_cache_directory))
    );
}

auto Editor_asset_registry::find(const std::filesystem::path& source_path) -> std::shared_ptr<Editor_asset>
{
    const std::filesystem::path normalized = normalize_asset_path(source_path);
    const std::lock_guard<std::mutex> lock{m_mutex};
    const auto i = m_assets.find(normalized);
    return (i != m_assets.end()) ? i->second : std::shared_ptr<Editor_asset>{};
}

auto Editor_asset_registry::get(const std::filesystem::path& source_path, const std::filesystem::path& record_path) -> std::shared_ptr<Editor_asset>
{
    const std::filesystem::path normalized = normalize_asset_path(source_path);
    const std::lock_guard<std::mutex> lock{m_mutex};
    const auto i = m_assets.find(normalized);
    if (i != m_assets.end()) {
        return i->second;
    }
    const Editor_asset_location primary_location = record_path.empty() ? Editor_asset_location::default_location : Editor_asset_location::record;
    const std::filesystem::path primary_path     = record_path.empty() ? get_default_editor_asset_path(normalized) : record_path;
    Editor_asset_location location = primary_location;
    std::filesystem::path asset_path = primary_path;
    std::unique_ptr<erhe::asset_store::Asset_store> store = erhe::asset_store::Asset_store::open(asset_path, erhe::asset_store::Open_mode::editor_read_write);
    if (!store) {
        // R2.5: a read-only location (an asset pack); the fallback is named from the source path.
        location   = Editor_asset_location::fallback;
        asset_path = get_fallback_editor_asset_path(normalized, m_cache_directory);
        static_cast<void>(erhe::file::ensure_directory_exists(m_cache_directory));
        store = erhe::asset_store::Asset_store::open(asset_path, erhe::asset_store::Open_mode::editor_read_write);
        if (!store) {
            log_export->error("Editor asset of {} - nothing is stored for this file", describe_unwritable(normalized, record_path));
            return {};
        }
        if (m_fallback_logged.insert(normalized).second) {
            log_export->warn(
                "Editor asset of '{}': '{}' cannot be written - using the fallback '{}'",
                erhe::file::to_string(normalized), erhe::file::to_string(primary_path), erhe::file::to_string(asset_path)
            );
        }
    }
    log_export->info("Editor asset '{}' ({}) opened for '{}'", erhe::file::to_string(asset_path), c_str(location), erhe::file::to_string(normalized));
    std::shared_ptr<Editor_asset> asset = std::make_shared<Editor_asset>(normalized, std::move(store), location);
    m_assets.emplace(normalized, asset);
    return asset;
}

auto make_editor_asset_record(
    Editor_asset_registry* const registry,
    const std::filesystem::path& source_path,
    const std::filesystem::path& scene_source_path,
    const std::filesystem::path& scene_record_path
) -> std::string
{
    const std::filesystem::path directory = normalize_asset_path(source_path).parent_path();
    const auto relative = [&directory](const std::filesystem::path& path) -> std::string {
        std::error_code error_code;
        const std::filesystem::path relative_path = std::filesystem::relative(path, directory, error_code);
        return (error_code || relative_path.empty()) ? path.generic_string() : relative_path.generic_string();
    };
    const std::shared_ptr<Editor_asset> open_asset = (registry != nullptr) ? registry->find(source_path) : std::shared_ptr<Editor_asset>{};
    if (open_asset && (open_asset->get_location() != Editor_asset_location::fallback)) {
        return relative(open_asset->get_asset_path());
    }
    if (!open_asset && !scene_record_path.empty() && (normalize_asset_path(scene_source_path) == normalize_asset_path(source_path))) {
        return relative(scene_record_path);
    }
    return relative(get_default_editor_asset_path(normalize_asset_path(source_path)));
}

}
