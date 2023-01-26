#pragma once

#include "erhe/application/rendergraph/rendergraph_node.hpp"
#include "erhe/graphics/framebuffer.hpp"

#include <fmt/format.h>

#include <string>
#include <deque>

namespace erhe::graphics
{
    class Gpu_timer;
    class Texture;
}

namespace erhe::application
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
    static constexpr uint32_t c_type_hash = compiletime_xxhash::xxh32(c_type_name.data(), c_type_name.size(), {});

    // TODO color format, depth and stencil format
    Multisample_resolve_node(
        const std::string_view name,
        int                    sample_count,
        const std::string_view label,
        int                    key
    );

    void reconfigure(int sample_count);

    // Implements Rendergraph_node
    [[nodiscard]] auto type_name() const -> std::string_view override { return c_type_name; }
    [[nodiscard]] auto type_hash() const -> uint32_t         override { return c_type_hash; }
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

    // Override so that size is always sources from output
    [[nodiscard]] auto get_consumer_input_viewport(
        Resource_routing resource_routing,
        int              key,
        int              depth = 0
    ) const -> erhe::scene::Viewport override;

    void execute_rendergraph_node() override;

private:
    std::shared_ptr<erhe::graphics::Texture>      m_color_texture;
    std::unique_ptr<erhe::graphics::Renderbuffer> m_depth_stencil_renderbuffer;
    std::shared_ptr<erhe::graphics::Framebuffer>  m_framebuffer;
    int                                           m_sample_count{0};
    std::string                                   m_label;
    int                                           m_key;
};

} // namespace editor
