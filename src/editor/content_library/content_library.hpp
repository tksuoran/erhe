#pragma once

#include "app_context.hpp"
#include "editor_log.hpp"
#include "graphics/icon_set.hpp"

#include "erhe_item/hierarchy.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_verify/verify.hpp"

#include <imgui/imgui.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

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
    [[nodiscard]] static auto get_static_type() -> uint64_t;
    auto get_type     () const -> uint64_t         override;
    auto get_type_name() const -> std::string_view override;

    auto make_folder(std::string_view folder_name) -> std::shared_ptr<Content_library_node>;

    template <typename T, typename ...Args>
    auto make(Args&& ...args) -> std::shared_ptr<T>;

    template <typename T>
    auto combo(App_context& context, const char* label, std::shared_ptr<T>& in_out_selected_entry, bool empty_option) const -> bool;

    template <typename T>
    void add(const std::shared_ptr<T>& entry);

    template <typename T>
    auto remove(const std::shared_ptr<T>& entry) -> bool;

    template <typename T>
    [[nodiscard]] auto get_all() -> std::vector<std::shared_ptr<T>> {
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
        return result;
    }

    uint64_t                         type_code{};
    std::string                      type_name{};
    std::shared_ptr<erhe::Item_base> item;
};

class Content_library
{
public:
    Content_library();

    std::shared_ptr<Content_library_node> root;
    std::shared_ptr<Content_library_node> brushes;
    std::shared_ptr<Content_library_node> animations;
    std::shared_ptr<Content_library_node> skins;
    std::shared_ptr<Content_library_node> materials;
    std::shared_ptr<Content_library_node> textures;

    ERHE_PROFILE_MUTEX(std::mutex, mutex);
};

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
    const char* preview_value = in_out_selected_entry ? in_out_selected_entry->get_label().c_str() : "(none)";
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

        // TODO Consideer keeping flat vector of entries
        for_each_const<Content_library_node>(
            [this, &context, &selection_changed, &in_out_selected_entry](const Content_library_node& node) -> bool {
                auto node_item_shared = std::dynamic_pointer_cast<T>(node.item);
                if (!node_item_shared) {
                    return true; // in for_each() lambda - continue to children
                }
                if (!node.item->is_shown_in_ui()) {
                    return true; // in for_each() lambda - continue to children
                }
                bool is_selected = (in_out_selected_entry == node.item);
                context.icon_set->add_icons(node.item->get_type(), 1.0f);
                if (ImGui::Selectable(node.item->get_label().c_str(), is_selected)) {
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
    i->remove();
    return true;
}

}
