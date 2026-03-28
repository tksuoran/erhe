#pragma once

#include "erhe_rendergraph/rendergraph_node.hpp"
#include "erhe_rendergraph/render_target.hpp"
#include "erhe_dataformat/dataformat.hpp"
#include "erhe_utility/debug_label.hpp"

#include <memory>
#include <string>

namespace erhe::graphics {
    class Render_pass;
    class Device;
    class Renderbuffer;
    class Swapchain;
    class Texture;
}

namespace erhe::rendergraph {

class Texture_rendergraph_node_create_info
{
public:
    Rendergraph&               rendergraph;
    erhe::utility::Debug_label debug_label;
    int                        output_key          {Rendergraph_node_key::none};
    erhe::dataformat::Format   color_format        {erhe::dataformat::Format::format_undefined};
    erhe::dataformat::Format   depth_stencil_format{erhe::dataformat::Format::format_undefined};
    int                        sample_count        {0};
};

// Rendergraph node that holds a render target (color/depth textures and render pass).
class Texture_rendergraph_node : public Rendergraph_node
{
public:
    static constexpr std::string_view c_type_name{"Texture_rendergraph_node"};

    explicit Texture_rendergraph_node(const Texture_rendergraph_node_create_info& create_info);
    ~Texture_rendergraph_node() noexcept override;

    // Implements Rendergraph_node
    auto get_type_name              () const -> std::string_view override { return c_type_name; }
    auto get_producer_output_texture(int key, int depth = 0) const -> std::shared_ptr<erhe::graphics::Texture> override;

    [[nodiscard]] auto get_render_target() -> Render_target&;
    [[nodiscard]] auto get_render_target() const -> const Render_target&;

protected:
    Render_target  m_render_target;

private:
    int            m_output_key;
};

} // namespace erhe::rendergraph
