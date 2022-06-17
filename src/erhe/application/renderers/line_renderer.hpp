#pragma once

#include "erhe/components/components.hpp"
#include "erhe/graphics/buffer.hpp"
#include "erhe/graphics/fragment_outputs.hpp"
#include "erhe/graphics/pipeline.hpp"
#include "erhe/graphics/shader_resource.hpp"
#include "erhe/graphics/state/color_blend_state.hpp"
#include "erhe/graphics/state/depth_stencil_state.hpp"
#include "erhe/graphics/state/input_assembly_state.hpp"
#include "erhe/graphics/state/rasterization_state.hpp"
#include "erhe/graphics/state/vertex_input_state.hpp"
#include "erhe/graphics/vertex_attribute_mappings.hpp"
#include "erhe/graphics/vertex_format.hpp"

#include <imgui.h>
#include <glm/glm.hpp>

#include <cstdint>
#include <deque>
#include <list>
#include <memory>
#include <vector>

namespace erhe::graphics
{
    class Buffer;
    class OpenGL_state_tracker;
    class Sampler;
    class Shader_stages;
}

namespace erhe::scene
{
    class ICamera;
    class Viewport;
}

namespace erhe::ui
{
    class Font;
}

namespace erhe::application
{

class Configuration;
class Shader_monitor;

class Line
{
public:
    glm::vec3 p0;
    glm::vec3 p1;
};

class Line4
{
public:
    glm::vec4 p0;
    glm::vec4 p1;
};

//class Color_line
//{
//public:
//    glm::vec4 color0;
//    glm::vec3 p0;
//    glm::vec4 color1;
//    glm::vec3 p1;
//};
class Line_renderer_pipeline
{
public:
    void initialize(Shader_monitor* shader_monitor);

    bool                                             reverse_depth{false};
    erhe::graphics::Fragment_outputs                 fragment_outputs;
    erhe::graphics::Vertex_attribute_mappings        attribute_mappings;
    erhe::graphics::Vertex_format                    vertex_format;
    std::unique_ptr<erhe::graphics::Shader_resource> view_block;
    std::unique_ptr<erhe::graphics::Shader_stages>   shader_stages;
    size_t                                           clip_from_world_offset       {0};
    size_t                                           view_position_in_world_offset{0};
    size_t                                           viewport_offset              {0};
    size_t                                           fov_offset                   {0};
};

class Line_renderer
{
public:
    explicit Line_renderer(const char* name);
    Line_renderer         (const Line_renderer&) = delete; // Due to std::deque<Frame_resources> m_frame_resources
    void operator=        (const Line_renderer&) = delete; // Style must be non-copyable and non-movable.
    Line_renderer         (Line_renderer&&)      = delete;
    void operator=        (Line_renderer&&)      = delete;

    // Public API
    void create_frame_resources(
        Line_renderer_pipeline* pipeline,
        const Configuration& configuration
    );

    void next_frame();

    void render(
        erhe::graphics::OpenGL_state_tracker& pipeline_state_tracker,
        const erhe::scene::Viewport           camera_viewport,
        const erhe::scene::ICamera&           camera,
        const bool                            show_visible_lines,
        const bool                            show_hidden_lines
    );

    void set_line_color(const uint32_t color)
    {
        m_line_color = color;
    }

    void set_line_color(const float r, const float g, const float b, const float a)
    {
        m_line_color = ImGui::ColorConvertFloat4ToU32(ImVec4{r, g, b, a});
    }

    void set_line_color(const glm::vec3 color)
    {
        m_line_color = ImGui::ColorConvertFloat4ToU32(ImVec4{color.r, color.g, color.b, 1.0f});
    }

    void set_line_color(const ImVec4 color)
    {
        m_line_color = ImGui::ColorConvertFloat4ToU32(color);
    }

    void add_lines(
        const std::initializer_list<Line> lines,
        const float                       thickness
    );

    void add_lines(
        const glm::mat4                   transform,
        const std::initializer_list<Line> lines,
        const float                       thickness
    );

    void add_lines(
        const glm::mat4                    transform,
        const std::initializer_list<Line4> lines
    );

    void add_lines(
        const glm::mat4                   transform,
        const uint32_t                    color,
        const std::initializer_list<Line> lines,
        const float                       thickness
    )
    {
        set_line_color(color);
        add_lines(transform, lines, thickness);
    }

    void add_sphere(
        const glm::mat4 transform,
        const uint32_t  color,
        const glm::vec3 center,
        const float     radius,
        const float     thickness
    );

    //void add_lines(
    //    const glm::mat4                         transform,
    //    const std::initializer_list<Color_line> color_lines,
    //    const float                             thickness = 2.0f
    //);
private:
    static constexpr size_t s_frame_resources_count = 4;

    class Frame_resources
    {
    public:
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
            const bool                                reverse_depth,
            const size_t                              view_stride,
            const size_t                              view_count,
            const size_t                              vertex_count,
            erhe::graphics::Shader_stages* const      shader_stages,
            erhe::graphics::Vertex_attribute_mappings attribute_mappings,
            erhe::graphics::Vertex_format&            vertex_format,
            const std::string&                        style_name,
            const size_t                              slot
        )
            : vertex_buffer{
                gl::Buffer_target::array_buffer,
                vertex_format.stride() * vertex_count,
                storage_mask,
                access_mask
            }
            , view_buffer{
                gl::Buffer_target::uniform_buffer,
                view_stride * view_count,
                storage_mask,
                access_mask
            }
            , vertex_input{
                erhe::graphics::Vertex_input_state_data::make(
                    attribute_mappings,
                    vertex_format,
                    &vertex_buffer,
                    nullptr
                )
            }
            , pipeline_depth_pass{
                {
                    .name           = "Line Renderer depth pass",
                    .shader_stages  = shader_stages,
                    .vertex_input   = &vertex_input,
                    .input_assembly = erhe::graphics::Input_assembly_state::lines,
                    .rasterization  = erhe::graphics::Rasterization_state::cull_mode_none,
                    .depth_stencil  = erhe::graphics::Depth_stencil_state::depth_test_enabled_stencil_test_disabled(reverse_depth),
                    .color_blend    = erhe::graphics::Color_blend_state::color_blend_premultiplied,
                }
            }
            , pipeline_depth_fail{
                {
                    .name           = "Line Renderer depth fail",
                    .shader_stages  = shader_stages,
                    .vertex_input   = &vertex_input,
                    .input_assembly = erhe::graphics::Input_assembly_state::lines,
                    .rasterization  = erhe::graphics::Rasterization_state::cull_mode_none,
                    .depth_stencil  = erhe::graphics::Depth_stencil_state::depth_test_disabled_stencil_test_disabled,
                    .color_blend    = {
                        .enabled = true,
                        .rgb = {
                            .equation_mode      = gl::Blend_equation_mode::func_add,
                            .source_factor      = gl::Blending_factor::constant_alpha,
                            .destination_factor = gl::Blending_factor::one_minus_constant_alpha
                        },
                        .alpha = {
                            .equation_mode      = gl::Blend_equation_mode::func_add,
                            .source_factor      = gl::Blending_factor::constant_alpha,
                            .destination_factor = gl::Blending_factor::one_minus_constant_alpha
                        },
                        .constant = { 0.0f, 0.0f, 0.0f, 0.1f },
                    }
                }
            }
        {
            vertex_buffer.set_debug_label(fmt::format("Line Renderer {} Vertex {}", style_name, slot));
            view_buffer  .set_debug_label(fmt::format("Line Renderer {} View {}", style_name, slot));
        }

        Frame_resources(const Frame_resources&) = delete;
        void operator= (const Frame_resources&) = delete;
        Frame_resources(Frame_resources&&)      = delete;
        void operator= (Frame_resources&&)      = delete;

        erhe::graphics::Buffer             vertex_buffer;
        erhe::graphics::Buffer             view_buffer;
        erhe::graphics::Vertex_input_state vertex_input;
        erhe::graphics::Pipeline           pipeline_depth_pass;
        erhe::graphics::Pipeline           pipeline_depth_fail;
    };

    class Buffer_range
    {
    public:
        size_t first_byte_offset{0};
        size_t byte_count       {0};
    };

    class Buffer_writer
    {
    public:
        Buffer_range range;
        size_t       write_offset{0};

        void begin()
        {
            range.first_byte_offset = write_offset;
        }

        void end()
        {
            range.byte_count = write_offset - range.first_byte_offset;
        }

        void reset()
        {
            range.first_byte_offset = 0;
            range.byte_count = 0;
            write_offset = 0;
        }
    };

    [[nodiscard]] auto current_frame_resources() -> Frame_resources&;

    void put(
        const glm::vec3            point,
        const float                thickness,
        const uint32_t             color,
        const gsl::span<float>&    gpu_float_data,
        const gsl::span<uint32_t>& gpu_uint_data,
        size_t&                    word_offset
    );

    std::deque<Frame_resources> m_frame_resources;
    std::string                 m_name;
    Line_renderer_pipeline*     m_pipeline  {nullptr};
    //bool                        m_world_space;
    size_t                      m_line_count{0};
    Buffer_writer               m_view_writer;
    Buffer_writer               m_vertex_writer;
    size_t                      m_current_frame_resource_slot{0};
    uint32_t                    m_line_color                 {0xffffffffu};
};

class Line_renderer_set
    : public erhe::components::Component
{
public:
    static constexpr std::string_view c_label{"Line_renderer_set"};
    static constexpr uint32_t hash = compiletime_xxhash::xxh32(c_label.data(), c_label.size(), {});

    Line_renderer_set ();
    ~Line_renderer_set() noexcept override;

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return hash; }
    void declare_required_components() override;
    void initialize_component       () override;
    void post_initialize            () override;

    // Public API
    void next_frame();
    void render(
        const erhe::scene::Viewport camera_viewport,
        const erhe::scene::ICamera& camera
    );

private:
    // Component dependencies
    std::shared_ptr<erhe::graphics::OpenGL_state_tracker> m_pipeline_state_tracker;

    Line_renderer_pipeline m_pipeline;

public:
    Line_renderer visible;
    Line_renderer hidden;
};

} // namespace erhe::application
