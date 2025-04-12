#pragma once

#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics/fragment_outputs.hpp"
#include "erhe_graphics/framebuffer.hpp"
#include "erhe_graphics/gpu_timer.hpp"
#include "erhe_graphics/pipeline.hpp"
#include "erhe_graphics/sampler.hpp"
#include "erhe_graphics/shader_resource.hpp"
#include "erhe_graphics/shader_stages.hpp"
#include "erhe_graphics/state/vertex_input_state.hpp"
#include "erhe_renderer/gpu_ring_buffer.hpp"
#include "erhe_rendergraph/rendergraph_node.hpp"

#include <string_view>

namespace erhe::graphics {
    class Texture;
}
namespace erhe::scene_renderer {
    class Program_interface;
}

namespace editor {

class Editor_context;
class Post_processing;
class Programs;


class Post_processing_node : public erhe::rendergraph::Rendergraph_node
{
public:
    Post_processing_node(
        erhe::graphics::Instance&       graphics_instance,
        erhe::rendergraph::Rendergraph& rendergraph,
        Post_processing&                post_processing,
        const std::string_view          name
    );

    // Implements Rendergraph_node
    auto get_type_name() const -> std::string_view override { return "Post_processing_node"; }
    void execute_rendergraph_node() override;

    // Overridden to provide texture from the first downsample node
    auto get_consumer_input_texture(erhe::rendergraph::Routing resource_routing, int key, int depth = 0) const -> std::shared_ptr<erhe::graphics::Texture> override;

    // Overridden to provide framebuffer from the first downsample node
    auto get_consumer_input_framebuffer(erhe::rendergraph::Routing resource_routing, int key, int depth = 0) const -> std::shared_ptr<erhe::graphics::Framebuffer> override;

    // Override so that size is always sources from output
    auto get_consumer_input_viewport(erhe::rendergraph::Routing resource_routing, int key, int depth = 0) const -> erhe::math::Viewport override;

    // Public API
    void viewport_toolbar();

    auto update_size() -> bool;

    void update_parameters();

    erhe::graphics::Instance& graphics_instance;
    Post_processing&          post_processing;

    float                                                     upsample_radius{1.0f};
    std::shared_ptr<erhe::graphics::Texture>                  downsample_texture;
    std::shared_ptr<erhe::graphics::Texture>                  upsample_texture;
    std::vector<std::shared_ptr<erhe::graphics::Framebuffer>> downsample_framebuffers;
    std::vector<std::shared_ptr<erhe::graphics::Framebuffer>> upsample_framebuffers;
    std::vector<std::shared_ptr<erhe::graphics::Texture>>     downsample_texture_views;
    std::vector<std::shared_ptr<erhe::graphics::Texture>>     upsample_texture_views;
    std::vector<int>                                          level_widths;
    std::vector<int>                                          level_heights;
    std::vector<size_t>                                       downsample_source_levels;
    std::vector<size_t>                                       upsample_source_levels;
    std::vector<float>                                        weights;
    int                                                       level0_width  {0};
    int                                                       level0_height {0};
    erhe::graphics::Buffer                                    parameter_buffer;
    float                                                     tonemap_luminance_max{1.5f};
    float                                                     tonemap_alpha{1.0f / 1.5f};
    int                                                       lowpass_count{2};
};

class Post_processing
{
public:
    class Offsets
    {
    public:
        Offsets(erhe::graphics::Shader_resource& block);

        std::size_t downsample_texture   {0}; // uvec2
        std::size_t upsample_texture     {0}; // uvec2

        std::size_t texel_scale          {0}; // vec2
        std::size_t source_lod           {0}; // float
        std::size_t level_count          {0}; // float

        std::size_t upsample_radius      {0}; // float
        std::size_t mix_weight           {0}; // float
        std::size_t tonemap_luminance_max{0}; // float
        std::size_t tonemap_alpha        {0}; // float
    };

    Post_processing(erhe::graphics::Instance& graphics_instance, Editor_context& editor_context);

    // Public API
    [[nodiscard]] auto create_node(
        erhe::graphics::Instance&       graphics_instance,
        erhe::rendergraph::Rendergraph& rendergraph,
        std::string_view                name
    ) -> std::shared_ptr<Post_processing_node>;

    [[nodiscard]] auto get_nodes() -> const std::vector<std::shared_ptr<Post_processing_node>>&;
    void post_process(Post_processing_node& node);
    void next_frame  ();

    auto get_offsets        () const -> const Offsets&                         { return m_offsets; }
    auto get_parameter_block() const -> const erhe::graphics::Shader_resource& { return m_parameter_block; }
    auto get_sampler        () const -> const erhe::graphics::Sampler&         { return m_linear_mipmap_nearest_sampler; }

private:
    [[nodiscard]] auto make_program(
        erhe::graphics::Instance&    graphics_instance,
        const char*                  name,
        const std::filesystem::path& fs_path
    ) -> erhe::graphics::Shader_stages_create_info;

    struct Shader_stages {
        erhe::graphics::Reloadable_shader_stages downsample_with_lowpass;
        erhe::graphics::Reloadable_shader_stages downsample;
        erhe::graphics::Reloadable_shader_stages upsample;
    };
    struct Pipelines {
        erhe::graphics::Pipeline downsample_with_lowpass;
        erhe::graphics::Pipeline downsample;
        erhe::graphics::Pipeline upsample;
    };

    static constexpr size_t s_max_level_count = 20;

    Editor_context&                                    m_context;
    std::vector<std::shared_ptr<Post_processing_node>> m_nodes;
    erhe::graphics::Fragment_outputs                   m_fragment_outputs;
    std::shared_ptr<erhe::graphics::Texture>           m_dummy_texture;
    erhe::graphics::Sampler                            m_linear_mipmap_nearest_sampler;
    erhe::graphics::Shader_resource                    m_parameter_block;
    Offsets                                            m_offsets;
    erhe::graphics::Vertex_input_state                 m_empty_vertex_input;
    erhe::graphics::Shader_resource                    m_default_uniform_block;       // containing sampler uniforms for non bindless textures
    const erhe::graphics::Shader_resource*             m_downsample_texture_resource; // for non bindless textures
    const erhe::graphics::Shader_resource*             m_upsample_texture_resource;   // for non bindless textures
    std::filesystem::path                              m_shader_path;
    Shader_stages                                      m_shader_stages;
    Pipelines                                          m_pipelines;
    erhe::graphics::Gpu_timer                          m_gpu_timer;
};

} // namespace editor
