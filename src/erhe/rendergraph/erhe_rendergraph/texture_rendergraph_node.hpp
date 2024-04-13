#pragma once

#include "erhe_rendergraph/rendergraph_node.hpp"

#include "igl/TextureFormat.h"

#include <memory>
#include <mutex>
#include <string_view>

namespace igl {
    class IFramebuffer;
    class ITexture;
};

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
    Rendergraph&       rendergraph;
    std::string        name;
    int                sample_count        {1};
    std::string_view   label;
    int                input_key           {Rendergraph_node_key::none};
    int                output_key          {Rendergraph_node_key::none};
    igl::TextureFormat color_format        {0};
    igl::TextureFormat depth_stencil_format{0};
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
    ) const -> std::shared_ptr<igl::ITexture> override;

    [[nodiscard]] auto get_consumer_input_framebuffer(
        Resource_routing resource_routing,
        int              key,
        int              depth = 0
    ) const -> std::shared_ptr<igl::IFramebuffer> override;

    [[nodiscard]] auto get_producer_output_texture(
        Resource_routing resource_routing,
        int              key,
        int              depth = 0
    ) const -> std::shared_ptr<igl::ITexture> override;

    [[nodiscard]] auto get_producer_output_framebuffer(
        Resource_routing resource_routing,
        int              key,
        int              depth = 0
    ) const -> std::shared_ptr<igl::IFramebuffer> override;

    void execute_rendergraph_node() override;

protected:
    int                                 m_input_key;
    int                                 m_output_key;
    std::string                         m_label;
    int                                 m_sample_count{1};
    igl::TextureFormat                  m_color_format;
    igl::TextureFormat                  m_depth_stencil_format;
    std::shared_ptr<igl::ITexture>      m_color_texture_multisample;
    std::shared_ptr<igl::ITexture>      m_depth_stencil_texture_multisample;
    std::shared_ptr<igl::ITexture>      m_color_texture;
    std::shared_ptr<igl::ITexture>      m_depth_stencil_texture;
    std::shared_ptr<igl::IFramebuffer>  m_framebuffer;
};

} // namespace erhe::rendergraph
