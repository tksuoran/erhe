#include "rendergraph/basic_viewport_window.hpp"

#include "scene/viewport_window.hpp"
#include "scene/viewport_windows.hpp"

#include "erhe/application/window.hpp"
#include "erhe/graphics/renderbuffer.hpp"


namespace editor
{

using erhe::graphics::Framebuffer;
using erhe::graphics::Texture;

Basic_viewport_window::Basic_viewport_window()
    : erhe::application::Sink_rendergraph_node{
        std::string{"(default constructed Basic_viewport_window)"}
    }
{
}

Basic_viewport_window::Basic_viewport_window(
    const std::string_view                  name,
    const std::shared_ptr<Viewport_window>& viewport_window
)
    : erhe::application::Sink_rendergraph_node{
        std::string{name}
    }
    , m_viewport_window{viewport_window}
{
    // Initially empty viewport. Layout is done by Viewport_windows
    m_viewport.x      = 0;
    m_viewport.y      = 0;
    m_viewport.width  = 0;
    m_viewport.height = 0;

    register_input(
        erhe::application::Resource_routing::Resource_provided_by_consumer,
        "viewport",
        erhe::application::Rendergraph_node_key::viewport
    );

    // "window" is slot / pseudo-resource which allows use rendergraph connection
    // to make Imgui_viewport_window a dependency for Imgui_viewport, forcing
    // correct rendering order (Imgui_viewport_window must be rendered before
    // Imgui_viewport).
    //
    // TODO Imgui_renderer should carry dependencies using Rendergraph.
    register_output(
        erhe::application::Resource_routing::None,
        "window",
        erhe::application::Rendergraph_node_key::window
    );
}

Basic_viewport_window::~Basic_viewport_window()
{
    if (m_viewport_windows)
    {
        m_viewport_windows->erase(this);
    }
}

[[nodiscard]] auto Basic_viewport_window::get_viewport_window() const -> std::shared_ptr<Viewport_window>
{
    return m_viewport_window.lock();
}

[[nodiscard]] auto Basic_viewport_window::get_consumer_input_viewport(
    const erhe::application::Resource_routing resource_routing,
    const int                                 key,
    const int                                 depth
) const -> erhe::scene::Viewport
{
    static_cast<void>(resource_routing); // TODO Validate
    static_cast<void>(depth);
    static_cast<void>(key);
    //ERHE_VERIFY(key == erhe::application::Rendergraph_node_key::window); TODO
    return get_viewport();
}

[[nodiscard]] auto Basic_viewport_window::get_producer_output_viewport(
    const erhe::application::Resource_routing resource_routing,
    const int                                 key,
    const int                                 depth
) const -> erhe::scene::Viewport
{
    static_cast<void>(resource_routing); // TODO Validate
    static_cast<void>(depth);
    static_cast<void>(key);
    //ERHE_VERIFY(key == erhe::application::Rendergraph_node_key::window); TODO
    return get_viewport();
}

[[nodiscard]] auto Basic_viewport_window::get_consumer_input_texture(
    const erhe::application::Resource_routing resource_routing,
    const int                                 key,
    const int                                 depth
) const -> std::shared_ptr<erhe::graphics::Texture>
{
    static_cast<void>(resource_routing);
    static_cast<void>(key);
    static_cast<void>(depth);
    return {};
}

[[nodiscard]] auto Basic_viewport_window::get_producer_output_texture(
    const erhe::application::Resource_routing resource_routing,
    const int                                 key,
    const int                                 depth
) const -> std::shared_ptr<erhe::graphics::Texture>
{
    static_cast<void>(resource_routing);
    static_cast<void>(key);
    static_cast<void>(depth);
    return {};
}

[[nodiscard]] auto Basic_viewport_window::get_consumer_input_framebuffer(
    const erhe::application::Resource_routing resource_routing,
    const int                                 key,
    const int                                 depth
) const -> std::shared_ptr<erhe::graphics::Framebuffer>
{
    static_cast<void>(resource_routing);
    static_cast<void>(key);
    static_cast<void>(depth);
    return {};
}

[[nodiscard]] auto Basic_viewport_window::get_producer_output_framebuffer(
    const erhe::application::Resource_routing resource_routing,
    const int                                 key,
    const int                                 depth
) const -> std::shared_ptr<erhe::graphics::Framebuffer>
{
    static_cast<void>(resource_routing);
    static_cast<void>(key);
    static_cast<void>(depth);
    return {};
}

[[nodiscard]] auto Basic_viewport_window::get_viewport() const -> const erhe::scene::Viewport&
{
    return m_viewport;
}

void Basic_viewport_window::set_viewport(erhe::scene::Viewport viewport)
{
    m_viewport = viewport;

    const auto viewport_window = m_viewport_window.lock();
    if (viewport_window)
    {
        viewport_window->set_window_viewport(viewport.x, viewport.y, viewport.width, viewport.height);
    }
}

void Basic_viewport_window::set_is_hovered(const bool is_hovered)
{
    const auto viewport_window = m_viewport_window.lock();
    if (viewport_window)
    {
        viewport_window->set_is_hovered(is_hovered);
    }
}

void Basic_viewport_window::connect(Viewport_windows* viewport_windows)
{
    m_viewport_windows = viewport_windows;
}


} // namespace editor
