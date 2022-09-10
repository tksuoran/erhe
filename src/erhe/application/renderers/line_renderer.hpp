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

#include <glm/glm.hpp>
#if defined(ERHE_GUI_LIBRARY_IMGUI)
#   include <imgui.h>
#endif

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
    class Camera;
    class Transform;
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

class Line_renderer_pipeline
{
public:
    Line_renderer_pipeline();

    void initialize(Shader_monitor* shader_monitor);

    bool                                             reverse_depth{false};
    erhe::graphics::Fragment_outputs                 fragment_outputs;
    erhe::graphics::Vertex_attribute_mappings        attribute_mappings;
    erhe::graphics::Vertex_format                    vertex_format;
    std::unique_ptr<erhe::graphics::Shader_resource> view_block;
    std::unique_ptr<erhe::graphics::Shader_stages>   shader_stages;
    std::size_t                                      clip_from_world_offset       {0};
    std::size_t                                      view_position_in_world_offset{0};
    std::size_t                                      viewport_offset              {0};
    std::size_t                                      fov_offset                   {0};
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

    void begin();
    void end();

    void render(
        erhe::graphics::OpenGL_state_tracker& pipeline_state_tracker,
        const erhe::scene::Viewport           camera_viewport,
        const erhe::scene::Camera&            camera,
        const bool                            show_visible_lines,
        const bool                            show_hidden_lines
    );

    void set_line_color(const uint32_t color);
    void set_line_color(const float r, const float g, const float b, const float a);
    void set_line_color(const glm::vec3 color);
#if defined(ERHE_GUI_LIBRARY_IMGUI)
    void set_line_color(const ImVec4 color);
#endif
    void set_thickness (const float thickness);

    void add_lines(
        const std::initializer_list<Line> lines
    );

    void add_lines(
        const glm::mat4                   transform,
        const std::initializer_list<Line> lines
    );

    void add_lines(
        const glm::mat4                    transform,
        const std::initializer_list<Line4> lines
    );

    void add_lines(
        const glm::mat4                   transform,
        const uint32_t                    color,
        const std::initializer_list<Line> lines
    )
    {
        set_line_color(color);
        add_lines(transform, lines);
    }


    void add_lines(
        const uint32_t                    color,
        const std::initializer_list<Line> lines
    )
    {
        set_line_color(color);
        add_lines(lines);
    }

    void add_cube(
        const glm::mat4 transform,
        const uint32_t  color,
        const glm::vec3 min_corner,
        const glm::vec3 max_corner,
        const bool      z_cross = false
    );

    void add_sphere(
        const glm::mat4               transform,
        uint32_t                      color,
        const glm::vec3               center,
        float                         radius,
        const erhe::scene::Transform* camera_world_from_node = nullptr
    );

private:
    static constexpr std::size_t s_frame_resources_count = 4;

    class Frame_resources
    {
    public:
        Frame_resources(
            const bool                                reverse_depth,
            const std::size_t                         view_stride,
            const std::size_t                         view_count,
            const std::size_t                         vertex_count,
            erhe::graphics::Shader_stages* const      shader_stages,
            erhe::graphics::Vertex_attribute_mappings attribute_mappings,
            erhe::graphics::Vertex_format&            vertex_format,
            const std::string&                        style_name,
            const std::size_t                         slot
        );

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
        std::size_t first_byte_offset{0};
        std::size_t byte_count       {0};
    };

    class Buffer_writer
    {
    public:
        Buffer_range range;
        std::size_t  write_offset{0};

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
    std::size_t                 m_line_count{0};
    Buffer_writer               m_view_writer;
    Buffer_writer               m_vertex_writer;
    std::size_t                 m_current_frame_resource_slot{0};
    uint32_t                    m_line_color                 {0xffffffffu};
    float                       m_line_thickness             {1.0f};
    bool                        m_inside_begin_end{false};
};

class Line_renderer_set
    : public erhe::components::Component
{
public:
    static constexpr std::string_view c_type_name{"Line_renderer_set"};
    static constexpr uint32_t c_type_hash = compiletime_xxhash::xxh32(c_type_name.data(), c_type_name.size(), {});

    Line_renderer_set ();
    ~Line_renderer_set() noexcept override;

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return c_type_hash; }
    void declare_required_components() override;
    void initialize_component       () override;
    void post_initialize            () override;

    // Public API
    void begin     ();
    void end       ();
    void next_frame();
    void render(
        const erhe::scene::Viewport camera_viewport,
        const erhe::scene::Camera&  camera
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
