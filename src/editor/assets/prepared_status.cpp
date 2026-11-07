#include "assets/prepared_status.hpp"
#include "assets/editor_asset.hpp"

#include "editor_log.hpp"

#include "erhe_asset_store/asset_store.hpp"
#include "erhe_file/file.hpp"

#include <fmt/format.h>

namespace editor {

using erhe::asset_store::Asset_store;
using erhe::asset_store::Item_info;
using erhe::asset_store::Item_kind;
using erhe::asset_store::Key;

auto c_str(const Prepared_drop drop) -> const char*
{
    switch (drop) {
        case Prepared_drop::none:               return "none";
        case Prepared_drop::unreferenced_items: return "unreferenced";
        case Prepared_drop::target_items:       return "target";
        case Prepared_drop::all_items:          return "all";
        case Prepared_drop::prepared_asset:     return "prepared_asset";
        default:                                return "?";
    }
}

auto prepared_drop_from_string(const std::string_view text, Prepared_drop& out) -> bool
{
    for (const Prepared_drop drop : {
        Prepared_drop::none, Prepared_drop::unreferenced_items, Prepared_drop::target_items, Prepared_drop::all_items, Prepared_drop::prepared_asset
    }) {
        if (text == c_str(drop)) {
            out = drop;
            return true;
        }
    }
    return false;
}

namespace {

[[nodiscard]] auto is_asked(const std::map<Item_kind, std::set<Key>>& asked, const Key& key) -> bool
{
    for (const auto& [kind, keys] : asked) {
        if (keys.contains(key)) {
            return true;
        }
    }
    return false;
}

} // anonymous namespace

auto measure_store_items(
    const Asset_store&                        store,
    const std::map<Item_kind, std::set<Key>>& asked,
    std::map<Item_kind, Prepared_kind_status>& out_kinds
) -> bool
{
    const std::shared_ptr<const Item_catalog> catalog = list_item_catalog(store);
    if (!catalog) {
        return false;
    }
    measure_catalog_items(*catalog, asked, out_kinds);
    return true;
}

void measure_catalog_items(
    const Item_catalog&                        catalog,
    const std::map<Item_kind, std::set<Key>>&  asked,
    std::map<Item_kind, Prepared_kind_status>& out_kinds
)
{
    for (const auto& [kind, keys] : asked) {
        Prepared_kind_status& status = out_kinds[kind];
        for (const Key& key : keys) {
            if (catalog.items.contains(key)) {
                ++status.present;
            } else {
                ++status.missing;
            }
        }
    }
    for (const auto& [key, entry] : catalog.items) {
        Prepared_kind_status& status = out_kinds[entry.kind];
        ++status.stored_count;
        status.stored_bytes += entry.payload_size;
        if (!is_asked(asked, key)) {
            ++status.unreferenced_count;
            status.unreferenced_bytes += entry.payload_size;
        }
    }
}

auto select_drop_keys(
    const Asset_store&     store,
    const Prepared_drop    drop,
    const std::set<Key>&   asked,
    const std::string_view target,
    std::vector<Key>&      out_keys
) -> bool
{
    out_keys.clear();
    if ((drop == Prepared_drop::none) || (drop == Prepared_drop::prepared_asset)) {
        return true;
    }
    return store.for_each_item([drop, &asked, target, &out_keys](const Item_info& info) {
        const bool selected =
            ((drop == Prepared_drop::all_items)) ||
            ((drop == Prepared_drop::unreferenced_items) && !asked.contains(info.key)) ||
            ((drop == Prepared_drop::target_items) && (info.target == target));
        if (selected) {
            out_keys.push_back(info.key);
        }
    });
}

auto drop_store_items(Asset_store& store, const std::span<const Key> keys) -> bool
{
    if (keys.empty()) {
        return true;
    }
    {
        erhe::asset_store::Write_transaction transaction{store};
        if (!transaction.is_active()) {
            log_export->error("Drop: '{}' takes no writes", erhe::file::to_string(store.get_path()));
            return false;
        }
        for (const Key& key : keys) {
            static_cast<void>(transaction.remove(key));
        }
        if (!transaction.commit()) {
            log_export->error("Drop: the removal from '{}' was rolled back", erhe::file::to_string(store.get_path()));
            return false;
        }
    }
    if (!store.vacuum()) {
        log_export->warn("Drop: VACUUM of '{}' failed", erhe::file::to_string(store.get_path()));
    }
    return true;
}

auto compare_prepared_items(
    const Asset_store&     prepared,
    const std::set<Key>&   asked,
    const Key&             scene_key,
    const std::string_view target,
    std::string&           out_mismatch
) -> bool
{
    out_mismatch.clear();
    const std::optional<erhe::asset_store::Prepared_scene_record> record = prepared.get_prepared_scene();
    if (!record.has_value()) {
        out_mismatch = "the prepared asset records no Scene item";
        return false;
    }
    if (record->target != target) {
        out_mismatch = fmt::format("the prepared asset is for target {}", record->target);
        return false;
    }
    if (record->scene_key != scene_key) {
        out_mismatch = "the prepared asset holds another Scene item";
        return false;
    }
    std::size_t held  = 0;
    std::size_t extra = 0;
    if (!prepared.for_each_item([&asked, &held, &extra](const Item_info& info) {
        if (asked.contains(info.key)) {
            ++held;
        } else {
            ++extra;
        }
    })) {
        out_mismatch = "the items of the prepared asset cannot be listed";
        return false;
    }
    if ((held != asked.size()) || (extra != 0)) {
        out_mismatch = fmt::format("the prepared asset lacks {} and has {} other items", asked.size() - held, extra);
        return false;
    }
    return true;
}

}
