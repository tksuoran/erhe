#pragma once

#include "erhe/application/renderers/buffer_writer.hpp"
#include "erhe/application/imgui/imgui_window.hpp"
#include "erhe/application/renderers/multi_buffer.hpp"
#include "erhe/application/rendergraph/rendergraph_node.hpp"
#include "erhe/components/components.hpp"
#include "erhe/gl/enum_bit_mask_operators.hpp"
#include "erhe/graphics/buffer.hpp"
#include "erhe/graphics/fragment_outputs.hpp"
#include "erhe/graphics/framebuffer.hpp"
#include "erhe/graphics/pipeline.hpp"
#include "erhe/graphics/shader_resource.hpp"
#include "erhe/graphics/vertex_attribute_mappings.hpp"
#include "erhe/graphics/vertex_format.hpp"

#include <fmt/format.h>

#include <string>
#include <deque>

namespace erhe::graphics
{
    class Gpu_timer;
    class Sampler;
    class Shader_resource;
    class Shader_stages;
    class Texture;
}

namespace editor
{

class Post_processing_node;
class Post_processing;

class Downsample_node
{
public:
    Downsample_node();
    Downsample_node(
        const std::string& label,
        int          width,
        int          height,
        int          axis
    );

    void bind_framebuffer() const;

    std::shared_ptr<erhe::graphics::Texture>     texture;
    std::shared_ptr<erhe::graphics::Framebuffer> framebuffer;
    int                                          axis{0};
};

/// <summary>
/// Rendergraph_node for applying bloom and tonemap.
/// </summary>
class Post_processing_node
    : public erhe::application::Rendergraph_node
{
public:
    static constexpr std::string_view c_type_name{"Post_processing_node"};
    static constexpr uint32_t c_type_hash = compiletime_xxhash::xxh32(c_type_name.data(), c_type_name.size(), {});

    explicit Post_processing_node(const std::string_view name);

    // Implements Rendergraph_node
    [[nodiscard]] auto type_name() const -> std::string_view override { return c_type_name; }
    [[nodiscard]] auto type_hash() const -> uint32_t         override { return c_type_hash; }

    void execute_rendergraph_node() override;

    // Overridden to provide texture from the first downsample node
    [[nodiscard]] auto get_consumer_input_texture(
        erhe::application::Resource_routing resource_routing,
        int                                 key,
        int                                 depth = 0
    ) const -> std::shared_ptr<erhe::graphics::Texture> override;

    // Overridden to provide framebuffer from the first downsample node
    [[nodiscard]] auto get_consumer_input_framebuffer(
        erhe::application::Resource_routing resource_routing,
        int                                 key,
        int                                 depth = 0
    ) const -> std::shared_ptr<erhe::graphics::Framebuffer> override;

    // Override so that size is always sources from output
    [[nodiscard]] auto get_consumer_input_viewport(
        erhe::application::Resource_routing resource_routing,
        int                                 key,
        int                                 depth = 0
    ) const -> erhe::scene::Viewport override;

    // Public API
    void viewport_toolbar();

    // For Post_processing
    [[nodiscard]] auto get_downsample_nodes() -> const std::vector<Downsample_node>&;

private:
    auto update_downsample_nodes() -> bool;

    std::vector<Downsample_node> m_downsample_nodes;
    int                          m_width  {0};
    int                          m_height {0};
};

class Post_processing
    : public erhe::components::Component
{
public:
    static constexpr std::string_view c_type_name{"Post_processing"};
    static constexpr uint32_t c_type_hash = compiletime_xxhash::xxh32(c_type_name.data(), c_type_name.size(), {});

    Post_processing();
    ~Post_processing();

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return c_type_hash; }
    void declare_required_components() override;
    void initialize_component       () override;
    void deinitialize_component     () override;

    // Public API
    auto create_node (const std::string_view name) -> std::shared_ptr<Post_processing_node>;
    auto get_nodes   () -> const std::vector<std::shared_ptr<Post_processing_node>>&;
    void post_process(Post_processing_node& node);
    void next_frame  ();

private:
    void downsample(
        const erhe::graphics::Texture*  source_texture,
        const Downsample_node&          node,
        const erhe::graphics::Pipeline& pipeline
    );
    void compose               (Post_processing_node& node);
    void create_frame_resources();

    std::vector<std::shared_ptr<Post_processing_node>>  m_nodes;
    erhe::graphics::Sampler*                            m_linear_sampler  {nullptr};
    erhe::graphics::Sampler*                            m_nearest_sampler {nullptr};
    erhe::application::Multi_buffer                     m_parameter_buffer{"Post Processing Parameter Buffer"};
    erhe::graphics::Shader_resource                     m_parameter_block;
    std::unique_ptr<erhe::graphics::Vertex_input_state> m_empty_vertex_input;
    std::unique_ptr<erhe::graphics::Shader_stages>      m_downsample_x_shader_stages;
    std::unique_ptr<erhe::graphics::Shader_stages>      m_downsample_y_shader_stages;
    std::unique_ptr<erhe::graphics::Shader_stages>      m_compose_shader_stages;
    std::shared_ptr<erhe::graphics::Texture>            m_dummy_texture;
    std::size_t                                         m_source_texture_count{24};
    const erhe::graphics::Shader_resource*              m_downsample_source_texture{nullptr}; // for non bindless textures
    const erhe::graphics::Shader_resource*              m_compose_source_textures  {nullptr}; // for non bindless textures
    erhe::graphics::Shader_resource                     m_downsample_default_uniform_block;   // containing sampler uniforms for non bindless textures
    erhe::graphics::Shader_resource                     m_compose_default_uniform_block;      // containing sampler uniforms for non bindless textures
    erhe::graphics::Fragment_outputs                    m_fragment_outputs;
    erhe::graphics::Pipeline                            m_downsample_x_pipeline;
    erhe::graphics::Pipeline                            m_downsample_y_pipeline;
    erhe::graphics::Pipeline                            m_compose_pipeline;
    std::unique_ptr<erhe::graphics::Gpu_timer>          m_gpu_timer;

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
    Offsets m_offsets;
};

extern Post_processing* g_post_processing;

} // namespace editor
