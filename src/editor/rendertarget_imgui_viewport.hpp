#pragma once

#include "erhe/application/imgui/imgui_viewport.hpp"

#include <string_view>

namespace erhe::application
{
    class Configuration;
    class Imgui_renderer;
    class Imgui_window;
    class Imgui_windows;
    class View;
}

namespace erhe::components
{
    class Components;
}

namespace editor
{

#if defined(ERHE_XR_LIBRARY_OPENXR)
class Hand_tracker;
class Headset_view;
#endif
class Rendertarget_node;
class Viewport_window;

class Rendertarget_imgui_viewport
    : public erhe::application::Imgui_viewport
{
public:
    Rendertarget_imgui_viewport(
        Rendertarget_node*                  rendertarget_node,
        const std::string_view              name,
        const erhe::components::Components& components,
        bool                                imgui_ini = true
    );
    virtual ~Rendertarget_imgui_viewport() noexcept;

    [[nodiscard]] auto rendertarget_node() -> Rendertarget_node*;
    void set_clear_color(const glm::vec4& value);

    // Implements Imgui_viewport
    [[nodiscard]] auto get_scale_value() const -> float override;
    [[nodiscard]] auto begin_imgui_frame() -> bool override;
    void end_imgui_frame() override;

    // Implements Rendergraph_node
    void execute_rendergraph_node() override;

    [[nodiscard]] auto get_consumer_input_texture(
        erhe::application::Resource_routing resource_routing,
        int                                 key,
        int                                 depth = 0
    ) const -> std::shared_ptr<erhe::graphics::Texture> override;

    [[nodiscard]] auto get_consumer_input_framebuffer(
        erhe::application::Resource_routing resource_routing,
        int                                 key,
        int                                 depth = 0
    ) const -> std::shared_ptr<erhe::graphics::Framebuffer> override;

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

private:
    Rendertarget_node*                                 m_rendertarget_node;
    std::shared_ptr<erhe::application::Configuration>  m_configuration;
    std::shared_ptr<erhe::application::Imgui_renderer> m_imgui_renderer;
    std::shared_ptr<erhe::application::Imgui_windows>  m_imgui_windows;
    std::shared_ptr<erhe::application::View>           m_view;
#if defined(ERHE_XR_LIBRARY_OPENXR)
    std::shared_ptr<Hand_tracker>                      m_hand_tracker;
    std::shared_ptr<Headset_view>                      m_headset_view;
    bool                                               m_last_mouse_finger{false};
#endif
    std::string m_name;
    std::string m_imgui_ini_path;
    glm::vec4   m_clear_color {0.0f, 0.0f, 0.0f, 0.0f};
    double      m_time        {0.0};
    double      m_last_mouse_x{0.0};
    double      m_last_mouse_y{0.0};
};

} // namespace erhe::application
