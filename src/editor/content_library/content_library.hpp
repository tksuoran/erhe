#pragma once

#include "app_context.hpp"
#include "assets/asset_key.hpp"
#include "editor_log.hpp"
#include "graphics/icon_set.hpp"
#include "scene/generated/gltf_source_reference.hpp"

#include "erhe_item/hierarchy.hpp"
#include "erhe_item/item_host.hpp"
#include "erhe_item/scope.hpp"
#include "erhe_item/typed.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_verify/verify.hpp"

#include <imgui/imgui.h>

#include <any>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace erhe::gltf {
    class Gltf_image_source;
}

namespace editor {

class Asset_manager;
class Asset_reference;

// What the library keeps beside a resource: where the resource came from and
// how its defining container addresses it. It is bookkeeping of the LIBRARY,
// not of the resource, so it lives here rather than on the prim, and it
// outlives the index entry - an undo takes a resource prim out of the tree
// and a redo puts it back, and the bookkeeping must survive that.
class Resource_metadata
{
public:
    // Guards the pointer key: the entry is this resource's only while the
    // weak pointer is unexpired.
    std::weak_ptr<erhe::Item_base>                 resource;
    // The resource is listed but owned by another container (a prefab
    // template's material or texture), so it is not a prim of this scene's
    // tree. 2e retires the concept.
    bool                                           is_reference{false};
    std::optional<Gltf_source_reference>           gltf_source;
    // Texture resources only: the retained compressed source image stream,
    // so glTF export can re-embed the image byte-exact
    // (doc/gltf-scene-roundtrip-plan.md phase 0).
    std::shared_ptr<erhe::gltf::Gltf_image_source> image_source;
    // Asset identity when the defining container is known (asset-manager
    // plan, R5 sub-plan resolution 2).
    std::optional<Asset_key>                       asset_key;
    // Declared usership of an asset-typed resource (R5.6), filled by the
    // asset manager's library hook when the owning scene is registered, so
    // the listing is a named user in unload refusals.
    std::unique_ptr<Asset_reference>               asset_usership;
};

// The per-scene index of the scene's resources (doc/usd-compatibility-plan.md
// U4). A resource - a material, a texture, a brush, a style, a physics
// material, a collision filter, joint settings, a geometry or texture graph,
// an animation, a skin - is a `Typed` prim of the scene's prim tree, under
// the `Scope` named for its kind (`Materials`, `Brushes`, ...) or under any
// other prim the user puts it under (C5). This class owns no prim: the index
// it answers `get_all<T>()`, `has_item()` and `get_all_of_kind()` from is
// maintained by the item-host hook (`erhe::Item_host::register_prim`), which
// the owning `Scene_root` forwards here.
//
// A library also lists resources it does NOT own: a prefab template's
// materials and textures, which the instancing scene's meshes reference and
// its material set must therefore give slots. Those belong to another
// scene's tree, so they are index entries with no prim placement of their
// own; `add_referenced()` lists them and `is_referenced()` tells them apart.
class Content_library : public erhe::Item_host
{
public:
    Content_library();
    ~Content_library() noexcept override;

    // Implements erhe::Item_host. A library with no owning scene (the
    // Scene_builder template palette) hosts its own prims, so one hook
    // maintains the index in both cases.
    [[nodiscard]] auto get_host_name() const -> const char* override;
    void register_prim  (const std::shared_ptr<erhe::Typed>& prim) override;
    void unregister_prim(const std::shared_ptr<erhe::Typed>& prim) override;

    // The resource kinds the index keeps, as `erhe::Item_type` bits. One kind
    // is one `Scope` under the scene root.
    [[nodiscard]] static auto get_kind_type_bits() -> std::span<const uint64_t>;
    // The one kind bit of an item type mask, 0 when it names no kind.
    [[nodiscard]] static auto get_kind_type_bit_of_type(uint64_t item_type) -> uint64_t;
    // The kind of an item, 0 when the item is not a library resource kind.
    [[nodiscard]] static auto get_kind_type_bit(const erhe::Item_base& item) -> uint64_t;
    // The name of a kind's scope, empty for a type bit that names no kind.
    [[nodiscard]] static auto get_kind_scope_name(uint64_t kind_type_bit) -> std::string_view;

    // The owning scene's `Scene_root` is the `Item_host` of every resource
    // prim, and `prim_root` - the scene's root node - is the prim the kind
    // scopes hang off. Set once by the owning `Scene_root`'s constructor and
    // cleared by its destructor; a library that is never given one keeps its
    // own detached root scope and hosts its prims itself.
    void set_owner(erhe::Item_host* owner, const std::shared_ptr<erhe::Hierarchy>& prim_root);
    [[nodiscard]] auto get_owner() const -> erhe::Item_host*;

    // The prim the kind scopes are children of: the owning scene's root node,
    // or this library's own root scope.
    [[nodiscard]] auto get_prim_root() const -> std::shared_ptr<erhe::Hierarchy>;

    // The `Scope` a resource of this kind is placed under. `get_scope()`
    // creates it on the first resource of that kind and then keeps it, so a
    // kind no resource ever reached adds no prim and a folder the user made
    // under a scope survives emptying it.
    [[nodiscard]] auto get_scope (uint64_t kind_type_bit) -> std::shared_ptr<erhe::Scope>;
    [[nodiscard]] auto find_scope(uint64_t kind_type_bit) const -> std::shared_ptr<erhe::Scope>;
    // The kind whose scope this prim is, or sits below; 0 when it is neither.
    [[nodiscard]] auto find_scope_kind(const erhe::Hierarchy& prim) const -> uint64_t;

    // R5.5 shadow registration hook: set by Asset_manager::on_scene_registered
    // (cleared at unregistration and at manager teardown) so the index
    // updates mirror owned asset-typed resources into the owning scene's
    // container record. Null for libraries whose scene is not registered
    // (previews, the tool scene, the Scene_builder template palette).
    void set_asset_manager(Asset_manager* asset_manager);
    [[nodiscard]] auto get_asset_manager() const -> Asset_manager*;

    // Re-announces every listed resource to the asset manager, for the
    // registration sweep of a scene that is already populated (a redo of a
    // scene open re-registers without re-importing). Idempotent.
    void announce_all_listed();

    // Every listed resource that is a T, owned prims and referenced listings
    // alike. The typed vector is derived from the kind's index list and
    // rebuilt only when that list changed since the last call.
    template <typename T>
    [[nodiscard]] auto get_all() const -> const std::vector<std::shared_ptr<T>>&;

    // An ImGui combo picking one listed resource of class T, also a drag and
    // drop target for a resource prim of that class.
    template <typename T>
    auto combo(App_context& context, const char* label, std::shared_ptr<T>& in_out_selected, bool empty_option) const -> bool;

    // Every listed resource of one kind, without naming its class.
    [[nodiscard]] auto get_all_of_kind(uint64_t kind_type_bit) const -> const std::vector<std::shared_ptr<erhe::Item_base>>&;

    // True when this library lists the item. Used by register-time
    // classification to distinguish an already-listed reference (e.g. a
    // prefab template resource) from an item that arrived with no
    // registration at all (R5.2b: loud warning, never adopt).
    [[nodiscard]] auto has_item(const erhe::Item_base& item) const -> bool;

    // Creates a resource and places it under its kind's scope.
    template <typename T, typename ...Args>
    auto make(Args&& ...args) -> std::shared_ptr<T>;

    // Places a resource prim under its kind's scope. Does nothing when the
    // library already lists it.
    template <typename T>
    void add(const std::shared_ptr<T>& entry);

    // Places a resource prim under its kind's scope and records where it came
    // from.
    template <typename T>
    void add(
        const std::shared_ptr<T>&                             entry,
        const Gltf_source_reference&                          gltf_source,
        const std::shared_ptr<erhe::gltf::Gltf_image_source>& image_source = {},
        const std::optional<Asset_key>&                       asset_key    = {}
    );

    // Lists a resource ANOTHER container owns, so the scene's material set
    // gives it a slot and the pickers offer it. Not a prim of this tree.
    void add_referenced   (const std::shared_ptr<erhe::Item_base>& item, const std::optional<Asset_key>& asset_key = {});
    void remove_referenced(const std::shared_ptr<erhe::Item_base>& item);
    [[nodiscard]] auto is_referenced(const erhe::Item_base& item) const -> bool;
    // Flips an owned listing to a referenced one in place (R7 make-external).
    void set_referenced(const erhe::Item_base& item);

    // Takes a resource out of the library: the prim leaves the tree, a
    // referenced listing leaves the index. False when the library does not
    // list it.
    template <typename T>
    auto remove(const std::shared_ptr<T>& entry) -> bool;

    // Per-resource library bookkeeping; null when the library holds none for
    // the item.
    [[nodiscard]] auto find_metadata(const erhe::Item_base& item) const -> const Resource_metadata*;
    void set_gltf_source (const std::shared_ptr<erhe::Item_base>& item, const Gltf_source_reference& gltf_source);
    void set_image_source(const std::shared_ptr<erhe::Item_base>& item, const std::shared_ptr<erhe::gltf::Gltf_image_source>& image_source);
    void set_asset_key   (const std::shared_ptr<erhe::Item_base>& item, const Asset_key& asset_key);

    ERHE_PROFILE_MUTEX(std::mutex, mutex);

private:
    // One kind's listed resources, with a serial that moves whenever the list
    // does, so a typed view can tell whether it is still current.
    class Kind_index
    {
    public:
        std::vector<std::shared_ptr<erhe::Item_base>> items;
        uint64_t                                      serial{0};
    };

    // One get_all<T>() result, valid while it carries its kind's serial.
    class Typed_view
    {
    public:
        std::any value;
        uint64_t serial{0};
        bool     valid {false};
    };

    [[nodiscard]] auto metadata_entry(const std::shared_ptr<erhe::Item_base>& item) -> Resource_metadata&;
    void index_insert(const std::shared_ptr<erhe::Item_base>& item);
    void index_erase (const std::shared_ptr<erhe::Item_base>& item);
    void announce_attached(const std::shared_ptr<erhe::Item_base>& item);
    void announce_detached(const std::shared_ptr<erhe::Item_base>& item);

    erhe::Item_host*                 m_owner        {nullptr};
    Asset_manager*                   m_asset_manager{nullptr};
    std::shared_ptr<erhe::Hierarchy> m_prim_root;
    std::shared_ptr<erhe::Scope>     m_own_root;

    std::unordered_map<uint64_t, std::shared_ptr<erhe::Scope>>                   m_scopes;
    mutable std::unordered_map<uint64_t, Kind_index>                             m_by_kind;
    mutable std::unordered_map<uint64_t, Typed_view>                             m_typed_views;
    std::unordered_set<const erhe::Item_base*>                                   m_listed;
    std::unordered_map<const erhe::Item_base*, std::shared_ptr<erhe::Item_base>> m_referenced;
    std::unordered_map<const erhe::Item_base*, Resource_metadata>                m_metadata;
};

// Copies every resource of a library into another library, so no resource is
// ever a member of two libraries (each library owns its resources). The scope
// structure below each kind scope is recreated. Brushes are copied via
// Brush::make_shared_payload_copy() - fresh per-library item identity, with
// the expensive immutable payload (geometry, GPU primitive, collision shapes)
// shared by reference; other kinds are cloned when clonable and skipped with a
// warning otherwise. Used to seed a new scene's library from the
// Scene_builder template palette.
void copy_content_library(const Content_library& source, Content_library& target);

// Copies one resource into the matching kind scope of another library.
// Copies never alias: the copy is a fresh resource owned by the target
// library's scene (a Brush shares its expensive payload by reference). A name
// collision in the target scope gets a sibling-unique suffix (M2). Returns
// the copy, or null when the kind is not copyable (textures and graph assets
// are shared GPU / graph resources). The caller holds the target library's
// mutex.
auto copy_library_item_to_library(const std::shared_ptr<erhe::Item_base>& item, Content_library& target_library) -> std::shared_ptr<erhe::Item_base>;

template <typename T>
auto Content_library::get_all() const -> const std::vector<std::shared_ptr<T>>&
{
    const uint64_t    key  = T::get_static_type();
    Typed_view&       view = m_typed_views[key];
    const Kind_index& kind = m_by_kind[get_kind_type_bit_of_type(key)];
    if (!view.valid || (view.serial != kind.serial)) {
        std::vector<std::shared_ptr<T>> result;
        result.reserve(kind.items.size());
        for (const std::shared_ptr<erhe::Item_base>& kind_item : kind.items) {
            std::shared_ptr<T> typed = std::dynamic_pointer_cast<T>(kind_item);
            if (typed) {
                result.push_back(std::move(typed));
            }
        }
        view.value  = std::move(result);
        view.serial = kind.serial;
        view.valid  = true;
    }
    return std::any_cast<const std::vector<std::shared_ptr<T>>&>(view.value);
}

template <typename T>
auto Content_library::combo(
    App_context&        context,
    const char*         label,
    std::shared_ptr<T>& in_out_selected,
    const bool          empty_option
) const -> bool
{
    const bool  empty_entry   = empty_option || (!in_out_selected);
    const char* preview_value = in_out_selected ? in_out_selected->get_name().c_str() : "(none)";
    bool        changed       = false;
    const bool  begin         = ImGui::BeginCombo(label, preview_value, ImGuiComboFlags_NoArrowButton | ImGuiComboFlags_HeightLarge);
    if (begin) {
        if (empty_entry) {
            const bool is_selected = !in_out_selected;
            if (ImGui::Selectable("(none)", is_selected)) {
                in_out_selected.reset();
                changed = true;
            }
            if (is_selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        for (const std::shared_ptr<T>& candidate : get_all<T>()) {
            const bool shown = candidate->is_shown_in_ui() ||
                (context.developer_mode && ((candidate->get_flag_bits() & erhe::Item_flags::show_in_developer_ui) != 0));
            if (!shown) {
                continue;
            }
            const bool is_selected = (in_out_selected == candidate);
            context.icon_set->add_icons(candidate->get_type(), 1.0f);
            if (ImGui::Selectable(candidate->get_debug_label().data(), is_selected)) {
                in_out_selected = candidate;
                changed = true;
            }
            if (is_selected) {
                ImGui::SetItemDefaultFocus();
            }
            if (changed) {
                break;
            }
        }
        ImGui::EndCombo();
    } else if (ImGui::BeginDragDropTarget()) {
        const ImGuiPayload* const payload = ImGui::AcceptDragDropPayload(T::static_type_name.data());
        if ((payload != nullptr) && (payload->Data != nullptr) && (payload->DataSize == sizeof(erhe::Item_base*))) {
            erhe::Item_base* const raw = *static_cast<erhe::Item_base**>(payload->Data);
            std::shared_ptr<T> dropped = (raw != nullptr) ? std::dynamic_pointer_cast<T>(raw->shared_from_this()) : std::shared_ptr<T>{};
            if (dropped && has_item(*dropped)) {
                in_out_selected = std::move(dropped);
                changed = true;
            }
        }
        ImGui::EndDragDropTarget();
    }
    return changed;
}

template <typename T, typename ...Args>
auto Content_library::make(Args&& ...args) -> std::shared_ptr<T>
{
    std::shared_ptr<T> new_item = std::make_shared<T>(std::forward<Args>(args)...);
    add(new_item);
    return new_item;
}

template <typename T>
void Content_library::add(const std::shared_ptr<T>& entry)
{
    ERHE_VERIFY(entry);
    if (has_item(*entry)) {
        return;
    }
    const std::shared_ptr<erhe::Scope> scope = get_scope(get_kind_type_bit(*entry));
    if (!scope) {
        return;
    }
    entry->set_parent(scope);
}

template <typename T>
void Content_library::add(
    const std::shared_ptr<T>&                             entry,
    const Gltf_source_reference&                          gltf_source,
    const std::shared_ptr<erhe::gltf::Gltf_image_source>& image_source,
    const std::optional<Asset_key>&                       asset_key
)
{
    ERHE_VERIFY(entry);
    add(entry);
    set_gltf_source(entry, gltf_source);
    if (image_source) {
        set_image_source(entry, image_source);
    }
    if (asset_key.has_value()) {
        set_asset_key(entry, asset_key.value());
    }
}

template <typename T>
auto Content_library::remove(const std::shared_ptr<T>& entry) -> bool
{
    ERHE_VERIFY(entry);
    if (is_referenced(*entry)) {
        remove_referenced(entry);
        return true;
    }
    if (!has_item(*entry)) {
        return false;
    }
    entry->erhe::Hierarchy::remove();
    return true;
}

}
