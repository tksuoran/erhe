#pragma once

#include "erhe_rendergraph/rendergraph_node.hpp"
#include "erhe_dataformat/dataformat.hpp"

#include <memory>
#include <string>

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
    int                      sample_count        {0};
};

// Rendergraph node that holds a texture and render pass.
class Texture_rendergraph_node : public Rendergraph_node
{
public:
    static constexpr std::string_view c_type_name{"Texture_rendergraph_node"};

    explicit Texture_rendergraph_node(const Texture_rendergraph_node_create_info& create_info);
    explicit Texture_rendergraph_node(const Texture_rendergraph_node_create_info&& create_info);
    ~Texture_rendergraph_node() noexcept override; 

    // Implements Rendergraph_node
    auto get_type_name              () const -> std::string_view override { return c_type_name; }
    auto get_producer_output_texture(int key, int depth = 0) const -> std::shared_ptr<erhe::graphics::Texture> override;

    void update_render_pass(int width, int height, bool use_default_framebuffer = false);
    void reconfigure       (int sample_count);

protected:
    int                                          m_input_key;
    int                                          m_output_key;
    erhe::dataformat::Format                     m_color_format;
    erhe::dataformat::Format                     m_depth_stencil_format;
    int                                          m_sample_count;
    std::shared_ptr<erhe::graphics::Texture>     m_color_texture;
    std::shared_ptr<erhe::graphics::Texture>     m_multisampled_color_texture;
    std::unique_ptr<erhe::graphics::Texture>     m_depth_stencil_texture;
    std::unique_ptr<erhe::graphics::Render_pass> m_render_pass;
};

} // namespace erhe::rendergraph
