#include "erhe/imgui/imgui_impl_erhe.hpp"

#include "erhe/graphics/buffer.hpp"
#include "erhe/graphics/fragment_outputs.hpp"
#include "erhe/graphics/pipeline.hpp"
#include "erhe/graphics/vertex_attribute_mappings.hpp"
#include "erhe/graphics/vertex_format.hpp"
#include "erhe/toolkit/math_util.hpp"
#include "erhe/toolkit/verify.hpp"

#include <deque>

using std::shared_ptr;
using std::unique_ptr;
using std::make_shared;
using std::make_unique;

namespace {

erhe::log::Category log_imgui(erhe::log::Color::CYAN, erhe::log::Color::GRAY, erhe::log::Level::LEVEL_INFO);

constexpr std::string_view c_vertex_shader_source =
    "\n"
    "out      vec4 v_color;\n"
    "out      vec2 v_texcoord;\n"
    "out flat uint v_texture_id;\n"
    "\n"
    "out gl_PerVertex {\n"
    "    vec4  gl_Position;\n"
    "    float gl_ClipDistance[4];\n"
    "};\n"
    "\n"
    "void main()\n"
    "{\n"
    "    vec2 scale         = projection.u_scale;\n"
    "    vec2 translate     = projection.u_translate;\n"
    "    vec4 clip_rect     = draw.draw_parameters[gl_DrawID].u_clip_rect;\n"
    "    gl_Position        = vec4(a_position * scale + translate, 0, 1);\n"
    "    gl_Position.y      = -gl_Position.y;\n"
    "    gl_ClipDistance[0] = a_position.x - clip_rect[0];\n"
    "    gl_ClipDistance[1] = a_position.y - clip_rect[1];\n"
    "    gl_ClipDistance[2] = clip_rect[2] - a_position.x;\n"
    "    gl_ClipDistance[3] = clip_rect[3] - a_position.y;\n"
    "    v_texture_id       = draw.draw_parameters[gl_DrawID].u_texture_indices[0];\n"
    "    v_texcoord         = a_texcoord;\n"
    "    v_color            = a_color;\n"
    "}\n";

const std::string_view c_fragment_shader_source =
    "in vec4      v_color;\n"
    "in vec2      v_texcoord;\n"
    "in flat uint v_texture_id;\n"
    "\n"
    "void main()\n"
    "{\n"
    "    vec4 texture_sample = texture(s_textures[v_texture_id], v_texcoord.st);\n"
    "    out_color.rgb       = v_color.rgb * texture_sample.rgb;\n"
    "    out_color.a         = texture_sample.a * v_color.a;\n"
    "}\n";

class Imgui_renderer
{
public:
    //static constexpr size_t vec2_size   = 2 * sizeof(float);
    static constexpr size_t uivec4_size = 4 * sizeof(uint32_t);
    static constexpr size_t vec4_size   = 4 * sizeof(float);

    // scale, translation, clip rectangle, texture indices
    //static constexpr size_t draw_parameters_block_size = vec2_size + vec2_size + vec4_size + uivec4_size;
    static constexpr size_t max_draw_count             =   6'000;
    static constexpr size_t max_index_count            = 300'000;
    static constexpr size_t max_vertex_count           = 800'000;
    static constexpr size_t texture_unit_count         = 16;

    Imgui_renderer()
    {
    }

    void create_device_resources()
    {
        create_attribute_mappings_and_vertex_format();
        create_blocks();
        create_shader_stages();
        create_font_texture();
        prebind_texture_units();
        create_frame_resources();
    }

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

        explicit Frame_resources(Imgui_renderer& imgui_renderer)
            : vertex_buffer{
                gl::Buffer_target::array_buffer,
                Imgui_renderer::max_vertex_count * imgui_renderer.vertex_format.stride(),
                storage_mask,
                access_mask
            }
            , index_buffer{
                gl::Buffer_target::element_array_buffer,
                Imgui_renderer::max_index_count * sizeof(uint16_t),
                storage_mask,
                access_mask
            }
            , draw_parameter_buffer{
                gl::Buffer_target::shader_storage_buffer,
                Imgui_renderer::max_draw_count * imgui_renderer.draw_parameter_block->size_bytes(),
                storage_mask,
                access_mask
            }
            , draw_indirect_buffer{
                gl::Buffer_target::draw_indirect_buffer,
                Imgui_renderer::max_draw_count * sizeof(gl::Draw_elements_indirect_command),
                storage_mask,
                access_mask
            }
            , vertex_input_state{
                imgui_renderer.attribute_mappings,
                imgui_renderer.vertex_format,
                &vertex_buffer,
                &index_buffer
            }
            , pipeline{
                imgui_renderer.shader_stages.get(),
                &vertex_input_state,
                &erhe::graphics::Input_assembly_state::triangles,
                &erhe::graphics::Rasterization_state::cull_mode_none,
                &erhe::graphics::Depth_stencil_state::depth_test_disabled_stencil_test_disabled,
                &imgui_renderer.color_blend_state,
                nullptr
            }
        {
            vertex_buffer        .set_debug_label("ImGui Renderer Vertex");
            index_buffer         .set_debug_label("ImGui Renderer Index");
            draw_parameter_buffer.set_debug_label("ImGui Renderer Draw Parameter");
            draw_indirect_buffer .set_debug_label("ImGui Renderer Draw Indirect");
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

    void create_attribute_mappings_and_vertex_format()
    {
        attribute_mappings.add(gl::Attribute_type::float_vec2, "a_position", {erhe::graphics::Vertex_attribute::Usage_type::position,  0}, 0);
        attribute_mappings.add(gl::Attribute_type::float_vec2, "a_texcoord", {erhe::graphics::Vertex_attribute::Usage_type::tex_coord, 0}, 1);
        attribute_mappings.add(gl::Attribute_type::float_vec4, "a_color",    {erhe::graphics::Vertex_attribute::Usage_type::color,     0}, 2);

        vertex_format.make_attribute(
            {erhe::graphics::Vertex_attribute::Usage_type::position, 0},
            gl::Attribute_type::float_vec2,
            {gl::Vertex_attrib_type::float_, false, 2}
        );
        vertex_format.make_attribute(
            {erhe::graphics::Vertex_attribute::Usage_type::tex_coord, 0},
            gl::Attribute_type::float_vec2,
            {gl::Vertex_attrib_type::float_, false, 2}
        );
        vertex_format.make_attribute(
            {erhe::graphics::Vertex_attribute::Usage_type::color, 0},
            gl::Attribute_type::float_vec4,
            {gl::Vertex_attrib_type::unsigned_byte, true, 4}
        );
    }

    void create_blocks()
    {
        // Draw parameter struct
        u_clip_rect_offset       = draw_parameter_struct.add_vec4("u_clip_rect"         )->offset_in_parent();
        u_texture_indices_offset = draw_parameter_struct.add_uint("u_texture_indices", 4)->offset_in_parent();

        // Create uniform block for per draw data
        projection_block = make_unique<erhe::graphics::Shader_resource>(
            "projection", // block name
            1,            // binding point
            erhe::graphics::Shader_resource::Type::uniform_block
        );

        // Projection block
        u_scale_offset     = projection_block->add_vec2("u_scale"    )->offset_in_parent();
        u_translate_offset = projection_block->add_vec2("u_translate")->offset_in_parent();

        // Create uniform block for per draw data
        draw_parameter_block = make_unique<erhe::graphics::Shader_resource>(
            "draw", // block name
            0,      // binding point
            erhe::graphics::Shader_resource::Type::shader_storage_block
            );

        draw_parameter_block->add_struct(
            "draw_parameters",
            &draw_parameter_struct,
            0 /* unsized */
        );
    }

    void create_shader_stages()
    {
        fragment_outputs.add("out_color", gl::Fragment_shader_output_type::float_vec4, 0);

        // sampler2d textures[16];
        samplers = default_uniform_block.add_sampler("s_textures", gl::Uniform_type::sampler_2d, texture_unit_count);

        erhe::graphics::Shader_stages::Create_info create_info{
            "ImGui Renderer",
            &default_uniform_block,
            &attribute_mappings,
            &fragment_outputs
        };
        create_info.add_interface_block(projection_block.get());
        create_info.add_interface_block(draw_parameter_block.get());
        create_info.struct_types.push_back(&draw_parameter_struct);
        create_info.shaders.emplace_back(gl::Shader_type::vertex_shader,   c_vertex_shader_source);
        create_info.shaders.emplace_back(gl::Shader_type::fragment_shader, c_fragment_shader_source);

        erhe::graphics::Shader_stages::Prototype prototype(create_info);
        VERIFY(prototype.is_valid());
        shader_stages = make_unique<erhe::graphics::Shader_stages>(std::move(prototype));
    }

    void create_font_texture()
    {
        // Build texture atlas
        ImGuiIO& io = ImGui::GetIO();
        erhe::graphics::Texture::Create_info create_info;
        unsigned char* pixels = nullptr;
        //create_info.internal_format = gl::Internal_format::r8;
        //io.Fonts->GetTexDataAsAlpha8(&pixels, &create_info.width, &create_info.height);
        create_info.internal_format = gl::Internal_format::rgba8;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &create_info.width, &create_info.height);

        uint8_t linear_to_srgb[256];
        for (size_t i = 0; i < 256; ++i)
        {
            float linear  = static_cast<float>(i) / 255.0f;
            float srgb    = erhe::toolkit::linear_rgb_to_srgb(linear);
            float srgb_u8 = std::min(255.0f * srgb, 255.0f);
            linear_to_srgb[i] = static_cast<uint8_t>(srgb_u8);
        }

        const size_t pixel_count = static_cast<size_t>(create_info.width) * static_cast<size_t>(create_info.height);
        const size_t byte_count  = 4 * pixel_count;
        std::vector<uint8_t> post_processed_data;
        post_processed_data.resize(byte_count);
        const uint8_t* src = reinterpret_cast<const uint8_t*>(pixels);
        for (size_t i = 0; i < pixel_count; ++i)
        {
            post_processed_data[i * 4 + 0] = linear_to_srgb[*src++];
            post_processed_data[i * 4 + 1] = linear_to_srgb[*src++];
            post_processed_data[i * 4 + 2] = linear_to_srgb[*src++];
            post_processed_data[i * 4 + 3] = *src++;
        }

        font_texture = std::make_unique<erhe::graphics::Texture>(create_info);
        font_texture->set_debug_label("ImGui Font");
        //gsl::span<const std::byte> image_data{reinterpret_cast<const std::byte*>(pixels), byte_count};
        gsl::span<const std::byte> image_data{reinterpret_cast<const std::byte*>(post_processed_data.data()), byte_count};
        font_texture->upload(create_info.internal_format, image_data,
                             create_info.width, create_info.height);

        // Store our identifier
        io.Fonts->SetTexID((ImTextureID)(intptr_t)font_texture.get());
    }

    void prebind_texture_units()
    {
        Expects(shader_stages->gl_name() != 0);

        nearest_sampler = make_unique<erhe::graphics::Sampler>(
            gl::Texture_min_filter::nearest,
            gl::Texture_mag_filter::nearest
        );

        linear_sampler = make_unique<erhe::graphics::Sampler>(
            gl::Texture_min_filter::linear,
            gl::Texture_mag_filter::linear
            );

        // Point shader uniforms to texture units.
        // Also apply nearest filter, non-mipmapped sampler.
        const int location = samplers->location();
        for (int texture_unit = 0;
             texture_unit < static_cast<int>(samplers->array_size().value());
             ++texture_unit)
        {
            gl::program_uniform_1i(
                shader_stages->gl_name(),
                location + texture_unit,
                texture_unit
            );
            gl::bind_sampler(
                texture_unit,
                nearest_sampler->gl_name()
            );
        }
    }

    void create_frame_resources()
    {
        for (size_t i = 0; i < frame_resources_count; ++i)
        {
            m_frame_resources.emplace_back(*this);
        }
    }

    auto current_frame_resources() -> Frame_resources&
    {
        return m_frame_resources[m_current_frame_resource_slot];
    }

    void next_frame()
    {
        m_current_frame_resource_slot = (m_current_frame_resource_slot + 1) % frame_resources_count;
    }

    erhe::graphics::OpenGL_state_tracker* pipeline_state_tracker{nullptr};

    unique_ptr<erhe::graphics::Texture>         font_texture;
    unique_ptr<erhe::graphics::Shader_stages>   shader_stages;

    unique_ptr<erhe::graphics::Shader_resource> projection_block;
    unique_ptr<erhe::graphics::Shader_resource> draw_parameter_block;

    erhe::graphics::Shader_resource             default_uniform_block; // containing sampler uniforms
    erhe::graphics::Shader_resource             draw_parameter_struct{"Draw_parameters"}; // struct Draw_parameters
    const erhe::graphics::Shader_resource*      samplers{nullptr};
    erhe::graphics::Vertex_attribute_mappings   attribute_mappings;
    erhe::graphics::Vertex_format               vertex_format;

    unique_ptr<erhe::graphics::Sampler>         nearest_sampler;
    unique_ptr<erhe::graphics::Sampler>         linear_sampler;

    size_t                                      u_scale_offset{0};
    size_t                                      u_translate_offset{0};
    size_t                                      u_clip_rect_offset{0};
    size_t                                      u_texture_indices_offset{0};

    erhe::graphics::Fragment_outputs            fragment_outputs;
    size_t                                      vertex_offset{0};
    size_t                                      index_offset{0};
    size_t                                      draw_parameter_offset{0};
    size_t                                      draw_indirect_offset{0};

    erhe::graphics::Color_blend_state           color_blend_state
    {
        true,                                       // enabled
        {gl::Blend_equation_mode::func_add,         // rgb.equation_mode
         gl::Blending_factor::src_alpha,            // rgb.source_factor
         gl::Blending_factor::one_minus_src_alpha}, // rgb.destination_factor
        {gl::Blend_equation_mode::func_add,         // alpha.equation_mode
         gl::Blending_factor::one,                  // alpha.source_factor
         gl::Blending_factor::one_minus_src_alpha}  // alpha.destination_factor
    };

private:
    std::deque<Frame_resources> m_frame_resources;
    size_t                      m_current_frame_resource_slot{0};

    static constexpr size_t frame_resources_count = 4;
};

Imgui_renderer imgui_renderer;

class Texture_unit_cache
{
public:
    explicit Texture_unit_cache(size_t texture_unit_count)
    {
        m_textures.resize(texture_unit_count);
        reset();
    }

    void reset()
    {
        for (size_t texture_unit = 0;
             texture_unit < m_textures.size();
             ++texture_unit)
        {
            m_textures[texture_unit] = nullptr;
        }
        m_used_textures.clear();
    }

    auto allocate_texture_unit(const erhe::graphics::Texture* texture)
    -> std::optional<std::size_t>
    {
        for (size_t texture_unit = 0;
             texture_unit < m_textures.size();
             ++texture_unit)
        {
            if (m_textures[texture_unit] == texture)
            {
                return texture_unit;
            }
        }

        for (size_t texture_unit = 0; texture_unit < m_textures.size(); ++texture_unit)
        {
            if (m_textures[texture_unit] == nullptr)
            {
                m_used_textures.push_back(texture->gl_name());
                m_textures[texture_unit] = texture;
                return texture_unit;
            }
        }
        return {};
    }

    void bind()
    {
        if (m_used_textures.size() == 0)
        {
            return;
        }

        // Assign textures to texture units
        std::stringstream ss;
        bool first{true};
        for (auto texture : m_used_textures)
        {
            if (!first)
            {
                ss << ", ";
            }
            ss << texture;
        }
        gl::bind_textures(
            0,
            static_cast<GLsizei>(m_used_textures.size()),
            reinterpret_cast<const GLuint *>(m_used_textures.data())
        );
    }

private:
    std::vector<const erhe::graphics::Texture*> m_textures;
    std::vector<GLuint>                         m_used_textures;
};

static constexpr std::string_view c_imgui_render{"ImGui_ImplErhe_RenderDrawData()"};

} // anonymous namespace

// Functions
bool ImGui_ImplErhe_Init(erhe::graphics::OpenGL_state_tracker* pipeline_state_tracker)
{
    imgui_renderer.pipeline_state_tracker = pipeline_state_tracker;

    // Setup backend capabilities flags
    ImGuiIO& io = ImGui::GetIO();
    io.BackendRendererName = "imgui_impl_erhe";
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
    //io.Fonts->AddFontFromFileTTF("res\\fonts\\Ubuntu-R.ttf", 20);

    ImVec4* colors = ImGui::GetStyle().Colors;
    colors[ImGuiCol_Text]                   = ImVec4(1.00f, 1.00f, 1.00f, 0.89f);
    colors[ImGuiCol_TextDisabled]           = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
    colors[ImGuiCol_WindowBg]               = ImVec4(0.10f, 0.14f, 0.13f, 0.94f);
    colors[ImGuiCol_ChildBg]                = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_PopupBg]                = ImVec4(0.08f, 0.08f, 0.08f, 0.94f);
    colors[ImGuiCol_Border]                 = ImVec4(0.21f, 0.35f, 0.36f, 0.51f);
    colors[ImGuiCol_BorderShadow]           = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_FrameBg]                = ImVec4(0.20f, 0.26f, 0.25f, 0.54f);
    colors[ImGuiCol_FrameBgHovered]         = ImVec4(0.25f, 0.34f, 0.33f, 0.54f);
    colors[ImGuiCol_FrameBgActive]          = ImVec4(0.29f, 0.40f, 0.39f, 0.54f);
    colors[ImGuiCol_TitleBg]                = ImVec4(0.17f, 0.27f, 0.28f, 1.00f);
    colors[ImGuiCol_TitleBgActive]          = ImVec4(0.14f, 0.36f, 0.49f, 1.00f);
    colors[ImGuiCol_TitleBgCollapsed]       = ImVec4(0.00f, 0.00f, 0.00f, 0.51f);
    colors[ImGuiCol_MenuBarBg]              = ImVec4(0.14f, 0.14f, 0.14f, 1.00f);
    colors[ImGuiCol_ScrollbarBg]            = ImVec4(0.02f, 0.02f, 0.02f, 0.53f);
    colors[ImGuiCol_ScrollbarGrab]          = ImVec4(0.31f, 0.31f, 0.31f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered]   = ImVec4(0.41f, 0.41f, 0.41f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabActive]    = ImVec4(0.51f, 0.51f, 0.51f, 1.00f);
    colors[ImGuiCol_CheckMark]              = ImVec4(0.64f, 0.83f, 0.31f, 1.00f);
    colors[ImGuiCol_SliderGrab]             = ImVec4(0.33f, 0.78f, 0.67f, 0.51f);
    colors[ImGuiCol_SliderGrabActive]       = ImVec4(0.34f, 0.90f, 0.76f, 0.51f);
    colors[ImGuiCol_Button]                 = ImVec4(0.28f, 0.30f, 0.31f, 1.00f);
    colors[ImGuiCol_ButtonHovered]          = ImVec4(0.30f, 0.36f, 0.37f, 1.00f);
    colors[ImGuiCol_ButtonActive]           = ImVec4(0.42f, 0.45f, 0.48f, 1.00f);
    colors[ImGuiCol_Header]                 = ImVec4(0.65f, 0.47f, 0.33f, 0.31f);
    colors[ImGuiCol_HeaderHovered]          = ImVec4(0.65f, 0.47f, 0.33f, 0.42f);
    colors[ImGuiCol_HeaderActive]           = ImVec4(0.65f, 0.47f, 0.33f, 0.57f);
    colors[ImGuiCol_Separator]              = ImVec4(0.50f, 0.50f, 0.50f, 0.50f);
    colors[ImGuiCol_SeparatorHovered]       = ImVec4(0.75f, 0.75f, 0.75f, 0.78f);
    colors[ImGuiCol_SeparatorActive]        = ImVec4(0.75f, 0.75f, 0.75f, 1.00f);
    colors[ImGuiCol_ResizeGrip]             = ImVec4(0.98f, 0.98f, 0.98f, 0.20f);
    colors[ImGuiCol_ResizeGripHovered]      = ImVec4(0.98f, 0.98f, 0.98f, 0.67f);
    colors[ImGuiCol_ResizeGripActive]       = ImVec4(1.00f, 0.98f, 0.98f, 0.95f);
    colors[ImGuiCol_Tab]                    = ImVec4(1.00f, 1.00f, 1.00f, 0.04f);
    colors[ImGuiCol_TabHovered]             = ImVec4(1.00f, 1.00f, 1.00f, 0.14f);
    colors[ImGuiCol_TabActive]              = ImVec4(1.00f, 1.00f, 1.00f, 0.18f);
    colors[ImGuiCol_TabUnfocused]           = ImVec4(0.15f, 0.15f, 0.15f, 0.00f);
    colors[ImGuiCol_TabUnfocusedActive]     = ImVec4(0.15f, 0.15f, 0.15f, 0.00f);
    colors[ImGuiCol_DockingPreview]         = ImVec4(0.20f, 0.59f, 0.97f, 0.70f);
    colors[ImGuiCol_DockingEmptyBg]         = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
    colors[ImGuiCol_PlotLines]              = ImVec4(0.61f, 0.61f, 0.61f, 1.00f);
    colors[ImGuiCol_PlotLinesHovered]       = ImVec4(1.00f, 0.43f, 0.35f, 1.00f);
    colors[ImGuiCol_PlotHistogram]          = ImVec4(0.90f, 0.70f, 0.00f, 1.00f);
    colors[ImGuiCol_PlotHistogramHovered]   = ImVec4(1.00f, 0.60f, 0.00f, 1.00f);
    colors[ImGuiCol_TableHeaderBg]          = ImVec4(0.09f, 0.19f, 0.22f, 1.00f);
    colors[ImGuiCol_TableBorderStrong]      = ImVec4(0.17f, 0.32f, 0.36f, 1.00f);
    colors[ImGuiCol_TableBorderLight]       = ImVec4(0.26f, 0.36f, 0.36f, 1.00f);
    colors[ImGuiCol_TableRowBg]             = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_TableRowBgAlt]          = ImVec4(1.00f, 1.00f, 1.00f, 0.06f);
    colors[ImGuiCol_TextSelectedBg]         = ImVec4(0.26f, 0.59f, 0.98f, 0.35f);
    colors[ImGuiCol_DragDropTarget]         = ImVec4(1.00f, 1.00f, 0.00f, 0.90f);
    colors[ImGuiCol_NavHighlight]           = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
    colors[ImGuiCol_NavWindowingHighlight]  = ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
    colors[ImGuiCol_NavWindowingDimBg]      = ImVec4(0.80f, 0.80f, 0.80f, 0.20f);
    colors[ImGuiCol_ModalWindowDimBg]       = ImVec4(0.80f, 0.80f, 0.80f, 0.35f);

    return true;
}

void ImGui_ImplErhe_Shutdown()
{
}

void ImGui_ImplErhe_NewFrame()
{
    if (!imgui_renderer.shader_stages)
    {
        imgui_renderer.create_device_resources();
    }
}

void ImGui_ImplErhe_RenderDrawData(const ImDrawData* draw_data)
{
    if (draw_data == nullptr)
    {
        return;
    }

    const int fb_width  = (int)(draw_data->DisplaySize.x * draw_data->FramebufferScale.x);
    const int fb_height = (int)(draw_data->DisplaySize.y * draw_data->FramebufferScale.y);
    if (fb_width <= 0 || fb_height <= 0)
    {
        return;
    }

    gl::push_debug_group(
        gl::Debug_source::debug_source_application,
        0,
        static_cast<GLsizei>(c_imgui_render.length()),
        c_imgui_render.data()
    );

    const auto& frame_resources       = imgui_renderer.current_frame_resources();
    const auto& draw_parameter_buffer = frame_resources.draw_parameter_buffer;
    const auto& vertex_buffer         = frame_resources.vertex_buffer;
    const auto& index_buffer          = frame_resources.index_buffer;
    const auto& draw_indirect_buffer  = frame_resources.draw_indirect_buffer;
    const auto& pipeline              = frame_resources.pipeline;

    auto draw_parameter_gpu_data = draw_parameter_buffer.map();
    auto vertex_gpu_data         = vertex_buffer.map();
    auto index_gpu_data          = index_buffer.map();
    auto draw_indirect_gpu_data  = draw_indirect_buffer.map();

    size_t draw_parameter_byte_offset{0};
    size_t vertex_byte_offset        {0};
    size_t index_byte_offset         {0};
    size_t draw_indirect_byte_offset {0};
    size_t list_vertex_offset        {0};
    size_t list_index_offset         {0};
    size_t draw_indirect_count       {0};

    const float scale[2] = { 2.0f / draw_data->DisplaySize.x,
                             2.0f / draw_data->DisplaySize.y};
    float translate[2] = { -1.0f - draw_data->DisplayPos.x * scale[0],
                           -1.0f - draw_data->DisplayPos.y * scale[1]};

    // Projection block repeats only once
    const size_t start_of_projection_block = draw_parameter_byte_offset;

    // Write scale
    gsl::span<const float> scale_cpu_data(&scale[0], 2);
    erhe::graphics::write(draw_parameter_gpu_data, draw_parameter_byte_offset, scale_cpu_data);
    draw_parameter_byte_offset += scale_cpu_data.size_bytes();

    // Write translate
    gsl::span<const float> translate_cpu_data(&translate[0], 2);
    erhe::graphics::write(draw_parameter_gpu_data, draw_parameter_byte_offset, translate_cpu_data);
    draw_parameter_byte_offset += translate_cpu_data.size_bytes();

    const size_t start_of_draw_parameter_block = draw_parameter_byte_offset;

    const ImVec2 clip_off   = draw_data->DisplayPos;
    const ImVec2 clip_scale = draw_data->FramebufferScale;
    Expects(imgui_renderer.samplers->array_size().value() <= Imgui_renderer::texture_unit_count);
    Texture_unit_cache texture_unit_cache{Imgui_renderer::texture_unit_count};

    gl::enable(gl::Enable_cap::clip_distance0);
    gl::enable(gl::Enable_cap::clip_distance1);
    gl::enable(gl::Enable_cap::clip_distance2);
    gl::enable(gl::Enable_cap::clip_distance3);
    // gl::enable(gl::Enable_cap::framebuffer_srgb);
    for (int n = 0; n < draw_data->CmdListsCount; n++)
    {
        const ImDrawList* cmd_list = draw_data->CmdLists[n];

        // Upload vertex buffer
        gsl::span<const uint8_t> vertex_cpu_data(reinterpret_cast<const uint8_t*>(cmd_list->VtxBuffer.begin()),
                                                 cmd_list->VtxBuffer.size_in_bytes());

        erhe::graphics::write(vertex_gpu_data, vertex_byte_offset, vertex_cpu_data);

        // Upload index buffer
        static_assert(sizeof(uint16_t) == sizeof(ImDrawIdx));
        gsl::span<const uint16_t> index_cpu_data(cmd_list->IdxBuffer.begin(),
                                                 cmd_list->IdxBuffer.size());
        erhe::graphics::write(index_gpu_data, index_byte_offset, index_cpu_data);

        for (int cmd_i = 0; cmd_i < cmd_list->CmdBuffer.Size; cmd_i++)
        {
            const ImDrawCmd* pcmd = &cmd_list->CmdBuffer[cmd_i];
            if (pcmd->UserCallback != nullptr)
            {
                if (pcmd->UserCallback == ImDrawCallback_ResetRenderState)
                {
                    FATAL("not implemented");
                }
                else
                {
                    pcmd->UserCallback(cmd_list, pcmd);
                }
            }
            else
            {
                // Project scissor/clipping rectangles into framebuffer space
                ImVec4 clip_rect { (pcmd->ClipRect.x - clip_off.x) * clip_scale.x,
                                   (pcmd->ClipRect.y - clip_off.y) * clip_scale.y,
                                   (pcmd->ClipRect.z - clip_off.x) * clip_scale.x,
                                   (pcmd->ClipRect.w - clip_off.y) * clip_scale.y };

                if ((clip_rect.x < fb_width)  &&
                    (clip_rect.y < fb_height) &&
                    (clip_rect.z >= 0.0f)     &&
                    (clip_rect.w >= 0.0f))
                {
                    // Write clip rectangle
                    gsl::span<const float> clip_rect_cpu_data(&clip_rect.x, 4);
                    erhe::graphics::write(draw_parameter_gpu_data, draw_parameter_byte_offset, clip_rect_cpu_data);
                    draw_parameter_byte_offset += Imgui_renderer::vec4_size;

                    // Write texture indices
                    const auto* texture = reinterpret_cast<erhe::graphics::Texture*>(pcmd->TextureId);
                    auto texture_unit = texture_unit_cache.allocate_texture_unit(texture);
                    VERIFY(texture_unit.has_value());
                    const uint32_t texture_indices[4] = { static_cast<uint32_t>(texture_unit.value()), 0, 0, 0 };
                    gsl::span<const uint32_t> texture_indices_cpu_data(&texture_indices[0], 4);
                    erhe::graphics::write(draw_parameter_gpu_data, draw_parameter_byte_offset, texture_indices_cpu_data);
                    draw_parameter_byte_offset += Imgui_renderer::uivec4_size;

                    const auto draw_command = gl::Draw_elements_indirect_command{
                        pcmd->ElemCount,
                        1,
                        pcmd->IdxOffset + static_cast<const uint32_t>(list_index_offset),
                        pcmd->VtxOffset + static_cast<const uint32_t>(list_vertex_offset),
                        0
                    };

                    erhe::graphics::write(
                        draw_indirect_gpu_data,
                        draw_indirect_byte_offset,
                        erhe::graphics::as_span(draw_command)
                    );
                    draw_indirect_byte_offset += sizeof(gl::Draw_elements_indirect_command);
                    ++draw_indirect_count;
                }
            }
        }

        vertex_byte_offset += vertex_cpu_data.size_bytes();
        list_vertex_offset += cmd_list->VtxBuffer.size();
        index_byte_offset += index_cpu_data.size_bytes();
        list_index_offset += cmd_list->IdxBuffer.size();
    }

    if (draw_indirect_count == 0)
    {
        gl::pop_debug_group();
        return;
    }

    // This binds vertex input states (VAO) and shader stages (shader program)
    // and most other state
    imgui_renderer.pipeline_state_tracker->execute(&pipeline);

    // TODO viewport states is not currently in pipeline
    gl::viewport(0, 0, (GLsizei)fb_width, (GLsizei)fb_height);

    texture_unit_cache.bind();

    VERIFY(draw_parameter_byte_offset > 0);

    gl::bind_buffer_range(
        gl::Buffer_target::uniform_buffer,
        static_cast<GLuint>    (imgui_renderer.projection_block->binding_point()),
        static_cast<GLuint>    (draw_parameter_buffer.gl_name()),
        static_cast<GLintptr>  (start_of_projection_block),
        static_cast<GLsizeiptr>(draw_parameter_byte_offset)
    );

    gl::bind_buffer_range(
        gl::Buffer_target::shader_storage_buffer,
        static_cast<GLuint>    (imgui_renderer.draw_parameter_block->binding_point()),
        static_cast<GLuint>    (draw_parameter_buffer.gl_name()),
        static_cast<GLintptr>  (start_of_draw_parameter_block),
        static_cast<GLsizeiptr>(draw_parameter_byte_offset)
    );

    gl::bind_buffer(
        gl::Buffer_target::draw_indirect_buffer,
        static_cast<GLuint>(draw_indirect_buffer.gl_name())
    );

    gl::multi_draw_elements_indirect(
        pipeline.input_assembly->primitive_topology,
        gl::Draw_elements_type::unsigned_short,
        nullptr,
        static_cast<GLsizei>(draw_indirect_count),
        static_cast<GLsizei>(sizeof(gl::Draw_elements_indirect_command))
    );


    imgui_renderer.next_frame();

    gl::disable(gl::Enable_cap::clip_distance0);
    gl::disable(gl::Enable_cap::clip_distance1);
    gl::disable(gl::Enable_cap::clip_distance2);
    gl::disable(gl::Enable_cap::clip_distance3);

    gl::pop_debug_group();
}
