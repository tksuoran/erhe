#pragma once

#include "erhe_rendergraph/rendergraph_node.hpp"
#include "erhe_dataformat/dataformat.hpp"
#include <memory>
#include <mutex>
#include <string_view>

namespace erhe::graphics {
    class Render_pass;
    class Device;
    class Renderbuffer;
    class Texture;
}

namespace erhe::rendergraph {

class Texture_rendergraph_node_create_info
{
public:
    Rendergraph&             rendergraph;
    std::string              name;
    int                      input_key           {Rendergraph_node_key::none};
    int                      output_key          {Rendergraph_node_key::none};
    erhe::dataformat::Format color_format        {erhe::dataformat::Format::format_undefined};
    erhe::dataformat::Format depth_stencil_format{erhe::dataformat::Format::format_undefined};
    // TODO multisample count
};

/// <summary>
/// Rendergraph node that holds a texture and framebuffer resource.
/// The texture is not multisampled (at least for now). In order to
/// add multisampling, use a separate Multisample_resolve node in
/// front of Texture_rendergraph_node.
/// </summary>
class Texture_rendergraph_node : public Rendergraph_node
{
public:
    static constexpr std::string_view c_type_name{"Texture_rendergraph_node"};

    explicit Texture_rendergraph_node(const Texture_rendergraph_node_create_info& create_info);
    explicit Texture_rendergraph_node(const Texture_rendergraph_node_create_info&& create_info);

    ~Texture_rendergraph_node() noexcept override; 

    // Implements Rendergraph_node
    auto get_type_name() const -> std::string_view override { return c_type_name; }

    // TODO Think if we want to provide here both consumer input and producer output
    auto get_consumer_input_texture     (Routing resource_routing, int key, int depth = 0) const -> std::shared_ptr<erhe::graphics::Texture> override;
    auto get_consumer_input_render_pass (Routing resource_routing, int key, int depth = 0) const -> erhe::graphics::Render_pass* override;
    auto get_producer_output_texture    (Routing resource_routing, int key, int depth = 0) const -> std::shared_ptr<erhe::graphics::Texture> override;
    auto get_producer_output_render_pass(Routing resource_routing, int key, int depth = 0) const -> erhe::graphics::Render_pass* override;
    void execute_rendergraph_node       () override;

protected:
    int                                           m_input_key;
    int                                           m_output_key;
    erhe::dataformat::Format                      m_color_format;
    erhe::dataformat::Format                      m_depth_stencil_format;
    std::shared_ptr<erhe::graphics::Texture>      m_color_texture;
    std::unique_ptr<erhe::graphics::Renderbuffer> m_depth_stencil_renderbuffer;
    std::unique_ptr<erhe::graphics::Render_pass>  m_render_pass;
};

} // namespace erhe::rendergraph
