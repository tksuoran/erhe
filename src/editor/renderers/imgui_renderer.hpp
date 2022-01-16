#pragma once

#include "erhe/components/component.hpp"

#include "erhe/graphics/buffer.hpp"
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

#include <imgui.h>

#include <deque>
#include <memory>
#include <optional>
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

class Texture_unit_cache
{
public:
    Texture_unit_cache(const size_t texture_unit_count);

    void create_dummy_texture();
    void reset();
    auto allocate_texture_unit(
        const std::shared_ptr<erhe::graphics::Texture>& texture
    ) -> std::optional<std::size_t>;

    void bind();

private:
    std::vector<std::shared_ptr<erhe::graphics::Texture>> m_textures;
    std::vector<unsigned int>                             m_used_textures;
    std::shared_ptr<erhe::graphics::Texture>              m_dummy_texture;
};

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
            //Imgui_renderer&                            imgui_renderer,
            const size_t                               slot,
            erhe::graphics::Vertex_attribute_mappings& attribute_mappings,
            erhe::graphics::Vertex_format&             vertex_format,
            erhe::graphics::Shader_stages*             shader_stages,
            erhe::graphics::Color_blend_state*         color_blend_state,
            size_t          vertex_count, size_t vertex_stride,
            size_t          index_count,  size_t index_stride,
            size_t          draw_count,   size_t draw_stride
        )
            : vertex_buffer{
                gl::Buffer_target::array_buffer,
                vertex_count * vertex_stride,
                storage_mask,
                access_mask
            }
            , index_buffer{
                gl::Buffer_target::element_array_buffer,
                index_count * index_stride,
                storage_mask,
                access_mask
            }
            , draw_parameter_buffer{
                gl::Buffer_target::shader_storage_buffer,
                draw_count * draw_stride,
                storage_mask,
                access_mask
            }
            , draw_indirect_buffer{
                gl::Buffer_target::draw_indirect_buffer,
                draw_count * sizeof(gl::Draw_elements_indirect_command),
                storage_mask,
                access_mask
            }
            , vertex_input_state{
                attribute_mappings,
                vertex_format,
                &vertex_buffer,
                &index_buffer
            }
            , pipeline{
                shader_stages,
                &vertex_input_state,
                &erhe::graphics::Input_assembly_state::triangles,
                &erhe::graphics::Rasterization_state::cull_mode_none,
                &erhe::graphics::Depth_stencil_state::depth_test_disabled_stencil_test_disabled,
                color_blend_state,
                nullptr
            }
        {
            vertex_buffer        .set_debug_label(fmt::format("ImGui Renderer Vertex {}",         slot));
            index_buffer         .set_debug_label(fmt::format("ImGui Renderer Index {}",          slot));
            draw_parameter_buffer.set_debug_label(fmt::format("ImGui Renderer Draw Parameter {}", slot));
            draw_indirect_buffer .set_debug_label(fmt::format("ImGui Renderer Draw Indirect {}",  slot));
        }

        Frame_resources(const Frame_resources&) = delete;
        void operator= (const Frame_resources&) = delete;
        Frame_resources(Frame_resources&&)      = delete;
        void operator= (Frame_resources&&)      = delete;

        erhe::graphics::Buffer             vertex_buffer;
        erhe::graphics::Buffer             index_buffer;
        erhe::graphics::Buffer             draw_parameter_buffer;
        erhe::graphics::Buffer             draw_indirect_buffer;
        erhe::graphics::Vertex_input_state vertex_input_state;
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
    static constexpr size_t s_uivec4_size = 4 * sizeof(uint32_t);
    static constexpr size_t s_vec4_size   = 4 * sizeof(float);

    // scale, translation, clip rectangle, texture indices
    //static constexpr size_t draw_parameters_block_size = vec2_size + vec2_size + vec4_size + uivec4_size;
    static constexpr size_t s_max_draw_count     =   6'000;
    static constexpr size_t s_max_index_count    = 300'000;
    static constexpr size_t s_max_vertex_count   = 800'000;
    static constexpr size_t s_texture_unit_count = 16;

    // Public API
    void use_as_backend_renderer_on_context(ImGuiContext* imgui_context);
    void render_draw_data();
    auto get_font_atlas  () -> ImFontAtlas*;

private:
    void create_attribute_mappings_and_vertex_format();
    void create_blocks                              ();
    void create_shader_stages                       ();
    void create_font_texture                        ();
    void create_frame_resources                     ();
    auto current_frame_resources                    () -> Frame_resources&;
    void prebind_texture_units                      ();
    void next_frame                                 ();

    ImFontAtlas                                           m_font_atlas;
    std::shared_ptr<erhe::graphics::OpenGL_state_tracker> m_pipeline_state_tracker;
    std::shared_ptr<erhe::graphics::Texture>              m_font_texture;
    std::unique_ptr<erhe::graphics::Shader_stages>        m_shader_stages;
    std::unique_ptr<erhe::graphics::Shader_resource>      m_projection_block;
    std::unique_ptr<erhe::graphics::Shader_resource>      m_draw_parameter_block;
    erhe::graphics::Shader_resource                       m_default_uniform_block; // containing sampler uniforms
    erhe::graphics::Shader_resource                       m_draw_parameter_struct{"Draw_parameters"}; // struct Draw_parameters
    const erhe::graphics::Shader_resource*                m_samplers{nullptr};
    erhe::graphics::Vertex_attribute_mappings             m_attribute_mappings;
    erhe::graphics::Vertex_format                         m_vertex_format;
    std::unique_ptr<erhe::graphics::Sampler>              m_nearest_sampler;
    std::unique_ptr<erhe::graphics::Sampler>              m_linear_sampler;
    size_t                                                m_u_scale_offset          {0};
    size_t                                                m_u_translate_offset      {0};
    size_t                                                m_u_clip_rect_offset      {0};
    size_t                                                m_u_texture_indices_offset{0};
    erhe::graphics::Fragment_outputs                      m_fragment_outputs;
    size_t                                                m_vertex_offset        {0};
    size_t                                                m_index_offset         {0};
    size_t                                                m_draw_parameter_offset{0};
    size_t                                                m_draw_indirect_offset {0};

    erhe::graphics::Color_blend_state                     m_color_blend_state
    {
        true,                                           // enabled
        {
            gl::Blend_equation_mode::func_add,          // rgb.equation_mode
            gl::Blending_factor::one,                   // rgb.source_factor
            gl::Blending_factor::one_minus_src_alpha    // rgb.destination_factor
        },
        {
            gl::Blend_equation_mode::func_add,          // alpha.equation_mode
            gl::Blending_factor::one,                   // alpha.source_factor
            gl::Blending_factor::one_minus_src_alpha    // alpha.destination_factor
        }
    };

    Texture_unit_cache          m_texture_unit_cache{s_texture_unit_count};

    std::deque<Frame_resources> m_frame_resources;
    size_t                      m_current_frame_resource_slot{0};

    static constexpr size_t frame_resources_count = 4;
};

} // namespace editor

void ImGui_ImplErhe_assert_user_error(const bool condition, const char* message);
