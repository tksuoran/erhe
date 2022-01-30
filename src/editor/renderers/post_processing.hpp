#pragma once

#include "renderers/buffer_writer.hpp"
#include "windows/imgui_window.hpp"
#include "erhe/components/component.hpp"
#include "erhe/graphics/fragment_outputs.hpp"
#include "erhe/graphics/pipeline.hpp"
#include "erhe/graphics/vertex_attribute_mappings.hpp"
#include "erhe/graphics/vertex_format.hpp"

#include <string>
#include <deque>

namespace erhe::graphics
{
    class Framebuffer;
    class OpenGL_state_tracker;
    class Shader_resource;
    class Shader_stages;
    class Texture;
}

namespace editor
{

class Programs;
class Shader_monitor;

class Rendertarget
{
public:
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
    , public Imgui_window
{
public:
    static constexpr std::string_view c_name {"Post_processing"};
    static constexpr std::string_view c_title{"Post Processing"};
    static constexpr uint32_t hash = compiletime_xxhash::xxh32(c_name.data(), c_name.size(), {});

    Post_processing();

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return hash; }
    void connect             () override;
    void initialize_component() override;

    // Implements Imgui_window
    void imgui() override;

    // Public API
    void post_process(erhe::graphics::Texture* source_texture);

    void next_frame();

private:
    void downsample(
        const erhe::graphics::Texture* source_texture,
        Rendertarget&                  rendertarget,
        const auto&                    pipeline
    );
    void compose(const erhe::graphics::Texture* source_texture);
    void create_frame_resources();

    // Component dependencies
    std::shared_ptr<erhe::graphics::OpenGL_state_tracker> m_pipeline_state_tracker;
    std::shared_ptr<Programs>                             m_programs;
    std::shared_ptr<Shader_monitor>                       m_shader_monitor;

    static constexpr size_t s_frame_resources_count = 4;
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
            const size_t parameter_stride,
            const size_t parameter_count,
            const size_t slot
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
    std::deque<Frame_resources> m_frame_resources;
    size_t                      m_current_frame_resource_slot{0};
    Buffer_writer               m_parameter_writer;
    [[nodiscard]] auto current_frame_resources() -> Frame_resources&;

    std::unique_ptr<erhe::graphics::Shader_resource>    m_parameter_block;
    std::unique_ptr<erhe::graphics::Vertex_input_state> m_empty_vertex_input;
    std::unique_ptr<erhe::graphics::Shader_stages>      m_downsample_x_shader_stages;
    std::unique_ptr<erhe::graphics::Shader_stages>      m_downsample_y_shader_stages;
    std::unique_ptr<erhe::graphics::Shader_stages>      m_compose_shader_stages;
    size_t                                              m_source_texture_offset{0};
    size_t                                              m_texel_scale_offset   {0};
    size_t                                              m_texture_count_offset {0};
    size_t                                              m_reserved0_offset     {0};
    size_t                                              m_reserved1_offset     {0};

    erhe::graphics::Fragment_outputs m_fragment_outputs;
    erhe::graphics::Pipeline         m_downsample_x_pipeline;
    erhe::graphics::Pipeline         m_downsample_y_pipeline;
    erhe::graphics::Pipeline         m_compose_pipeline;

    int                       m_source_width {0};
    int                       m_source_height{0};
    std::vector<Rendertarget> m_rendertargets;
};

} // namespace editor
