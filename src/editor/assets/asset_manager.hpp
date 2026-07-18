#pragma once

#include "app_message.hpp"
#include "assets/asset_key.hpp"
#include "assets/asset_reference.hpp"

#include "erhe_gltf/gltf.hpp"
#include "erhe_message_bus/message_bus.hpp"

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
    class Item_host;
}
namespace erhe::scene {
    class Node;
}

namespace editor {

class App_context;
class App_message_bus;
class App_scenes;
class Content_library_node;
class Scene_root;

// A glTF container file parsed once by the Asset_manager (plan D3). The
// parsed Gltf_data keeps every contained object alive; the free root node
// (no holding scene - container node trees are never rendered) keeps the
// parsed node tree parented. Managed asset types get per-type resolution
// maps built at load, with identifiability validated per plan decision 11.
//
// Since R5.3 every scene registered with App_scenes also has a record.
// Since the R5.6 single-loader flip the record's per-type SCENE ENTRIES
// hold STRONG references: runtime ownership of a scene's asset-typed
// content (brushes, materials, animations) belongs to the manager. The
// scene's content-library nodes still hold the objects for UI and
// serialization and declare per-entry userships; asset-typed items never
// claim the scene as their Item_host. A scene record keeps its entries
// until the courtesy unload at scene close (plan resolution 4) releases
// them - or keeps the whole container loaded when declared users refuse
// the unload, in which case the record survives the scene as a plain
// loaded container.
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

    // R5.6 scene entries: one STRONG reference per owning asset-typed item
    // of a scene's content library - the manager's runtime ownership of the
    // scene's assets. Unlike Type_entries there are no prebuilt name maps:
    // scene content is mutable (items get renamed while listed), so
    // resolution reads names live from the items.
    class Scene_entries
    {
    public:
        std::vector<std::shared_ptr<erhe::Item_base>> items;
    };

    [[nodiscard]] auto get_type_entries (Asset_type type) -> Type_entries*;
    [[nodiscard]] auto get_type_entries (Asset_type type) const -> const Type_entries*;
    [[nodiscard]] auto get_scene_entries(Asset_type type) -> Scene_entries*;
    [[nodiscard]] auto get_scene_entries(Asset_type type) const -> const Scene_entries*;

    // True for a record created for a registered scene. Such records resolve
    // through their scene entries (live names) for their whole lifetime,
    // including after the scene closed with a refused courtesy unload.
    [[nodiscard]] auto is_scene_record() const -> bool { return scene_record; }
    // True while the record's scene is registered (open). Cleared at scene
    // unregistration; the identity pointers below survive until the courtesy
    // unload point so close_scene subscribers can keep resolving "is this
    // the closing scene's asset" regardless of subscription order.
    [[nodiscard]] auto is_open_as_scene() const -> bool { return scene_open; }

    std::uint64_t                      id{0};
    std::filesystem::path              canonical_path;  // path-index key (normalize_asset_path form); empty for session (never-saved) scene records
    std::string                        display_path;    // portable string for messages / MCP; empty for session scene records
    erhe::gltf::Gltf_data              gltf_data;
    std::shared_ptr<erhe::scene::Node> root_node;
    std::vector<std::string>           errors;          // decision-11 identifiability violations
    bool                               dirty{false};    // live asset edits not yet written back (used from R5 on)

    // Scene identity link (R5.3): weak so the record never pins the scene;
    // the raw pointer duplicates the identity for matching during
    // unregistration paths where the weak_ptr can no longer lock
    // (~Scene_root calls unregister while shared_from_this is gone). The
    // cached scene_name keeps messages readable after the scene is gone.
    bool                               scene_record{false};
    bool                               scene_open{false};
    std::string                        scene_name;
    std::weak_ptr<Scene_root>          scene_root;
    Scene_root*                        scene_root_identity{nullptr};

    Type_entries                       materials;
    Type_entries                       animations;

    // R5.6 strong scene entries (scene records only; loaded containers keep
    // using Type_entries). Maintained by the content-library claim/release
    // hooks through Asset_manager::on_library_node_attached / _detached and
    // seeded by the registration sweep in on_scene_registered; released by
    // the courtesy unload at scene close (or by explicit request_unload of
    // a surviving path-bound record).
    Scene_entries                      scene_brushes;
    Scene_entries                      scene_materials;
    Scene_entries                      scene_animations;
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
    std::string              path;          // empty for session scene records
    std::size_t              brush_count{0};
    std::size_t              material_count{0};
    std::size_t              animation_count{0};
    std::vector<std::string> errors;
    bool                     dirty{false};
    bool                     open_as_scene{false}; // record belongs to an open scene (R5.3)
    bool                     session{false};       // no bound path (scene never saved)
    std::string              scene_name;           // when open_as_scene
};

// The asset registry and the only asset loader (plan D3, single-loader
// axiom). Guarantees every asset is loaded at most once: two references to
// the same asset always resolve to the same object. Loads on request
// (acquire), unloads on request (request_unload, refused while users
// exist). Main-thread only in v1 (container parsing needs the frame command
// buffer for texture upload).
//
// Since the R5.6 flip the manager OWNS the runtime lifetime of every
// scene's asset-typed content: scene records hold strong entries, library
// entries are declared users, and scene close issues a courtesy unload
// (resolution 4) at the watchdog arming point. A file-scope acquire of a
// path open as a scene serves THE scene's object (no second parse). The
// remaining pre-single-loader seam is the open direction (opening a scene
// over an already-loaded container re-parses; R5.7 record adoption).
// save_container() / dirty tracking arrive with R5.8.
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

    // The in-editor asset creation funnel (R5 sub-plan step R5.5): every
    // in-editor site that brings a new managed asset into existence
    // constructs it through the manager, naming the scene whose container
    // record is the asset's defining container. The caller adds the object
    // to the scene's content library; the library attach hooks give the
    // scene record its strong entry (manager ownership) and the library
    // entry its usership.
    template <typename T, typename... Args>
    auto create(Scene_root& defining_scene, Args&&... args) -> std::shared_ptr<T>
    {
        std::shared_ptr<T> item = std::make_shared<T>(std::forward<Args>(args)...);
        register_created(item, defining_scene);
        return item;
    }

    // Library bookkeeping hooks (R5.6), called by the content-library
    // claim/release walks for manager-owned asset types (and by the
    // on_scene_registered sweep). An OWNING entry adds/removes the scene
    // record's strong entry; every entry (owning and reference) holds a
    // declared usership on its node ("scene '<name>' library <type>
    // '<item>'"), so unload refusals name library entries. No-ops for
    // owners without a record (preview scenes, the tool scene).
    void on_library_node_attached(erhe::Item_host* owner, Content_library_node& node);
    void on_library_node_detached(erhe::Item_host* owner, Content_library_node& node);

    // Identity
    [[nodiscard]] auto find_loaded(const Asset_key& key) const -> std::shared_ptr<erhe::Item_base>;
    // Finds a loaded managed asset object by its unique item id (scans the
    // builtins and every loaded container's entries). MCP id-addressed
    // tools use this to reach container assets, which live in no scene
    // library.
    [[nodiscard]] auto find_loaded_by_id(std::size_t item_id) const -> std::shared_ptr<erhe::Item_base>;
    [[nodiscard]] auto make_key(const erhe::Item_base& item) const -> Asset_key;

    // Defining-scene lookups (R5.6, plan resolution 2): asset types are not
    // hosted, so "the scene that owns this asset" is the manager's recorded
    // fact - the scene whose container record holds the item.
    [[nodiscard]] auto get_defining_scene_root(const erhe::Item_base& item) const -> std::shared_ptr<Scene_root>;
    // True when the item's defining container record is the scene identified
    // by host (identity compare - stays valid through the whole close_scene
    // dispatch regardless of subscription order).
    [[nodiscard]] auto is_defined_by(const erhe::Item_base& item, const erhe::Item_host* host) const -> bool;
    // "Does this item belong to this host": items with a defining record
    // compare against the record's scene identity; everything else falls
    // back to Item_host comparison (structure and library hosting).
    [[nodiscard]] auto is_hosted_or_defined_by(const erhe::Item_base& item, const erhe::Item_host* host) const -> bool;
    // True when the item is known to the manager: a builtin, a loaded
    // container's asset, or a scene record's entry.
    [[nodiscard]] auto is_managed(const erhe::Item_base& item) const -> bool;
    // Cross-scene use rule (plan resolution 11): referencing an asset
    // defined in another scene is accepted when the defining container is
    // path-bound - exactly the condition under which a durable file-scope
    // key exists. Session-only scene assets are rejected; builtins and
    // items without a defining record are trivially referenceable.
    [[nodiscard]] auto is_cross_scene_referenceable(const erhe::Item_base& item) const -> bool;

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

    // One parse per container. Refuses (with a clear error) a missing file
    // and a file that produces no content. A path currently open as a scene
    // returns the scene's record, whose strong entries serve the scene's
    // own objects - no second parse.
    auto get_or_load_container(const std::filesystem::path& path, std::string& out_error) -> std::shared_ptr<Asset_container_record>;

    // R5.3 scene identity records. Called by App_scenes when a Scene_root
    // registers / unregisters, and (via App_scenes) whenever a scene's
    // source path changes: registration creates the scene's record (picking
    // up the source path an opened scene already carries), the first save
    // binds the record to the path, save-as re-homes it. Since R5.6
    // unregistration KEEPS the record (it owns the scene's assets); the
    // Editor severs the scene link at the watchdog arming point via
    // detach_scene_record and then issues the courtesy unload.
    void on_scene_registered         (const std::shared_ptr<Scene_root>& scene_root);
    void on_scene_unregistered       (Scene_root* scene_root);
    void on_scene_source_path_changed(Scene_root& scene_root);

    // Scene-close teardown (R5.6, plan resolution 4), driven by the Editor
    // at the update_scene_close_leak_watches arming point:
    // detach_scene_record severs the record's scene identity (called while
    // the closing Scene_root is still alive; returns the record id, 0 when
    // the scene has no record), and courtesy_unload_container - called
    // after the Scene_root's destruction released the library's entry
    // userships - unloads the record unless declared users (slots, other
    // scenes' reference entries, debug holds) refuse it, which is normal
    // and keeps the container loaded.
    auto detach_scene_record     (Scene_root* scene_root) -> std::uint64_t;
    void courtesy_unload_container(std::uint64_t record_id);

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

    // create<T>() bookkeeping (R5.5): validates the type and records the
    // creation against the defining scene's container record.
    void register_created(const std::shared_ptr<erhe::Item_base>& item, Scene_root& defining_scene);

    // close_scene subscription: resolution 5 - resets every manager-known
    // animation channel target that points at a node of the closing scene
    // (nodes must die with the scene; a surviving animation must not pin
    // them). Channels keep their sampler data.
    void on_close_scene(Scene_root* scene_root);

    // The unload core shared by request_unload and the courtesy unload:
    // refuses (naming users) while any asset of the record has declared
    // users or undo/redo history references, otherwise drops the record and
    // weak-verifies exclusivity (undeclared-user detection).
    auto unload_record(std::shared_ptr<Asset_container_record> record) -> Unload_result;

    [[nodiscard]] auto find_defining_record(const erhe::Item_base& item) const -> std::shared_ptr<Asset_container_record>;

    auto resolve_builtin     (const Asset_key& key, std::string& out_error) const -> std::shared_ptr<erhe::Item_base>;
    auto resolve_in_container(const Asset_container_record& record, const Asset_key& key, std::string& out_error) const -> std::shared_ptr<erhe::Item_base>;
    auto resolve_scene_local (const Asset_key& key, std::string& out_error) const -> std::shared_ptr<erhe::Item_base>;

    // Points the path index at the record's (possibly new) path: releases
    // the slot the record held, then claims the new one. A path already
    // bound to ANOTHER record logs a loud warning and is not re-claimed
    // (two records sharing one path is the open-direction two-loader
    // hazard surfacing; resolved by the R5.7 record adoption).
    void bind_record_path(Asset_container_record& record, const std::filesystem::path& path);
    [[nodiscard]] auto find_scene_record(const Scene_root* scene_root) const -> std::shared_ptr<Asset_container_record>;
    [[nodiscard]] auto find_record_by_path(const std::filesystem::path& canonical_path) const -> std::shared_ptr<Asset_container_record>;
    // Scene record lookup by the Item_host identity the content-library
    // hooks carry (a Scene_root IS the erhe::Item_host of its library items).
    [[nodiscard]] auto find_record_by_owner(const erhe::Item_host* owner) const -> std::shared_ptr<Asset_container_record>;

    App_context&     m_context;
    App_message_bus& m_app_message_bus;
    App_scenes&      m_app_scenes;

    erhe::message_bus::Subscription<Close_scene_message> m_close_scene_subscription;

    std::uint64_t m_next_container_id{1};

    std::map<Asset_type, std::map<std::string, std::shared_ptr<erhe::Item_base>>>  m_builtins;
    // Registry re-keyed by session container id (R5.3); the path index maps
    // a bound path to its record. Session (never-saved) scene records are
    // only in the id map.
    std::map<std::uint64_t, std::shared_ptr<Asset_container_record>>               m_containers;
    std::map<std::filesystem::path, std::uint64_t>                                 m_path_index;
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
