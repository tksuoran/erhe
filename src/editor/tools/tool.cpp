#include "tools/tool.hpp"
#include "editor_log.hpp"
#include "app_message.hpp"
#include "scene/content_library.hpp"
#include "scene/scene_root.hpp"
#include "scene/scene_view.hpp"
#include "tools/selection_tool.hpp"
#include "erhe_bit/bit_helpers.hpp"
#include "erhe_primitive/material.hpp"

namespace editor {

Tool::Tool(App_context& app_context)
    : m_context{app_context}
{
}

void Tool::on_message(App_message& message)
{
    using namespace erhe::bit;
    if (test_all_rhs_bits_set(message.update_flags, Message_flag_bit::c_flag_bit_hover_scene_view)) {
        set_hover_scene_view(message.scene_view);
    }
}

auto Tool::get_priority() const -> int
{
    return m_base_priority + m_priority_boost;
}

auto Tool::get_base_priority() const -> int
{
    return m_base_priority;
}

auto Tool::get_priority_boost() const -> int
{
    return m_priority_boost;
}

void Tool::set_hover_scene_view(Scene_view* scene_view)
{
    m_hover_scene_view = scene_view;
    if (scene_view != nullptr) {
        m_last_hover_scene_view = scene_view;
    }
}

auto Tool::get_hover_scene_view() const -> Scene_view*
{
    return m_hover_scene_view;
}

auto Tool::get_last_hover_scene_view() const -> Scene_view*
{
    return m_last_hover_scene_view;
}

auto Tool::get_flags() const -> uint64_t
{
    return m_flags;
}

auto Tool::get_icon() const -> std::optional<glm::vec2>
{
    return m_icon;
}

void Tool::set_priority_boost(const int priority_boost)
{
    log_tools->trace("{} priority_boost set to {}", get_description(), priority_boost);

    const int old_priority = get_priority();
    m_priority_boost = priority_boost;
    const int new_priority = get_priority();
    handle_priority_update(old_priority, new_priority);
};

void Tool::set_base_priority(const int base_priority)
{
    m_base_priority = base_priority;
}

void Tool::set_flags(const uint64_t flags)
{
    m_flags = flags;
}

void Tool::set_icon(const glm::vec2 icon)
{
    m_icon = icon;
}
    
auto Tool::get_scene_root() const -> std::shared_ptr<Scene_root>
{
    auto* scene_view = get_hover_scene_view();
    if (scene_view == nullptr) {
        return {};
    }
    return scene_view->get_scene_root();
}

auto Tool::get_content_library() const -> std::shared_ptr<Content_library>
{
    const auto& scene_root = get_scene_root();
    if (!scene_root) {
        return {};
    }
    return scene_root->content_library();
}

auto Tool::get_material() const -> std::shared_ptr<erhe::primitive::Material>
{
    const std::shared_ptr<Content_library>& content_library = get_content_library();
    if (!content_library) {
        return {};
    }
    std::shared_ptr<erhe::primitive::Material> material = m_context.selection->get_last_selected<erhe::primitive::Material>();
    if (!material) {
        content_library->materials->for_each<Content_library_node>(
            [&material](const Content_library_node& node) {
                auto entry = std::dynamic_pointer_cast<erhe::primitive::Material>(node.item);
                if (entry) {
                    material = entry;
                    return false;
                }
                return true;
            }
        );
    }
    return material;
}

// If Node is currently selected, returns it.
// If Node_attachment is currently selected and it has owner node, returns the owner node.
// If a Node was selected, and it still exists, return it
// If Node_attachemnt was selected, and it still exists, and it has owner node, returns the owner node.
auto Tool::get_node() const -> std::shared_ptr<erhe::scene::Node>
{
    Selection* selection = m_context.selection;
    {
        std::shared_ptr<erhe::scene::Node> node = selection->get<erhe::scene::Node>();
        if (node) {
            return node;
        }
    }

    {
        std::shared_ptr<erhe::scene::Node_attachment> attachment = selection->get<erhe::scene::Node_attachment>();
        if (attachment) {
            erhe::scene::Node* node = attachment->get_node();
            if (node != nullptr) {
                std::shared_ptr<erhe::Item_base> item = node->shared_from_this();
                std::shared_ptr<erhe::scene::Node> shared_node = std::dynamic_pointer_cast<erhe::scene::Node>(item);
                if (shared_node) {
                    return shared_node;
                }
            }
        }
    }

    {
        std::shared_ptr<erhe::scene::Node> node = selection->get_last_selected<erhe::scene::Node>();
        if (node && (node->get_item_host() != nullptr)) {
            return node;
        }
    }

    {
        std::shared_ptr<erhe::scene::Node_attachment> attachment = selection->get_last_selected<erhe::scene::Node_attachment>();
        if (attachment && (attachment->get_item_host() != nullptr)) {
            erhe::scene::Node* node = attachment->get_node();
            if (node != nullptr) {
                std::shared_ptr<erhe::Item_base> item = node->shared_from_this();
                std::shared_ptr<erhe::scene::Node> shared_node = std::dynamic_pointer_cast<erhe::scene::Node>(item);
                if (shared_node && (shared_node->get_item_host() != nullptr)) {
                    return shared_node;
                }
            }
        }
    }

    return {};
}

}
