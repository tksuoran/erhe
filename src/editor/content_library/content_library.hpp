#pragma once

#include "app_context.hpp"
#include "assets/asset_key.hpp"
#include "editor_log.hpp"
#include "graphics/icon_set.hpp"
#include "scene/generated/gltf_source_reference.hpp"

#include "erhe_item/hierarchy.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_verify/verify.hpp"

#include <imgui/imgui.h>

#include <algorithm>
#include <any>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace erhe::gltf {
    class Gltf_image_source;
}

namespace erhe::scene {
    class Animation;
    class Camera;
    class Light;
    class Mesh;
    class Skin;
};

namespace editor {

class Asset_manager;
class Asset_reference;
class Brush;
class App_context;
class Content_library;

class Content_library_node : public erhe::Item<erhe::Item_base, erhe::Hierarchy, Content_library_node>
{
public:
    explicit Content_library_node(const Content_library_node&);
    Content_library_node& operator=(const Content_library_node&);
    ~Content_library_node() noexcept override;

    explicit Content_library_node(const std::shared_ptr<erhe::Item_base>& item);
    // A folder: category_owner_type is the owner type of the category's
    // item class (doc/content-library-folders.md D8), the secondary owner
    // type of the folder and of every folder made below it.
    Content_library_node(std::string_view folder_name, uint64_t type, std::string_view type_name, std::optional<erhe::property::Owner_type> category_owner_type = {});

    // Implements Item_base
    static constexpr std::string_view static_type_name{"Content_library_node"};
    [[nodiscard]] static constexpr auto get_static_type() -> uint64_t { return erhe::Item_type::content_library_node; }

    // Overrides Hierarchy
    void handle_add_child   (const std::shared_ptr<erhe::Hierarchy>& child_node, std::size_t position) override;
    void handle_remove_child(erhe::Hierarchy* child_node) override;
    // Sibling-unique names (doc/usd-compatibility-plan.md M2): an owning
    // entry's wrapped item is renamed with the entry node, because the item's
    // name is the one the user sees and the entry node's name is the one the
    // path is built from. A reference entry lists an item owned by another
    // scene, which this library never renames.
    void handle_sibling_unique_rename(const std::string& unique_name) override;

    // Overrides Dependency_object (doc/content-library-folders.md D1): the
    // hierarchy children, then the owning entry's wrapped item, so a folder's
    // inheritable values reach the items and a move notifies them.
    void for_each_inheritance_child(const std::function<void(erhe::property::Dependency_object&)>& callback) override;
    // A folder holds its category's item properties (D8); an entry node
    // holds none.
    [[nodiscard]] auto get_secondary_property_owner_type() const -> std::optional<erhe::property::Owner_type> override;
    // A node has no Item_host of its own: an item name in a reference or
    // expression resolves through the library owner (its scene), so a
    // folder's texture reference loads by name.
    [[nodiscard]] auto resolve_expression_object(std::string_view path) const -> erhe::property::Dependency_object* override;

    // Walks up the node hierarchy to the root, which carries the back-pointer
    // to the owning Content_library. Null for nodes not (yet) in a library.
    [[nodiscard]] auto get_library() const -> Content_library*;

    auto make_folder(std::string_view folder_name) -> std::shared_ptr<Content_library_node>;

    template <typename T, typename ...Args>
    auto make(Args&& ...args) -> std::shared_ptr<T>;

    template <typename T>
    auto combo(App_context& context, const char* label, std::shared_ptr<T>& in_out_selected_entry, bool empty_option) const -> bool;

    template <typename T>
    void add(const std::shared_ptr<T>& entry);

    // Adds a REFERENCE entry: a listing of an item owned elsewhere (e.g. the
    // material preview scene listing the inspected material of another
    // scene). Never claims or releases the item's Item_host. The optional
    // asset_key records the defining container when the caller knows it
    // (R5.6: register_mesh stamps file-scope keys on container-owned
    // materials); an already-listed entry only gains a key it lacks.
    template <typename T>
    void add_reference(const std::shared_ptr<T>& entry, const std::optional<Asset_key>& asset_key = {});

    template <typename T>
    void add(
        const std::shared_ptr<T>&                               entry,
        const Gltf_source_reference&                            gltf_source,
        const std::shared_ptr<erhe::gltf::Gltf_image_source>&   image_source = {},
        bool                                                    is_reference = false,
        const std::optional<Asset_key>&                         asset_key    = {}
    );

    template <typename T>
    auto remove(const std::shared_ptr<T>& entry) -> bool;

    // True when an entry (owning or reference) anywhere in this subtree wraps
    // the given item. Used by register-time classification to distinguish an
    // already-listed reference (e.g. a prefab template resource) from an item
    // that arrived with no registration at all (R5.2b: loud warning, never
    // adopt).
    [[nodiscard]] auto has_item(const erhe::Item_base& item) const -> bool;

    // The entry node (owning or reference) anywhere in this subtree that
    // wraps the given item, or null. An item is listed once per library
    // (doc/content-library-folders.md R1: folders are subtrees of their
    // category), so add() / remove() on a category folder find an entry
    // that sits in one of its folders.
    [[nodiscard]] auto find_entry(const erhe::Item_base& item) const -> std::shared_ptr<Content_library_node>;

    // template <typename T>
    // [[nodiscard]] auto get_all() -> std::vector<std::shared_ptr<T>> {
    //     std::vector<std::shared_ptr<T>> result;
    //     for_each<Content_library_node>(
    //         [&result](const Content_library_node& node) {
    //             auto entry = std::dynamic_pointer_cast<T>(node.item);
    //             if (entry) {
    //                 result.push_back(entry);
    //             }
    //             return true;
    //         }
    //     );
    //     return result;
    // }
    template <typename T>
    [[nodiscard]] auto get_all() -> const std::vector<std::shared_ptr<T>>&
    {
        const uint64_t key{T::get_static_type()};
        auto it = m_cache.find(key);
        if (it != m_cache.end()) {
            return
                std::any_cast<
                    const std::vector<std::shared_ptr<T>>&
                >(it->second);
        }

        // Build and store cache
        std::vector<std::shared_ptr<T>> result;
        for_each<Content_library_node>(
            [&result](const Content_library_node& node) {
                auto entry = std::dynamic_pointer_cast<T>(node.item);
                if (entry) {
                    result.push_back(entry);
                }
                return true;
            }
        );
        auto [inserted_it, _] = m_cache.emplace(key, std::move(result));
        return std::any_cast<const std::vector<std::shared_ptr<T>>&>(inserted_it->second);
    }
    uint64_t                                  type_code{};
    std::string                               type_name{};
    std::optional<erhe::property::Owner_type> category_owner_type{};
    std::shared_ptr<erhe::Item_base>          item;
    // A reference entry lists an item owned elsewhere (e.g. a prefab
    // template's texture / material, shared by every instancing scene
    // because GPU textures cannot be duplicated per scene); it never claims
    // or releases the item's Item_host. An owning entry (the default) hosts
    // its item: item->get_item_host() == the library owner.
    bool                                      is_reference{false};
    // Asset identity of the wrapped item when its defining container is
    // known (asset-manager plan, R5 sub-plan resolution 2): file-scope keys
    // stamped on prefab-template and container-owned reference entries.
    // Inert metadata until the R6 wire format - nothing resolves through
    // it. (Scene-asset references stay key-less until the R5.7 key flip
    // makes their keys durable.)
    std::optional<Asset_key>                  asset_key;
    // Declared usership of an asset-typed entry (R5.6): the manager's
    // library attach hook fills this for manager-owned asset types when
    // the owning scene is registered, so the entry is a named user in
    // unload refusals ("scene '<name>' library <type> '<item>'").
    // Deliberately not copied with the node.
    std::unique_ptr<Asset_reference>          asset_usership;
    std::optional<Gltf_source_reference>      gltf_source;
    // Texture entries only: the retained compressed source image stream, so
    // glTF export can re-embed the image byte-exact
    // (doc/gltf-scene-roundtrip-plan.md phase 0).
    std::shared_ptr<erhe::gltf::Gltf_image_source> image_source;

private:
    // Clears the get_all() caches of this node and every ancestor: a cache
    // covers the whole subtree, so a change anywhere below a node stales it.
    void invalidate_caches_up_to_root();

    friend class Content_library;

    // Set only on a library's root node, by the Content_library constructor.
    Content_library*                       m_library{nullptr};
    std::unordered_map<uint64_t, std::any> m_cache;
};

class Content_library
{
public:
    Content_library();
    ~Content_library() noexcept;

    // The owner is the erhe::Item_host - in practice the owning Scene_root -
    // that every wrapped library item reports from get_item_host(). Set once
    // by the owning Scene_root's constructor (retroactively hosting items
    // added before the Scene_root existed). A library whose owner is never
    // set (e.g. Scene_builder's template palette) hosts nothing.
    void set_owner(erhe::Item_host* owner);
    [[nodiscard]] auto get_owner() const -> erhe::Item_host*;

    // R5.5 shadow registration hook: set by Asset_manager::on_scene_registered
    // (cleared at unregistration and at manager teardown) so the claim/
    // release walks mirror owning asset-typed entries into the owning
    // scene's container record. Null for libraries whose scene is not
    // registered (previews, the tool scene, Scene_builder's template
    // palette) - those walks touch only the items' hosts, as before.
    void set_asset_manager(Asset_manager* asset_manager);
    [[nodiscard]] auto get_asset_manager() const -> Asset_manager*;

    std::shared_ptr<Content_library_node> root;
    std::shared_ptr<Content_library_node> brushes;
    std::shared_ptr<Content_library_node> animations;
    std::shared_ptr<Content_library_node> skins;
    std::shared_ptr<Content_library_node> materials;
    std::shared_ptr<Content_library_node> textures;
    std::shared_ptr<Content_library_node> graph_textures;
    std::shared_ptr<Content_library_node> graph_meshes;
    std::shared_ptr<Content_library_node> physics_materials;
    std::shared_ptr<Content_library_node> collision_filters;
    std::shared_ptr<Content_library_node> physics_joints;
    std::shared_ptr<Content_library_node> styles; // doc/style-library.md D2

    ERHE_PROFILE_MUTEX(std::mutex, mutex);

private:
    erhe::Item_host* m_owner        {nullptr};
    Asset_manager*   m_asset_manager{nullptr};
};

// Recursively copies a content-library subtree into another library so that
// no item is ever a member of two libraries (each library owns its items).
// Folders are recreated. Brush leaves are copied via
// Brush::make_shared_payload_copy() - fresh per-library item identity, with
// the expensive immutable payload (geometry, GPU primitive, collision
// shapes) shared by reference. Other leaf types are cloned when clonable and
// skipped with a warning otherwise. Used to seed a new scene's library from
// the Scene_builder template palette.
void copy_content_library_folder(const Content_library_node& src_folder, Content_library_node& dst_folder);

// Copies a single library item into the matching category folder of another
// library. Copies never alias: the copy is a fresh item owned by the target
// library's scene (a Brush shares its expensive payload by reference). A
// name collision in the target folder gets a " (N)" suffix. Returns the
// copy, or null when the item type is not copyable (textures and graph
// assets are shared GPU / graph resources) or has no category folder. The
// caller is responsible for holding the target library's mutex.
auto copy_library_item_to_library(const std::shared_ptr<erhe::Item_base>& item, Content_library& target_library) -> std::shared_ptr<erhe::Item_base>;

template <typename T, typename ...Args>
auto Content_library_node::make(Args&& ...args) -> std::shared_ptr<T>
{
    auto new_item = std::make_shared<T>(std::forward<Args>(args)...);
    auto new_node = std::make_shared<Content_library_node>(new_item);
    new_node->set_parent(this);
    return new_item;
}

template <typename T>
auto Content_library_node::combo(
    App_context&        context,
    const char*         label,
    std::shared_ptr<T>& in_out_selected_entry,
    const bool          empty_option
) const -> bool
{
    const bool empty_entry = empty_option || (!in_out_selected_entry);
    const char* preview_value = in_out_selected_entry ? in_out_selected_entry->get_name().c_str() : "(none)";
    bool selection_changed = false;
    const bool begin = ImGui::BeginCombo(label, preview_value, ImGuiComboFlags_NoArrowButton | ImGuiComboFlags_HeightLarge);
    if (begin) {
        if (empty_entry) {
            bool is_selected = !in_out_selected_entry;
            if (ImGui::Selectable("(none)", is_selected)) {
                in_out_selected_entry.reset();
                selection_changed = true;
            }
            if (is_selected) {
                ImGui::SetItemDefaultFocus();
            }
        }

        // TODO Consider keeping flat vector of entries
        for_each_const<Content_library_node>(
            [this, &context, &selection_changed, &in_out_selected_entry](const Content_library_node& node) -> bool {
                auto node_item_shared = std::dynamic_pointer_cast<T>(node.item);
                if (!node_item_shared) {
                    return true; // in for_each() lambda - continue to children
                }
                const bool shown = node.item->is_shown_in_ui() ||
                    (context.developer_mode && ((node.item->get_flag_bits() & erhe::Item_flags::show_in_developer_ui) != 0));
                if (!shown) {
                    return true; // in for_each() lambda - continue to children
                }
                bool is_selected = (in_out_selected_entry == node.item);
                context.icon_set->add_icons(node.item->get_type(), 1.0f);
                if (ImGui::Selectable(node.item->get_debug_label().data(), is_selected)) {
                    in_out_selected_entry = node_item_shared;
                    selection_changed = true;
                }
                if (is_selected) {
                    ImGui::SetItemDefaultFocus();
                }
                return !selection_changed; // in for_each() lambda - continue to children if not selection_changed
            }
        );

        ImGui::EndCombo();
    } else if (ImGui::BeginDragDropTarget()) {
        const ImGuiPayload* drag_node_payload = ImGui::AcceptDragDropPayload(Content_library_node::static_type_name.data());
        const ImGuiPayload* drag_item_payload = ImGui::AcceptDragDropPayload(T::static_type_name.data());
        if ((drag_node_payload == nullptr) && (drag_item_payload == nullptr)) {
            ImGui::EndDragDropTarget();
            return false;
        }
        const erhe::Item_base*      drag_node_ = (drag_node_payload != nullptr) ? *(static_cast<erhe::Item_base**>(drag_node_payload->Data)) : nullptr;
        const Content_library_node* drag_node  = dynamic_cast<const Content_library_node*>(drag_node_);
        const erhe::Item_base*      drag_item  = (drag_item_payload != nullptr)
            ? *(static_cast<erhe::Item_base**>(drag_item_payload->Data))
            : drag_node->item.get();
        if (drag_item != nullptr) {
            for_each_const<Content_library_node>(
                [&selection_changed, &in_out_selected_entry, drag_item](const Content_library_node& node) -> bool {
                    auto node_item_shared = std::dynamic_pointer_cast<T>(node.item);
                    if (node_item_shared && (node_item_shared.get() == drag_item)) {
                        in_out_selected_entry = node_item_shared;
                        selection_changed = true;
                        return false; // in for_each() lambda - selection changed, do not continue to children
                    }
                    return true; // in for_each() lambda - selection not changed, continue to childnre
                }
            );
        } else {
            log_tree_frame->trace("Dnd payload is not {}", T::static_type_name.data());
        }
        ImGui::EndDragDropTarget();
        return selection_changed;
    }

    return selection_changed;
}

template <typename T>
void Content_library_node::add(const std::shared_ptr<T>& entry)
{
    ERHE_VERIFY(entry);
    const std::shared_ptr<Content_library_node> existing = find_entry(*entry);
    if (existing) {
        return;
    }
    auto node = std::make_shared<Content_library_node>(entry);
    node->set_parent(this);
}

template <typename T>
void Content_library_node::add_reference(const std::shared_ptr<T>& entry, const std::optional<Asset_key>& asset_key)
{
    ERHE_VERIFY(entry);
    const std::shared_ptr<Content_library_node> existing = find_entry(*entry);
    if (existing) {
        if (asset_key.has_value() && !existing->asset_key.has_value()) {
            existing->asset_key = asset_key;
        }
        return;
    }
    auto node = std::make_shared<Content_library_node>(entry);
    // Must be set before set_parent(): handle_add_child() decides whether to
    // claim the item's host based on it.
    node->is_reference = true;
    node->asset_key    = asset_key;
    node->set_parent(this);
}

template <typename T>
void Content_library_node::add(
    const std::shared_ptr<T>&                             entry,
    const Gltf_source_reference&                          gltf_source,
    const std::shared_ptr<erhe::gltf::Gltf_image_source>& image_source,
    const bool                                            is_reference,
    const std::optional<Asset_key>&                       asset_key
)
{
    ERHE_VERIFY(entry);
    const std::shared_ptr<Content_library_node> existing = find_entry(*entry);
    if (existing) {
        existing->gltf_source = gltf_source;
        if (image_source) {
            existing->image_source = image_source;
        }
        if (asset_key.has_value()) {
            existing->asset_key = asset_key;
        }
        return;
    }
    auto node = std::make_shared<Content_library_node>(entry);
    // Must be set before set_parent(): handle_add_child() decides whether to
    // claim the item's host based on it.
    node->is_reference = is_reference;
    node->gltf_source  = gltf_source;
    node->image_source = image_source;
    node->asset_key    = asset_key;
    node->set_parent(this);
}

template <typename T>
auto Content_library_node::remove(const std::shared_ptr<T>& entry) -> bool
{
    ERHE_VERIFY(entry);
    const std::shared_ptr<Content_library_node> existing = find_entry(*entry);
    if (!existing) {
        return false;
    }
    existing->erhe::Hierarchy::remove();
    return true;
}

}
