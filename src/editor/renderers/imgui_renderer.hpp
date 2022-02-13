#pragma once

#include "erhe/components/components.hpp"

#include "erhe/graphics/buffer.hpp"
#include "erhe/graphics/debug.hpp"
#include "erhe/graphics/fragment_outputs.hpp"
#include "erhe/graphics/shader_resource.hpp"
#include "erhe/graphics/vertex_attribute_mappings.hpp"
#include "erhe/graphics/vertex_format.hpp"
#include "erhe/graphics/state/color_blend_state.hpp"
#include "erhe/graphics/state/depth_stencil_state.hpp"
#include "erhe/graphics/state/input_assembly_state.hpp"
#include "erhe/graphics/state/rasterization_state.hpp"
#include "erhe/graphics/state/vertex_input_state.hpp"
#include "erhe/graphics/pipeline.hpp"
#include "erhe/toolkit/optional.hpp"

#include <imgui.h>

#include <deque>
#include <memory>
#include <string_view>
#include <vector>

namespace erhe::graphics {
    class OpenGL_state_tracker;
    class Sampler;
    class Shader_stages;
    class Texture;
    class Vertex_attribute_mappings;
}

namespace editor {

class Imgui_renderer
    : public erhe::components::Component
{
public:
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
            const size_t                               slot,
            erhe::graphics::Vertex_attribute_mappings& attribute_mappings,
            erhe::graphics::Vertex_format&             vertex_format,
            erhe::graphics::Shader_stages*             shader_stages,
            size_t          vertex_count, size_t vertex_stride,
            size_t          index_count,  size_t index_stride,
            size_t          draw_count,   size_t draw_stride
        );

        Frame_resources(const Frame_resources&) = delete;
        void operator= (const Frame_resources&) = delete;
        Frame_resources(Frame_resources&&)      = delete;
        void operator= (Frame_resources&&)      = delete;

        erhe::graphics::Buffer             vertex_buffer;
        erhe::graphics::Buffer             index_buffer;
        erhe::graphics::Buffer             draw_parameter_buffer;
        erhe::graphics::Buffer             draw_indirect_buffer;
        erhe::graphics::Vertex_input_state vertex_input;
        erhe::graphics::Pipeline           pipeline;
    };

    static constexpr std::string_view c_name{"Imgui_renderer"};
    static constexpr uint32_t hash = compiletime_xxhash::xxh32(c_name.data(), c_name.size(), {});

    Imgui_renderer();

    // Implements Component
    auto get_type_hash       () const -> uint32_t override { return hash; }
    void connect             () override;
    void initialize_component() override;

    //static constexpr size_t vec2_size   = 2 * sizeof(float);
    static constexpr size_t s_uvec2_size = 2 * sizeof(uint32_t);
    static constexpr size_t s_vec4_size  = 4 * sizeof(float);

    // scale, translation, clip rectangle, texture indices
    //static constexpr size_t draw_parameters_block_size = vec2_size + vec2_size + vec4_size + uivec4_size;
    static constexpr size_t s_max_draw_count   =   6'000;
    static constexpr size_t s_max_index_count  = 300'000;
    static constexpr size_t s_max_vertex_count = 800'000;

    // Public API
    [[nodiscard]] auto get_font_atlas() -> ImFontAtlas*;
    [[nodiscard]] auto gpu_time      () const -> double;
    void use_as_backend_renderer_on_context(ImGuiContext* imgui_context);

    void image(
        const std::shared_ptr<erhe::graphics::Texture>& texture,
        const int                                       width,
        const int                                       height,
        const glm::vec2                                 uv0        = {0.0f, 1.0f},
        const glm::vec2                                 uv1        = {1.0f, 0.0f},
        const glm::vec4                                 tint_color = {1.0f, 1.0f, 1.0f, 1.0f}
    );

    void use(
        const std::shared_ptr<erhe::graphics::Texture>& texture,
        const uint64_t                                  handle
    );
    void render_draw_data();

private:
    void create_attribute_mappings_and_vertex_format();
    void create_blocks                              ();
    void create_shader_stages                       ();
    void create_samplers                            ();
    void create_font_texture                        ();
    void create_frame_resources                     ();
    auto current_frame_resources                    () -> Frame_resources&;
    void next_frame                                 ();

    ImFontAtlas                                           m_font_atlas;
    std::shared_ptr<erhe::graphics::OpenGL_state_tracker> m_pipeline_state_tracker;
    std::shared_ptr<erhe::graphics::Texture>              m_font_texture;
    std::unique_ptr<erhe::graphics::Shader_stages>        m_shader_stages;
    std::unique_ptr<erhe::graphics::Shader_resource>      m_projection_block;
    std::unique_ptr<erhe::graphics::Shader_resource>      m_draw_parameter_block;
    erhe::graphics::Shader_resource                       m_draw_parameter_struct{"Draw_parameters"};
    erhe::graphics::Vertex_attribute_mappings             m_attribute_mappings;
    erhe::graphics::Vertex_format                         m_vertex_format;
    std::unique_ptr<erhe::graphics::Sampler>              m_nearest_sampler;
    std::unique_ptr<erhe::graphics::Sampler>              m_linear_sampler;
    std::unique_ptr<erhe::graphics::Sampler>              m_linear_mipmap_linear_sampler;
    size_t                                                m_u_scale_offset       {0};
    size_t                                                m_u_translate_offset   {0};
    size_t                                                m_u_clip_rect_offset   {0};
    size_t                                                m_u_texture_offset     {0};
    size_t                                                m_u_extra_offset       {0};
    erhe::graphics::Fragment_outputs                      m_fragment_outputs;
    std::vector<std::shared_ptr<erhe::graphics::Texture>> m_used_textures;
    std::vector<uint64_t>                                 m_used_texture_handles;

    std::unique_ptr<erhe::graphics::Gpu_timer>            m_gpu_timer;

    std::deque<Frame_resources> m_frame_resources;
    size_t                      m_current_frame_resource_slot{0};

    static constexpr size_t frame_resources_count = 4;
};

} // namespace editor

void ImGui_ImplErhe_assert_user_error(const bool condition, const char* message);