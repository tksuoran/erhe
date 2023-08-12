#pragma once

#include "erhe/renderer/buffer_writer.hpp"
#include "erhe/graphics/buffer.hpp"
#include "erhe/graphics/fragment_outputs.hpp"
#include "erhe/graphics/pipeline.hpp"
#include "erhe/graphics/instance.hpp"
#include "erhe/graphics/shader_resource.hpp"
#include "erhe/graphics/state/color_blend_state.hpp"
#include "erhe/graphics/state/depth_stencil_state.hpp"
#include "erhe/graphics/state/input_assembly_state.hpp"
#include "erhe/graphics/state/rasterization_state.hpp"
#include "erhe/graphics/state/vertex_input_state.hpp"
#include "erhe/graphics/vertex_attribute_mappings.hpp"
#include "erhe/graphics/vertex_format.hpp"
#include "erhe/toolkit/viewport.hpp"

#include <glm/glm.hpp>
#if defined(ERHE_GUI_LIBRARY_IMGUI)
#   include <imgui/imgui.h>
#endif

#include <cstdint>
#include <deque>
#include <list>
#include <memory>
#include <vector>

namespace erhe::graphics
{
    class Buffer;
    class Shader_stages;
}

namespace erhe::scene
{
    class Camera;
    class Transform;
}

namespace erhe::renderer
{

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
    explicit Line_renderer_pipeline(erhe::graphics::Instance& graphics_instance);

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
    Line_renderer(
        erhe::graphics::Instance& graphics_instance,
        Line_renderer_pipeline&   pipeline,
        const char*               name,
        unsigned int              stencil_reference
    );

    Line_renderer (const Line_renderer&) = delete; // Due to std::deque<Frame_resources> m_frame_resources
    void operator=(const Line_renderer&) = delete; // Style must be non-copyable and non-movable.
    Line_renderer (Line_renderer&&)      = delete;
    void operator=(Line_renderer&&)      = delete;

    // Public API
    void next_frame();
    void begin     ();
    void end       ();
    void render(
        const erhe::toolkit::Viewport camera_viewport,
        const erhe::scene::Camera&  camera,
        const bool                  show_visible_lines,
        const bool                  show_hidden_lines
    );
#if defined(ERHE_GUI_LIBRARY_IMGUI)
    void imgui();
#endif

    void set_line_color(float r, float g, float b, float a);
    void set_line_color(const glm::vec3& color);
    void set_line_color(const glm::vec4& color);
#if defined(ERHE_GUI_LIBRARY_IMGUI)
    void set_line_color(const ImVec4 color);
#endif
    void set_thickness (float thickness);

    void add_lines(
        const std::initializer_list<Line> lines
    );

    void add_lines(
        const glm::mat4&                  transform,
        const std::initializer_list<Line> lines
    );

    void add_lines(
        const glm::mat4&                   transform,
        const std::initializer_list<Line4> lines
    );

    void add_lines(
        const glm::mat4                   transform,
        const glm::vec4&                  color,
        const std::initializer_list<Line> lines
    )
    {
        set_line_color(color);
        add_lines(transform, lines);
    }


    void add_lines(
        const glm::vec4&                  color,
        const std::initializer_list<Line> lines
    )
    {
        set_line_color(color);
        add_lines(lines);
    }

    void add_cube(
        const glm::mat4& transform,
        const glm::vec4& color,
        const glm::vec3& min_corner,
        const glm::vec3& max_corner,
        bool             z_cross = false
    );

    void add_bone(
        const glm::mat4& transform,
        const glm::vec4& color,
        const glm::vec3& start,
        const glm::vec3& end
    );

    void add_sphere(
        const erhe::scene::Transform& transform,
        const glm::vec4&              edge_color,
        const glm::vec4&              great_circle_color,
        float                         edge_thickness,
        float                         great_circle_thickness,
        const glm::vec3&              local_center,
        float                         local_radius,
        const erhe::scene::Transform* camera_world_from_node = nullptr,
        int                           step_count = 40
    );

    void add_cone(
        const erhe::scene::Transform& transform,
        const glm::vec4&              major_color,
        const glm::vec4&              minor_color,
        float                         major_thickness,
        float                         minor_thickness,
        const glm::vec3&              center,
        float                         height,
        float                         bottom_radius,
        float                         top_radius,
        const glm::vec3&              camera_position_in_world,
        int                           side_count
    );

    void add_torus(
        const erhe::scene::Transform& world_from_node,
        const glm::vec4&              major_color,
        const glm::vec4&              minor_color,
        float                         major_thickness,
        float                         major_radius,
        float                         minor_radius,
        const glm::vec3&              camera_position_in_world,
        int                           major_step_count,
        int                           minor_step_count,
        float                         epsilon,
        int                           debug_major,
        int                           debug_minor
    );

private:
    static constexpr std::size_t s_frame_resources_count = 4;

    class Frame_resources
    {
    public:
        Frame_resources(
            erhe::graphics::Instance&                 graphics_instance,
            unsigned int                              stencil_reference,
            bool                                      reverse_depth,
            std::size_t                               view_stride,
            std::size_t                               view_count,
            std::size_t                               vertex_count,
            erhe::graphics::Shader_stages*            shader_stages,
            erhe::graphics::Vertex_attribute_mappings attribute_mappings,
            erhe::graphics::Vertex_format&            vertex_format,
            const std::string&                        style_name,
            std::size_t                               slot
        );

        Frame_resources(const Frame_resources&) = delete;
        void operator= (const Frame_resources&) = delete;
        Frame_resources(Frame_resources&&)      = delete;
        void operator= (Frame_resources&&)      = delete;

        erhe::graphics::Buffer             vertex_buffer;
        erhe::graphics::Buffer             view_buffer;
        erhe::graphics::Vertex_input_state vertex_input;
        erhe::graphics::Pipeline           pipeline_visible;
        erhe::graphics::Pipeline           pipeline_hidden;

        [[nodiscard]] auto make_pipeline(
            bool                           reverse_depth,
            erhe::graphics::Shader_stages* shader_stages,
            bool                           visible,
            unsigned int                   stencil_reference
        ) -> erhe::graphics::Pipeline;
    };

    class Buffer_range
    {
    public:
        std::size_t first_byte_offset{0};
        std::size_t byte_count       {0};
    };

    [[nodiscard]] auto current_frame_resources() -> Frame_resources&;

    void put(
        const glm::vec3&        point,
        float                   thickness,
        const glm::vec4&        color,
        const gsl::span<float>& gpu_float_data,
        std::size_t&            word_offset
    );

    erhe::graphics::Instance&   m_graphics_instance;
    Line_renderer_pipeline&     m_pipeline;
    std::deque<Frame_resources> m_frame_resources;
    std::string                 m_name;
    Buffer_writer               m_view_writer;
    Buffer_writer               m_vertex_writer;
    std::size_t                 m_current_frame_resource_slot{0};
    std::size_t                 m_line_count                 {0};
    glm::vec4                   m_line_color                 {1.0f, 1.0f, 1.0f, 1.0f};
    float                       m_line_thickness             {1.0f};
    bool                        m_inside_begin_end           {false};
};

class Line_renderer_set
{
public:
    explicit Line_renderer_set(erhe::graphics::Instance& graphics_instance);
    ~Line_renderer_set() noexcept;

    // Public API
    void begin     ();
    void end       ();
    void next_frame();
    void render(
        const erhe::toolkit::Viewport camera_viewport,
        const erhe::scene::Camera&    camera
    );

public:
    static constexpr unsigned int s_max_stencil_reference = 4;
    std::array<std::unique_ptr<Line_renderer>, s_max_stencil_reference + 1> visible;
    std::array<std::unique_ptr<Line_renderer>, s_max_stencil_reference + 1> hidden;

private:
    erhe::graphics::Instance& m_graphics_instance;
    Line_renderer_pipeline    m_pipeline;
};

} // namespace erhe::renderer
