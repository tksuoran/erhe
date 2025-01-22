#pragma once

#include "erhe_graphics/pipeline.hpp"
#include "erhe_graphics/instance.hpp"
#include <vector>

namespace erhe::graphics {
    class Shader_stages;
}

namespace erhe::renderer {

class Line_renderer;

class Line_draw_entry
{
public:
    std::size_t first_line;
    std::size_t line_count;
};

class Line_renderer_config
{
public:
    unsigned int stencil_reference{0};
    bool         draw_visible     {true};
    bool         draw_hidden      {false};
    bool         reverse_depth    {true};
    bool         indirect         {false};
};

auto operator==(const Line_renderer_config& lhs, const Line_renderer_config& rhs) -> bool;

class Line_renderer_bucket
{
public:
    Line_renderer_bucket(Line_renderer& line_renderer, Line_renderer_config config);

    void clear       ();
    void append_lines(std::size_t first_line, std::size_t line_count);
    auto match       (const Line_renderer_config& config) const -> bool;
    void render      (
        erhe::graphics::Instance& graphics_instance,
        erhe::graphics::Buffer*   vertex_buffer,
        size_t                    vertex_buffer_offset,
        bool                      draw_hidden,
        bool                      draw_visible
    );
     
private:
    [[nodiscard]] auto make_pipeline(bool visible) -> erhe::graphics::Pipeline;

    Line_renderer&                 m_line_renderer;
    Line_renderer_config           m_config;
    erhe::graphics::Shader_stages* m_compute_shader_stages{nullptr};
    erhe::graphics::Pipeline       m_compute;
    erhe::graphics::Pipeline       m_pipeline_visible;
    erhe::graphics::Pipeline       m_pipeline_hidden;
    std::vector<Line_draw_entry>   m_draws;
};

} // namespace erhe::renderer
