#pragma once

#include "erhe_rendergraph/rendergraph_node.hpp"
#include "erhe_rendergraph/render_target.hpp"
#include "erhe_dataformat/dataformat.hpp"
#include "erhe_utility/debug_label.hpp"

#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace erhe::graphics {
    class Render_pass;
    class Device;
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
    // Forwarded to Render_target: store depth/stencil so a later pass can load
    // the same attachment (post-processing overlay pass, issue #230).
    bool                       store_depth_stencil {false};
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

    // Prepare phase (main thread): the render target size the render phase
    // of the frame being prepared applies (apply_requested_size()). One
    // request per frame in flight, so the prepare phase of a later frame
    // does not touch the request the render phase of this frame reads.
    void request_size(int width, int height);

    // Render phase, at the start of the node's execute: sizes the render
    // target to the request the prepare phase of this frame made; a frame
    // whose prepare phase made no request keeps the current size. The render
    // target's textures and render pass are thus created, replaced and
    // dropped by the render phase only, which is the only phase that reads
    // them.
    void apply_requested_size();

protected:
    Render_target  m_render_target;

private:
    class Size_request
    {
    public:
        uint64_t frame_index{std::numeric_limits<uint64_t>::max()};
        int      width      {0};
        int      height     {0};
    };

    int                       m_output_key;
    // One per frame in flight, indexed by frame index modulo their count.
    std::vector<Size_request> m_size_requests;
};

} // namespace erhe::rendergraph
