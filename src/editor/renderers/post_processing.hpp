#pragma once

#include "erhe/application/renderers/buffer_writer.hpp"
#include "erhe/application/windows/imgui_window.hpp"
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

namespace erhe::application
{
    class Shader_monitor;
}

namespace erhe::graphics
{
    class Gpu_timer;
    class OpenGL_state_tracker;
    class Shader_resource;
    class Shader_stages;
    class Texture;
}

namespace editor
{

class Programs;

class Rendertarget
{
public:
    Rendertarget();
    Rendertarget(
        const std::string& label,
        const int          width,
        const int          height
    );

    void bind_framebuffer();

    std::shared_ptr<erhe::graphics::Texture>     texture;
    std::unique_ptr<erhe::graphics::Framebuffer> framebuffer;
};

class Post_processing
    : public erhe::components::Component
    , public erhe::application::Imgui_window
{
public:
    static constexpr std::string_view c_label{"Post_processing"};
    static constexpr std::string_view c_title{"Post Processing"};
    static constexpr uint32_t hash = compiletime_xxhash::xxh32(c_label.data(), c_label.size(), {});

    Post_processing();

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return hash; }
    void declare_required_components() override;
    void initialize_component       () override;
    void post_initialize            () override;

    // Implements Imgui_window
    void imgui() override;

    // Public API
    void post_process(erhe::graphics::Texture* source_texture);
    [[nodiscard]] auto get_output() -> std::shared_ptr<erhe::graphics::Texture>;

    void next_frame();

private:
    void downsample(
        const erhe::graphics::Texture*  source_texture,
        Rendertarget&                   rendertarget,
        const erhe::graphics::Pipeline& pipeline
    );
    void compose(const erhe::graphics::Texture* source_texture);
    void create_frame_resources();

    // Component dependencies
    std::shared_ptr<erhe::graphics::OpenGL_state_tracker> m_pipeline_state_tracker;
    std::shared_ptr<Programs>                             m_programs;
    std::shared_ptr<erhe::application::Shader_monitor>    m_shader_monitor;

    static constexpr std::size_t s_frame_resources_count = 4;
    struct Frame_resources
    {
        static constexpr gl::Buffer_storage_mask storage_mask{
            gl::Buffer_storage_mask::map_coherent_bit   |
            gl::Buffer_storage_mask::map_persistent_bit |
            gl::Buffer_storage_mask::map_write_bit
        };
        static constexpr gl::Map_buffer_access_mask access_mask{
            gl::Map_buffer_access_mask::map_coherent_bit   |
            gl::Map_buffer_access_mask::map_persistent_bit |
            gl::Map_buffer_access_mask::map_write_bit
        };

        Frame_resources(
            const std::size_t parameter_stride,
            const std::size_t parameter_count,
            const std::size_t slot
        )
            : parameter_buffer{
                gl::Buffer_target::uniform_buffer,
                parameter_stride * parameter_count,
                storage_mask,
                access_mask
            }
        {
            parameter_buffer.set_debug_label(
                fmt::format("Post Processing Parameter {}", slot)
            );
        }

        Frame_resources(const Frame_resources&) = delete;
        void operator= (const Frame_resources&) = delete;
        Frame_resources(Frame_resources&&)      = delete;
        void operator= (Frame_resources&&)      = delete;

        erhe::graphics::Buffer parameter_buffer;
    };
    std::deque<Frame_resources>      m_frame_resources;
    std::size_t                      m_current_frame_resource_slot{0};
    erhe::application::Buffer_writer m_parameter_writer;
    [[nodiscard]] auto current_frame_resources() -> Frame_resources&;

    erhe::graphics::Shader_resource                     m_parameter_block;
    std::unique_ptr<erhe::graphics::Vertex_input_state> m_empty_vertex_input;
    std::unique_ptr<erhe::graphics::Shader_stages>      m_downsample_x_shader_stages;
    std::unique_ptr<erhe::graphics::Shader_stages>      m_downsample_y_shader_stages;
    std::unique_ptr<erhe::graphics::Shader_stages>      m_compose_shader_stages;
    std::shared_ptr<erhe::graphics::Texture>            m_dummy_texture;
    std::size_t                                         m_source_texture_count {24};
    std::unique_ptr<erhe::graphics::Gpu_timer>          m_gpu_timer;

    class Offsets
    {
    public:
        explicit Offsets(
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

    const erhe::graphics::Shader_resource*              m_downsample_source_texture{nullptr}; // for non bindless textures
    const erhe::graphics::Shader_resource*              m_compose_source_textures{nullptr}; // for non bindless textures
    erhe::graphics::Shader_resource                     m_downsample_default_uniform_block; // containing sampler uniforms for non bindless textures
    erhe::graphics::Shader_resource                     m_compose_default_uniform_block;    // containing sampler uniforms for non bindless textures

    erhe::graphics::Fragment_outputs m_fragment_outputs;
    erhe::graphics::Pipeline         m_downsample_x_pipeline;
    erhe::graphics::Pipeline         m_downsample_y_pipeline;
    erhe::graphics::Pipeline         m_compose_pipeline;

    int                       m_source_width {0};
    int                       m_source_height{0};
    std::vector<Rendertarget> m_rendertargets;

};

} // namespace editor
