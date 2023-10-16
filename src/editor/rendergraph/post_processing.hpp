#pragma once

#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics/fragment_outputs.hpp"
#include "erhe_graphics/framebuffer.hpp"
#include "erhe_graphics/gpu_timer.hpp"
#include "erhe_graphics/pipeline.hpp"
#include "erhe_graphics/sampler.hpp"
#include "erhe_graphics/shader_resource.hpp"
#include "erhe_graphics/shader_stages.hpp"
#include "erhe_graphics/vertex_attribute_mappings.hpp"
#include "erhe_graphics/vertex_format.hpp"
#include "erhe_graphics/state/vertex_input_state.hpp"
#include "erhe_imgui/imgui_window.hpp"
#include "erhe_renderer/buffer_writer.hpp"
#include "erhe_renderer/multi_buffer.hpp"
#include "erhe_rendergraph/rendergraph_node.hpp"

#include <fmt/format.h>

#include <string>
#include <deque>

namespace erhe::graphics {
    class Texture;
}
namespace erhe::scene_renderer {
    class Program_interface;
}

namespace editor
{

class Editor_context;
class Post_processing_node;
class Post_processing;
class Programs;

class Downsample_node
{
public:
    //Downsample_node();
    Downsample_node(
        erhe::graphics::Instance& graphics_instance,
        const std::string&        label,
        int                       width,
        int                       height,
        int                       axis
    );

    void bind_framebuffer() const;

    std::shared_ptr<erhe::graphics::Texture>     texture;
    std::shared_ptr<erhe::graphics::Framebuffer> framebuffer;
    int                                          axis{0};
};

/// Rendergraph_node for applying bloom and tonemap.
class Post_processing_node
    : public erhe::rendergraph::Rendergraph_node
{
public:
    Post_processing_node(
        erhe::rendergraph::Rendergraph& rendergraph,
        Editor_context&                 editor_context,
        const std::string_view          name
    );

    // Implements Rendergraph_node
    [[nodiscard]] auto get_type_name() const -> std::string_view override { return "Post_processing_node"; }
    void execute_rendergraph_node() override;

    // Overridden to provide texture from the first downsample node
    [[nodiscard]] auto get_consumer_input_texture(
        erhe::rendergraph::Resource_routing resource_routing,
        int                                 key,
        int                                 depth = 0
    ) const -> std::shared_ptr<erhe::graphics::Texture> override;

    // Overridden to provide framebuffer from the first downsample node
    [[nodiscard]] auto get_consumer_input_framebuffer(
        erhe::rendergraph::Resource_routing resource_routing,
        int                                 key,
        int                                 depth = 0
    ) const -> std::shared_ptr<erhe::graphics::Framebuffer> override;

    // Override so that size is always sources from output
    [[nodiscard]] auto get_consumer_input_viewport(
        erhe::rendergraph::Resource_routing resource_routing,
        int                                 key,
        int                                 depth = 0
    ) const -> erhe::math::Viewport override;

    // Public API
    void viewport_toolbar();

    // For Post_processing
    [[nodiscard]] auto get_downsample_nodes() -> const std::vector<Downsample_node>&;

private:
    auto update_downsample_nodes(
        erhe::graphics::Instance& graphics_instance
    ) -> bool;

    Editor_context& m_context;

    std::vector<Downsample_node> m_downsample_nodes;
    int                          m_width  {0};
    int                          m_height {0};
};

class Post_processing
{
public:
    Post_processing(
        erhe::graphics::Instance& graphics_instance,
        Editor_context&           editor_context,
        Programs&                 programs
    );

    // Public API
    auto create_node (
        erhe::rendergraph::Rendergraph& rendergraph,
        const std::string_view          name
    ) -> std::shared_ptr<Post_processing_node>;

    auto get_nodes   () -> const std::vector<std::shared_ptr<Post_processing_node>>&;
    void post_process(Post_processing_node& node);
    void next_frame  ();

private:
    [[nodiscard]] auto make_program(
        erhe::graphics::Instance&    graphics_instance,
        const char*                  name,
        const std::filesystem::path& fs_path
    ) -> erhe::graphics::Shader_stages_create_info;

    void downsample(
        const erhe::graphics::Texture*  source_texture,
        const Downsample_node&          node,
        const erhe::graphics::Pipeline& pipeline
    );
    void compose               (Post_processing_node& node);
    void create_frame_resources();

    class Offsets
    {
    public:
        Offsets(
            erhe::graphics::Shader_resource& block,
            std::size_t                      source_texture_count
        );

        std::size_t texel_scale   {0}; // float
        std::size_t texture_count {0}; // uint
        std::size_t reserved0     {0}; // float
        std::size_t reserved1     {0}; // float
        std::size_t source_texture{0}; // uvec2[source_texture_count]
    };

    Editor_context&                                    m_context;
    std::vector<std::shared_ptr<Post_processing_node>> m_nodes;
    erhe::graphics::Fragment_outputs                   m_fragment_outputs;
    std::shared_ptr<erhe::graphics::Texture>           m_dummy_texture;
    erhe::graphics::Sampler&                           m_linear_sampler;
    erhe::graphics::Sampler&                           m_nearest_sampler;
    erhe::renderer::Multi_buffer                       m_parameter_buffer;
    erhe::graphics::Shader_resource                    m_parameter_block;
    std::size_t                                        m_source_texture_count;
    Offsets                                            m_offsets;
    erhe::graphics::Vertex_input_state                 m_empty_vertex_input;
    erhe::graphics::Shader_resource                    m_default_uniform_block;              // containing sampler uniforms for non bindless textures
    const erhe::graphics::Shader_resource*             m_downsample_source_texture_resource; // for non bindless textures
    const erhe::graphics::Shader_resource*             m_compose_source_textures_resource;   // for non bindless textures
    std::filesystem::path                              m_shader_path;
    erhe::graphics::Reloadable_shader_stages           m_downsample_x_shader_stages;
    erhe::graphics::Reloadable_shader_stages           m_downsample_y_shader_stages;
    erhe::graphics::Reloadable_shader_stages           m_compose_shader_stages;
    erhe::graphics::Pipeline                           m_downsample_x_pipeline;
    erhe::graphics::Pipeline                           m_downsample_y_pipeline;
    erhe::graphics::Pipeline                           m_compose_pipeline;
    erhe::graphics::Gpu_timer                          m_gpu_timer;
};

} // namespace editor
