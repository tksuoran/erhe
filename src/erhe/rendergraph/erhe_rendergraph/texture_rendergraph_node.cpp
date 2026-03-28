#include "erhe_rendergraph/texture_rendergraph_node.hpp"

#include "erhe_rendergraph/rendergraph.hpp"
#include "erhe_rendergraph/rendergraph_log.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/texture.hpp"

namespace erhe::rendergraph {

Texture_rendergraph_node::Texture_rendergraph_node(const Texture_rendergraph_node_create_info& create_info)
    : Rendergraph_node{create_info.rendergraph, create_info.debug_label}
    , m_render_target {
        Render_target_create_info{
            .graphics_device      = create_info.rendergraph.get_graphics_device(),
            .debug_label          = create_info.debug_label,
            .color_format         = create_info.color_format,
            .depth_stencil_format = create_info.depth_stencil_format,
            .sample_count         = create_info.sample_count
        }
    }
    , m_output_key{create_info.output_key}
{
    if (create_info.output_key != Rendergraph_node_key::none) {
        register_output(create_info.debug_label, create_info.output_key);
    }
}

Texture_rendergraph_node::~Texture_rendergraph_node() noexcept = default;

auto Texture_rendergraph_node::get_producer_output_texture(const int key, int) const -> std::shared_ptr<erhe::graphics::Texture>
{
    if ((key == m_output_key) || (key == Rendergraph_node_key::wildcard)) {
        return m_render_target.get_color_texture();
    }
    return {};
}

auto Texture_rendergraph_node::get_render_target() -> Render_target&
{
    return m_render_target;
}

auto Texture_rendergraph_node::get_render_target() const -> const Render_target&
{
    return m_render_target;
}

} // namespace erhe::rendergraph
