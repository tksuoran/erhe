#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>

namespace erhe {
    class Item_base;
}

namespace editor {

class Content_library;
class Content_library_node;

// Asset identity (asset-manager plan D1). An asset is a thing defined in
// exactly one container; its key carries where it lives (scope + path) and
// how it is addressed inside the container (uid first, unique conforming
// name as the glTF 2.1-sanctioned fallback). There is deliberately NO
// array-index field: index addressing is positional and silently re-binds
// after reorder; ambiguous identities are a load error (plan decision 11).

enum class Asset_scope : int {
    none        = 0,
    scene_local = 1, // by-name lookup against the manager registry and open scene libraries (legacy name-only refs: slots, graph nodes)
    builtin     = 2, // procedural item registered with the manager (Scene_builder palette brushes)
    file        = 3  // asset defined in a container file
};

enum class Asset_type : int {
    none      = 0,
    brush     = 1,
    material  = 2,
    animation = 3,
    mesh      = 4  // scene_local-only (graph source nodes); later: texture, graph_texture, graph_mesh, skin
};

[[nodiscard]] auto c_str(Asset_scope scope) -> const char*;
[[nodiscard]] auto c_str(Asset_type type) -> const char*;
[[nodiscard]] auto parse_asset_scope(std::string_view text) -> Asset_scope;
[[nodiscard]] auto parse_asset_type (std::string_view text) -> Asset_type;

class Asset_key
{
public:
    Asset_scope scope{Asset_scope::none};
    Asset_type  type {Asset_type::none};
    std::string path {};   // file scope; portable forward-slash string (see asset_paths.hpp)
    std::string uid  {};   // glTF 2.1 unique ID - primary identity within the container when present
    std::string name {};   // fallback identity; must be unique within (container, type) to serve as an identifier

    [[nodiscard]] auto operator==(const Asset_key& other) const -> bool = default;
    [[nodiscard]] auto is_empty() const -> bool;
    [[nodiscard]] auto describe() const -> std::string;
};

class Asset_key_hash
{
public:
    [[nodiscard]] auto operator()(const Asset_key& key) const -> std::size_t;
};

// Per-type traits (plan D1): adding a managed asset type is a one-row
// change. library_folder is the Content_library category folder searched by
// scene_local resolution; null for types not stored in content libraries
// (mesh resolves against scene nodes' Mesh attachments instead).
class Asset_type_info
{
public:
    Asset_type                                              type;
    const char*                                             name;
    uint64_t                                                item_type_bit;
    std::shared_ptr<Content_library_node> Content_library::* library_folder;
};

[[nodiscard]] auto get_asset_type_infos() -> std::span<const Asset_type_info>;
[[nodiscard]] auto get_asset_type_info (Asset_type type) -> const Asset_type_info*;

// Managed asset type of an item, from its Item_type bits; Asset_type::none
// for items that are not managed asset types.
[[nodiscard]] auto asset_type_from_item(const erhe::Item_base& item) -> Asset_type;

// The R5.6 flip scope: asset types whose runtime ownership belongs to the
// Asset_manager (scene container records hold them strongly) and which
// never claim an Item_host. mesh is a scene_local resolution convenience
// for graph source nodes, not a manager-owned type.
[[nodiscard]] auto is_manager_owned_asset_type(Asset_type type) -> bool;

}
