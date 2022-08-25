#pragma once

#include "windows/viewport_config.hpp"
#include "erhe/application/rendergraph/texture_rendergraph_node.hpp"
#include "erhe/application/imgui/imgui_window.hpp"

#include <glm/glm.hpp>

#include <memory>

namespace erhe::application
{
    class Imgui_viewport;
    class Multisample_resolve_node;
}

namespace editor
{

class Post_processing_node;
class Viewport_window;

/// <summary>
/// Rendergraph sink node for showing contents originating from Viewport_window
/// </summary>
class Imgui_viewport_window
    : public erhe::application::Imgui_window
    , public erhe::application::Texture_rendergraph_node
    , public erhe::application::Mouse_input_sink
{
public:
    static constexpr std::string_view c_type_name{"Imgui_viewport_window"};
    static constexpr uint32_t c_type_hash{
        compiletime_xxhash::xxh32(
            c_type_name.data(),
            c_type_name.size(),
            {}
        )
    };

    Imgui_viewport_window();

    Imgui_viewport_window(
        const std::string_view                  name,
        const std::shared_ptr<Viewport_window>& viewport_window
    );

    // Implements Imgui_window
    void imgui               () override;
    auto get_window_type_hash() const -> uint32_t override { return c_type_hash; }
    auto consumes_mouse_input() const -> bool override;
    void on_begin            () override;
    void on_end              () override;
    void set_viewport        (erhe::application::Imgui_viewport* imgui_viewport) override;

    // Implements Rendergraph_node
    [[nodiscard]] auto get_consumer_input_viewport(
        erhe::application::Resource_routing resource_routing,
        int                                 key,
        int                                 depth = 0
    ) const -> erhe::scene::Viewport override;

    // Overridden to source viewport size from ImGui window
    [[nodiscard]] auto get_producer_output_viewport(
        erhe::application::Resource_routing resource_routing,
        int                                 key,
        int                                 depth = 0
    ) const -> erhe::scene::Viewport override;

    // Public API
    [[nodiscard]] auto viewport_window() const -> std::shared_ptr<Viewport_window>;
    [[nodiscard]] auto is_hovered     () const -> bool;

private:
    std::weak_ptr<Viewport_window> m_viewport_window;
    bool                           m_is_hovered     {false};
    erhe::scene::Viewport          m_viewport;
};

inline auto is_imgui_viewport_window(erhe::application::Imgui_window* window) -> bool
{
    return
        (window != nullptr) &&
        (window->get_window_type_hash() == Imgui_viewport_window::c_type_hash);
}

inline auto as_imgui_viewport_window(erhe::application::Imgui_window* window) -> Imgui_viewport_window*
{
    return
        (window != nullptr) &&
        (window->get_window_type_hash() == Imgui_viewport_window::c_type_hash)
            ? reinterpret_cast<Imgui_viewport_window*>(window)
            : nullptr;
}

} // namespace editor
