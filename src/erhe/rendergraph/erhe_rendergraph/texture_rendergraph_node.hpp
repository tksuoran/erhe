#pragma once

#include "erhe_rendergraph/rendergraph_node.hpp"
#include "erhe_gl/wrapper_enums.hpp"
#include <memory>
#include <mutex>
#include <string_view>

namespace erhe::graphics {
    class Framebuffer;
    class Instance;
    class Renderbuffer;
    class Texture;
}

namespace erhe::rendergraph
{

class Texture_rendergraph_node_create_info
{
public:
    Rendergraph&        rendergraph;
    std::string         name;
    int                 input_key           {Rendergraph_node_key::none};
    int                 output_key          {Rendergraph_node_key::none};
    gl::Internal_format color_format        {0};
    gl::Internal_format depth_stencil_format{0};
};

/// <summary>
/// Rendergraph node that holds a texture and framebuffer resource.
/// The texture is not multisampled (at least for now). In order to
/// add multisampling, use a separate Multisample_resolve node in
/// front of Texture_rendergraph_node.
/// </summary>
class Texture_rendergraph_node
    : public Rendergraph_node
{
public:
    static constexpr std::string_view c_type_name{"Texture_rendergraph_node"};

    explicit Texture_rendergraph_node(
        const Texture_rendergraph_node_create_info& create_info
    );
    explicit Texture_rendergraph_node(
        const Texture_rendergraph_node_create_info&& create_info
    );

    ~Texture_rendergraph_node() noexcept override; 

    // Implements Rendergraph_node
    [[nodiscard]] auto get_type_name() const -> std::string_view override { return c_type_name; }

    // TODO Think if we want to provide here both consumer input and producer output
    [[nodiscard]] auto get_consumer_input_texture(
        Resource_routing resource_routing,
        int              key,
        int              depth = 0
    ) const -> std::shared_ptr<erhe::graphics::Texture> override;

    [[nodiscard]] auto get_consumer_input_framebuffer(
        Resource_routing resource_routing,
        int              key,
        int              depth = 0
    ) const -> std::shared_ptr<erhe::graphics::Framebuffer> override;

    [[nodiscard]] auto get_producer_output_texture(
        Resource_routing resource_routing,
        int              key,
        int              depth = 0
    ) const -> std::shared_ptr<erhe::graphics::Texture> override;

    [[nodiscard]] auto get_producer_output_framebuffer(
        Resource_routing resource_routing,
        int              key,
        int              depth = 0
    ) const -> std::shared_ptr<erhe::graphics::Framebuffer> override;

    void execute_rendergraph_node() override;

protected:
    int                                           m_input_key;
    int                                           m_output_key;
    gl::Internal_format                           m_color_format;
    gl::Internal_format                           m_depth_stencil_format;
    std::shared_ptr<erhe::graphics::Texture>      m_color_texture;
    std::unique_ptr<erhe::graphics::Renderbuffer> m_depth_stencil_renderbuffer;
    std::shared_ptr<erhe::graphics::Framebuffer>  m_framebuffer;
};

} // namespace erhe::rendergraph
