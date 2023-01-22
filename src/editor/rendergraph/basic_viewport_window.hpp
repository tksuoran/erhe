#pragma once

#include "erhe/application/rendergraph/sink_rendergraph_node.hpp"

#include <glm/glm.hpp>

#include <memory>

namespace erhe::application
{

class Window;

}

namespace editor
{

class Viewport_window;
class Viewport_windows;
class Window;

class Basic_viewport_window
    : public erhe::application::Sink_rendergraph_node
{
public:
    static constexpr std::string_view c_type_name{"Basic_viewport_window"};
    static constexpr uint32_t c_type_hash{
        compiletime_xxhash::xxh32(
            c_type_name.data(),
            c_type_name.size(),
            {}
        )
    };

    Basic_viewport_window();

    Basic_viewport_window(
        const std::string_view                  name,
        const std::shared_ptr<Viewport_window>& viewport_window
    );

    ~Basic_viewport_window();

    // Implements Rendergraph_node
    [[nodiscard]] auto get_consumer_input_viewport(
        erhe::application::Resource_routing resource_routing,
        int                                 key,
        int                                 depth = 0
    ) const -> erhe::scene::Viewport override;

    [[nodiscard]] auto get_producer_output_viewport(
        erhe::application::Resource_routing resource_routing,
        int                                 key,
        int                                 depth = 0
    ) const -> erhe::scene::Viewport override;

    [[nodiscard]] auto get_consumer_input_texture(
        erhe::application::Resource_routing resource_routing,
        int                                 key,
        int                                 depth = 0
    ) const -> std::shared_ptr<erhe::graphics::Texture> override;

    [[nodiscard]] auto get_producer_output_texture(
        erhe::application::Resource_routing resource_routing,
        int                                 key,
        int                                 depth = 0
    ) const -> std::shared_ptr<erhe::graphics::Texture> override;

    [[nodiscard]] auto get_consumer_input_framebuffer(
        erhe::application::Resource_routing resource_routing,
        int                                 key,
        int                                 depth = 0
    ) const -> std::shared_ptr<erhe::graphics::Framebuffer> override;

    [[nodiscard]] auto get_producer_output_framebuffer(
        erhe::application::Resource_routing resource_routing,
        int                                 key,
        int                                 depth = 0
    ) const -> std::shared_ptr<erhe::graphics::Framebuffer> override;

    // Public API
    [[nodiscard]] auto get_viewport_window() const -> std::shared_ptr<Viewport_window>;
    [[nodiscard]] auto get_viewport       () const -> const erhe::scene::Viewport&;
    void set_viewport  (erhe::scene::Viewport viewport);
    void set_is_hovered(bool is_hovered);

private:
    // Does *not* directly point to erhe::application::window,
    // because layout is done by Viewport_windows.
    std::weak_ptr<Viewport_window> m_viewport_window;
    //Viewport_windows*              m_viewport_windows;
    erhe::scene::Viewport          m_viewport;
};

} // namespace editor
