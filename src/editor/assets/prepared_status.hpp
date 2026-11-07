#pragma once

#include "erhe_asset_store/item_kind.hpp"
#include "erhe_asset_store/key.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace erhe::asset_store { class Asset_store; }
namespace editor { class Item_catalog; }

// The status of a scene's editor asset and prepared asset and the drops of
// R8.13 (doc/editor/prepared_assets.md "Status").
namespace editor {

enum class Prepared_drop : unsigned int
{
    none = 0,
    unreferenced_items, // editor asset items the scene does not ask for
    target_items,       // editor asset items of the status target
    all_items,          // every editor asset item
    prepared_asset      // the prepared asset file of the target
};

[[nodiscard]] auto c_str(Prepared_drop drop) -> const char*;
[[nodiscard]] auto prepared_drop_from_string(std::string_view text, Prepared_drop& out) -> bool;

class Prepared_kind_status
{
public:
    std::size_t   present           {0}; // keys the scene asks for that the editor asset holds
    std::size_t   missing           {0}; // keys the scene asks for that the editor asset lacks
    std::uint64_t stored_count      {0}; // editor asset items of the kind
    std::uint64_t stored_bytes      {0}; // their payload bytes
    std::uint64_t unreferenced_count{0}; // of those, the ones the scene does not ask for
    std::uint64_t unreferenced_bytes{0};
};

// One source file of the scene and its editor asset (R10.1).
class Prepared_file_status
{
public:
    std::filesystem::path source_path;
    std::filesystem::path editor_asset_path;
    std::string           role;     // scene, prefab or material_container
    std::size_t           depth{0}; // prefab nesting depth
    std::string           location; // record, default or fallback
    std::uint64_t         editor_asset_bytes{0};
};

class Prepared_status_report
{
public:
    std::vector<Prepared_file_status>                            files; // Ensure order, the scene last
    std::string                                                  scene_name;
    std::string                                                  target;
    std::filesystem::path                                        editor_asset_path;
    std::uint64_t                                                editor_asset_bytes{0};
    std::filesystem::path                                        prepared_asset_path;
    bool                                                         prepared_asset_exists{false};
    std::uint64_t                                                prepared_asset_bytes{0};
    std::uint64_t                                                prepared_item_count {0};
    std::string                                                  scene_key;          // empty: unknown (a mesh item is in neither store)
    std::string                                                  prepared_scene_key; // what the prepared asset records
    bool                                                         matches{false};
    std::string                                                  mismatch;           // why it does not match
    std::map<erhe::asset_store::Item_kind, Prepared_kind_status> kinds;
    std::size_t                                                  inline_primitive_count{0};
    // A key check (no scene hold) leaves the Scene key to a status job: false then.
    bool                                                         scene_key_checked{true};
    std::size_t                                                  hash_pending_count {0}; // key check: derived content hashes left to a status job
    std::size_t                                                  computed_hash_count{0}; // content hashes this job computed (the shape had none)
    std::size_t                                                  asked_key_count    {0}; // every key the scene asks for, over the kinds
    Prepared_drop                                                drop{Prepared_drop::none};
    std::uint64_t                                                dropped_count{0};
    std::uint64_t                                                bytes_before_drop{0}; // of the store the drop changed
    double                                                       compute_ms{0.0};
};

// Per kind of 'asked' the present and missing counts, and per kind of every
// item of the store the stored and unreferenced counts and payload bytes.
[[nodiscard]] auto measure_store_items(
    const erhe::asset_store::Asset_store&                                         store,
    const std::map<erhe::asset_store::Item_kind, std::set<erhe::asset_store::Key>>& asked,
    std::map<erhe::asset_store::Item_kind, Prepared_kind_status>&                 out_kinds
) -> bool;

// measure_store_items() over a listing of the store (Editor_asset::get_item_catalog()).
void measure_catalog_items(
    const Item_catalog&                                                           catalog,
    const std::map<erhe::asset_store::Item_kind, std::set<erhe::asset_store::Key>>& asked,
    std::map<erhe::asset_store::Item_kind, Prepared_kind_status>&                 out_kinds
);

// The editor asset items a drop removes (prepared_asset and none: none).
[[nodiscard]] auto select_drop_keys(
    const erhe::asset_store::Asset_store&  store,
    Prepared_drop                          drop,
    const std::set<erhe::asset_store::Key>& asked,
    std::string_view                       target,
    std::vector<erhe::asset_store::Key>&   out_keys
) -> bool;

// Removes the items in one write transaction, then VACUUM gives the space back.
[[nodiscard]] auto drop_store_items(erhe::asset_store::Asset_store& store, std::span<const erhe::asset_store::Key> keys) -> bool;

// True when the prepared asset holds exactly 'asked' and records scene_key and target; else 'out_mismatch' says why.
[[nodiscard]] auto compare_prepared_items(
    const erhe::asset_store::Asset_store&  prepared,
    const std::set<erhe::asset_store::Key>& asked,
    const erhe::asset_store::Key&          scene_key,
    std::string_view                       target,
    std::string&                           out_mismatch
) -> bool;

}
