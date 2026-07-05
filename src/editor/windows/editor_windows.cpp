#include "windows/editor_windows.hpp"

#include "app_context.hpp"
#include "app_scenes.hpp"
#include "content_library/content_library.hpp"
#include "geometry_graph/geometry_graph_window.hpp"
#include "geometry_graph/graph_mesh.hpp"
#include "scene/scene_root.hpp"
#include "scene/viewport_scene_views.hpp"
#include "texture_graph/graph_texture.hpp"
#include "texture_graph/texture_graph_window.hpp"
#include "windows/properties.hpp"
#include "windows/window_placement.hpp"

#include "erhe_imgui/imgui_windows.hpp"
#include "erhe_item/item.hpp"
#include "erhe_scene/scene.hpp"

#include <fmt/format.h>

#include <algorithm>

namespace editor {

namespace {

// Drop (and destroy) the extra instances whose window was closed by the user.
// erase() releases the last shared_ptr, running ~Imgui_window ->
// unregister_imgui_window; the caller guarantees this runs outside ImGui
// iteration.
template <typename T>
void prune_closed(std::vector<std::shared_ptr<T>>& windows)
{
    windows.erase(
        std::remove_if(
            windows.begin(),
            windows.end(),
            [](const std::shared_ptr<T>& window) {
                return !window->is_window_visible();
            }
        ),
        windows.end()
    );
}

} // anonymous namespace

Editor_windows::Editor_windows(
    erhe::imgui::Imgui_renderer& imgui_renderer,
    erhe::imgui::Imgui_windows&  imgui_windows,
    App_context&                 app_context
)
    : m_imgui_renderer{imgui_renderer}
    , m_imgui_windows {imgui_windows}
    , m_app_context   {app_context}
{
}

void Editor_windows::open_properties_window(const std::shared_ptr<erhe::Item_base>& target)
{
    // Defer construction to flush_queue() (outside ImGui iteration): open_*
    // is called from context menus / double-click (inside iteration) and
    // from off-thread MCP.
    m_imgui_windows.queue(
        [this, target]() {
            const std::string title = fmt::format("Properties [{}]", ++m_properties_counter);
            std::shared_ptr<Properties> window = std::make_shared<Properties>(
                m_imgui_renderer, m_imgui_windows, m_app_context, title, std::string_view{}
            );
            window->set_target(target);
            window->show_window();
            apply_properties_window_placement(m_imgui_windows, *window);
            m_properties_windows.push_back(window);
        }
    );
}

void Editor_windows::open_geometry_graph_window(const std::shared_ptr<Graph_mesh>& target)
{
    m_imgui_windows.queue(
        [this, target]() {
            const std::string title = fmt::format("Geometry Graph [{}]", ++m_geometry_graph_counter);
            std::shared_ptr<Geometry_graph_window> window = std::make_shared<Geometry_graph_window>(
                m_imgui_renderer, m_imgui_windows, m_app_context, title, std::string_view{}
            );
            window->set_target(target);
            window->show_window();
            apply_editor_window_placement(m_imgui_windows, *window);
            m_geometry_graph_windows.push_back(window);
        }
    );
}

void Editor_windows::open_texture_graph_window(const std::shared_ptr<Graph_texture>& target)
{
    m_imgui_windows.queue(
        [this, target]() {
            const std::string title = fmt::format("Texture Graph [{}]", ++m_texture_graph_counter);
            std::shared_ptr<Texture_graph_window> window = std::make_shared<Texture_graph_window>(
                m_imgui_renderer, m_imgui_windows, m_app_context, title, std::string_view{}
            );
            window->set_target(target);
            window->show_window();
            apply_editor_window_placement(m_imgui_windows, *window);
            m_texture_graph_windows.push_back(window);
        }
    );
}

auto Editor_windows::item_has_editor(const std::shared_ptr<erhe::Item_base>& item) -> bool
{
    if (!item) {
        return false;
    }
    // Scenes are the Hierarchy header item (not wrapped in a Content_library_node).
    if (std::dynamic_pointer_cast<erhe::scene::Scene>(item)) {
        return true;
    }
    // Content-library assets are selected wrapped; unwrap to the inner item.
    std::shared_ptr<erhe::Item_base> inner = item;
    const std::shared_ptr<Content_library_node> content_node = std::dynamic_pointer_cast<Content_library_node>(item);
    if (content_node && content_node->item) {
        inner = content_node->item;
    }
    return static_cast<bool>(std::dynamic_pointer_cast<Graph_mesh>(inner))
        || static_cast<bool>(std::dynamic_pointer_cast<Graph_texture>(inner));
}

void Editor_windows::open_or_reuse_geometry_graph_window(const std::shared_ptr<Graph_mesh>& target)
{
    Geometry_graph_window* primary = m_app_context.geometry_graph_window;
    if ((primary != nullptr) && !primary->get_target()) {
        primary->set_target(target);
        primary->show_window();
    } else {
        open_geometry_graph_window(target);
    }
}

void Editor_windows::open_or_reuse_texture_graph_window(const std::shared_ptr<Graph_texture>& target)
{
    Texture_graph_window* primary = m_app_context.texture_graph_window;
    if ((primary != nullptr) && !primary->get_target()) {
        primary->set_target(target);
        primary->show_window();
    } else {
        open_texture_graph_window(target);
    }
}

void Editor_windows::open_scene_viewport(const std::shared_ptr<Scene_root>& scene_root)
{
    // Viewport creation constructs an Imgui_window + rendergraph nodes; defer
    // it out of ImGui iteration (open_editor_for_item runs from a context-menu
    // deferred op / double-click, both inside iteration).
    m_imgui_windows.queue(
        [this, scene_root]() {
            if (m_app_context.scene_views != nullptr) {
                m_app_context.scene_views->open_new_viewport_scene_view_node(scene_root);
            }
        }
    );
}

void Editor_windows::open_editor_for_item(const std::shared_ptr<erhe::Item_base>& item)
{
    if (!item) {
        return;
    }
    // Scene -> open a new viewport bound to its Scene_root.
    const std::shared_ptr<erhe::scene::Scene> scene = std::dynamic_pointer_cast<erhe::scene::Scene>(item);
    if (scene) {
        if (m_app_context.app_scenes != nullptr) {
            for (const std::shared_ptr<Scene_root>& scene_root : m_app_context.app_scenes->get_scene_roots()) {
                if (&scene_root->get_scene() == scene.get()) {
                    open_scene_viewport(scene_root);
                    return;
                }
            }
        }
        return;
    }
    // Content-library graph asset -> its graph editor.
    std::shared_ptr<erhe::Item_base> inner = item;
    const std::shared_ptr<Content_library_node> content_node = std::dynamic_pointer_cast<Content_library_node>(item);
    if (content_node && content_node->item) {
        inner = content_node->item;
    }
    const std::shared_ptr<Graph_mesh> graph_mesh = std::dynamic_pointer_cast<Graph_mesh>(inner);
    if (graph_mesh) {
        open_or_reuse_geometry_graph_window(graph_mesh);
        return;
    }
    const std::shared_ptr<Graph_texture> graph_texture = std::dynamic_pointer_cast<Graph_texture>(inner);
    if (graph_texture) {
        open_or_reuse_texture_graph_window(graph_texture);
        return;
    }
}

void Editor_windows::open_properties_for_item(const std::shared_ptr<erhe::Item_base>& item)
{
    // Pin to the inner asset when the item is a content-library wrapper, so
    // the Properties window shows the Material / Graph_texture / etc. directly.
    std::shared_ptr<erhe::Item_base> inner = item;
    const std::shared_ptr<Content_library_node> content_node = std::dynamic_pointer_cast<Content_library_node>(item);
    if (content_node && content_node->item) {
        inner = content_node->item;
    }
    open_properties_window(inner);
}

void Editor_windows::update_once_per_frame()
{
    prune_closed(m_properties_windows);
    prune_closed(m_geometry_graph_windows);
    prune_closed(m_texture_graph_windows);
}

} // namespace editor
