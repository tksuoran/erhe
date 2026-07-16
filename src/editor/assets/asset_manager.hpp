#pragma once

#include "assets/asset_key.hpp"
#include "assets/asset_reference.hpp"

#include "erhe_gltf/gltf.hpp"

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace erhe {
    class Item_base;
}
namespace erhe::scene {
    class Node;
}

namespace editor {

class App_context;
class App_message_bus;
class App_scenes;

// A glTF container file parsed once by the Asset_manager (plan D3). The
// parsed Gltf_data keeps every contained object alive; the free root node
// (no holding scene - container node trees are never rendered) keeps the
// parsed node tree parented. Managed asset types get per-type resolution
// maps built at load, with identifiability validated per plan decision 11.
class Asset_container_record
{
public:
    // Resolution maps for one managed asset type within this container.
    // Only identifiable assets (uid, or a unique non-empty name) are
    // acquirable; violations are recorded in Asset_container_record::errors.
    class Type_entries
    {
    public:
        std::vector<std::shared_ptr<erhe::Item_base>>                     items;          // identifiable, acquirable
        std::unordered_map<std::string, std::shared_ptr<erhe::Item_base>> by_uid;
        std::unordered_map<std::string, std::shared_ptr<erhe::Item_base>> by_unique_name; // names occurring exactly once
        std::unordered_map<std::string, std::size_t>                      name_counts;    // for exact ambiguity errors
    };

    [[nodiscard]] auto get_type_entries(Asset_type type) -> Type_entries*;
    [[nodiscard]] auto get_type_entries(Asset_type type) const -> const Type_entries*;

    std::uint64_t                      id{0};
    std::filesystem::path              canonical_path;  // registry key (normalize_asset_path form)
    std::string                        display_path;    // portable string for messages / MCP
    erhe::gltf::Gltf_data              gltf_data;
    std::shared_ptr<erhe::scene::Node> root_node;
    std::vector<std::string>           errors;          // decision-11 identifiability violations
    bool                               dirty{false};    // live asset edits not yet written back (used from R5 on)

    Type_entries                       materials;
    Type_entries                       animations;
};

class Asset_user_info
{
public:
    std::string label;      // Asset_reference user label
    std::string asset_name; // name of the used asset
};

class Unload_result
{
public:
    bool                         ok{false};
    std::string                  message;
    std::vector<Asset_user_info> users;                  // populated when refused because users exist
    std::size_t                  undeclared_survivors{0}; // post-unload exclusivity check hits
};

// Observability snapshots for the MCP query_asset_manager tool.
class Asset_info
{
public:
    Asset_key                key;
    std::vector<std::string> user_labels;
};

class Asset_container_info
{
public:
    std::uint64_t            id{0};
    std::string              path;
    std::size_t              material_count{0};
    std::size_t              animation_count{0};
    std::vector<std::string> errors;
    bool                     dirty{false};
};

// The asset registry and the only asset loader (plan D3, single-loader
// axiom). Guarantees every asset is loaded at most once: two references to
// the same asset always resolve to the same object. Loads on request
// (acquire), unloads on request (request_unload, refused while users
// exist). Main-thread only in v1 (container parsing needs the frame command
// buffer for texture upload).
//
// R1 scope: registry + usership bookkeeping + file-scope acquire for
// externally-referenced containers (materials, animations) + builtin and
// scene_local resolution. Scene open / import do NOT route through the
// manager yet (that is the R5 single-loader flip); until then
// get_or_load_container refuses a path currently open as a scene, keeping
// the two-loader hazard (plan risk 2) out of reach. create() /
// save_container() / dirty tracking arrive with R5.
class Asset_manager
{
public:
    Asset_manager(App_context& context, App_message_bus& app_message_bus, App_scenes& app_scenes);
    ~Asset_manager() noexcept;

    Asset_manager(const Asset_manager&)            = delete;
    Asset_manager& operator=(const Asset_manager&) = delete;

    // The only way assets come to exist (single-loader axiom): loads on
    // first request, otherwise returns THE copy. Resolution precedence
    // within a container: uid match wins; else a unique conforming name;
    // ambiguous identities fail with a clear error (plan decision 11).
    auto acquire(const Asset_key& key) -> std::shared_ptr<erhe::Item_base>;
    auto acquire(const Asset_key& key, std::string& out_error) -> std::shared_ptr<erhe::Item_base>;

    // Registers a procedural item as a builtin-scope asset (Scene_builder
    // palette brushes). Builtin names are a persistence contract.
    void register_builtin(Asset_type type, const std::shared_ptr<erhe::Item_base>& item);

    // Identity
    [[nodiscard]] auto find_loaded(const Asset_key& key) const -> std::shared_ptr<erhe::Item_base>;
    [[nodiscard]] auto make_key(const erhe::Item_base& item) const -> Asset_key;

    // Users / unload. v1 unload granularity is the container: the parsed
    // Gltf_data pins every contained object, so unloading a single asset of
    // a live container could never actually release it. request_unload on a
    // file-scope key unloads the containing container; it is refused while
    // ANY asset of the container has users (naming them), and a successful
    // unload verifies exclusivity: a managed asset still alive afterwards
    // means a raw shared_ptr bypassed Asset_reference - logged loudly as an
    // undeclared user.
    [[nodiscard]] auto get_users(const Asset_key& key) const -> std::vector<Asset_user_info>;
    auto request_unload(const Asset_key& key) -> Unload_result;

    // One parse per container. Refuses (with a clear error) a missing file,
    // a file that produces no content, and a path currently open as a scene
    // (see class comment).
    auto get_or_load_container(const std::filesystem::path& path, std::string& out_error) -> std::shared_ptr<Asset_container_record>;

    // Observability (MCP query_asset_manager)
    [[nodiscard]] auto inspect_assets    () const -> std::vector<Asset_info>;
    [[nodiscard]] auto inspect_containers() const -> std::vector<Asset_container_info>;

    // Scene-close leak watchdog integration: true when the manager keeps
    // this item alive intentionally - a manager-owned strong reference
    // (builtin registry, loaded container) or a declared usership (a
    // registered Asset_reference holds it). Such scene-close survivors are
    // reported as info, not leak warnings.
    [[nodiscard]] auto is_pinned(const erhe::Item_base* item) const -> bool;

    // Debug holds: named manager-owned Asset_references, the MCP test hooks
    // for driving acquire / release / unload headless. A hold is a declared
    // user like any other.
    auto debug_acquire(std::string_view hold_name, const Asset_key& key, std::string& out_error) -> std::shared_ptr<erhe::Item_base>;
    auto debug_release(std::string_view hold_name) -> bool;
    [[nodiscard]] auto get_debug_holds() const -> const std::map<std::string, Asset_reference>&;

private:
    friend class Asset_reference;
    void register_user  (Asset_reference* reference);
    void unregister_user(Asset_reference* reference);

    void verify_main_thread() const;

    auto resolve_builtin     (const Asset_key& key, std::string& out_error) const -> std::shared_ptr<erhe::Item_base>;
    auto resolve_in_container(const Asset_container_record& record, const Asset_key& key, std::string& out_error) const -> std::shared_ptr<erhe::Item_base>;
    auto resolve_scene_local (const Asset_key& key, std::string& out_error) const -> std::shared_ptr<erhe::Item_base>;

    App_context&     m_context;
    App_message_bus& m_app_message_bus; // reserved for later phases (R5 scene routing); unused in R1
    App_scenes&      m_app_scenes;

    std::uint64_t m_next_container_id{1};

    std::map<Asset_type, std::map<std::string, std::shared_ptr<erhe::Item_base>>>  m_builtins;
    std::map<std::filesystem::path, std::shared_ptr<Asset_container_record>>       m_containers;
    // Declared users per live asset object (usership belongs to the object:
    // a name-keyed and a uid-keyed reference to the same asset are users of
    // one object). Entries are maintained by Asset_reference's special
    // members through register_user / unregister_user.
    std::unordered_map<const erhe::Item_base*, std::vector<Asset_reference*>>      m_users;
    // Declared LAST so the holds (which unregister into m_users on
    // destruction) are destroyed while the maps above are still alive; the
    // destructor also clears them explicitly.
    std::map<std::string, Asset_reference>                                         m_debug_holds;
};

}
