#pragma once

#include "renderers/light_buffer.hpp"
#include "erhe/application/rendergraph/rendergraph_node.hpp"

#include <glm/glm.hpp>

#include <memory>

namespace editor
{

class Light_projections;
class Scene_root;
class Shadow_renderer;
class Viewport_window;

/// <summary>
/// Helper rendergraph node calling Shadow_renderer
/// </summary>
/// TODO Think about Shadow_render_node / Shadow_renderer relationship,
///      should Shadow_render_node have more state?
class Shadow_render_node
    : public erhe::application::Rendergraph_node
{
public:
    static constexpr std::string_view c_type_name{"Shadow_render_node"};
    static constexpr uint32_t c_type_hash = compiletime_xxhash::xxh32(c_type_name.data(), c_type_name.size(), {});

    Shadow_render_node(
        Shadow_renderer&                        shadow_renderer,
        const std::shared_ptr<Viewport_window>& viewport_window,
        int                                     resolution,
        int                                     light_count,
        bool                                    reverse_depth
    );

    // Implements Rendergraph_node
    [[nodiscard]] auto type_name() const -> std::string_view override { return c_type_name; }
    [[nodiscard]] auto type_hash() const -> uint32_t         override { return c_type_hash; }

    void execute_rendergraph_node() override;

    [[nodiscard]] auto get_producer_output_texture(
        erhe::application::Resource_routing resource_routing,
        int                                 key,
        int                                 depth = 0
    ) const -> std::shared_ptr<erhe::graphics::Texture> override;

    [[nodiscard]] auto get_producer_output_viewport(
        erhe::application::Resource_routing resource_routing,
        int                                 key,
        int                                 depth = 0
    ) const -> erhe::scene::Viewport override;

    [[nodiscard]] auto inputs_allowed() const -> bool override;

    // Public API
    auto viewport_window  () const -> std::shared_ptr<Viewport_window>;
    auto light_projections() -> Light_projections&;
    [[nodiscard]] auto texture () const -> std::shared_ptr<erhe::graphics::Texture>;
    [[nodiscard]] auto viewport() const -> erhe::scene::Viewport;

private:
    Shadow_renderer&                 m_shadow_renderer;
    std::shared_ptr<Viewport_window> m_viewport_window;

    std::shared_ptr<erhe::graphics::Texture>                  m_texture;
    std::vector<std::unique_ptr<erhe::graphics::Framebuffer>> m_framebuffers;
    erhe::scene::Viewport                                     m_viewport{0, 0, 0, 0, true};

    Scene_root*       m_shadow_scene{nullptr};
    Light_projections m_light_projections;
};

} // namespace editor
