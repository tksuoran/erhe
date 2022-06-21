#include "erhe/application/renderers/imgui_renderer.hpp"
#include "erhe/application/configuration.hpp"
#include "erhe/application/graphics/gl_context_provider.hpp"
#include "erhe/application/log.hpp"
#include "erhe/application/window.hpp"

#include "erhe/graphics/buffer.hpp"
#include "erhe/graphics/configuration.hpp"
#include "erhe/graphics/debug.hpp"
#include "erhe/graphics/fragment_outputs.hpp"
#include "erhe/graphics/pipeline.hpp"
#include "erhe/graphics/opengl_state_tracker.hpp"
#include "erhe/graphics/vertex_attribute_mappings.hpp"
#include "erhe/graphics/vertex_format.hpp"
#include "erhe/toolkit/math_util.hpp"
#include "erhe/toolkit/profile.hpp"
#include "erhe/toolkit/verify.hpp"

#include <imgui.h>

#include <deque>

namespace erhe::application {

using erhe::graphics::Texture;
using std::shared_ptr;
using std::unique_ptr;
using std::make_shared;
using std::make_unique;

namespace {

constexpr std::string_view c_vertex_shader_source =
    "\n"
    "out      vec4  v_color;\n"
    "out      vec2  v_texcoord;\n"
    "#if defined(ERHE_BINDLESS_TEXTURE)\n"
    "out flat uvec2 v_texture;\n"
    "#else\n"
    "out flat uint v_texture_id;\n"
    "#endif\n"
    "\n"
    "out gl_PerVertex {\n"
    "    vec4  gl_Position;\n"
    "    float gl_ClipDistance[4];\n"
    "};\n"
    "\n"
    "float srgb_to_linear(float x)\n"
    "{\n"
    "    if (x <= 0.04045)\n"
    "    {\n"
    "        return x / 12.92;\n"
    "    }\n"
    "    else\n"
    "    {\n"
    "        return pow((x + 0.055) / 1.055, 2.4);\n"
    "    }\n"
    "}\n"
    "\n"
    "vec3 srgb_to_linear(vec3 v)\n"
    "{\n"
    "    return vec3(\n"
    "        srgb_to_linear(v.r),\n"
    "        srgb_to_linear(v.g),\n"
    "        srgb_to_linear(v.b)\n"
    "    );\n"
    "}\n"
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
    "    v_texcoord         = a_texcoord;\n"
    "#if defined(ERHE_BINDLESS_TEXTURE)\n"
    "    v_texture          = draw.draw_parameters[gl_DrawID].u_texture;\n"
    "#else\n"
    "    v_texture_id       = draw.draw_parameters[gl_DrawID].u_texture_indices[0];\n"
    "#endif\n"
    "    v_color            = vec4(srgb_to_linear(a_color.rgb), a_color.a);\n"
    "}\n";

const std::string_view c_fragment_shader_source =
    "in      vec4  v_color;\n"
    "in      vec2  v_texcoord;\n"
    "#if defined(ERHE_BINDLESS_TEXTURE)\n"
    "in flat uvec2 v_texture;\n"
    "#else\n"
    "in flat uint v_texture_id;\n"
    "#endif\n"
    "\n"
    "void main()\n"
    "{\n"
    "#if defined(ERHE_BINDLESS_TEXTURE)\n"
    "    sampler2D s_texture = sampler2D(v_texture);\n"
    "    vec4 texture_sample = texture(s_texture, v_texcoord.st);\n"
    "#else\n"
    "    vec4 texture_sample = texture(s_textures[v_texture_id], v_texcoord.st);\n"
    "#endif\n"
    "    out_color.rgb       = v_color.rgb * texture_sample.rgb * v_color.a;\n"
    "    out_color.a         = texture_sample.a * v_color.a;\n"
    "}\n";

} // anonymous namespace

Imgui_renderer::Frame_resources::Frame_resources(
    const std::size_t                          slot,
    erhe::graphics::Vertex_attribute_mappings& attribute_mappings,
    erhe::graphics::Vertex_format&             vertex_format,
    erhe::graphics::Shader_stages*             shader_stages,
    std::size_t vertex_count, std::size_t vertex_stride,
    std::size_t index_count,  std::size_t index_stride,
    std::size_t draw_count,   std::size_t draw_stride
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
    , vertex_input{
        erhe::graphics::Vertex_input_state_data::make(
            attribute_mappings,
            vertex_format,
            &vertex_buffer,
            &index_buffer
        )
    }
    , pipeline {
        {
            .name           = "ImGui Renderer",
            .shader_stages  = shader_stages,
            .vertex_input   = &vertex_input,
            .input_assembly = erhe::graphics::Input_assembly_state::triangles,
            .rasterization  = erhe::graphics::Rasterization_state::cull_mode_none,
            .depth_stencil  = erhe::graphics::Depth_stencil_state::depth_test_disabled_stencil_test_disabled,
            .color_blend    = erhe::graphics::Color_blend_state::color_blend_premultiplied
        }
    }
{
    vertex_buffer        .set_debug_label(fmt::format("ImGui Renderer Vertex {}",         slot));
    index_buffer         .set_debug_label(fmt::format("ImGui Renderer Index {}",          slot));
    draw_parameter_buffer.set_debug_label(fmt::format("ImGui Renderer Draw Parameter {}", slot));
    draw_indirect_buffer .set_debug_label(fmt::format("ImGui Renderer Draw Indirect {}",  slot));
}

Imgui_renderer::Imgui_renderer()
    : erhe::components::Component{c_label}
{
}

void Imgui_renderer::declare_required_components()
{
    require<erhe::application::Configuration>();
    require<erhe::graphics::OpenGL_state_tracker>();
    require<Gl_context_provider>();
}

void Imgui_renderer::initialize_component()
{
    const auto& config = get<erhe::application::Configuration>();
    if (!config->imgui.enabled)
    {
        return;
    }

    const Scoped_gl_context gl_context{Component::get<Gl_context_provider>()};

    m_pipeline_state_tracker = get<erhe::graphics::OpenGL_state_tracker>();

    create_attribute_mappings_and_vertex_format();
    create_blocks                              ();
    create_shader_stages                       ();
    create_samplers                            ();
    create_frame_resources                     ();
    create_font_texture                        ();

    m_gpu_timer = std::make_unique<erhe::graphics::Gpu_timer>("Imgui_renderer");
}

void Imgui_renderer::create_attribute_mappings_and_vertex_format()
{
    using erhe::graphics::Vertex_attribute;

    m_attribute_mappings.add(
        {
            .layout_location = 0,
            .shader_type     = gl::Attribute_type::float_vec2,
            .name            = "a_position",
            .src_usage = {
                .type        = Vertex_attribute::Usage_type::position,
            }
        }
    );
    m_attribute_mappings.add(
        {
            .layout_location = 1,
            .shader_type     = gl::Attribute_type::float_vec2,
            .name            = "a_texcoord",
            .src_usage = {
                .type        = Vertex_attribute::Usage_type::tex_coord,
            }
        }
    );
    m_attribute_mappings.add(
        {
            .layout_location = 2,
            .shader_type     = gl::Attribute_type::float_vec4,
            .name            = "a_color",
            .src_usage = {
                .type        = Vertex_attribute::Usage_type::color,
            }
        }
    );

    m_vertex_format.add(
        {
            .usage = {
                .type      = Vertex_attribute::Usage_type::position
            },
            .shader_type   = gl::Attribute_type::float_vec2,
            .data_type = {
                .type      = gl::Vertex_attrib_type::float_,
                .dimension = 2
            }
        }
    );
    m_vertex_format.add(
        {
            .usage = {
                .type      = Vertex_attribute::Usage_type::tex_coord
            },
            .shader_type   = gl::Attribute_type::float_vec2,
            .data_type = {
                .type      = gl::Vertex_attrib_type::float_,
                .dimension = 2
            }
        }
    );
    m_vertex_format.add(
        {
            .usage = {
                .type       = Vertex_attribute::Usage_type::color
            },
            .shader_type    = gl::Attribute_type::float_vec4,
            .data_type = {
                .type       = gl::Vertex_attrib_type::unsigned_byte,
                .normalized = true,
                .dimension  = 4
            }
        }
    );
}

void Imgui_renderer::create_blocks()
{
    // Draw parameter struct
    m_u_clip_rect_offset = m_draw_parameter_struct.add_vec4("u_clip_rect")->offset_in_parent();
    if (erhe::graphics::Instance::info.use_bindless_texture)
    {
        m_u_texture_offset = m_draw_parameter_struct.add_uvec2("u_texture")->offset_in_parent();
        m_u_extra_offset   = m_draw_parameter_struct.add_uvec2("u_extra"  )->offset_in_parent();
    }
    else
    {
        m_u_texture_indices_offset = m_draw_parameter_struct.add_uint("u_texture_indices", 4)->offset_in_parent();
    }

    // Create uniform block for per draw data
    m_projection_block = make_unique<erhe::graphics::Shader_resource>(
        "projection", // block name
        1,            // binding point
        erhe::graphics::Shader_resource::Type::uniform_block
    );

    // Projection block
    m_u_scale_offset     = m_projection_block->add_vec2("u_scale"    )->offset_in_parent();
    m_u_translate_offset = m_projection_block->add_vec2("u_translate")->offset_in_parent();

    // Create uniform block for per draw data
    m_draw_parameter_block = make_unique<erhe::graphics::Shader_resource>(
        "draw", // block name
        0,      // binding point
        erhe::graphics::Shader_resource::Type::shader_storage_block
    );

    m_draw_parameter_block->add_struct(
        "draw_parameters",
        &m_draw_parameter_struct,
        0 /* unsized */
    );
}

void Imgui_renderer::create_shader_stages()
{
    ERHE_PROFILE_FUNCTION

    m_fragment_outputs.add("out_color", gl::Fragment_shader_output_type::float_vec4, 0);

    erhe::graphics::Shader_stages::Create_info create_info{
        .name                      = "ImGui Renderer",
        .vertex_attribute_mappings = &m_attribute_mappings,
        .fragment_outputs          = &m_fragment_outputs,
    };
    create_info.add_interface_block(m_projection_block.get());
    create_info.add_interface_block(m_draw_parameter_block.get());
    create_info.struct_types.push_back(&m_draw_parameter_struct);
    create_info.shaders.emplace_back(gl::Shader_type::vertex_shader,   c_vertex_shader_source);
    create_info.shaders.emplace_back(gl::Shader_type::fragment_shader, c_fragment_shader_source);

    if (erhe::graphics::Instance::info.use_bindless_texture)
    {
        create_info.extensions.push_back({gl::Shader_type::fragment_shader, "GL_ARB_bindless_texture"});
        create_info.defines.emplace_back("ERHE_BINDLESS_TEXTURE", "1");
    }
    else
    {
        m_default_uniform_block.add_sampler(
            "s_textures",
            gl::Uniform_type::sampler_2d,
            0,
            s_texture_unit_count
        );
        create_info.default_uniform_block = &m_default_uniform_block;
    }

    erhe::graphics::Shader_stages::Prototype prototype{create_info};
    ERHE_VERIFY(prototype.is_valid());
    m_shader_stages = make_unique<erhe::graphics::Shader_stages>(std::move(prototype));
}

void Imgui_renderer::create_samplers()
{
    m_dummy_texture = erhe::graphics::create_dummy_texture();

    m_nearest_sampler = std::make_unique<erhe::graphics::Sampler>(
        gl::Texture_min_filter::nearest,
        gl::Texture_mag_filter::nearest
    );

    m_linear_sampler = std::make_unique<erhe::graphics::Sampler>(
        gl::Texture_min_filter::linear,
        gl::Texture_mag_filter::linear
    );

    m_linear_mipmap_linear_sampler = std::make_unique<erhe::graphics::Sampler>(
        gl::Texture_min_filter::linear_mipmap_linear,
        gl::Texture_mag_filter::linear
    );
}

auto Imgui_renderer::primary_font() const -> ImFont*
{
    return m_primary_font;
}

auto Imgui_renderer::mono_font() const -> ImFont*
{
    return m_mono_font;
}

void Imgui_renderer::create_font_texture()
{
    ERHE_PROFILE_FUNCTION

    m_primary_font = m_font_atlas.AddFontFromFileTTF("res/fonts/SourceSansPro-Regular.otf", 17.0f);
    m_mono_font    = m_font_atlas.AddFontFromFileTTF("res/fonts/SourceCodePro-Semibold.otf", 17.0f);

    // Build texture atlas
    Texture::Create_info create_info{};
    unsigned char* pixels = nullptr;
    m_font_atlas.GetTexDataAsAlpha8(&pixels, &create_info.width, &create_info.height);
    create_info.internal_format = gl::Internal_format::rgba8;
    m_font_atlas.GetTexDataAsRGBA32(&pixels, &create_info.width, &create_info.height);

    const std::size_t pixel_count = static_cast<size_t>(create_info.width) * static_cast<size_t>(create_info.height);
    const std::size_t byte_count  = 4 * pixel_count;
    std::vector<uint8_t> post_processed_data;
    post_processed_data.resize(byte_count);
    const uint8_t* src = reinterpret_cast<const uint8_t*>(pixels);
    for (size_t i = 0; i < pixel_count; ++i)
    {
        const float r = static_cast<float>(*src++) / 255.0f;
        const float g = static_cast<float>(*src++) / 255.0f;
        const float b = static_cast<float>(*src++) / 255.0f;
        const float a = static_cast<float>(*src++) / 255.0f;
        const float premultiplied_r = r * a;
        const float premultiplied_g = g * a;
        const float premultiplied_b = b * a;
        post_processed_data[i * 4 + 0] = static_cast<uint8_t>(std::min(255.0f * premultiplied_r, 255.0f));
        post_processed_data[i * 4 + 1] = static_cast<uint8_t>(std::min(255.0f * premultiplied_g, 255.0f));
        post_processed_data[i * 4 + 2] = static_cast<uint8_t>(std::min(255.0f * premultiplied_b, 255.0f));
        post_processed_data[i * 4 + 3] = static_cast<uint8_t>(std::min(255.0f * a, 255.0f));;
    }

    m_font_texture = make_shared<Texture>(create_info);
    m_font_texture->set_debug_label("ImGui Font");
    const gsl::span<const std::byte> image_data{
        reinterpret_cast<const std::byte*>(post_processed_data.data()),
        byte_count
    };
    m_font_texture->upload(
        create_info.internal_format,
        image_data,
        create_info.width,
        create_info.height
    );

    // Store our handle
    const uint64_t handle = get_handle(
        *m_font_texture.get(),
        *m_linear_sampler.get()
    );
    m_font_atlas.SetTexID(handle);
}

void Imgui_renderer::create_frame_resources()
{
    ERHE_PROFILE_FUNCTION

    for (size_t slot = 0; slot < frame_resources_count; ++slot)
    {
        m_frame_resources.emplace_back(
            slot,
            m_attribute_mappings,
            m_vertex_format,
            m_shader_stages.get(),
            s_max_vertex_count, m_vertex_format.stride(),
            s_max_index_count,  sizeof(uint16_t),
            s_max_draw_count,   m_draw_parameter_block->size_bytes()
        );
    }
}

auto Imgui_renderer::current_frame_resources() -> Frame_resources&
{
    return m_frame_resources[m_current_frame_resource_slot];
}

void Imgui_renderer::next_frame()
{
    m_current_frame_resource_slot = (m_current_frame_resource_slot + 1) % frame_resources_count;
    m_used_textures.clear();
    m_used_texture_handles.clear();
}

static constexpr std::string_view c_imgui_render{"ImGui_ImplErhe_RenderDrawData()"};

void Imgui_renderer::use_as_backend_renderer_on_context(ImGuiContext* imgui_context)
{
    ImGuiIO& io = ImGui::GetIO(imgui_context);

    IM_ASSERT(io.BackendRendererUserData == NULL && "Already initialized a platform backend renderer");

    io.BackendRendererUserData = this;
    io.BackendRendererName = "erhe";
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
    io.ConfigFlags  |= ImGuiConfigFlags_NavNoCaptureKeyboard;
    io.ConfigWindowsMoveFromTitleBarOnly = true;

    auto& style = ImGui::GetStyle();
    style.WindowMenuButtonPosition = ImGuiDir_None;

    style.WindowPadding    = ImVec2{2.0f, 2.0f};
    style.FramePadding     = ImVec2{2.0f, 2.0f};
    style.CellPadding      = ImVec2{2.0f, 2.0f};
    style.ItemSpacing      = ImVec2{2.0f, 2.0f};
    style.ItemInnerSpacing = ImVec2{2.0f, 2.0f};

    style.WindowBorderSize = 0.0f;
    style.ChildBorderSize  = 0.0f;
    style.PopupBorderSize  = 0.0f;
    style.FrameBorderSize  = 0.0f;
    style.TabBorderSize    = 0.0f;

    style.WindowRounding    = 3.0f;
    style.ChildRounding     = 3.0f;
    style.FrameRounding     = 3.0f;
    style.PopupRounding     = 3.0f;
    style.ScrollbarRounding = 3.0f;
    style.GrabRounding      = 3.0f;
    style.TabRounding       = 3.0f;

    ImVec4* colors = ImGui::GetStyle().Colors;
    colors[ImGuiCol_Text]                   = ImVec4(1.00f, 1.00f, 1.00f, 0.61f);
    colors[ImGuiCol_TextDisabled]           = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
    colors[ImGuiCol_WindowBg]               = ImVec4(0.10f, 0.14f, 0.13f, 0.99f);
    colors[ImGuiCol_ChildBg]                = ImVec4(0.04f, 0.17f, 0.22f, 1.00f);
    colors[ImGuiCol_PopupBg]                = ImVec4(0.04f, 0.17f, 0.22f, 1.00f);
    colors[ImGuiCol_Border]                 = ImVec4(0.50f, 0.50f, 0.50f, 0.00f);
    colors[ImGuiCol_BorderShadow]           = ImVec4(0.50f, 0.50f, 0.50f, 0.00f);
    colors[ImGuiCol_FrameBg]                = ImVec4(0.20f, 0.26f, 0.25f, 0.54f);
    colors[ImGuiCol_FrameBgHovered]         = ImVec4(0.25f, 0.34f, 0.33f, 0.54f);
    colors[ImGuiCol_FrameBgActive]          = ImVec4(0.29f, 0.40f, 0.39f, 0.54f);
    colors[ImGuiCol_TitleBg]                = ImVec4(0.07f, 0.21f, 0.24f, 1.00f);
    colors[ImGuiCol_TitleBgActive]          = ImVec4(0.07f, 0.29f, 0.33f, 1.00f);
    colors[ImGuiCol_TitleBgCollapsed]       = ImVec4(0.04f, 0.15f, 0.17f, 1.00f);
    colors[ImGuiCol_MenuBarBg]              = ImVec4(0.04f, 0.17f, 0.22f, 1.00f);
    colors[ImGuiCol_ScrollbarBg]            = ImVec4(0.02f, 0.02f, 0.02f, 0.30f);
    colors[ImGuiCol_ScrollbarGrab]          = ImVec4(0.31f, 0.31f, 0.31f, 0.67f);
    colors[ImGuiCol_ScrollbarGrabHovered]   = ImVec4(0.31f, 0.31f, 0.31f, 0.82f);
    colors[ImGuiCol_ScrollbarGrabActive]    = ImVec4(0.31f, 0.31f, 0.31f, 1.00f);
    colors[ImGuiCol_CheckMark]              = ImVec4(0.64f, 0.83f, 0.31f, 1.00f);
    colors[ImGuiCol_SliderGrab]             = ImVec4(0.33f, 0.78f, 0.67f, 0.51f);
    colors[ImGuiCol_SliderGrabActive]       = ImVec4(0.34f, 0.90f, 0.76f, 0.51f);
    colors[ImGuiCol_Button]                 = ImVec4(0.28f, 0.30f, 0.31f, 1.00f);
    colors[ImGuiCol_ButtonHovered]          = ImVec4(0.30f, 0.36f, 0.37f, 1.00f);
    colors[ImGuiCol_ButtonActive]           = ImVec4(0.42f, 0.45f, 0.48f, 1.00f);
    colors[ImGuiCol_Header]                 = ImVec4(0.12f, 0.37f, 0.61f, 0.48f);
    colors[ImGuiCol_HeaderHovered]          = ImVec4(0.10f, 0.38f, 0.65f, 0.56f);
    colors[ImGuiCol_HeaderActive]           = ImVec4(0.10f, 0.41f, 0.71f, 0.63f);
    colors[ImGuiCol_Separator]              = ImVec4(0.07f, 0.21f, 0.24f, 1.00f);
    colors[ImGuiCol_SeparatorHovered]       = ImVec4(0.11f, 0.34f, 0.38f, 1.00f);
    colors[ImGuiCol_SeparatorActive]        = ImVec4(0.16f, 0.59f, 0.68f, 1.00f);
    colors[ImGuiCol_ResizeGrip]             = ImVec4(0.98f, 0.98f, 0.98f, 0.20f);
    colors[ImGuiCol_ResizeGripHovered]      = ImVec4(0.98f, 0.98f, 0.98f, 0.67f);
    colors[ImGuiCol_ResizeGripActive]       = ImVec4(1.00f, 0.98f, 0.98f, 0.95f);
    colors[ImGuiCol_Tab]                    = ImVec4(0.09f, 0.28f, 0.32f, 1.00f);
    colors[ImGuiCol_TabHovered]             = ImVec4(0.10f, 0.38f, 0.44f, 1.00f);
    colors[ImGuiCol_TabActive]              = ImVec4(0.09f, 0.32f, 0.37f, 1.00f);
    colors[ImGuiCol_TabUnfocused]           = ImVec4(0.09f, 0.28f, 0.32f, 1.00f);
    colors[ImGuiCol_TabUnfocusedActive]     = ImVec4(0.09f, 0.28f, 0.32f, 1.00f);
    colors[ImGuiCol_DockingPreview]         = ImVec4(0.20f, 0.59f, 0.97f, 0.70f);
    colors[ImGuiCol_DockingEmptyBg]         = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
    colors[ImGuiCol_PlotLines]              = ImVec4(0.86f, 1.00f, 0.06f, 0.62f);
    colors[ImGuiCol_PlotLinesHovered]       = ImVec4(1.00f, 0.43f, 0.35f, 1.00f);
    colors[ImGuiCol_PlotHistogram]          = ImVec4(0.90f, 0.70f, 0.00f, 1.00f);
    colors[ImGuiCol_PlotHistogramHovered]   = ImVec4(1.00f, 0.60f, 0.00f, 1.00f);
    colors[ImGuiCol_TableHeaderBg]          = ImVec4(0.09f, 0.19f, 0.22f, 1.00f);
    colors[ImGuiCol_TableBorderStrong]      = ImVec4(0.17f, 0.32f, 0.36f, 1.00f);
    colors[ImGuiCol_TableBorderLight]       = ImVec4(0.26f, 0.36f, 0.36f, 1.00f);
    colors[ImGuiCol_TableRowBg]             = ImVec4(0.00f, 0.00f, 0.00f, 0.08f);
    colors[ImGuiCol_TableRowBgAlt]          = ImVec4(0.00f, 0.00f, 0.00f, 0.12f);
    colors[ImGuiCol_TextSelectedBg]         = ImVec4(0.26f, 0.59f, 0.98f, 0.35f);
    colors[ImGuiCol_DragDropTarget]         = ImVec4(1.00f, 1.00f, 0.00f, 0.90f);
    colors[ImGuiCol_NavHighlight]           = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
    colors[ImGuiCol_NavWindowingHighlight]  = ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
    colors[ImGuiCol_NavWindowingDimBg]      = ImVec4(0.80f, 0.80f, 0.80f, 0.20f);
    colors[ImGuiCol_ModalWindowDimBg]       = ImVec4(0.80f, 0.80f, 0.80f, 0.35f);
}

auto Imgui_renderer::get_font_atlas() -> ImFontAtlas*
{
    return &m_font_atlas;
}

auto Imgui_renderer::image(
    const std::shared_ptr<erhe::graphics::Texture>& texture,
    const int                                       width,
    const int                                       height,
    const glm::vec2                                 uv0,
    const glm::vec2                                 uv1,
    const glm::vec4                                 tint_color,
    const bool                                      linear
) -> bool
{
    SPDLOG_LOGGER_TRACE(log_imgui, "Imgui_renderer::image() texture {}", texture->gl_name());
    const auto& sampler = linear ? m_linear_sampler : m_nearest_sampler;
    const uint64_t handle = erhe::graphics::get_handle(
        *texture.get(),
        *sampler.get()
    );
    ImGui::Image(
        handle,
        ImVec2{
            static_cast<float>(width),
            static_cast<float>(height)
        },
        uv0,
        uv1,
        tint_color
    );
    use(texture, handle);
    return ImGui::IsItemClicked();
}

void Imgui_renderer::use(
    const std::shared_ptr<erhe::graphics::Texture>& texture,
    const uint64_t                                  handle
)
{
    ERHE_PROFILE_FUNCTION

#if !defined(NDEBUG)
    if (!erhe::graphics::Instance::info.use_bindless_texture)
    {
        const GLuint texture_name = erhe::graphics::get_texture_from_handle(handle);
        const GLuint sampler_name = erhe::graphics::get_sampler_from_handle(handle);
        ERHE_VERIFY(texture_name != 0);
        ERHE_VERIFY(sampler_name != 0);
        ERHE_VERIFY(texture_name == texture->gl_name());
        ERHE_VERIFY(gl::is_texture(texture_name) == GL_TRUE);
        ERHE_VERIFY(gl::is_sampler(sampler_name) == GL_TRUE);
    }
#endif

    m_used_textures.insert(texture);
    m_used_texture_handles.insert(handle);
}

void Imgui_renderer::render_draw_data()
{
    SPDLOG_LOGGER_TRACE(log_frame, "begin Imgui_renderer::render_draw_data()");

    ERHE_PROFILE_FUNCTION

    // Also make font texture handle resident
    {
        const uint64_t handle = get_handle(
            *m_font_texture.get(),
            *m_linear_sampler.get()
        );
        use(m_font_texture, handle);
    }

    const ImDrawData* draw_data = ImGui::GetDrawData();
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

    erhe::graphics::Scoped_debug_group pass_scope{c_imgui_render};
    erhe::graphics::Scoped_gpu_timer   timer     {*m_gpu_timer.get()};

    const auto& frame_resources       = current_frame_resources();
    const auto& draw_parameter_buffer = frame_resources.draw_parameter_buffer;
    const auto& vertex_buffer         = frame_resources.vertex_buffer;
    const auto& index_buffer          = frame_resources.index_buffer;
    const auto& draw_indirect_buffer  = frame_resources.draw_indirect_buffer;
    const auto& pipeline              = frame_resources.pipeline;

    auto draw_parameter_gpu_data = draw_parameter_buffer.map();
    auto vertex_gpu_data         = vertex_buffer.map();
    auto index_gpu_data          = index_buffer.map();
    auto draw_indirect_gpu_data  = draw_indirect_buffer.map();

    std::size_t draw_parameter_byte_offset{0};
    std::size_t vertex_byte_offset        {0};
    std::size_t index_byte_offset         {0};
    std::size_t draw_indirect_byte_offset {0};
    std::size_t list_vertex_offset        {0};
    std::size_t list_index_offset         {0};
    std::size_t draw_indirect_count       {0};

    const float scale[2] = {
        2.0f / draw_data->DisplaySize.x,
        2.0f / draw_data->DisplaySize.y
    };
    const float translate[2] = {
        -1.0f - draw_data->DisplayPos.x * scale[0],
        -1.0f - draw_data->DisplayPos.y * scale[1]
    };

    // Projection block repeats only once
    const std::size_t start_of_projection_block = draw_parameter_byte_offset;

    // Write scale
    const gsl::span<const float> scale_cpu_data{&scale[0], 2};
    erhe::graphics::write(draw_parameter_gpu_data, draw_parameter_byte_offset, scale_cpu_data);
    draw_parameter_byte_offset += scale_cpu_data.size_bytes();

    // Write translate
    const gsl::span<const float> translate_cpu_data{&translate[0], 2};
    erhe::graphics::write(draw_parameter_gpu_data, draw_parameter_byte_offset, translate_cpu_data);
    draw_parameter_byte_offset += translate_cpu_data.size_bytes();

    while ((draw_parameter_byte_offset % erhe::graphics::Instance::implementation_defined.uniform_buffer_offset_alignment) != 0)
    {
        ++draw_parameter_byte_offset;
    }

    const std::size_t start_of_draw_parameter_block = draw_parameter_byte_offset;

    const ImVec2 clip_off   = draw_data->DisplayPos;
    const ImVec2 clip_scale = draw_data->FramebufferScale;

    if (!erhe::graphics::Instance::info.use_bindless_texture)
    {
        erhe::graphics::s_texture_unit_cache.reset();
    }

    for (int n = 0; n < draw_data->CmdListsCount; n++)
    {
        const ImDrawList* cmd_list = draw_data->CmdLists[n];

        // Upload vertex buffer
        const gsl::span<const uint8_t> vertex_cpu_data{
            reinterpret_cast<const uint8_t*>(cmd_list->VtxBuffer.begin()),
            static_cast<size_t>(cmd_list->VtxBuffer.size_in_bytes())
        };

        erhe::graphics::write(vertex_gpu_data, vertex_byte_offset, vertex_cpu_data);

        // Upload index buffer
        static_assert(sizeof(uint16_t) == sizeof(ImDrawIdx));
        const gsl::span<const uint16_t> index_cpu_data{
            cmd_list->IdxBuffer.begin(),
            static_cast<size_t>(cmd_list->IdxBuffer.size())
        };
        erhe::graphics::write(index_gpu_data, index_byte_offset, index_cpu_data);

        for (int cmd_i = 0; cmd_i < cmd_list->CmdBuffer.Size; cmd_i++)
        {
            const ImDrawCmd* pcmd = &cmd_list->CmdBuffer[cmd_i];
            if (pcmd->UserCallback != nullptr)
            {
                if (pcmd->UserCallback == ImDrawCallback_ResetRenderState)
                {
                    ERHE_FATAL("not implemented");
                }
                else
                {
                    pcmd->UserCallback(cmd_list, pcmd);
                }
            }
            else
            {
                // Project scissor/clipping rectangles into framebuffer space
                const ImVec4 clip_rect{
                    (pcmd->ClipRect.x - clip_off.x) * clip_scale.x,
                    (pcmd->ClipRect.y - clip_off.y) * clip_scale.y,
                    (pcmd->ClipRect.z - clip_off.x) * clip_scale.x,
                    (pcmd->ClipRect.w - clip_off.y) * clip_scale.y
                };

                if (
                    (clip_rect.x < fb_width)  &&
                    (clip_rect.y < fb_height) &&
                    (clip_rect.z >= 0.0f)     &&
                    (clip_rect.w >= 0.0f)
                )
                {
                    // Write clip rectangle
                    const gsl::span<const float> clip_rect_cpu_data{&clip_rect.x, 4};
                    erhe::graphics::write(draw_parameter_gpu_data, draw_parameter_byte_offset, clip_rect_cpu_data);
                    draw_parameter_byte_offset += s_vec4_size;

                    // Write texture indices
                    if (erhe::graphics::Instance::info.use_bindless_texture)
                    {
                        //const auto texture_unit = m_texture_unit_cache.allocate_texture_unit(pcmd->TextureId);
                        //ERHE_VERIFY(texture_unit.has_value());
                        const uint64_t handle = pcmd->TextureId;
                        const uint32_t texture_handle[2] =
                        {
                            static_cast<uint32_t>((handle & 0xffffffffu)),
                            static_cast<uint32_t>(handle >> 32u)
                        };
                        const gsl::span<const uint32_t> texture_handle_cpu_data{&texture_handle[0], 2};
                        erhe::graphics::write(draw_parameter_gpu_data, draw_parameter_byte_offset, texture_handle_cpu_data);
                        draw_parameter_byte_offset += s_uvec2_size;

                        const uint32_t extra[2] =
                        {
                            0u,
                            0u
                        };
                        const gsl::span<const uint32_t> extra_cpu_data{&extra[0], 2};
                        erhe::graphics::write(draw_parameter_gpu_data, draw_parameter_byte_offset, extra_cpu_data);
                        draw_parameter_byte_offset += s_uvec2_size;
                    }
                    else
                    {
                        const uint64_t handle = pcmd->TextureId;
                        const auto texture_unit_opt = erhe::graphics::s_texture_unit_cache.allocate_texture_unit(handle);
                        if (texture_unit_opt.has_value())
                        {
                            const auto texture_unit = texture_unit_opt.value();
                            const uint32_t texture_indices[4] = { static_cast<uint32_t>(texture_unit), 0, 0, 0 };
                            const gsl::span<const uint32_t> texture_indices_cpu_data{&texture_indices[0], 4};
                            erhe::graphics::write(draw_parameter_gpu_data, draw_parameter_byte_offset, texture_indices_cpu_data);
                            draw_parameter_byte_offset += s_uivec4_size;
                        }
                        else
                        {
                            const uint32_t texture_indices[4] = { 0, 0, 0, 0 };
                            const gsl::span<const uint32_t> texture_indices_cpu_data{&texture_indices[0], 4};
                            erhe::graphics::write(draw_parameter_gpu_data, draw_parameter_byte_offset, texture_indices_cpu_data);
                            draw_parameter_byte_offset += s_uivec4_size;
                        }
                    }

                    const auto draw_command = gl::Draw_elements_indirect_command{
                        pcmd->ElemCount,
                        1,
                        pcmd->IdxOffset + static_cast<uint32_t>(list_index_offset),
                        pcmd->VtxOffset + static_cast<uint32_t>(list_vertex_offset),
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

    if (draw_indirect_count > 0)
    {
        gl::enable(gl::Enable_cap::clip_distance0);
        gl::enable(gl::Enable_cap::clip_distance1);
        gl::enable(gl::Enable_cap::clip_distance2);
        gl::enable(gl::Enable_cap::clip_distance3);

        // This binds vertex input states (VAO) and shader stages (shader program)
        // and most other state
        m_pipeline_state_tracker->execute(pipeline);

        // TODO viewport states is not currently in pipeline
        gl::viewport(0, 0, static_cast<GLsizei>(fb_width), static_cast<GLsizei>(fb_height));

        if (erhe::graphics::Instance::info.use_bindless_texture)
        {
            for (const auto handle : m_used_texture_handles)
            {
                gl::make_texture_handle_resident_arb(handle);
            }
        }
        else
        {
#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_TRACE
            for (const auto& texture : m_used_textures)
            {
                SPDLOG_LOGGER_TRACE(log_imgui, ("used texture: {} {}", texture->gl_name(), texture->debug_label());
            }
            for (const auto texture_handle : m_used_texture_handles)
            {
                const GLuint texture_name = erhe::graphics::get_texture_from_handle(texture_handle);
                const GLuint sampler_name = erhe::graphics::get_sampler_from_handle(texture_handle);
                SPDLOG_LOGGER_TRACE(log_imgui, "used texture: {}", texture_name);
                SPDLOG_LOGGER_TRACE(log_imgui, "used sampler: {}", sampler_name);
            }
#endif

            const auto dummy_handle = erhe::graphics::get_handle(
                *m_dummy_texture.get(),
                *m_nearest_sampler.get()
            );

            const auto texture_unit_use_count = erhe::graphics::s_texture_unit_cache.bind(dummy_handle);

            for (size_t i = texture_unit_use_count; i < s_texture_unit_count; ++i)
            {
                gl::bind_texture_unit(static_cast<GLuint>(i), m_dummy_texture->gl_name());
                gl::bind_sampler     (static_cast<GLuint>(i), m_nearest_sampler->gl_name());
            }
        }

        ERHE_VERIFY(draw_parameter_byte_offset > 0);

        gl::bind_buffer_range(
            gl::Buffer_target::uniform_buffer,
            static_cast<GLuint>    (m_projection_block->binding_point()),
            static_cast<GLuint>    (draw_parameter_buffer.gl_name()),
            static_cast<GLintptr>  (start_of_projection_block),
            static_cast<GLsizeiptr>(draw_parameter_byte_offset)
        );

        gl::bind_buffer_range(
            gl::Buffer_target::shader_storage_buffer,
            static_cast<GLuint>    (m_draw_parameter_block->binding_point()),
            static_cast<GLuint>    (draw_parameter_buffer.gl_name()),
            static_cast<GLintptr>  (start_of_draw_parameter_block),
            static_cast<GLsizeiptr>(draw_parameter_byte_offset)
        );

        gl::bind_buffer(
            gl::Buffer_target::draw_indirect_buffer,
            static_cast<GLuint>(draw_indirect_buffer.gl_name())
        );

        gl::multi_draw_elements_indirect(
            pipeline.data.input_assembly.primitive_topology,
            gl::Draw_elements_type::unsigned_short,
            nullptr,
            static_cast<GLsizei>(draw_indirect_count),
            static_cast<GLsizei>(sizeof(gl::Draw_elements_indirect_command))
        );

        if (erhe::graphics::Instance::info.use_bindless_texture)
        {
            for (const auto handle : m_used_texture_handles)
            {
                gl::make_texture_handle_non_resident_arb(handle);
            }
        }

        next_frame();
        gl::disable(gl::Enable_cap::clip_distance0);
        gl::disable(gl::Enable_cap::clip_distance1);
        gl::disable(gl::Enable_cap::clip_distance2);
        gl::disable(gl::Enable_cap::clip_distance3);
    }

    SPDLOG_LOGGER_TRACE(log_frame, "end Imgui_renderer::render_draw_data()");
}

} // namespace erhe::application

void ImGui_ImplErhe_assert_user_error(const bool condition, const char* message)
{
    if (!condition)
    {
        erhe::application::log_imgui->error("{}", message);
    }
}

#include "imgui_internal.h"

namespace ImGui
{
    ImGuiIO& GetIO(ImGuiContext* context)
    {
        return context->IO;
    }
}
