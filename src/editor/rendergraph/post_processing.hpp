#pragma once

#include "erhe_graphics/bind_group_layout.hpp"
#include "erhe_graphics/enums.hpp"
#include "erhe_graphics/fragment_outputs.hpp"
#include "erhe_graphics/gpu_timer.hpp"
#include "erhe_graphics/render_pass.hpp"
#include "erhe_graphics/render_pipeline.hpp"
#include "erhe_graphics/render_pipeline_state.hpp"
#include "erhe_graphics/ring_buffer_client.hpp"
#include "erhe_graphics/ring_buffer_range.hpp"
#include "erhe_graphics/sampler.hpp"
#include "erhe_graphics/shader_resource.hpp"
#include "erhe_graphics/shader_stages.hpp"
#include "erhe_graphics/state/vertex_input_state.hpp"
#include "erhe_rendergraph/rendergraph_node.hpp"
#include "erhe_utility/debug_label.hpp"

#include <string>
#include <string_view>

namespace erhe::graphics       { class Command_buffer; class Texture; class Texture_heap; }
namespace erhe::scene_renderer { class Program_interface; }

namespace editor {

class App_context;
class Post_processing;
class Post_processing_node;
class Programs;

class Post_processing_node_texture_reference : public erhe::graphics::Texture_reference
{
public:
    Post_processing_node_texture_reference(const std::shared_ptr<Post_processing_node>& node, int direction, size_t level);
    ~Post_processing_node_texture_reference() noexcept override;

    auto get_referenced_texture() const -> const erhe::graphics::Texture* override;

private:
    // Weak, to break the owning cycle: Post_processing_node owns
    // texture_references, whose entries would otherwise own back the node
    // and keep its GPU textures alive across shutdown.
    std::weak_ptr<Post_processing_node> m_node;
    int                                 m_direction;
    size_t                              m_level;
};

class Post_processing_node : public erhe::rendergraph::Rendergraph_node
{
public:
    Post_processing_node(
        erhe::graphics::Device&         graphics_device,
        erhe::rendergraph::Rendergraph& rendergraph,
        Post_processing&                post_processing,
        erhe::utility::Debug_label      debug_label
    );
    ~Post_processing_node() noexcept override;

    // Implements Rendergraph_node
    auto get_type_name() const -> std::string_view override { return "Post_processing_node"; }
    void execute_rendergraph_node(erhe::graphics::Command_buffer& command_buffer) override;

    // Override so that we return post processing output
    auto get_producer_output_texture(int key, int depth = 0) const -> std::shared_ptr<erhe::graphics::Texture> override;

    // Public API
    void viewport_toolbar();

    auto update_size() -> bool;

    void update_parameters();

    float                                                     upsample_radius{1.0f};
    // Per-level non-mipmapped textures. downsample_textures[k] stores pyramid
    // level k+1 (pyramid starts at half resolution = level 1). upsample_textures[k]
    // stores pyramid level k. The deepest pyramid level (only written by
    // downsample, never rendered by upsample) has no upsample texture.
    std::vector<std::shared_ptr<erhe::graphics::Texture>>     downsample_textures;
    std::vector<std::shared_ptr<erhe::graphics::Texture>>     upsample_textures;
    std::vector<std::unique_ptr<erhe::graphics::Render_pass>> downsample_render_passes;
    std::vector<std::unique_ptr<erhe::graphics::Render_pass>> upsample_render_passes;
    std::vector<int>                                          level_widths;
    std::vector<int>                                          level_heights;
    std::vector<size_t>                                       downsample_source_levels;
    std::vector<size_t>                                       upsample_source_levels;
    std::vector<float>                                        weights;
    std::vector<std::shared_ptr<Post_processing_node_texture_reference>> texture_references;
    int                                                       level0_width  {0};
    int                                                       level0_height {0};
    erhe::graphics::Ring_buffer_range                         parameter_buffer_range;
    float                                                     tonemap_luminance_max{1.5f};
    float                                                     tonemap_alpha{1.0f / 1.5f};
    int                                                       lowpass_count{2};

private:
    erhe::graphics::Device& m_graphics_device;
    Post_processing&        m_post_processing;
};

class Post_processing
{
public:
    class Offsets
    {
    public:
        explicit Offsets(erhe::graphics::Shader_resource& block);

        std::size_t input_texture        {0}; // uvec2
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

    Post_processing(erhe::graphics::Device& graphics_device, erhe::graphics::Command_buffer& init_command_buffer, App_context& context);
    ~Post_processing() noexcept;

    // Public API
    [[nodiscard]] auto create_node(
        erhe::graphics::Device&         graphics_device,
        erhe::rendergraph::Rendergraph& rendergraph,
        erhe::utility::Debug_label      debug_label
    ) -> std::shared_ptr<Post_processing_node>;

    [[nodiscard]] auto get_nodes() -> const std::vector<std::shared_ptr<Post_processing_node>>&;
    void post_process(Post_processing_node& node, erhe::graphics::Command_buffer& command_buffer);

    auto get_offsets                      () const -> const Offsets&                         { return m_offsets; }
    auto get_parameter_block              () const -> const erhe::graphics::Shader_resource& { return m_parameter_block; }
    auto get_sampler_linear               () const -> const erhe::graphics::Sampler&         { return m_sampler_linear; }
    auto get_sampler_linear_mipmap_nearest() const -> const erhe::graphics::Sampler&         { return m_sampler_linear_mipmap_nearest; }
    auto get_parameter_buffer_client      () -> erhe::graphics::Ring_buffer_client&          { return m_parameter_buffer_client; }

    static constexpr unsigned int s_max_mipmap_levels = 20u;

private:
    static constexpr unsigned int flag_source_input      = 0x01u;
    static constexpr unsigned int flag_source_downsample = 0x02u;
    static constexpr unsigned int flag_source_upsample   = 0x04u;
    static constexpr unsigned int flag_first_pass        = 0x10u;
    static constexpr unsigned int flag_last_pass         = 0x20u;
    [[nodiscard]] auto make_program(
        erhe::graphics::Device& graphics_device,
        const char*             name,
        const std::string&      fs_path,
        unsigned int            flags
    ) -> erhe::graphics::Shader_stages_create_info;

    struct Shader_stages {
        erhe::graphics::Reloadable_shader_stages downsample_with_lowpass_input;
        erhe::graphics::Reloadable_shader_stages downsample_with_lowpass;
        erhe::graphics::Reloadable_shader_stages downsample;
        erhe::graphics::Reloadable_shader_stages upsample_first;
        erhe::graphics::Reloadable_shader_stages upsample;
        erhe::graphics::Reloadable_shader_stages upsample_last;
    };
    struct Pipelines {
        erhe::graphics::Lazy_render_pipeline downsample_with_lowpass_input;
        erhe::graphics::Lazy_render_pipeline downsample_with_lowpass;
        erhe::graphics::Lazy_render_pipeline downsample;
        erhe::graphics::Lazy_render_pipeline upsample_first;
        erhe::graphics::Lazy_render_pipeline upsample;
        erhe::graphics::Lazy_render_pipeline upsample_last;
    };

    static constexpr int    s_input_texture               = 0;
    static constexpr int    s_downsample_texture          = 1;
    static constexpr int    s_upsample_texture            = 2;
    static constexpr int    s_downsample_dst_texture      = 3;
    static constexpr int    s_reserved_texture_slot_count = 4;
    static constexpr size_t s_max_level_count = 20;

    App_context&                                       m_context;
    std::vector<std::shared_ptr<Post_processing_node>> m_nodes;
    std::unique_ptr<erhe::graphics::Texture_heap>      m_texture_heap;
    erhe::graphics::Fragment_outputs                   m_fragment_outputs;
    std::shared_ptr<erhe::graphics::Texture>           m_dummy_texture;
    erhe::graphics::Sampler                            m_sampler_linear;
    erhe::graphics::Sampler                            m_sampler_linear_mipmap_nearest;
    erhe::graphics::Shader_resource                    m_parameter_block;
    erhe::graphics::Bind_group_layout                  m_bind_group_layout;
    erhe::graphics::Ring_buffer_client                 m_parameter_buffer_client;
    Offsets                                            m_offsets;
    erhe::graphics::Vertex_input_state                 m_empty_vertex_input;
    std::filesystem::path                              m_shader_path;
    Shader_stages                                      m_shader_stages;
    Pipelines                                          m_pipelines;
    erhe::graphics::Gpu_timer                          m_gpu_timer;
};

}
