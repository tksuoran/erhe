// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "erhe_imgui/imgui_renderer.hpp"
#include "erhe_imgui/imgui_log.hpp"
#include "erhe_imgui/imgui_viewport.hpp"

#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics/debug.hpp"
#include "erhe_graphics/instance.hpp"
#include "erhe_graphics/opengl_state_tracker.hpp"
#include "erhe_graphics/pipeline.hpp"
#include "erhe_graphics/vertex_attribute_mappings.hpp"
#include "erhe_graphics/vertex_format.hpp"
#include "erhe_graphics/vertex_format.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_verify/verify.hpp"

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>

namespace erhe::imgui {

using igl::ITexture;
using std::shared_ptr;
using std::unique_ptr;
using std::make_shared;
using std::make_unique;

namespace {

constexpr std::string_view c_vertex_shader_source = R"NUL(
out vec4 v_color;
out vec2 v_texcoord;

#if defined(ERHE_BINDLESS_TEXTURE)
out flat uvec2 v_texture;
#else
out flat uint v_texture_id;
#endif

out gl_PerVertex {
    vec4  gl_Position;
    float gl_ClipDistance[4];
};

float srgb_to_linear(float x)
{
    if (x <= 0.04045) {
        return x / 12.92;
    } else {
        return pow((x + 0.055) / 1.055, 2.4);
    }
}

vec3 srgb_to_linear(vec3 v)
{
    return vec3(
        srgb_to_linear(v.r),
        srgb_to_linear(v.g),
        srgb_to_linear(v.b)
    );
}

void main()
{
    vec2 scale         = draw.scale.xy;
    vec2 translate     = draw.translate.xy;
    vec4 clip_rect     = draw.draw_parameters[gl_DrawID].clip_rect;
    gl_Position        = vec4(a_position * scale + translate, 0, 1);
    gl_Position.y      = -gl_Position.y;
    gl_ClipDistance[0] = a_position.x - clip_rect[0];
    gl_ClipDistance[1] = a_position.y - clip_rect[1];
    gl_ClipDistance[2] = clip_rect[2] - a_position.x;
    gl_ClipDistance[3] = clip_rect[3] - a_position.y;
    v_texcoord         = a_texcoord;
#if defined(ERHE_BINDLESS_TEXTURE)
    v_texture          = draw.draw_parameters[gl_DrawID].texture;
#else
    v_texture_id       = draw.draw_parameters[gl_DrawID].texture_indices.x;
#endif
    v_color            = vec4(srgb_to_linear(a_color.rgb), a_color.a);
}
)NUL";

const std::string_view c_fragment_shader_source = R"NUL(
in vec4 v_color;
in vec2 v_texcoord;

#if defined(ERHE_BINDLESS_TEXTURE)
in flat uvec2 v_texture;
#else
in flat uint v_texture_id;
#endif

void main()
{
#if defined(ERHE_BINDLESS_TEXTURE)
    sampler2D s_texture = sampler2D(v_texture);
    vec4 texture_sample = texture(s_texture, v_texcoord.st);
#else
    vec4 texture_sample = texture(s_textures[v_texture_id], v_texcoord.st);
#endif
    out_color.rgb       = v_color.rgb * texture_sample.rgb * v_color.a;
    out_color.a         = texture_sample.a * v_color.a;
}
)NUL";

} // anonymous namespace

#pragma region Multi_pipeline
Multi_pipeline::Multi_pipeline(const std::string_view name)
    : m_name{name}
{
}

void Multi_pipeline::next_frame()
{
    m_current_slot = (m_current_slot + 1) % s_frame_resources_count;

    SPDLOG_LOGGER_TRACE(
        log_multi_buffer,
        "{} next_frame() - current slot is now {}",
        m_name,
        m_current_slot
    );
}

void Multi_pipeline::allocate(
    const erhe::graphics::Vertex_attribute_mappings& attribute_mappings,
    const erhe::graphics::Vertex_format&             vertex_format,
    erhe::graphics::Shader_stages*                   shader_stages,
    erhe::renderer::Multi_buffer&                    vertex_buffer,
    erhe::renderer::Multi_buffer&                    index_buffer
)
{
    std::vector<erhe::graphics::Buffer>& vertex_buffers = vertex_buffer.buffers();
    std::vector<erhe::graphics::Buffer>& index_buffers  = index_buffer .buffers();

    for (std::size_t slot = 0; slot < s_frame_resources_count; ++slot) {
        erhe::graphics::Buffer* const a_vertex_buffer = &vertex_buffers.at(slot);
        erhe::graphics::Buffer* const a_index_buffer  = &index_buffers.at(slot);

        auto vertex_input = std::make_unique<erhe::graphics::Vertex_input_state>(
            erhe::graphics::Vertex_input_state_data::make(
                attribute_mappings,
                vertex_format,
                a_vertex_buffer,
                a_index_buffer
            )
        );
        auto* raw_vertex_input = vertex_input.get();
        m_vertex_inputs.push_back(std::move(vertex_input));
        m_pipelines.at(slot).data = erhe::graphics::Pipeline_data{
            .name           = "ImGui Renderer",
            .shader_stages  = shader_stages,
            .vertex_input   = raw_vertex_input,
            .input_assembly = erhe::graphics::Input_assembly_state::triangles,
            .rasterization  = erhe::graphics::Rasterization_state::cull_mode_none,
            .depth_stencil  = erhe::graphics::Depth_stencil_state::depth_test_disabled_stencil_test_disabled,
            .color_blend    = erhe::graphics::Color_blend_state::color_blend_premultiplied
        };
    }
}

[[nodiscard]] auto Multi_pipeline::current_pipeline() -> erhe::graphics::Pipeline&
{
    return m_pipelines.at(m_current_slot);
}

#pragma endregion Multi_pipeline
#pragma region Imgui_program_interface
auto get_shader_extensions(erhe::graphics::Instance& graphics_instance) -> std::vector<erhe::graphics::Shader_stage_extension>
{
    std::vector<erhe::graphics::Shader_stage_extension> extensions;
    if (graphics_instance.info.gl_version < 430) {
        ERHE_VERIFY(gl::is_extension_supported(gl::Extension::Extension_GL_ARB_shader_storage_buffer_object));
        extensions.push_back({igl::ShaderStage::vertex_shader,   "GL_ARB_shader_storage_buffer_object"});
        extensions.push_back({igl::ShaderStage::fragment_shader, "GL_ARB_shader_storage_buffer_object"});
    }
    if (graphics_instance.info.gl_version < 460) {
        ERHE_VERIFY(gl::is_extension_supported(gl::Extension::Extension_GL_ARB_shader_draw_parameters));
        extensions.push_back({igl::ShaderStage::vertex_shader,   "GL_ARB_shader_draw_parameters"});
    }
    if (graphics_instance.info.use_bindless_texture) {
        extensions.push_back({igl::ShaderStage::fragment_shader, "GL_ARB_bindless_texture"});
    }
    return extensions;
}

auto get_shader_defines(erhe::graphics::Instance& graphics_instance)
-> std::vector<std::pair<std::string, std::string>>
{
    std::vector<std::pair<std::string, std::string>> defines;
    if (graphics_instance.info.gl_version < 460) {
        ERHE_VERIFY(gl::is_extension_supported(gl::Extension::Extension_GL_ARB_shader_draw_parameters));
        defines.push_back({"gl_DrawID", "gl_DrawIDARB"});
    }
    if (graphics_instance.info.use_bindless_texture) {
        defines.emplace_back("ERHE_BINDLESS_TEXTURE", "1");
    }
    return defines;
}

auto get_shader_default_uniform_block(
    erhe::graphics::Instance& graphics_instance,
    const int                 dedicated_texture_unit
) -> erhe::graphics::Shader_resource
{
    erhe::graphics::Shader_resource default_uniform_block{graphics_instance};
    if (!graphics_instance.info.use_bindless_texture) {
        default_uniform_block.add_sampler(
            "s_textures",
            igl::UniformType::sampler_2d,
            0,
            dedicated_texture_unit
        );
    }
    return default_uniform_block;
}

Imgui_program_interface::Imgui_program_interface(
    erhe::graphics::Instance& graphics_instance
)
    : draw_parameter_block{
        graphics_instance,
        "draw", // block name
        0,      // binding point
        erhe::graphics::Shader_resource::Type::shader_storage_block
    }
    , draw_parameter_struct{graphics_instance, "Draw_parameters"}
    , draw_parameter_struct_offsets{
        .clip_rect       = draw_parameter_struct.add_vec4("clip_rect"      )->offset_in_parent(),
        .texture         = graphics_instance.info.use_bindless_texture ? draw_parameter_struct.add_uvec2("texture"        )->offset_in_parent() : 0, // bindless
        .extra           = graphics_instance.info.use_bindless_texture ? draw_parameter_struct.add_uvec2("extra"          )->offset_in_parent() : 0, // bindless
        .texture_indices = graphics_instance.info.use_bindless_texture ? 0 : draw_parameter_struct.add_uvec4("texture_indices")->offset_in_parent()  // non-bindless
    }
    , block_offsets{
        .scale                       = draw_parameter_block.add_vec4  ("scale"    )->offset_in_parent(),
        .translate                   = draw_parameter_block.add_vec4  ("translate")->offset_in_parent(),
        .draw_parameter_struct_array = draw_parameter_block.add_struct("draw_parameters", &draw_parameter_struct, 0 /* unsized array*/ )->offset_in_parent()
    }
    , fragment_outputs{
        erhe::graphics::Fragment_output{
            .name     = "out_color",
            .type     = gl::Fragment_shader_output_type::float_vec4,
            .location = 0
        }
    }
    , attribute_mappings{
        graphics_instance,
        {
            erhe::graphics::Vertex_attribute_mapping{
                .layout_location = 0,
                .shader_type     = gl::Attribute_type::float_vec2,
                .name            = "a_position",
                .src_usage = {
                    .type        = erhe::graphics::Vertex_attribute::Usage_type::position,
                }
            },
            erhe::graphics::Vertex_attribute_mapping{
                .layout_location = 1,
                .shader_type     = gl::Attribute_type::float_vec2,
                .name            = "a_texcoord",
                .src_usage = {
                    .type        = erhe::graphics::Vertex_attribute::Usage_type::tex_coord,
                }
            },
            erhe::graphics::Vertex_attribute_mapping{
                .layout_location = 2,
                .shader_type     = gl::Attribute_type::float_vec4,
                .name            = "a_color",
                .src_usage = {
                    .type        = erhe::graphics::Vertex_attribute::Usage_type::color,
                }
            }
        }
    }
    , vertex_format{
        erhe::graphics::Vertex_attribute{
            .usage = {
                .type      = erhe::graphics::Vertex_attribute::Usage_type::position
            },
            .shader_type   = gl::Attribute_type::float_vec2,
            .data_type = {
                .type      = igl::VertexAttributeFormat::float_,
                .dimension = 2
            }
        },
        erhe::graphics::Vertex_attribute{
            .usage = {
                .type      = erhe::graphics::Vertex_attribute::Usage_type::tex_coord
            },
            .shader_type   = gl::Attribute_type::float_vec2,
            .data_type = {
                .type      = igl::VertexAttributeFormat::float_,
                .dimension = 2
            }
        },
        erhe::graphics::Vertex_attribute{
            .usage = {
                .type       = erhe::graphics::Vertex_attribute::Usage_type::color
            },
            .shader_type    = gl::Attribute_type::float_vec4,
            .data_type = {
                .type       = igl::VertexAttributeFormat::unsigned_byte,
                .normalized = true,
                .dimension  = 4
            }
        }
    }
    , default_uniform_block{get_shader_default_uniform_block(graphics_instance, s_texture_unit_count)}
    , shader_stages{
        erhe::graphics::Shader_stages_prototype{
            graphics_instance,
            erhe::graphics::Shader_stages_create_info{
                .name                      = "ImGui Renderer",
                .defines                   = get_shader_defines(graphics_instance),
                .extensions                = get_shader_extensions(graphics_instance),
                .struct_types              = { &draw_parameter_struct },
                .interface_blocks          = { &draw_parameter_block },
                .vertex_attribute_mappings = &attribute_mappings,
                .fragment_outputs          = &fragment_outputs,
                .default_uniform_block     = graphics_instance.info.use_bindless_texture
                    ? nullptr
                    : &default_uniform_block,
                .shaders = {
                    { igl::ShaderStage::vertex_shader,   c_vertex_shader_source },
                    { igl::ShaderStage::fragment_shader, c_fragment_shader_source }
                },
                .build = true
            }
        }
    }
    , vertex_buffer        {graphics_instance, "ImGui Vertex Buffer"}
    , index_buffer         {graphics_instance, "ImGui Index Buffer"}
    , draw_parameter_buffer{graphics_instance, "ImGui Draw Parameter Buffer"}
    , draw_indirect_buffer {graphics_instance, "ImGui Draw Indirect Buffer"}
    , pipeline             {"ImGui Pipeline"}
{
    vertex_buffer.allocate(gl::Buffer_target::array_buffer, s_max_vertex_count * vertex_format.stride());
    index_buffer.allocate(gl::Buffer_target::element_array_buffer, s_max_index_count * sizeof(uint16_t));
    draw_parameter_buffer.allocate(
        gl::Buffer_target::shader_storage_buffer,
        draw_parameter_block.binding_point(),
        block_offsets.draw_parameter_struct_array + s_max_draw_count * draw_parameter_struct.size_bytes()
    );
    draw_indirect_buffer.allocate(
        gl::Buffer_target::draw_indirect_buffer,
        s_max_draw_count * sizeof(gl::Draw_elements_indirect_command)
    );
    pipeline.allocate(
        attribute_mappings,
        vertex_format,
        &shader_stages,
        vertex_buffer,
        index_buffer
    );
}

void Imgui_program_interface::next_frame()
{
    vertex_buffer        .next_frame();
    index_buffer         .next_frame();
    draw_parameter_buffer.next_frame();
    draw_indirect_buffer .next_frame();
    pipeline             .next_frame();
}
#pragma endregion Imgui_program_interface

auto make_font_texture_create_info(
    erhe::graphics::Instance& graphics_instance,
    ImFontAtlas&              font_atlas
) -> Texture::Create_info
{
    ERHE_PROFILE_FUNCTION();

    // Build texture atlas
    Texture::Create_info create_info{
        .instance        = graphics_instance,
        .internal_format = gl::Internal_format::rgba8
    };

    unsigned char* pixels = nullptr;
    font_atlas.GetTexDataAsAlpha8(&pixels, &create_info.width, &create_info.height);
    return create_info;
}

auto get_font_atlas_pixel_data(ImFontAtlas& font_atlas) -> std::vector<uint8_t>
{
    unsigned char* pixels = nullptr;
    int width = 0;
    int height = 0;
    font_atlas.GetTexDataAsAlpha8(&pixels, &width, &height);
    const std::size_t pixel_count = static_cast<size_t>(width) * static_cast<size_t>(height);
    const std::size_t byte_count  = 4 * pixel_count;
    std::vector<uint8_t> post_processed_data;
    post_processed_data.resize(byte_count);
    const uint8_t* src = reinterpret_cast<const uint8_t*>(pixels);
    for (size_t i = 0; i < pixel_count; ++i) {
        const uint8_t a = *src++;
        post_processed_data[i * 4 + 0] = a;
        post_processed_data[i * 4 + 1] = a;
        post_processed_data[i * 4 + 2] = a;
        post_processed_data[i * 4 + 3] = a;
    }
    return post_processed_data;
}

Imgui_renderer::Imgui_renderer(
    erhe::graphics::Instance& graphics_instance,
    Imgui_settings&           settings
)
    // Parse config
    : m_graphics_instance{graphics_instance}

    // Compile shader program
    , m_imgui_program_interface{graphics_instance}

    , m_dummy_texture{graphics_instance.create_dummy_texture()}

    // Create samplers
    , m_nearest_sampler{{
        .min_filter  = gl::Texture_min_filter::nearest,
        .mag_filter  = gl::Texture_mag_filter::nearest,
        .debug_label = "Imgui_renderer nearest"
    }}
    , m_linear_sampler{{
        .min_filter  = gl::Texture_min_filter::linear,
        .mag_filter  = gl::Texture_mag_filter::linear,
        .debug_label = "Imgui_renderer linear"
    }}
    , m_linear_mipmap_linear_sampler{{
        .min_filter  = gl::Texture_min_filter::linear_mipmap_linear,
        .mag_filter  = gl::Texture_mag_filter::linear,
        .debug_label = "Imgui_renderer linear mipmap"
    }}
    , m_gpu_timer{"Imgui_renderer"}
{
    ERHE_PROFILE_FUNCTION();

    apply_font_config_changes(settings);
}

void Imgui_renderer::lock_mutex()
{
    m_mutex.lock();
}

void Imgui_renderer::unlock_mutex()
{
    m_mutex.unlock();
}

void Imgui_renderer::make_current(const Imgui_viewport* imgui_viewport)
{
    m_current_viewport = imgui_viewport;
    if (imgui_viewport != nullptr) {
        ImGui::SetCurrentContext(imgui_viewport->imgui_context());
    } else {
        ImGui::SetCurrentContext(nullptr);
    }
}

void Imgui_renderer::register_imgui_viewport(
    Imgui_viewport* viewport
)
{
    //ERHE_VERIFY(!m_iterating);
    const std::lock_guard<std::recursive_mutex> lock{m_mutex};
#if !defined(NDEBUG)
    const auto i = std::find_if(
        m_imgui_viewports.begin(),
        m_imgui_viewports.end(),
        [viewport](Imgui_viewport* entry) {
            return entry == viewport;
        }
    );
    if (i != m_imgui_viewports.end()) {
        log_imgui->error("Imgui_viewport '{}' is already registered to Imgui_windows", viewport->get_name());
        return;
    }
#endif

    m_imgui_viewports.push_back(viewport);

}

void Imgui_renderer::unregister_imgui_viewport(
    Imgui_viewport* viewport
)
{
    //ERHE_VERIFY(!m_iterating);

    const std::lock_guard<std::recursive_mutex> lock{m_mutex};
    const auto i = std::find_if(
        m_imgui_viewports.begin(),
        m_imgui_viewports.end(),
        [viewport](Imgui_viewport* entry) {
            return entry == viewport;
        }
    );
    if (i == m_imgui_viewports.end()) {
        log_imgui->error("Imgui_windows::unregister_imgui_viewport(): viewport '{}' is not registered", viewport->get_name());
        return;
    }
    m_imgui_viewports.erase(i);
}

[[nodiscard]] auto Imgui_renderer::get_imgui_viewports() const -> const std::vector<Imgui_viewport*>&
{
    return m_imgui_viewports;
}

void Imgui_renderer::on_font_config_changed(Imgui_settings& settings)
{
    at_end_of_frame(
        [this, settings](){
            apply_font_config_changes(settings);
        }
    );
}

void Imgui_renderer::apply_font_config_changes(const Imgui_settings& settings)
{
    m_font_atlas.Clear();
    m_primary_font    = m_font_atlas.AddFontFromFileTTF(settings.primary_font.c_str(), settings.font_size);
    m_mono_font       = m_font_atlas.AddFontFromFileTTF(settings.mono_font   .c_str(), settings.font_size);
    m_vr_primary_font = m_font_atlas.AddFontFromFileTTF(settings.primary_font.c_str(), settings.vr_font_size);
    m_vr_mono_font    = m_font_atlas.AddFontFromFileTTF(settings.mono_font   .c_str(), settings.vr_font_size);

    // Create textures
    m_font_texture = std::make_shared<igl::ITexture>(
        make_font_texture_create_info(m_graphics_instance, m_font_atlas)
    );

    for (auto viewport : m_imgui_viewports) {
        auto* context = viewport->get_imgui_context();
        if (context == nullptr) {
            continue;
        }
        ImGuiIO& io = context->IO;
        io.Fonts       = &m_font_atlas;
        io.FontDefault = m_primary_font;
    }

    const auto pixel_data = get_font_atlas_pixel_data(m_font_atlas);
    const std::span<const std::byte> image_data{
        reinterpret_cast<const std::byte*>(pixel_data.data()),
        pixel_data.size()
    };
    m_font_texture->upload(
        gl::Internal_format::rgba8,
        image_data,
        m_font_texture->width(),
        m_font_texture->height()
    );
    m_font_texture->set_debug_label("ImGui Font");

    // Store our handle
    const uint64_t handle = m_graphics_instance.get_handle(
        *m_font_texture.get(),
        m_linear_sampler
    );
    m_font_atlas.SetTexID(handle);
}

auto Imgui_renderer::primary_font() const -> ImFont*
{
    return m_primary_font;
}

auto Imgui_renderer::mono_font() const -> ImFont*
{
    return m_mono_font;
}

auto Imgui_renderer::vr_primary_font() const -> ImFont*
{
    return m_vr_primary_font;
}

auto Imgui_renderer::vr_mono_font() const -> ImFont*
{
    return m_vr_mono_font;
}

void Imgui_renderer::at_end_of_frame(std::function<void()>&& func)
{
    m_at_end_of_frame.push_back(std::move(func));
}

void Imgui_renderer::next_frame()
{
    for (auto& operation : m_at_end_of_frame) {
        operation();
    }
    m_at_end_of_frame.clear();

#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_TRACE
    std::stringstream ss;
    for (const auto& used_texture : m_used_textures) {
        if (used_texture) {
            ss << fmt::format("{}-{} ", used_texture->gl_name(), used_texture->debug_label());
        } else {
            ss << "<> ";
        }
    }
    SPDLOG_LOGGER_TRACE(
        log_imgui,
        "clearing {} used textures: {}",
        m_used_textures.size(),
        ss.str()
    );
#endif

    m_used_textures.clear();
    m_used_texture_handles.clear();

    m_imgui_program_interface.next_frame();
}

static constexpr std::string_view c_imgui_render{"ImGui_ImplErhe_RenderDrawData()"};

void Imgui_renderer::use_as_backend_renderer_on_context(ImGuiContext* imgui_context)
{
    ImGuiIO& io = imgui_context->IO;

    IM_ASSERT(io.BackendRendererUserData == NULL && "Already initialized a platform backend renderer");

    io.BackendRendererUserData = this;
    io.BackendRendererName     = "erhe";
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
    io.ConfigFlags  |= ImGuiConfigFlags_NavNoCaptureKeyboard;
    io.ConfigWindowsMoveFromTitleBarOnly = true;

    auto& style = imgui_context->Style;
    style.WindowMenuButtonPosition = ImGuiDir_None;

    style.WindowPadding    = ImVec2{2.0f, 2.0f};
    style.FramePadding     = ImVec2{2.0f, 2.0f};
    style.CellPadding      = ImVec2{2.0f, 2.0f};
    style.ItemSpacing      = ImVec2{2.0f, 2.0f};
    style.ItemInnerSpacing = ImVec2{2.0f, 2.0f};

    style.WindowBorderSize = 1.0f;
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

    ImVec4* colors = style.Colors;
    colors[ImGuiCol_Text]                   = ImVec4(1.00f, 1.00f, 1.00f, 0.61f);
    colors[ImGuiCol_TextDisabled]           = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
    colors[ImGuiCol_WindowBg]               = ImVec4(0.10f, 0.14f, 0.13f, 0.99f);
    colors[ImGuiCol_ChildBg]                = ImVec4(0.04f, 0.17f, 0.22f, 1.00f);
    colors[ImGuiCol_PopupBg]                = ImVec4(0.04f, 0.17f, 0.22f, 0.95f);
    colors[ImGuiCol_Border]                 = ImVec4(0.17f, 0.31f, 0.34f, 1.00f);
    colors[ImGuiCol_BorderShadow]           = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_FrameBg]                = ImVec4(0.20f, 0.26f, 0.25f, 0.72f);
    colors[ImGuiCol_FrameBgHovered]         = ImVec4(0.25f, 0.34f, 0.33f, 0.72f);
    colors[ImGuiCol_FrameBgActive]          = ImVec4(0.29f, 0.40f, 0.39f, 0.72f);
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
    colors[ImGuiCol_Separator]              = ImVec4(0.17f, 0.31f, 0.34f, 1.00f);
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
    const std::shared_ptr<igl::ITexture>& texture,
    const int                                       width,
    const int                                       height,
    const glm::vec2                                 uv0,
    const glm::vec2                                 uv1,
    const glm::vec4                                 tint_color,
    const bool                                      linear
) -> bool
{
    SPDLOG_LOGGER_TRACE(
        log_imgui,
        "Imgui_renderer::image(texture {}, width = {}, height = {}, uv0 = {}, uv1 = {}, tint_color = {}, linear = {})",
        texture->gl_name(),
        width,
        height,
        uv0,
        uv1,
        tint_color,
        linear
    );
    const auto& sampler = linear ? m_linear_sampler : m_nearest_sampler;
    const uint64_t handle = m_graphics_instance.get_handle(
        *texture.get(),
        sampler
    );
    SPDLOG_LOGGER_TRACE(
        log_imgui,
        "sampler = {}, handle = {:16x}",
        sampler->gl_name(),
        handle
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

namespace {

[[nodiscard]] auto from_glm(const glm::vec4& v) -> ImVec4
{
    return ImVec4{v.x, v.y, v.z, v.w};
}

}

auto Imgui_renderer::image_button(
    const uint32_t                                  id,
    const std::shared_ptr<igl::ITexture>& texture,
    const int                                       width,
    const int                                       height,
    const glm::vec2                                 uv0,
    const glm::vec2                                 uv1,
    const glm::vec4                                 background_color,
    const glm::vec4                                 tint_color,
    const bool                                      linear
) -> bool
{
    if (!texture) {
        if ((width == 0) || (height == 0)) {
            return false;
        }
        ImGui::PushID        (id);
        ImGui::PushStyleColor(ImGuiCol_Button, background_color);
        ImGui::Button        ("", ImVec2{static_cast<float>(width), static_cast<float>(height)});
        ImGui::PopStyleColor ();
        ImGui::PopID         ();
        return ImGui::IsItemClicked();
    }

    const auto& sampler = linear ? m_linear_sampler : m_nearest_sampler;
    const uint64_t handle = m_graphics_instance.get_handle(
        *texture.get(),
        sampler
    );
    ImGui::ImageButtonEx(
        id,
        handle,
        ImVec2{
            static_cast<float>(width),
            static_cast<float>(height)
        },
        uv0,
        uv1,
        from_glm(background_color),
        from_glm(tint_color)
    );
    use(texture, handle);
    return ImGui::IsItemClicked();
}

void Imgui_renderer::use(
    const std::shared_ptr<igl::ITexture>& texture,
    const uint64_t                                  handle
)
{
    ERHE_PROFILE_FUNCTION();

#if !defined(NDEBUG)
    if (!m_graphics_instance.info.use_bindless_texture) {
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

    ERHE_PROFILE_FUNCTION();

    // Also make font texture handle resident
    {
        const uint64_t handle = m_graphics_instance.get_handle(
            *m_font_texture.get(),
            m_linear_sampler
        );
        use(m_font_texture, handle);
    }

    const ImDrawData* draw_data = ImGui::GetDrawData();
    if (draw_data == nullptr) {
        return;
    }

    const int fb_width  = (int)(draw_data->DisplaySize.x * draw_data->FramebufferScale.x);
    const int fb_height = (int)(draw_data->DisplaySize.y * draw_data->FramebufferScale.y);
    if (fb_width <= 0 || fb_height <= 0) {
        return;
    }

    erhe::graphics::Scoped_debug_group pass_scope{c_imgui_render};
    erhe::graphics::Scoped_gpu_timer   timer     {m_gpu_timer};

    auto&       program               = m_imgui_program_interface;
    auto&       draw_parameter_buffer = program.draw_parameter_buffer.current_buffer();
    auto&       draw_indirect_buffer  = program.draw_indirect_buffer .current_buffer();
    auto&       vertex_buffer         = program.vertex_buffer        .current_buffer();
    auto&       index_buffer          = program.index_buffer         .current_buffer();
    const auto& pipeline              = program.pipeline             .current_pipeline();

    const auto& draw_parameter_struct_offsets = program.draw_parameter_struct_offsets;
    const auto  draw_parameter_entry_size     = program.draw_parameter_struct.size_bytes();

    auto& draw_parameter_writer = program.draw_parameter_buffer.writer();
    auto& draw_indirect_writer  = program.draw_indirect_buffer .writer();
    auto& vertex_writer         = program.vertex_buffer        .writer();
    auto& index_writer          = program.index_buffer         .writer();

    std::size_t draw_indirect_count{0};

    const float scale[2] = {
        2.0f / draw_data->DisplaySize.x,
        2.0f / draw_data->DisplaySize.y
    };
    const float translate[2] = {
        -1.0f - draw_data->DisplayPos.x * scale[0],
        -1.0f - draw_data->DisplayPos.y * scale[1]
    };

    // Pass 1: Count how much memory will be needed
    std::size_t vertex_byte_count         = 0;
    std::size_t index_byte_count          = 0;
    std::size_t draw_parameter_byte_count = m_imgui_program_interface.block_offsets.draw_parameter_struct_array;
    std::size_t draw_indirect_byte_count  = 0;
    for (int n = 0; n < draw_data->CmdListsCount; n++) {
        const ImDrawList* cmd_list = draw_data->CmdLists[n];
        vertex_byte_count += static_cast<size_t>(cmd_list->VtxBuffer.size_in_bytes());
        index_byte_count  += static_cast<size_t>(cmd_list->IdxBuffer.size_in_bytes());
        for (int cmd_i = 0; cmd_i < cmd_list->CmdBuffer.Size; cmd_i++) {
            const ImDrawCmd* pcmd = &cmd_list->CmdBuffer[cmd_i];
            if (pcmd->UserCallback != nullptr) {
                if (pcmd->UserCallback == ImDrawCallback_ResetRenderState) {
                    ERHE_FATAL("not implemented");
                } else {
                    pcmd->UserCallback(cmd_list, pcmd);
                }
            } else {
                draw_parameter_byte_count += draw_parameter_entry_size;
                draw_indirect_byte_count  += sizeof(gl::Draw_elements_indirect_command);
            }
        }
    }

    auto draw_parameter_gpu_data = draw_parameter_writer.begin(&draw_parameter_buffer, draw_parameter_byte_count);
    auto draw_indirect_gpu_data  = draw_indirect_writer .begin(&draw_indirect_buffer,  draw_indirect_byte_count);
    auto vertex_gpu_data         = vertex_writer        .begin(&vertex_buffer,         vertex_byte_count);
    auto index_gpu_data          = index_writer         .begin(&index_buffer,          index_byte_count);

    using erhe::graphics::write;

    // Write scale
    const std::span<const float> scale_cpu_data{&scale[0], 2};
    write(
        draw_parameter_gpu_data,
        draw_parameter_writer.write_offset + m_imgui_program_interface.block_offsets.scale,
        scale_cpu_data
    );

    // Write translate
    const std::span<const float> translate_cpu_data{&translate[0], 2};
    write(
        draw_parameter_gpu_data,
        draw_parameter_writer.write_offset + m_imgui_program_interface.block_offsets.translate,
        translate_cpu_data
    );

    draw_parameter_writer.write_offset += m_imgui_program_interface.block_offsets.draw_parameter_struct_array;

    const ImVec2 clip_off   = draw_data->DisplayPos;
    const ImVec2 clip_scale = draw_data->FramebufferScale;

    if (!m_graphics_instance.info.use_bindless_texture) {
        m_graphics_instance.texture_unit_cache_reset(0);
    }

    const std::size_t vertex_stride = m_imgui_program_interface.vertex_format.stride();
    const std::size_t index_stride  = sizeof(uint16_t);

    // Pass 2: fill buffers
    std::size_t list_vertex_offset{vertex_writer.range.first_byte_offset / vertex_stride};
    std::size_t list_index_offset {index_writer .range.first_byte_offset / index_stride};
    for (int n = 0; n < draw_data->CmdListsCount; n++) {
        const ImDrawList* cmd_list = draw_data->CmdLists[n];

        // Upload vertex buffer
        const std::span<const uint8_t> vertex_cpu_data{
            reinterpret_cast<const uint8_t*>(cmd_list->VtxBuffer.begin()),
            static_cast<size_t>(cmd_list->VtxBuffer.size_in_bytes())
        };

        write(vertex_gpu_data, vertex_writer.write_offset, vertex_cpu_data);

        // Upload index buffer
        static_assert(sizeof(uint16_t) == sizeof(ImDrawIdx));
        const std::span<const uint16_t> index_cpu_data{
            cmd_list->IdxBuffer.begin(),
            static_cast<size_t>(cmd_list->IdxBuffer.size())
        };
        write(index_gpu_data, index_writer.write_offset, index_cpu_data);

        for (int cmd_i = 0; cmd_i < cmd_list->CmdBuffer.Size; cmd_i++) {
            const ImDrawCmd* pcmd = &cmd_list->CmdBuffer[cmd_i];
            if (pcmd->UserCallback != nullptr) {
                if (pcmd->UserCallback == ImDrawCallback_ResetRenderState) {
                    ERHE_FATAL("not implemented");
                } else {
                    pcmd->UserCallback(cmd_list, pcmd);
                }
            } else {
                // Project scissor/clipping rectangles into framebuffer space
                const ImVec4 clip_rect{
                    (pcmd->ClipRect.x - clip_off.x) * clip_scale.x,
                    (pcmd->ClipRect.y - clip_off.y) * clip_scale.y,
                    (pcmd->ClipRect.z - clip_off.x) * clip_scale.x,
                    (pcmd->ClipRect.w - clip_off.y) * clip_scale.y
                };

                if (
                    (clip_rect.x <  fb_width)  &&
                    (clip_rect.y <  fb_height) &&
                    (clip_rect.z >= 0.0f)      &&
                    (clip_rect.w >= 0.0f)
                ) {
                    // Write clip rectangle
                    const std::span<const float> clip_rect_cpu_data{&clip_rect.x, 4};
                    write(
                        draw_parameter_gpu_data,
                        draw_parameter_writer.write_offset + draw_parameter_struct_offsets.clip_rect,
                        clip_rect_cpu_data
                    );

                    // Write texture indices
                    if (m_graphics_instance.info.use_bindless_texture) {
                        const uint64_t handle = pcmd->TextureId;
                        const uint32_t texture_handle[2] =
                        {
                            static_cast<uint32_t>((handle & 0xffffffffu)),
                            static_cast<uint32_t>(handle >> 32u)
                        };
                        const std::span<const uint32_t> texture_handle_cpu_data{&texture_handle[0], 2};

                        write(
                            draw_parameter_gpu_data,
                            draw_parameter_writer.write_offset + draw_parameter_struct_offsets.texture,
                            texture_handle_cpu_data
                        );

                        const uint32_t extra[2] =
                        {
                            0u,
                            0u
                        };
                        const std::span<const uint32_t> extra_cpu_data{&extra[0], 2};

                        write(
                            draw_parameter_gpu_data,
                            draw_parameter_writer.write_offset + draw_parameter_struct_offsets.extra,
                            extra_cpu_data
                        );
                    } else {
                        const uint64_t handle = pcmd->TextureId;
                        const auto texture_unit_opt = m_graphics_instance.texture_unit_cache_allocate(handle);
                        if (texture_unit_opt.has_value()) {
                            const auto texture_unit = texture_unit_opt.value();
                            const uint32_t texture_indices[4] = { static_cast<uint32_t>(texture_unit), 0, 0, 0 };
                            const std::span<const uint32_t> texture_indices_cpu_data{&texture_indices[0], 4};

                            write(
                                draw_parameter_gpu_data,
                                draw_parameter_writer.write_offset + draw_parameter_struct_offsets.texture_indices,
                                texture_indices_cpu_data
                            );
                        } else {
                            const uint32_t texture_indices[4] = { 0, 0, 0, 0 };
                            const std::span<const uint32_t> texture_indices_cpu_data{&texture_indices[0], 4};
                            write(
                                draw_parameter_gpu_data,
                                draw_parameter_writer.write_offset + draw_parameter_struct_offsets.texture_indices,
                                texture_indices_cpu_data
                            );
                        }
                    }

                    draw_parameter_writer.write_offset += draw_parameter_entry_size;

                    const auto draw_command = gl::Draw_elements_indirect_command{
                        pcmd->ElemCount,
                        1,
                        pcmd->IdxOffset + static_cast<uint32_t>(list_index_offset),
                        pcmd->VtxOffset + static_cast<uint32_t>(list_vertex_offset),
                        0
                    };

                    write(
                        draw_indirect_gpu_data,
                        draw_indirect_writer.write_offset,
                        erhe::graphics::as_span(draw_command)
                    );
                    draw_indirect_writer.write_offset += sizeof(gl::Draw_elements_indirect_command);
                    ++draw_indirect_count;
                }
            }
        }

        vertex_writer.write_offset += vertex_cpu_data.size_bytes();
        index_writer.write_offset += index_cpu_data.size_bytes();

        list_vertex_offset += cmd_list->VtxBuffer.size();
        list_index_offset += cmd_list->IdxBuffer.size();
    }

    draw_parameter_writer.end();
    draw_indirect_writer .end();
    vertex_writer        .end();
    index_writer         .end();

    if (draw_indirect_count > 0) {
        gl::enable(gl::Enable_cap::clip_distance0);
        gl::enable(gl::Enable_cap::clip_distance1);
        gl::enable(gl::Enable_cap::clip_distance2);
        gl::enable(gl::Enable_cap::clip_distance3);

        // This binds vertex input states (VAO) and shader stages (shader program)
        // and most other state
        m_graphics_instance.opengl_state_tracker.execute(pipeline);

        // TODO viewport states is not currently in pipeline
        gl::viewport(0, 0, static_cast<GLsizei>(fb_width), static_cast<GLsizei>(fb_height));

        if (m_graphics_instance.info.use_bindless_texture) {
            for (const auto handle : m_used_texture_handles) {
                SPDLOG_LOGGER_TRACE(
                    log_imgui,
                    "making texture handle {:16x} resident",
                    handle
                );
                gl::make_texture_handle_resident_arb(handle);
            }
        } else {
#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_TRACE
            for (const auto& texture : m_used_textures) {
                SPDLOG_LOGGER_TRACE(
                    log_imgui,
                    "used texture: {} {}",
                    texture->gl_name(),
                    texture->debug_label()
                );
            }
            for (const auto texture_handle : m_used_texture_handles) {
                const GLuint texture_name = erhe::graphics::get_texture_from_handle(texture_handle);
                const GLuint sampler_name = erhe::graphics::get_sampler_from_handle(texture_handle);
                SPDLOG_LOGGER_TRACE(log_imgui, "used texture: {}", texture_name);
                SPDLOG_LOGGER_TRACE(log_imgui, "used sampler: {}", sampler_name);
            }
#endif

            const auto dummy_handle = m_graphics_instance.get_handle(
                *m_dummy_texture.get(),
                m_nearest_sampler
            );

            const auto texture_unit_use_count = m_graphics_instance.texture_unit_cache_bind(dummy_handle);

            for (size_t i = texture_unit_use_count; i < Imgui_program_interface::s_texture_unit_count; ++i) {
                gl::bind_texture_unit(static_cast<GLuint>(i), m_dummy_texture->gl_name());
                gl::bind_sampler     (static_cast<GLuint>(i), m_nearest_sampler.gl_name());
            }
        }

        ERHE_VERIFY(draw_parameter_writer.range.byte_count > 0);

        gl::bind_buffer_range(
            draw_parameter_buffer.target(),
            static_cast<GLuint>    (m_imgui_program_interface.draw_parameter_block.binding_point()),
            static_cast<GLuint>    (draw_parameter_buffer.gl_name()),
            static_cast<GLintptr>  (draw_parameter_writer.range.first_byte_offset),
            static_cast<GLsizeiptr>(draw_parameter_writer.range.byte_count)
        );

        gl::bind_buffer(
            gl::Buffer_target::draw_indirect_buffer,
            static_cast<GLuint>(draw_indirect_buffer.gl_name())
        );

        gl::multi_draw_elements_indirect(
            pipeline.data.input_assembly.primitive_topology,
            gl::Draw_elements_type::unsigned_short,
            reinterpret_cast<const void*>(draw_indirect_writer.range.first_byte_offset),
            static_cast<GLsizei>(draw_indirect_count),
            static_cast<GLsizei>(sizeof(gl::Draw_elements_indirect_command))
        );

        if (m_graphics_instance.info.use_bindless_texture) {
            for (const auto handle : m_used_texture_handles) {
                SPDLOG_LOGGER_TRACE(
                    log_imgui,
                    "making texture handle {:16x} non-resident",
                    handle
                );

                gl::make_texture_handle_non_resident_arb(handle);
            }
        }

        gl::disable(gl::Enable_cap::clip_distance0);
        gl::disable(gl::Enable_cap::clip_distance1);
        gl::disable(gl::Enable_cap::clip_distance2);
        gl::disable(gl::Enable_cap::clip_distance3);
    }

    SPDLOG_LOGGER_TRACE(log_frame, "end Imgui_renderer::render_draw_data()");
}

} // namespace erhe::imgui

void ImGui_ImplErhe_assert_user_error(const bool condition, const char* message)
{
    if (!condition) {
        erhe::imgui::log_imgui->error("{}", message);
    }
}
