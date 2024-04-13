#if 0
#pragma once

#include "erhe_rendergraph/rendergraph_node.hpp"

#include <string>

namespace igl {
    class ITexture;
    class IFramebuffer;
}

namespace erhe::graphics {
    class Gpu_timer;
    class Texture;
}

namespace erhe::rendergraph
{

/// <summary>
/// Rendergraph processer node for adding multisampling to output rendergraph node
/// </summary>
/// Creates multisampled variant of output rendergraph node and resolves it to
/// the target rendergraph node.
class Multisample_resolve_node
    : public Rendergraph_node
{
public:
    static constexpr std::string_view c_type_name{"Multisample_resolve_node"};

    // TODO color format, depth and stencil format
    Multisample_resolve_node(
        Rendergraph&           rendergraph,
        const std::string_view name,
        int                    sample_count,
        const std::string_view label,
        int                    key
    );

    void reconfigure(int sample_count);

    // Implements Rendergraph_node
    [[nodiscard]] auto get_type_name                 () const -> std::string_view override { return c_type_name; }
    [[nodiscard]] auto get_consumer_input_texture    (Resource_routing resource_routing, int key, int depth = 0) const -> std::shared_ptr<igl::ITexture> override;
    [[nodiscard]] auto get_consumer_input_framebuffer(Resource_routing resource_routing, int key, int depth = 0 ) const -> std::shared_ptr<igl::IFramebuffer> override;

    // Override so that size is always sources from output
    [[nodiscard]] auto get_consumer_input_viewport   (Resource_routing resource_routing, int key, int depth = 0) const -> erhe::math::Viewport override;

    void execute_rendergraph_node() override;

private:
    std::shared_ptr<igl::ITexture>     m_color_texture;
    std::shared_ptr<igl::ITexture>     m_depth_stencil_texture;
    std::shared_ptr<igl::IFramebuffer> m_framebuffer;
    int                                m_sample_count{0};
    std::string                        m_label;
    int                                m_key;
};

} // namespace erhe::rendergraph
#endif
