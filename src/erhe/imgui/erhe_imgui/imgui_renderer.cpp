// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "erhe_imgui/imgui_renderer.hpp"
#include "erhe_imgui/imgui_log.hpp"
#include "erhe_imgui/imgui_host.hpp"

// For user clip enable/disable
#if defined(ERHE_GRAPHICS_LIBRARY_OPENGL)
# include "erhe_gl/wrapper_enums.hpp"
# include "erhe_gl/wrapper_functions.hpp"
#endif

#include "erhe_defer/defer.hpp"

#include "erhe_graphics/blit_command_encoder.hpp"
#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/draw_indirect.hpp"
#include "erhe_graphics/render_command_encoder.hpp"
#include "erhe_graphics/render_pipeline_state.hpp"
#include "erhe_graphics/ring_buffer.hpp"
#include "erhe_graphics/ring_buffer_client.hpp"
#include "erhe_graphics/ring_buffer_range.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_graphics/texture_heap.hpp"
#include "erhe_graphics/state/vertex_input_state.hpp"
#include "erhe_graphics/span.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_verify/verify.hpp"

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>

namespace erhe::imgui {

using erhe::graphics::Texture;
using std::shared_ptr;
using std::unique_ptr;
using std::make_shared;
using std::make_unique;

namespace {

#define ERHE_IMGUI_VERTEX_ATTRIBUTE_POSITION 0
#define ERHE_IMGUI_VERTEX_ATTRIBUTE_TEXCOORD 1
#define ERHE_IMGUI_VERTEX_ATTRIBUTE_COLOR    2

constexpr std::string_view c_vertex_shader_source = R"NUL(
layout(location = 0) out vec4 v_color;
layout(location = 1) out vec2 v_texcoord;

#if defined(ERHE_HAS_ARB_BINDLESS_TEXTURE)
layout(location = 2) out flat uvec2 v_texture;
#else
layout(location = 2) out flat uint v_texture_id;
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
    vec4 clip_rect     = draw.draw_parameters[ERHE_DRAW_ID].clip_rect;
    gl_Position        = vec4(a_position * scale + translate, 0, 1);
    gl_Position.y      = -gl_Position.y;
    gl_ClipDistance[0] = a_position.x - clip_rect[0];
    gl_ClipDistance[1] = a_position.y - clip_rect[1];
    gl_ClipDistance[2] = clip_rect[2] - a_position.x;
    gl_ClipDistance[3] = clip_rect[3] - a_position.y;
    v_texcoord         = a_texcoord_0;
#if defined(ERHE_HAS_ARB_BINDLESS_TEXTURE)
    v_texture          = draw.draw_parameters[ERHE_DRAW_ID].texture;
#else
    v_texture_id       = draw.draw_parameters[ERHE_DRAW_ID].texture.x;
#endif
    v_color            = vec4(srgb_to_linear(a_color_0.rgb), a_color_0.a);
}
)NUL";

const std::string_view c_fragment_shader_source = R"NUL(
layout(location = 0) in vec4 v_color;
layout(location = 1) in vec2 v_texcoord;

#if defined(ERHE_HAS_ARB_BINDLESS_TEXTURE)
layout(location = 2) in flat uvec2 v_texture;
#else
layout(location = 2) in flat uint v_texture_id;
#endif

void main()
{
#if defined(ERHE_HAS_ARB_BINDLESS_TEXTURE)
    sampler2D s_texture = sampler2D(v_texture);
    vec4 texture_sample = texture(s_texture, v_texcoord.st);
#else
    vec4 texture_sample = texture(s_textures[v_texture_id], v_texcoord.st);
#endif
    out_color = v_color * texture_sample;
    //out_color.rgb       = v_color.rgb * texture_sample.rgb * v_color.a;
    //out_color.a         = texture_sample.a * v_color.a;
}
)NUL";

} // anonymous namespace

#pragma region Imgui_program_interface
auto get_shader_default_uniform_block(erhe::graphics::Device& graphics_device, const int dedicated_texture_unit) -> erhe::graphics::Shader_resource
{
    erhe::graphics::Shader_resource default_uniform_block{graphics_device};
    if (!graphics_device.get_info().use_bindless_texture) {
        default_uniform_block.add_sampler("s_textures", erhe::graphics::Glsl_type::sampler_2d, 0, dedicated_texture_unit);
    }
    return default_uniform_block;
}

Imgui_program_interface::Imgui_program_interface(erhe::graphics::Device& graphics_device)
    : draw_parameter_block{graphics_device, "draw", 0, erhe::graphics::Shader_resource::Type::shader_storage_block}
    , draw_parameter_struct{graphics_device, "Draw_parameters"}
    , draw_parameter_struct_offsets{
        .clip_rect = draw_parameter_struct.add_vec4 ("clip_rect")->get_offset_in_parent(),
        .texture   = draw_parameter_struct.add_uvec2("texture")  ->get_offset_in_parent(),
        .padding   = draw_parameter_struct.add_uvec2("padding")  ->get_offset_in_parent()
    }
    , block_offsets{
        .scale                       = draw_parameter_block.add_vec4  ("scale"    )->get_offset_in_parent(),
        .translate                   = draw_parameter_block.add_vec4  ("translate")->get_offset_in_parent(),
        .draw_parameter_struct_array = draw_parameter_block.add_struct("draw_parameters", &draw_parameter_struct, 0 /* unsized array*/ )->get_offset_in_parent()
    }
    , fragment_outputs{
        erhe::graphics::Fragment_output{
            .name     = "out_color",
            .type     = erhe::graphics::Glsl_type::float_vec4,
            .location = 0
        }
    }
    , vertex_format{
        {
            0,
            {
                { erhe::dataformat::Format::format_32_vec2_float, erhe::dataformat::Vertex_attribute_usage::position },
                { erhe::dataformat::Format::format_32_vec2_float, erhe::dataformat::Vertex_attribute_usage::tex_coord },
                { erhe::dataformat::Format::format_8_vec4_unorm,  erhe::dataformat::Vertex_attribute_usage::color }
            }
        }
    }
    , default_uniform_block{
        get_shader_default_uniform_block(
            graphics_device,
            graphics_device.get_info().max_per_stage_descriptor_samplers
        )
    }
{
}

#pragma endregion Imgui_program_interface

Imgui_renderer::Imgui_renderer(erhe::graphics::Device& graphics_device, Imgui_settings& settings)
    : m_graphics_device{graphics_device}
    , m_imgui_program_interface{graphics_device}
    , m_shader_stages{
        graphics_device,
        erhe::graphics::Shader_stages_prototype{
            graphics_device,
            erhe::graphics::Shader_stages_create_info{
                .name                  = "ImGui Renderer",
                .struct_types          = { &m_imgui_program_interface.draw_parameter_struct },
                .interface_blocks      = { &m_imgui_program_interface.draw_parameter_block },
                .fragment_outputs      = &m_imgui_program_interface.fragment_outputs,
                .vertex_format         = &m_imgui_program_interface.vertex_format,
                .default_uniform_block = graphics_device.get_info().use_bindless_texture ? nullptr : &m_imgui_program_interface.default_uniform_block,
                .shaders = {
                    { erhe::graphics::Shader_type::vertex_shader,   c_vertex_shader_source   },
                    { erhe::graphics::Shader_type::fragment_shader, c_fragment_shader_source }
                },
                .build = true
            }
        }
    }
    , m_vertex_buffer{
        graphics_device,
        erhe::graphics::Buffer_target::vertex,
        "ImGui Vertex Buffer"
    }
    , m_index_buffer{
        graphics_device,
        erhe::graphics::Buffer_target::index,
        "ImGui Index Buffer"
    }
    , m_draw_parameter_buffer{
        graphics_device,
        erhe::graphics::Buffer_target::storage,
        "ImGui Draw Parameter Buffer",
        m_imgui_program_interface.draw_parameter_block.get_binding_point(),
    }
    , m_draw_indirect_buffer{
        graphics_device,
        erhe::graphics::Buffer_target::draw_indirect,
        "ImGui Draw Indirect Buffer"
    }
    , m_vertex_input{graphics_device, erhe::graphics::Vertex_input_state_data::make(m_imgui_program_interface.vertex_format)}
    , m_pipeline{
        erhe::graphics::Render_pipeline_data{
            .name           = "ImGui Renderer",
            .shader_stages  = &m_shader_stages,
            .vertex_input   = &m_vertex_input,
            .input_assembly = erhe::graphics::Input_assembly_state::triangle,
            .rasterization  = erhe::graphics::Rasterization_state::cull_mode_none,
            .depth_stencil  = erhe::graphics::Depth_stencil_state::depth_test_disabled_stencil_test_disabled,
            .color_blend    = erhe::graphics::Color_blend_state::color_blend_alpha
        }
    }
    , m_dummy_texture{graphics_device.create_dummy_texture(erhe::dataformat::Format::format_8_vec4_srgb)}
    , m_nearest_sampler{
        graphics_device,
        {
            .min_filter  = erhe::graphics::Filter::nearest, // nearest_mipmap_nearest,
            .mag_filter  = erhe::graphics::Filter::nearest,
            .mipmap_mode = erhe::graphics::Sampler_mipmap_mode::not_mipmapped,
            .debug_label = "Imgui_renderer nearest"
        }
    }
    , m_linear_sampler{
        graphics_device,
        {
            .min_filter  = erhe::graphics::Filter::linear,
            .mag_filter  = erhe::graphics::Filter::linear,
            .mipmap_mode = erhe::graphics::Sampler_mipmap_mode::not_mipmapped,
            .debug_label = "Imgui_renderer linear"
        }
    }

    , m_nearest_mipmap_nearest_sampler{
        graphics_device,
        {
            .min_filter  = erhe::graphics::Filter::nearest,
            .mag_filter  = erhe::graphics::Filter::nearest,
            .mipmap_mode = erhe::graphics::Sampler_mipmap_mode::nearest,
            .debug_label = "Imgui_renderer nearest mipmap nearest"
        }
    }
    , m_linear_mipmap_nearest_sampler{
        graphics_device,
        {
            .min_filter  = erhe::graphics::Filter::linear,
            .mag_filter  = erhe::graphics::Filter::linear,
            .mipmap_mode = erhe::graphics::Sampler_mipmap_mode::linear,
            .debug_label = "Imgui_renderer linear mipmap nearest"
        }
    }

    , m_nearest_mipmap_linear_sampler{
        graphics_device,
        {
            .min_filter  = erhe::graphics::Filter::nearest,
            .mag_filter  = erhe::graphics::Filter::nearest,
            .mipmap_mode = erhe::graphics::Sampler_mipmap_mode::linear,
            .debug_label = "Imgui_renderer nearest mipmap linear"
        }
    }
    , m_linear_mipmap_linear_sampler{
        graphics_device,
        {
            .min_filter  = erhe::graphics::Filter::linear,
            .mag_filter  = erhe::graphics::Filter::linear,
            .mipmap_mode = erhe::graphics::Sampler_mipmap_mode::linear,
            .debug_label = "Imgui_renderer linear mipmap linear"
        }
    }
    , m_gpu_timer{graphics_device, "Imgui_renderer"}
{
    ERHE_PROFILE_FUNCTION();

    m_font_atlas.RefCount = 1; // This should never be deleted due to refcount going to zero
    m_primary_font    = m_font_atlas.AddFontFromFileTTF(settings.primary_font.c_str(), settings.scale_factor * settings.font_size);
    m_mono_font       = m_font_atlas.AddFontFromFileTTF(settings.mono_font   .c_str(), settings.scale_factor * settings.font_size);
    m_vr_primary_font = m_font_atlas.AddFontFromFileTTF(settings.primary_font.c_str(), settings.scale_factor * settings.vr_font_size);
    m_vr_mono_font    = m_font_atlas.AddFontFromFileTTF(settings.mono_font   .c_str(), settings.scale_factor * settings.vr_font_size);

    if (settings.material_design_font_size > 0.0f) {
    // TODO Something nicer
#define ICON_MIN_MDI 0xF68C
#define ICON_MAX_16_MDI 0xF68C
#define ICON_MAX_MDI 0xF1D17
        ImFontGlyphRangesBuilder builder;
        static const ImWchar ranges[] = { ICON_MIN_MDI, ICON_MAX_MDI, 0 };
        builder.AddRanges(ranges);
        ImVector<ImWchar> range;
        builder.BuildRanges(&range);
        m_material_design_font = m_font_atlas.AddFontFromFileTTF(
            settings.material_design_font.c_str(),
            settings.scale_factor * settings.material_design_font_size,
            nullptr, range.Data
        );
    }
    if (settings.icon_font_size > 0.0f) {
        m_icon_font = m_font_atlas.AddFontFromFileTTF(
            settings.icon_font.c_str(),
            settings.scale_factor * settings.icon_font_size
        );
    }

    // (1) If you manage font atlases yourself, e.g. create a ImFontAtlas yourself you need to call ImFontAtlasUpdateNewFrame() on it.
    // Otherwise, calling ImGui::CreateContext() without parameter will create an atlas owned by the context.
    // (2) If you have multiple font atlases, make sure the 'atlas->RendererHasTextures' as specified in the ImFontAtlasUpdateNewFrame() call matches for that.
    // (3) If you have multiple imgui contexts, they also need to have a matching value for ImGuiBackendFlags_RendererHasTextures.
    m_font_atlas.RendererHasTextures = true;
}

void Imgui_renderer::lock_mutex()
{
    m_mutex.lock();
}

void Imgui_renderer::unlock_mutex()
{
    m_mutex.unlock();
}

void Imgui_renderer::make_current(const Imgui_host* imgui_host)
{
    m_current_host = imgui_host;
    if (imgui_host != nullptr) {
        ImGui::SetCurrentContext(imgui_host->imgui_context());
    } else {
        ImGui::SetCurrentContext(nullptr);
    }
}

void Imgui_renderer::register_imgui_host(Imgui_host* imgui_host)
{
    //ERHE_VERIFY(!m_iterating);
    const std::lock_guard<std::recursive_mutex> lock{m_mutex};
#if !defined(NDEBUG)
    const auto i = std::find_if(
        m_imgui_hosts.begin(),
        m_imgui_hosts.end(),
        [imgui_host](Imgui_host* entry) {
            return entry == imgui_host;
        }
    );
    if (i != m_imgui_hosts.end()) {
        log_imgui->error("Imgui_host '{}' is already registered to Imgui_windows", imgui_host->get_name());
        return;
    }
#endif

    m_imgui_hosts.push_back(imgui_host);
}

void Imgui_renderer::unregister_imgui_host(Imgui_host* imgui_host)
{
    //ERHE_VERIFY(!m_iterating);

    const std::lock_guard<std::recursive_mutex> lock{m_mutex};
    const auto i = std::find_if(
        m_imgui_hosts.begin(),
        m_imgui_hosts.end(),
        [imgui_host](Imgui_host* entry) {
            return entry == imgui_host;
        }
    );
    if (i == m_imgui_hosts.end()) {
        log_imgui->error("Imgui_windows::unregister_imgui_host(): imgui_host '{}' is not registered", imgui_host->get_name());
        return;
    }
    m_imgui_hosts.erase(i);
}

auto Imgui_renderer::get_imgui_hosts() const -> const std::vector<Imgui_host*>&
{
    return m_imgui_hosts;
}

auto Imgui_renderer::get_imgui_settings() const -> const Imgui_settings&
{
    return m_imgui_settings;
}

void Imgui_renderer::on_font_config_changed(Imgui_settings& settings)
{
    m_imgui_settings = settings;
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

auto Imgui_renderer::material_design_font() const -> ImFont*
{
    return m_material_design_font;
}

auto Imgui_renderer::icon_font() const -> ImFont*
{
    return m_icon_font;
}

void Imgui_renderer::begin_frame()
{
    ImFontAtlasUpdateNewFrame(&m_font_atlas, static_cast<int>(m_graphics_device.get_frame_index()), true);
}

void Imgui_renderer::at_end_of_frame(std::function<void()>&& func)
{
    m_at_end_of_frame.push_back(std::move(func));
}

void Imgui_renderer::next_frame()
{
    ERHE_PROFILE_FUNCTION();

    for (auto& operation : m_at_end_of_frame) {
        operation();
    }
    m_at_end_of_frame.clear();
}

static constexpr std::string_view c_imgui_render{"ImGui_ImplErhe_RenderDrawData()"};


static void ImGui_Impl_erhe_PlatformSetImeData(ImGuiContext* context, ImGuiViewport* viewport, ImGuiPlatformImeData* data)
{
    static_cast<void>(context);
    void* backend_user_data = ImGui::GetCurrentContext() ? ImGui::GetIO().BackendPlatformUserData : nullptr;
    auto* imgui_host = static_cast<Imgui_host*>(backend_user_data);
    if (imgui_host == nullptr) {
        return;
    }
    Imgui_renderer& imgui_renderer = imgui_host->get_imgui_renderer();
    imgui_renderer.set_ime_data(viewport, data);
}

void Imgui_renderer::set_ime_data(ImGuiViewport* viewport, ImGuiPlatformImeData* data)
{
    auto* imgui_host = static_cast<Imgui_host*>(viewport->PlatformHandle);
    if ((!data->WantVisible || (m_ime_host != imgui_host)) && (m_ime_host != nullptr)) {
        imgui_host->stop_text_input();
        m_ime_host = nullptr;
    }
    if (imgui_host == nullptr) {
        return;
    }
    if (data->WantVisible) {
        int x = (int)(data->InputPos.x - viewport->Pos.x);
        int y = (int)(data->InputPos.y - viewport->Pos.y + data->InputLineHeight);
        int w = 1;
        int h = (int)data->InputLineHeight;
        imgui_host->set_text_input_area(x, y, w, h);
        imgui_host->start_text_input();
        m_ime_host = imgui_host;
    }
}

void Imgui_renderer::use_as_backend_renderer_on_context(ImGuiContext* imgui_context)
{
    ImGuiIO& io = imgui_context->IO;

    IM_ASSERT((io.BackendRendererUserData == nullptr) && "Already initialized a platform backend renderer");

    io.BackendRendererUserData = this;
    io.BackendRendererName     = "erhe";
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;

    ImGuiPlatformIO& platform_io = ImGui::GetPlatformIO();
    platform_io.Renderer_TextureMaxWidth = platform_io.Renderer_TextureMaxHeight = m_graphics_device.get_info().max_texture_size;

    // TODO platform_io.Platform_SetClipboardTextFn = ImGui_ImplSDL3_SetClipboardText;
    // TODO platform_io.Platform_GetClipboardTextFn = ImGui_ImplSDL3_GetClipboardText;
    platform_io.Platform_SetImeDataFn = ImGui_Impl_erhe_PlatformSetImeData;
    // TODO platform_io.Platform_OpenInShellFn = [](ImGuiContext*, const char* url) { return SDL_OpenURL(url) == 0; };

    io.ConfigNavCaptureKeyboard = false;
    io.ConfigWindowsMoveFromTitleBarOnly = true;

    // ConfigErrorRecovery appears to be expensive, so it is disabled for now.
    // TODO Re-enable if this ever turns out to be less expensive.
    io.ConfigErrorRecovery = false;

    // This seems to cause mouse move events to have lantency when mouse wheel events are used
    // for scrolling (at least from headset).
    io.ConfigInputTrickleEventQueue = false;

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
    colors[ImGuiCol_InputTextCursor]        = ImVec4(1.00f, 0.55f, 0.00f, 1.00f);
    colors[ImGuiCol_Text]                   = ImVec4(1.00f, 1.00f, 1.00f, 0.61f);
    colors[ImGuiCol_TextDisabled]           = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
    colors[ImGuiCol_WindowBg]               = ImVec4(0.12f, 0.12f, 0.12f, 1.00f);
    colors[ImGuiCol_ChildBg]                = ImVec4(0.14f, 0.14f, 0.14f, 1.00f);
    colors[ImGuiCol_PopupBg]                = ImVec4(0.07f, 0.07f, 0.07f, 1.00f);
    colors[ImGuiCol_Border]                 = ImVec4(0.22f, 0.22f, 0.22f, 1.00f);
    colors[ImGuiCol_BorderShadow]           = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_FrameBg]                = ImVec4(0.07f, 0.07f, 0.07f, 0.72f);
    colors[ImGuiCol_FrameBgHovered]         = ImVec4(0.45f, 0.50f, 0.59f, 0.15f);
    colors[ImGuiCol_FrameBgActive]          = ImVec4(0.45f, 0.50f, 0.59f, 0.24f);
    colors[ImGuiCol_TitleBg]                = ImVec4(0.12f, 0.12f, 0.12f, 1.00f);
    colors[ImGuiCol_TitleBgActive]          = ImVec4(0.12f, 0.12f, 0.12f, 1.00f);
    colors[ImGuiCol_TitleBgCollapsed]       = ImVec4(0.12f, 0.12f, 0.12f, 1.00f);
    colors[ImGuiCol_MenuBarBg]              = ImVec4(0.07f, 0.07f, 0.07f, 1.00f);
    colors[ImGuiCol_ScrollbarBg]            = ImVec4(0.21f, 0.21f, 0.21f, 0.62f);
    colors[ImGuiCol_ScrollbarGrab]          = ImVec4(0.83f, 0.83f, 0.83f, 0.10f);
    colors[ImGuiCol_ScrollbarGrabHovered]   = ImVec4(0.83f, 0.83f, 0.83f, 0.31f);
    colors[ImGuiCol_ScrollbarGrabActive]    = ImVec4(0.64f, 0.83f, 0.31f, 1.00f);
    colors[ImGuiCol_CheckMark]              = ImVec4(0.64f, 0.83f, 0.31f, 1.00f);
    colors[ImGuiCol_SliderGrab]             = ImVec4(0.31f, 0.34f, 0.76f, 1.00f);
    colors[ImGuiCol_SliderGrabActive]       = ImVec4(0.41f, 0.45f, 1.00f, 1.00f);
    colors[ImGuiCol_Button]                 = ImVec4(0.22f, 0.22f, 0.22f, 1.00f);
    colors[ImGuiCol_ButtonHovered]          = ImVec4(0.45f, 0.50f, 0.59f, 0.18f);
    colors[ImGuiCol_ButtonActive]           = ImVec4(0.29f, 0.34f, 0.45f, 0.64f);
    colors[ImGuiCol_Header]                 = ImVec4(0.18f, 0.18f, 0.18f, 1.00f);
    colors[ImGuiCol_HeaderHovered]          = ImVec4(0.37f, 0.37f, 0.37f, 0.17f);
    colors[ImGuiCol_HeaderActive]           = ImVec4(0.45f, 0.45f, 0.45f, 0.25f);
    colors[ImGuiCol_HeaderSelected]         = ImVec4(0.15f, 0.15f, 0.44f, 0.68f);
    colors[ImGuiCol_Separator]              = ImVec4(0.26f, 0.26f, 0.26f, 0.21f);
    colors[ImGuiCol_SeparatorHovered]       = ImVec4(0.20f, 0.59f, 0.97f, 0.39f);
    colors[ImGuiCol_SeparatorActive]        = ImVec4(0.20f, 0.59f, 0.97f, 0.70f);
    colors[ImGuiCol_ResizeGrip]             = ImVec4(0.20f, 0.59f, 0.97f, 0.70f);
    colors[ImGuiCol_ResizeGripHovered]      = ImVec4(0.20f, 0.59f, 0.97f, 0.39f);
    colors[ImGuiCol_ResizeGripActive]       = ImVec4(0.20f, 0.59f, 0.97f, 0.70f);
    colors[ImGuiCol_TabHovered]             = ImVec4(0.19f, 0.19f, 0.19f, 1.00f);
    colors[ImGuiCol_Tab]                    = ImVec4(0.14f, 0.14f, 0.14f, 1.00f);
    colors[ImGuiCol_TabSelected]            = ImVec4(0.19f, 0.19f, 0.19f, 1.00f);
    colors[ImGuiCol_TabSelectedOverline]    = ImVec4(0.31f, 0.34f, 0.76f, 1.00f);
    colors[ImGuiCol_TabDimmed]              = ImVec4(0.14f, 0.14f, 0.14f, 1.00f);
    colors[ImGuiCol_TabDimmedSelected]      = ImVec4(0.14f, 0.14f, 0.14f, 1.00f);
    colors[ImGuiCol_TabDimmedSelectedOverline]  = ImVec4(0.31f, 0.34f, 0.76f, 1.00f);
    colors[ImGuiCol_DockingPreview]         = ImVec4(0.20f, 0.59f, 0.97f, 0.70f);
    colors[ImGuiCol_DockingEmptyBg]         = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
    colors[ImGuiCol_PlotLines]              = ImVec4(0.86f, 1.00f, 0.06f, 0.62f);
    colors[ImGuiCol_PlotLinesHovered]       = ImVec4(1.00f, 0.43f, 0.35f, 1.00f);
    colors[ImGuiCol_PlotHistogram]          = ImVec4(0.90f, 0.70f, 0.00f, 1.00f);
    colors[ImGuiCol_PlotHistogramHovered]   = ImVec4(1.00f, 0.60f, 0.00f, 1.00f);
    colors[ImGuiCol_TableHeaderBg]          = ImVec4(0.00f, 0.00f, 0.00f, 1.00f);
    colors[ImGuiCol_TableBorderStrong]      = ImVec4(0.39f, 0.39f, 0.39f, 0.21f);
    colors[ImGuiCol_TableBorderLight]       = ImVec4(0.34f, 0.34f, 0.34f, 0.02f);
    colors[ImGuiCol_TableRowBg]             = ImVec4(0.04f, 0.04f, 0.04f, 0.40f);
    colors[ImGuiCol_TableRowBgAlt]          = ImVec4(0.04f, 0.04f, 0.04f, 0.17f);
    colors[ImGuiCol_TextLink]               = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
    colors[ImGuiCol_TextSelectedBg]         = ImVec4(0.26f, 0.59f, 0.98f, 0.35f);
    colors[ImGuiCol_DragDropTarget]         = ImVec4(1.00f, 1.00f, 0.00f, 0.90f);
    colors[ImGuiCol_NavCursor]              = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
    colors[ImGuiCol_NavWindowingHighlight]  = ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
    colors[ImGuiCol_NavWindowingDimBg]      = ImVec4(0.80f, 0.80f, 0.80f, 0.20f);
    colors[ImGuiCol_ModalWindowDimBg]       = ImVec4(0.80f, 0.80f, 0.80f, 0.35f);
}

auto Imgui_renderer::get_font_atlas() -> ImFontAtlas*
{
    return &m_font_atlas;
}

auto Imgui_renderer::get_sampler(const Erhe_ImTextureID& texture_id) const -> const erhe::graphics::Sampler&
{
    return get_sampler(
        static_cast<erhe::graphics::Filter             >(texture_id.filter),
        static_cast<erhe::graphics::Sampler_mipmap_mode>(texture_id.mipmap_mode)
    );
}

auto Imgui_renderer::get_sampler(
    const erhe::graphics::Filter              filter,
    const erhe::graphics::Sampler_mipmap_mode mipmap_mode
) const -> const erhe::graphics::Sampler&
{
    switch (mipmap_mode) {
        case erhe::graphics::Sampler_mipmap_mode::not_mipmapped: {
            switch (filter) {
                case erhe::graphics::Filter::nearest: return m_nearest_sampler;
                case erhe::graphics::Filter::linear:  return m_linear_sampler;
            }
            break;
        }
        case erhe::graphics::Sampler_mipmap_mode::nearest: {
            switch (filter) {
                case erhe::graphics::Filter::nearest: return m_nearest_mipmap_nearest_sampler;
                case erhe::graphics::Filter::linear:  return m_linear_mipmap_nearest_sampler;
            }
            break;
        }
        case erhe::graphics::Sampler_mipmap_mode::linear: {
            switch (filter) {
                case erhe::graphics::Filter::nearest: return m_nearest_mipmap_linear_sampler;
                case erhe::graphics::Filter::linear:  return m_linear_mipmap_linear_sampler;
            }
            break;
        }
        break;
    }
    ERHE_FATAL("get_sampler() bad logic");
    return m_nearest_sampler;
}

auto Imgui_renderer::image(Draw_texture_parameters&& parameters) -> bool
{
    SPDLOG_LOGGER_TRACE(
        log_imgui,
        "Imgui_renderer::image(texture {}, width = {}, height = {}, uv0 = {}, uv1 = {}, tint_color = {}, linear = {})",
        (parameters.texture_reference) ? parameters.texture_reference.get_texture().get_impl().gl_name() : 0,
        parameters.width,
        parameters.height,
        parameters.uv0,
        parameters.uv1,
        parameters.tint_color,
        parameters.linear
    );
    //const erhe::graphics::Sampler& sampler = get_sampler(parameters.filter, parameters.mipmap_mode);

    if (parameters.texture_reference) {
        ImGui::ImageWithBg(
            Erhe_ImTextureID{
                .texture_reference = parameters.texture_reference.get(),
                .filter            = static_cast<unsigned int>(parameters.filter),
                .mipmap_mode       = static_cast<unsigned int>(parameters.mipmap_mode),
                .debug_label       = parameters.debug_label
            },
            ImVec2{static_cast<float>(parameters.width), static_cast<float>(parameters.height)},
            parameters.uv0, parameters.uv1,
            parameters.background_color, parameters.tint_color
        );
        m_draw_texture_references.push_back(parameters.texture_reference);
    } else {
        ImGui::Dummy(ImVec2{static_cast<float>(parameters.width), static_cast<float>(parameters.height)});
    }

    return ImGui::IsItemClicked();
}

namespace {

[[nodiscard]] auto from_glm(const glm::vec4& v) -> ImVec4
{
    return ImVec4{v.x, v.y, v.z, v.w};
}

}

auto Imgui_renderer::image_button(Draw_texture_parameters&& parameters) -> bool
{
    if (parameters.texture_reference) {
        if ((parameters.width == 0) || (parameters.height == 0)) {
            return false;
        }
        if (parameters.id != 0) {
            ImGui::PushID(parameters.id);
        }
        ImGui::PushStyleColor(ImGuiCol_Button, parameters.background_color);
        ImGui::Button        ("", ImVec2{static_cast<float>(parameters.width), static_cast<float>(parameters.height)});
        ImGui::PopStyleColor ();
        if (parameters.id != 0) {
            ImGui::PopID();
        }
        return ImGui::IsItemClicked();
    }

    //const erhe::graphics::Sampler& sampler = get_sampler(parameters.filter, parameters.mipmap_mode);
    //// const uint64_t handle = m_graphics_device.get_handle(*texture.get(), sampler);
    ImGui::ImageButtonEx(
        parameters.id,
        Erhe_ImTextureID{
            .texture_reference = parameters.texture_reference.get(),
            .filter            = static_cast<unsigned int>(parameters.filter),
            .mipmap_mode       = static_cast<unsigned int>(parameters.mipmap_mode),
            .debug_label       = parameters.debug_label
        },
        ImVec2{static_cast<float>(parameters.width), static_cast<float>(parameters.height)},
        parameters.uv0,
        parameters.uv1,
        from_glm(parameters.background_color),
        from_glm(parameters.tint_color)
    );
    m_draw_texture_references.push_back(parameters.texture_reference);
    return ImGui::IsItemClicked();
}

void Imgui_renderer::update_texture(ImTextureData* tex)
{
    erhe::graphics::Scoped_debug_group pass_scope{"Imgui_renderer::update_texture()"};

    ERHE_VERIFY(tex != nullptr);

    if (tex->Status == ImTextureStatus_WantCreate) {
        // Create and upload new texture to graphics system
        IM_ASSERT(tex->TexID == ImTextureID_Invalid && tex->BackendUserData == nullptr);

        /// Create texture
        const erhe::graphics::Texture_create_info create_info{
            .device      = m_graphics_device,
            .usage_mask  =
                erhe::graphics::Image_usage_flag_bit_mask::sampled |
                erhe::graphics::Image_usage_flag_bit_mask::transfer_dst,
            .pixelformat = (tex->Format == ImTextureFormat_RGBA32) ? erhe::dataformat::Format::format_8_vec4_unorm : erhe::dataformat::Format::format_8_scalar_unorm,
            .use_mipmaps = false,
            .width       = tex->Width,
            .height      = tex->Height,
            .depth       = 1,
            .level_count = 1,
            .row_stride  = tex->GetPitch(),
            .debug_label = "ImGui texture"
        };
        auto texture = std::make_shared<erhe::graphics::Texture>(m_graphics_device, create_info);
        m_imgui_textures.push_back(texture); // This keeps textures alive
        log_imgui->trace("created texture {}", fmt::ptr(texture.get()));

        // Upload pixel data
        const std::span<const std::uint8_t> src_span{
            static_cast<const std::uint8_t*>(tex->GetPixels()),
            static_cast<size_t>(tex->GetSizeInBytes())
        };

        std::size_t                        byte_count = src_span.size_bytes();
        erhe::graphics::Ring_buffer_client texture_upload_buffer{m_graphics_device, erhe::graphics::Buffer_target::transfer_src, "font upload"};
        erhe::graphics::Ring_buffer_range  buffer_range = texture_upload_buffer.acquire(erhe::graphics::Ring_buffer_usage::CPU_write, byte_count);
        std::span<std::byte>               dst_span     = buffer_range.get_span();
        memcpy(dst_span.data(), src_span.data(), byte_count);
        buffer_range.bytes_written(byte_count);
        buffer_range.close();

        erhe::graphics::Blit_command_encoder encoder{m_graphics_device};
        encoder.copy_from_buffer(
            buffer_range.get_buffer()->get_buffer(),          // source_buffer
            buffer_range.get_byte_start_offset_in_buffer(),   // source_offset
            tex->GetPitch(),                                  // source_bytes_per_row
            tex->GetSizeInBytes(),                            // source_bytes_per_image
            glm::ivec3{tex->Width, tex->Height, 1},           // source_size
            texture.get(),                                    // destination_texture
            0,                                                // destination_slice
            0,                                                // destination_level
            glm::ivec3{0, 0, 0}                               // destination_origin
        );

        buffer_range.release();

        tex->SetTexID(
            Erhe_ImTextureID(
                texture.get(),
                static_cast<unsigned int>(erhe::graphics::Filter::linear),
                static_cast<unsigned int>(erhe::graphics::Sampler_mipmap_mode::not_mipmapped),
                "ImGui texture"
            )
        );
        tex->SetStatus(ImTextureStatus_OK);

    } else if (tex->Status == ImTextureStatus_WantUpdates) {

        // Update selected blocks. We only ever write to textures regions which have never been used before!
        Erhe_ImTextureID                         texture_id        = tex->GetTexID();
        const erhe::graphics::Texture_reference* texture_reference = texture_id.texture_reference;
        const erhe::graphics::Texture*           texture           = (texture_reference != nullptr) ? texture_reference->get_referenced_texture() : nullptr;
        ERHE_VERIFY(texture != nullptr);
        log_imgui->trace("updating texture {}", fmt::ptr(texture));

        erhe::graphics::Blit_command_encoder encoder{m_graphics_device};

        auto update_rect = [this, &encoder, texture, tex](ImTextureRect& r) -> void
        {
            const std::span<const std::uint8_t> data{
                static_cast<const std::uint8_t*>(tex->GetPixels()),
                static_cast<size_t>(tex->GetSizeInBytes())
            };
            const std::span<const std::uint8_t> src_span{
                static_cast<const std::uint8_t*>(tex->GetPixels()),
                static_cast<size_t>(tex->GetSizeInBytes())
            };

            const std::size_t buffer_offset = r.x * tex->BytesPerPixel + r.y * tex->GetPitch();

            // TODO We don't necessarily always need full texture size buffer range, just for the rectangle
            const std::size_t                  byte_count   = src_span.size_bytes();
            erhe::graphics::Ring_buffer_client texture_upload_buffer{m_graphics_device, erhe::graphics::Buffer_target::transfer_src, "ImGui Draw Texture Update"};
            erhe::graphics::Ring_buffer_range  buffer_range = texture_upload_buffer.acquire(erhe::graphics::Ring_buffer_usage::CPU_write, byte_count);
            std::span<std::byte>               dst_span     = buffer_range.get_span();
            memcpy(dst_span.data(), src_span.data(), byte_count);
            buffer_range.bytes_written(byte_count);
            buffer_range.close();

            encoder.copy_from_buffer(
                buffer_range.get_buffer()->get_buffer(),                        // source_buffer
                buffer_range.get_byte_start_offset_in_buffer() + buffer_offset, // source_offset
                tex->GetPitch(),                                                // source_bytes_per_row
                tex->GetSizeInBytes(),                                          // source_bytes_per_image
                glm::ivec3{r.w, r.h, 1},                                        // source_size
                texture,                                                        // destination_texture
                0,                                                              // destination_slice
                0,                                                              // destination_level
                glm::ivec3{r.x, r.y, 0}                                         // destination_origin
            );

            buffer_range.release();
        };

        if (tex->Updates.Size < 20) {
            for (ImTextureRect& r : tex->Updates) {
                update_rect(r);
            }
        } else {
            update_rect(tex->UpdateRect);
        }
        tex->SetStatus(ImTextureStatus_OK);

    } else if (tex->Status == ImTextureStatus_WantDestroy && tex->UnusedFrames > 0) {

        const Erhe_ImTextureID                   texture_id        = tex->GetTexID();
        const erhe::graphics::Texture_reference* texture_reference = texture_id.texture_reference;
        const erhe::graphics::Texture*           texture           = (texture_reference != nullptr) ? texture_reference->get_referenced_texture() : nullptr;
        ERHE_VERIFY(texture != nullptr);
        log_imgui->trace("removing texture {}", fmt::ptr(texture));
        m_imgui_textures.erase(
            std::remove_if(
                m_imgui_textures.begin(),
                m_imgui_textures.end(),
                [texture](const std::shared_ptr<erhe::graphics::Texture>& entry) {
                    return entry.get() == texture;
                }
            ),
            m_imgui_textures.end()
        );

        tex->SetTexID(ImTextureID_Invalid);
        tex->SetStatus(ImTextureStatus_Destroyed);
    }
}

void Imgui_renderer::render_draw_data(erhe::graphics::Render_command_encoder& render_encoder)
{
    SPDLOG_LOGGER_TRACE(log_frame, "begin Imgui_renderer::render_draw_data()");

    ERHE_PROFILE_FUNCTION();

    ERHE_DEFER( m_draw_texture_references.clear(); );

    const ImDrawData* draw_data = ImGui::GetDrawData();
    if (draw_data == nullptr) {
        return;
    }

    if (draw_data->Textures != nullptr) {
        for (ImTextureData* tex : *draw_data->Textures) {
            if (tex->Status != ImTextureStatus_OK) {
                update_texture(tex);
            }
        }
    }

    const int fb_width  = (int)(draw_data->DisplaySize.x * draw_data->FramebufferScale.x);
    const int fb_height = (int)(draw_data->DisplaySize.y * draw_data->FramebufferScale.y);
    if (fb_width <= 0 || fb_height <= 0) {
        return;
    }

    erhe::graphics::Scoped_debug_group pass_scope{"Imgui_renderer::render_draw_data()"};
    erhe::graphics::Scoped_gpu_timer   timer     {m_gpu_timer};

    auto&       program                       = m_imgui_program_interface;
    const auto& draw_parameter_struct_offsets = program.draw_parameter_struct_offsets;
    const auto  draw_parameter_entry_size     = program.draw_parameter_struct.get_size_bytes();

    const float scale[2] = {
        2.0f / draw_data->DisplaySize.x,
        2.0f / draw_data->DisplaySize.y
    };
    const float translate[2] = {
        -1.0f - draw_data->DisplayPos.x * scale[0],
        -1.0f - draw_data->DisplayPos.y * scale[1]
    };

    using Ring_buffer_range = erhe::graphics::Ring_buffer_range;
    using erhe::graphics::write;
    constexpr erhe::graphics::Ring_buffer_usage usage{erhe::graphics::Ring_buffer_usage::CPU_write};

#if defined(ERHE_GRAPHICS_LIBRARY_OPENGL)
    gl::enable(gl::Enable_cap::clip_distance0);
    gl::enable(gl::Enable_cap::clip_distance1);
    gl::enable(gl::Enable_cap::clip_distance2);
    gl::enable(gl::Enable_cap::clip_distance3);
#endif // TODO

    // This binds vertex input states (VAO) and shader stages (shader program)
    // and most other state
    render_encoder.set_render_pipeline_state(m_pipeline);

    erhe::graphics::Texture_heap texture_heap{
        m_graphics_device,
        *m_dummy_texture.get(),
        m_nearest_sampler,
        0
    };

    for (int n = 0; n < draw_data->CmdListsCount; n++) {
        erhe::graphics::Scoped_debug_group cmd_list_scope{"CmdList"};

        // log_frame->trace("CmdList {}", n);
        const ImDrawList* cmd_list = draw_data->CmdLists[n];

        std::size_t vertex_byte_count         = static_cast<size_t>(cmd_list->VtxBuffer.size_in_bytes());
        std::size_t index_byte_count          = static_cast<size_t>(cmd_list->IdxBuffer.size_in_bytes());
        std::size_t draw_parameter_byte_count = m_imgui_program_interface.block_offsets.draw_parameter_struct_array;
        std::size_t draw_indirect_byte_count  = 0;

        // This is outer loop going throught batches of commands.
        // When bindless textures are used, there is only one batch.
        // Else, each batch is filled with draw commands until there
        // are no available texture units and new one is needed.
        int cmd_batch_end = 0;
        for (;;) {
            // Pass 1: Count how much memory will be needed, and assign texture unit cache
            int cmd_batch_start = cmd_batch_end;
            {
                int cmd_i;
                for (cmd_i = cmd_batch_start; cmd_i < cmd_list->CmdBuffer.Size; cmd_i++) {
                    const ImDrawCmd* pcmd = &cmd_list->CmdBuffer[cmd_i];
                    if (pcmd->UserCallback != nullptr) {
                        if (pcmd->UserCallback == ImDrawCallback_ResetRenderState) {
                            log_imgui->debug("ImDrawCallback_ResetRenderState - ignored");
                        } else {
                            pcmd->UserCallback(cmd_list, pcmd);
                        }
                    } else {
                        // Check texture heap
                        const ImTextureID                        texture_id        = pcmd->TexRef.GetTexID();
                        const erhe::graphics::Texture_reference* texture_reference = texture_id.texture_reference;
                        const erhe::graphics::Texture*           texture           = (texture_reference != nullptr) ? texture_reference->get_referenced_texture() : nullptr;
                        const erhe::graphics::Sampler&           sampler           = get_sampler(texture_id);
                        if (texture == nullptr) {
                            texture = m_dummy_texture.get();
                        }
                        ERHE_VERIFY(texture != nullptr);
                        const uint64_t shader_handle = texture_heap.allocate(texture, &sampler);
                        if (shader_handle == erhe::graphics::invalid_texture_handle) {
                            break;
                        }
                        draw_parameter_byte_count += draw_parameter_entry_size;
                        draw_indirect_byte_count  += sizeof(erhe::graphics::Draw_indexed_primitives_indirect_command);
                    }
                }
                cmd_batch_end = cmd_i;
            }
            if (
                (draw_parameter_byte_count == 0) ||
                (draw_indirect_byte_count == 0) ||
                (vertex_byte_count == 0) ||
                (index_byte_count == 0)
            ) {
                continue;
            }

            Ring_buffer_range    draw_parameter_buffer_range = m_draw_parameter_buffer.acquire(usage, draw_parameter_byte_count);
            Ring_buffer_range    draw_indirect_buffer_range  = m_draw_indirect_buffer .acquire(usage, draw_indirect_byte_count);
            Ring_buffer_range    vertex_buffer_range         = m_vertex_buffer        .acquire(usage, vertex_byte_count);
            Ring_buffer_range    index_buffer_range          = m_index_buffer         .acquire(usage, index_byte_count);
            size_t               draw_parameter_write_offset = 0;
            size_t               draw_indirect_write_offset  = 0;
            size_t               vertex_write_offset         = 0;
            size_t               index_write_offset          = 0;
            size_t               index_stride                = sizeof(uint16_t);
            std::span<std::byte> draw_parameter_gpu_data     = draw_parameter_buffer_range.get_span();
            std::span<std::byte> draw_indirect_gpu_data      = draw_indirect_buffer_range .get_span();
            std::span<std::byte> vertex_gpu_data             = vertex_buffer_range        .get_span();
            std::span<std::byte> index_gpu_data              = index_buffer_range         .get_span();

            // glVertexArrayElementBuffer() / set_index_buffer() does not take offset.
            // thus index buffer offset must be baked into MDI DrawIndirect records
            std::size_t list_index_offset{index_buffer_range.get_byte_start_offset_in_buffer() / index_stride};

            std::size_t draw_indirect_count{0};

            // Write scale and translate
            const std::span<const float> scale_cpu_data    {&scale    [0], 2};
            const std::span<const float> translate_cpu_data{&translate[0], 2};
            write(draw_parameter_gpu_data, draw_parameter_write_offset + m_imgui_program_interface.block_offsets.scale,     scale_cpu_data);
            write(draw_parameter_gpu_data, draw_parameter_write_offset + m_imgui_program_interface.block_offsets.translate, translate_cpu_data);
            draw_parameter_write_offset += m_imgui_program_interface.block_offsets.draw_parameter_struct_array;

            const ImVec2 clip_off   = draw_data->DisplayPos;
            const ImVec2 clip_scale = draw_data->FramebufferScale;

            // Upload vertex buffer
            const std::span<const uint8_t> vertex_cpu_data{
                reinterpret_cast<const uint8_t*>(cmd_list->VtxBuffer.begin()),
                static_cast<size_t>(cmd_list->VtxBuffer.size_in_bytes())
            };
            write(vertex_gpu_data, vertex_write_offset, vertex_cpu_data);

            // Upload index buffer
            static_assert(sizeof(uint16_t) == sizeof(ImDrawIdx));
            const std::span<const uint16_t> index_cpu_data{
                cmd_list->IdxBuffer.begin(),
                static_cast<size_t>(cmd_list->IdxBuffer.size())
            };
            write(index_gpu_data, index_write_offset, index_cpu_data);

            // Pass 2: fill buffers
            for (int cmd_i = cmd_batch_start; cmd_i < cmd_batch_end; cmd_i++) {
                const ImDrawCmd* pcmd = &cmd_list->CmdBuffer[cmd_i];
                if (pcmd->UserCallback != nullptr) {
                    if (pcmd->UserCallback == ImDrawCallback_ResetRenderState) {
                        log_imgui->info("ImDrawCallback_ResetRenderState - skipped");
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
                            draw_parameter_write_offset + draw_parameter_struct_offsets.clip_rect,
                            clip_rect_cpu_data
                        );

                        // Write texture indices
                        {
                            const ImTextureID                        texture_id        = pcmd->TexRef.GetTexID();
                            const erhe::graphics::Texture_reference* texture_reference = texture_id.texture_reference;
                            const erhe::graphics::Texture*           texture           = (texture_reference != nullptr) ? texture_reference->get_referenced_texture() : nullptr;
                            const erhe::graphics::Sampler&           sampler           = get_sampler(texture_id);

                            if (texture == nullptr) {
                                texture = m_dummy_texture.get();
                            }
                            ERHE_VERIFY(texture != nullptr);

                            const uint64_t shader_handle  = texture_heap.get_shader_handle(texture, &sampler);
                            const uint64_t padding{0};

                            write(
                                draw_parameter_gpu_data,
                                draw_parameter_write_offset + draw_parameter_struct_offsets.texture,
                                erhe::graphics::as_span(shader_handle)
                            );


                            write(
                                draw_parameter_gpu_data,
                                draw_parameter_write_offset + draw_parameter_struct_offsets.padding,
                                erhe::graphics::as_span(padding)
                            );
                        }

                        draw_parameter_write_offset += draw_parameter_entry_size;
                        const erhe::graphics::Draw_indexed_primitives_indirect_command draw_command{
                            pcmd->ElemCount,
                            1,
                            pcmd->IdxOffset + static_cast<uint32_t>(list_index_offset),
                            pcmd->VtxOffset,
                            0
                        };
                        write(
                            draw_indirect_gpu_data,
                            draw_indirect_write_offset,
                            erhe::graphics::as_span(draw_command)
                        );
                        draw_indirect_write_offset += sizeof(erhe::graphics::Draw_indexed_primitives_indirect_command);
                        ++draw_indirect_count;
                    }
                }
            }

            vertex_write_offset += vertex_cpu_data.size_bytes();
            index_write_offset += index_cpu_data.size_bytes();

            draw_parameter_buffer_range.bytes_written(draw_parameter_write_offset);
            draw_parameter_buffer_range.close();
            draw_indirect_buffer_range .bytes_written(draw_indirect_write_offset);
            draw_indirect_buffer_range .close();
            vertex_buffer_range        .bytes_written(vertex_write_offset);
            vertex_buffer_range        .close();
            index_buffer_range         .bytes_written(index_write_offset);
            index_buffer_range         .close();

            const size_t vertex_buffer_binding_offset = vertex_buffer_range.get_byte_start_offset_in_buffer();

            if (draw_indirect_count > 0) {
                erhe::graphics::Buffer* index_buffer  = index_buffer_range.get_buffer()->get_buffer();
                erhe::graphics::Buffer* vertex_buffer = vertex_buffer_range.get_buffer()->get_buffer();

                render_encoder.set_index_buffer(index_buffer);
                render_encoder.set_vertex_buffer(vertex_buffer, vertex_buffer_binding_offset, 0);

                texture_heap.bind();

                ERHE_VERIFY(draw_parameter_buffer_range.get_written_byte_count() > 0);
                m_draw_parameter_buffer.bind(render_encoder, draw_parameter_buffer_range);
                m_draw_indirect_buffer.bind(render_encoder, draw_indirect_buffer_range);

                render_encoder.multi_draw_indexed_primitives_indirect(
                    m_pipeline.data.input_assembly.primitive_topology,
                    erhe::dataformat::Format::format_16_scalar_uint,
                    draw_indirect_buffer_range.get_byte_start_offset_in_buffer(),
                    draw_indirect_count,
                    sizeof(erhe::graphics::Draw_indexed_primitives_indirect_command)
                );

                draw_parameter_buffer_range.release();
                draw_indirect_buffer_range .release();
                vertex_buffer_range        .release();
                index_buffer_range         .release();
            } else {
                draw_parameter_buffer_range.cancel();
                draw_indirect_buffer_range .cancel();
                vertex_buffer_range        .cancel();
                index_buffer_range         .cancel();
            }

            if (cmd_batch_end == cmd_list->CmdBuffer.Size) {
                break;
            } else {
                texture_heap.reset();
            }
        } // outer loop going throught batches of commands
    } // for all cmd lists

#if defined(ERHE_GRAPHICS_LIBRARY_OPENGL)
    gl::disable(gl::Enable_cap::clip_distance0);
    gl::disable(gl::Enable_cap::clip_distance1);
    gl::disable(gl::Enable_cap::clip_distance2);
    gl::disable(gl::Enable_cap::clip_distance3);
#endif // TODO

    texture_heap.unbind();

    SPDLOG_LOGGER_TRACE(log_frame, "end Imgui_renderer::render_draw_data()");
}

} // namespace erhe::imgui

void ImGui_ImplErhe_assert_user_error(const char* message)
{
    erhe::imgui::log_imgui->error("{}", message);
}
