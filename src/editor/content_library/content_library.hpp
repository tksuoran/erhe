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

namespace erhe::graphics {
    class Texture_reference;
}

namespace erhe::scene {
    class Animation;
    class Camera;
    class Light;
    class Mesh;
    class Skin;
};

namespace editor {

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
    Content_library_node(std::string_view folder_name, uint64_t type, std::string_view type_name);

    // Implements Item_base
    static constexpr std::string_view static_type_name{"Content_library_node"};
    [[nodiscard]] static constexpr auto get_static_type() -> uint64_t { return erhe::Item_type::content_library_node; }

    // Overrides Hierarchy
    void handle_add_child   (const std::shared_ptr<erhe::Hierarchy>& child_node, std::size_t position) override;
    void handle_remove_child(erhe::Hierarchy* child_node) override;

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
    // scene). Never claims or releases the item's Item_host.
    template <typename T>
    void add_reference(const std::shared_ptr<T>& entry);

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
    std::shared_ptr<erhe::Item_base>          item;
    // A reference entry lists an item owned elsewhere (e.g. a prefab
    // template's texture / material, shared by every instancing scene
    // because GPU textures cannot be duplicated per scene); it never claims
    // or releases the item's Item_host. An owning entry (the default) hosts
    // its item: item->get_item_host() == the library owner.
    bool                                      is_reference{false};
    // Asset identity of the wrapped item when its defining container is
    // known (asset-manager plan, R5 sub-plan resolution 2): today a
    // file-scope key stamped on prefab-template material reference
    // entries; the R5.6 single-loader flip generalizes this to every
    // asset-typed entry (definitions record this scene's container,
    // references the defining one). Inert metadata until then - nothing
    // resolves through it.
    std::optional<Asset_key>                  asset_key;
    std::optional<Gltf_source_reference>      gltf_source;
    // Texture entries only: the retained compressed source image stream, so
    // glTF export can re-embed the image byte-exact
    // (doc/gltf-scene-roundtrip-plan.md phase 0).
    std::shared_ptr<erhe::gltf::Gltf_image_source> image_source;

private:
    template <typename T>
    void invalidate_cache()
    {
        m_cache.erase(T::get_static_type());
    }

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

    // One combo listing both plain textures and Graph Texture assets - the two
    // sources a Material_texture_sampler::texture_reference can hold. Both
    // erhe::graphics::Texture and Graph_texture derive from Texture_reference,
    // so the selection is stored as the common interface.
    auto texture_reference_combo(
        App_context&                                        context,
        const char*                                         label,
        std::shared_ptr<erhe::graphics::Texture_reference>& in_out_reference,
        bool                                                empty_option
    ) const -> bool;

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

    ERHE_PROFILE_MUTEX(std::mutex, mutex);

private:
    erhe::Item_host* m_owner{nullptr};
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
    const auto i = std::find_if(
        m_children.begin(),
        m_children.end(),
        [&entry](const std::shared_ptr<Hierarchy>& hierarchy) {
            std::shared_ptr<Content_library_node> node = std::dynamic_pointer_cast<Content_library_node>(hierarchy);
            if (!node) {
                return false;
            }
            return std::dynamic_pointer_cast<T>(node->item) == entry;
        }
    );
    if (i != m_children.end()) {
        return;
    }
    auto node = std::make_shared<Content_library_node>(entry);
    node->set_parent(this);
    invalidate_cache<T>();
}

template <typename T>
void Content_library_node::add_reference(const std::shared_ptr<T>& entry)
{
    ERHE_VERIFY(entry);
    const auto i = std::find_if(
        m_children.begin(),
        m_children.end(),
        [&entry](const std::shared_ptr<Hierarchy>& hierarchy) {
            std::shared_ptr<Content_library_node> node = std::dynamic_pointer_cast<Content_library_node>(hierarchy);
            if (!node) {
                return false;
            }
            return std::dynamic_pointer_cast<T>(node->item) == entry;
        }
    );
    if (i != m_children.end()) {
        return;
    }
    auto node = std::make_shared<Content_library_node>(entry);
    // Must be set before set_parent(): handle_add_child() decides whether to
    // claim the item's host based on it.
    node->is_reference = true;
    node->set_parent(this);
    invalidate_cache<T>();
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
    const auto i = std::find_if(
        m_children.begin(),
        m_children.end(),
        [&entry](const std::shared_ptr<Hierarchy>& hierarchy) {
            std::shared_ptr<Content_library_node> node = std::dynamic_pointer_cast<Content_library_node>(hierarchy);
            if (!node) {
                return false;
            }
            return std::dynamic_pointer_cast<T>(node->item) == entry;
        }
    );
    if (i != m_children.end()) {
        std::shared_ptr<Content_library_node> existing = std::dynamic_pointer_cast<Content_library_node>(*i);
        if (existing) {
            existing->gltf_source = gltf_source;
            if (image_source) {
                existing->image_source = image_source;
            }
            if (asset_key.has_value()) {
                existing->asset_key = asset_key;
            }
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
    invalidate_cache<T>();
}

template <typename T>
auto Content_library_node::remove(const std::shared_ptr<T>& entry) -> bool
{
    ERHE_VERIFY(entry);
    const auto i = std::find_if(
        m_children.begin(),
        m_children.end(),
        [&entry](const std::shared_ptr<Hierarchy>& hierarchy) {
            std::shared_ptr<Content_library_node> node = std::dynamic_pointer_cast<Content_library_node>(hierarchy);
            if (!node) {
                return false;
            }
            return std::dynamic_pointer_cast<T>(node->item) == entry;
        }
    );
    if (i == m_children.end()) {
        return false;
    }
    std::shared_ptr<Hierarchy> hierarchy = *i;
    hierarchy->remove();
    invalidate_cache<T>();
    return true;
}

}
