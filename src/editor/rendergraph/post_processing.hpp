#pragma once

#include "renderers/prepared_frame_values.hpp"

#include "erhe_graphics/bind_group_layout.hpp"
#include "erhe_graphics/enums.hpp"
#include "erhe_graphics/fragment_outputs.hpp"
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

#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace erhe::graphics       { class Command_buffer; class Gpu_timer; class Render_command_encoder; class Render_pass; class Texture; class Texture_heap; }
namespace erhe::scene_renderer { class Program_interface; }

namespace editor {

class App_context;
class Post_processing;
class Post_processing_node;
class Programs;

enum class Post_processing_texture_role : unsigned int {
    input = 0,  // the node's input (the viewport's resolved color)
    downsample, // downsample_textures[level]
    upsample    // upsample_textures[level]
};

// One texture of a Post_processing_node, resolved when the render thread
// draws it: the pyramid and its input are created and replaced by the
// render phase, so an ImGui window passes this reference instead of the
// texture (doc/editor/editor.md "Frame phases").
class Post_processing_node_texture_reference : public erhe::graphics::Texture_reference
{
public:
    Post_processing_node_texture_reference(const std::shared_ptr<Post_processing_node>& node, Post_processing_texture_role role, size_t level);
    ~Post_processing_node_texture_reference() noexcept override;

    auto get_referenced_texture() const -> const erhe::graphics::Texture* override;

private:
    // Weak: the reference may outlive the node (the ImGui renderer keeps
    // the references of the frame it drew last).
    std::weak_ptr<Post_processing_node> m_node;
    Post_processing_texture_role        m_role;
    size_t                              m_level;
};

// The pyramid layout the render phase built last (Post_processing_node::
// update_size), copied for the ImGui phase: a one-frame-late copy.
class Post_processing_pyramid_info
{
public:
    int              level0_width    {0};
    int              level0_height   {0};
    std::vector<int> level_widths    {};
    std::vector<int> level_heights   {};
    std::size_t      downsample_count{0};
    std::size_t      upsample_count  {0};
    std::string      input_label     {};
};

// The parameters of a Post_processing_node the Post Processing window edits
// (main thread, the live copy Post_processing_node::parameters). The node
// copies them into the frame being prepared
// (Post_processing_node::prepare_rendergraph_node()); the render phase
// uploads the frame's copy (update_parameters()).
class Post_processing_parameters
{
public:
    float              upsample_radius      {1.0f};
    float              tonemap_luminance_max{1.5f};
    float              tonemap_alpha        {1.0f / 1.5f};
    int                lowpass_count        {2};
    // The mix weight curve over the pyramid levels
    // (compute_post_processing_weights()).
    float              weight_c0            { 0.12f};
    float              weight_c1            { 0.05f};
    float              weight_c2            {-1.0f};
    float              weight_c3            { 2.0f};
    // Per-level mix weights the window edits, one per level of the pyramid
    // the window last saw (sized there from the published pyramid layout).
    // When the count does not match the pyramid the render phase built (empty,
    // or the pyramid was resized since), the render phase derives the weights
    // from the curve.
    std::vector<float> weights              {};
};

// The mix weight of each of level_count pyramid levels from the curve of
// parameters; out is assigned (capacity kept).
void compute_post_processing_weights(const Post_processing_parameters& parameters, std::size_t level_count, std::vector<float>& out);

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

    // Render phase, at the start of execute_rendergraph_node(), after the
    // producer of the input texture sized it for this frame: (re)builds the
    // pyramid when the input size changed, so the pyramid's textures, render
    // passes and timers are created, replaced and dropped by the render
    // phase only. Returns true when the pyramid matches a non-empty input.
    auto update_size() -> bool;
    // The pyramid matches the input texture's current size.
    [[nodiscard]] auto is_sized_for_input() const -> bool;

    // Render phase: uploads the parameters of the frame's copy
    // (get_render_parameters()) for the pyramid as built.
    void update_parameters();

    // Render phase: the copy of the parameters the prepare phase of the
    // rendered frame made (verified).
    [[nodiscard]] auto get_render_parameters() const -> const Post_processing_parameters&;

    // Any thread: copies the pyramid layout the render phase built last into
    // `out` (assign, capacity kept).
    void get_pyramid_info(Post_processing_pyramid_info& out) const;

    // The live parameters: main thread (the Post Processing window).
    Post_processing_parameters                                parameters;
    // Per-level non-mipmapped textures. downsample_textures[k] stores pyramid
    // level k+1 (pyramid starts at half resolution = level 1). upsample_textures[k]
    // stores pyramid level k. The deepest pyramid level (only written by
    // downsample, never rendered by upsample) has no upsample texture.
    std::vector<std::shared_ptr<erhe::graphics::Texture>>     downsample_textures;
    std::vector<std::shared_ptr<erhe::graphics::Texture>>     upsample_textures;
    std::vector<std::unique_ptr<erhe::graphics::Render_pass>> downsample_render_passes;
    std::vector<std::unique_ptr<erhe::graphics::Render_pass>> upsample_render_passes;
    std::vector<std::string>                                  downsample_gpu_timer_labels;
    std::vector<std::string>                                  upsample_gpu_timer_labels;
    std::vector<std::unique_ptr<erhe::graphics::Gpu_timer>>   downsample_gpu_timers;
    std::vector<std::unique_ptr<erhe::graphics::Gpu_timer>>   upsample_gpu_timers;
    std::vector<int>                                          level_widths;
    std::vector<int>                                          level_heights;
    std::vector<size_t>                                       downsample_source_levels;
    std::vector<size_t>                                       upsample_source_levels;
    int                                                       level0_width  {0};
    int                                                       level0_height {0};
    erhe::graphics::Ring_buffer_range                         parameter_buffer_range;

protected:
    // Rendergraph::prepare_frame(): copies the live parameters into the
    // frame being prepared.
    void prepare_rendergraph_node(uint64_t frame_index) override;

private:
    void publish_pyramid_info(const erhe::graphics::Texture& input_texture);

    erhe::graphics::Device&                               m_graphics_device;
    Post_processing&                                      m_post_processing;
    mutable std::mutex                                    m_pyramid_info_mutex;
    Post_processing_pyramid_info                          m_pyramid_info; // written by update_size (render phase), read by get_pyramid_info
    Prepared_frame_values<Post_processing_parameters>     m_frame_parameters; // M2 copies of `parameters`, read by the render phase
    std::vector<float>                                    m_render_weights;   // render phase: the uploaded mix weights
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

    // Fullscreen 1:1 copy of input_texture into the currently-bound render pass.
    // Used by the post-processing overlay pass to seed its (multisampled) color
    // buffer with the post-processed image before tool / rendertarget overlay
    // meshes are drawn on top. See issue #230.
    void composite_input(
        erhe::graphics::Render_command_encoder& encoder,
        const erhe::graphics::Render_pass&      render_pass,
        const erhe::graphics::Texture&          input_texture
    );

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
        erhe::graphics::Base_render_pipeline downsample_with_lowpass_input;
        erhe::graphics::Base_render_pipeline downsample_with_lowpass;
        erhe::graphics::Base_render_pipeline downsample;
        erhe::graphics::Base_render_pipeline upsample_first;
        erhe::graphics::Base_render_pipeline upsample;
        erhe::graphics::Base_render_pipeline upsample_last;
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
    std::filesystem::path                              m_shader_path;
    Shader_stages                                      m_shader_stages;
    Pipelines                                          m_pipelines;

    // Overlay composite (issue #230): a minimal fullscreen 1:1 texture copy
    // (its own one-sampler bind group layout + shader + pipeline) used to seed
    // the overlay color buffer with the post-processed image.
    erhe::graphics::Bind_group_layout                  m_overlay_copy_bind_group_layout;
    erhe::graphics::Reloadable_shader_stages           m_overlay_copy_shader;
    erhe::graphics::Base_render_pipeline               m_overlay_copy_pipeline;
    // TODO Re-add per-pass GPU timers; downsample/upsample render passes
    // are stored on Bloom_node and could each carry their own Gpu_timer.
};

}
