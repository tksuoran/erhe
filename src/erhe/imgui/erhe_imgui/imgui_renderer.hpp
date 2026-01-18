#pragma once

#include "erhe_dataformat/vertex_format.hpp"
#include "erhe_graphics/fragment_outputs.hpp"
#include "erhe_graphics/gpu_timer.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/render_pipeline_state.hpp"
#include "erhe_graphics/ring_buffer_client.hpp"
#include "erhe_graphics/shader_resource.hpp"
#include "erhe_graphics/shader_stages.hpp"
#include "erhe_graphics/sampler.hpp"
#include "erhe_graphics/state/vertex_input_state.hpp"

#include <imgui/imgui.h>

#include <functional>
#include <memory>
#include <mutex>
#include <vector>

namespace erhe::graphics {
    class Device;
    class Render_command_encoder;
    class Sampler;
    class Texture;
    class Vertex_input_state;
}
namespace erhe::window {
    class Context_window;
}

namespace erhe::imgui {

class Imgui_settings
{
public:
    std::string primary_font             {"res/fonts/SourceSansPro-Regular.otf"};
    std::string mono_font                {"res/fonts/SourceCodePro-Semibold.otf"};
    std::string material_design_font     {"res/fonts/materialdesignicons-webfont.ttf"};
    std::string icon_font                {"res/fonts/icons.ttf"};
    float       font_size                {24.0f};
    float       vr_font_size             {24.0f};
    float       material_design_font_size{0.0f};
    float       icon_font_size           {0.0f};
};

class Imgui_host;

class Imgui_draw_parameter_block_offsets
{
public:
    std::size_t scale                      {0}; // vec2
    std::size_t translate                  {0}; // vec2
    std::size_t draw_parameter_struct_array{0}; // struct
};

class Imgui_draw_parameter_struct_offsets
{
public:
    std::size_t clip_rect{0}; // vec4
    std::size_t texture  {0}; // uvec2
    std::size_t padding  {0}; // uvec2
};

class Imgui_program_interface
{
public:
    explicit Imgui_program_interface(erhe::graphics::Device& graphics_device);

    // scale, translation, clip rectangle, texture indices
    erhe::graphics::Shader_resource     draw_parameter_block;
    erhe::graphics::Shader_resource     draw_parameter_struct;
    Imgui_draw_parameter_struct_offsets draw_parameter_struct_offsets{};
    Imgui_draw_parameter_block_offsets  block_offsets                {};

    erhe::graphics::Fragment_outputs fragment_outputs;
    erhe::dataformat::Vertex_format  vertex_format;
    erhe::graphics::Shader_resource  default_uniform_block; // containing sampler uniforms for non bindless textures
};

class Imgui_renderer final
{
public:
    Imgui_renderer(erhe::graphics::Device& graphics_device, Imgui_settings& settings);

    static constexpr std::size_t s_uivec4_size = 4 * sizeof(uint32_t); // for non bindless textures
    static constexpr std::size_t s_uvec2_size  = 2 * sizeof(uint32_t);
    static constexpr std::size_t s_vec4_size   = 4 * sizeof(float);

    // Public API
    [[nodiscard]] auto get_font_atlas() -> ImFontAtlas*;
    void use_as_backend_renderer_on_context(ImGuiContext* imgui_context);

    void on_font_config_changed(Imgui_settings& settings);

    auto image(
        const erhe::graphics::Texture_reference* texture_reference,
        int                                      width,
        int                                      height,
        glm::vec2                                uv0              = {0.0f, 1.0f},
        glm::vec2                                uv1              = {1.0f, 0.0f},
        glm::vec4                                background_color = {0.0f, 0.0f, 0.0f, 0.0f},
        glm::vec4                                tint_color       = {1.0f, 1.0f, 1.0f, 1.0f},
        erhe::graphics::Filter                   filter           = erhe::graphics::Filter::nearest,
        erhe::graphics::Sampler_mipmap_mode      mipmap_mode      = erhe::graphics::Sampler_mipmap_mode::not_mipmapped
    ) -> bool;

    auto image_button(
        uint32_t                                 id,
        const erhe::graphics::Texture_reference* texture_reference,
        int                                      width,
        int                                      height,
        glm::vec2                                uv0              = {0.0f, 1.0f},
        glm::vec2                                uv1              = {1.0f, 0.0f},
        glm::vec4                                background_color = {0.0f, 0.0f, 0.0f, 0.0f},
        glm::vec4                                tint_color       = {1.0f, 1.0f, 1.0f, 1.0f},
        erhe::graphics::Filter                   filter           = erhe::graphics::Filter::nearest,
        erhe::graphics::Sampler_mipmap_mode      mipmap_mode      = erhe::graphics::Sampler_mipmap_mode::not_mipmapped
    ) const -> bool;

    void render_draw_data(erhe::graphics::Render_command_encoder& encoder);

    void begin_frame    ();
    void at_end_of_frame(std::function<void()>&& func);
    void next_frame     ();

    auto primary_font        () const -> ImFont*;
    auto mono_font           () const -> ImFont*;
    auto vr_primary_font     () const -> ImFont*;
    auto vr_mono_font        () const -> ImFont*;
    auto material_design_font() const -> ImFont*;
    auto icon_font           () const -> ImFont*;

    void make_current         (const Imgui_host* imgui_host);
    void register_imgui_host  (Imgui_host* viewport);
    void unregister_imgui_host(Imgui_host* viewport);
    [[nodiscard]] auto get_imgui_hosts   () const -> const std::vector<Imgui_host*>&;
    [[nodiscard]] auto get_imgui_settings() const -> const Imgui_settings&;

    void set_ime_data(ImGuiViewport* viewport, ImGuiPlatformImeData* data);

    void lock_mutex();
    void unlock_mutex();

private:
    void update_texture(ImTextureData* tex);

    [[nodiscard]] auto get_sampler(
        erhe::graphics::Filter              filter,
        erhe::graphics::Sampler_mipmap_mode mipmap_mode
    ) const -> const erhe::graphics::Sampler&;

    static constexpr std::size_t s_max_draw_count     =    64'000;
    static constexpr std::size_t s_max_index_count    = 2'400'000;
    static constexpr std::size_t s_max_vertex_count   = 2'400'000;

    erhe::graphics::Device&                  m_graphics_device;
    Imgui_program_interface                  m_imgui_program_interface;
    erhe::graphics::Shader_stages            m_shader_stages;
    erhe::graphics::Ring_buffer_client       m_vertex_buffer;
    erhe::graphics::Ring_buffer_client       m_index_buffer;
    erhe::graphics::Ring_buffer_client       m_draw_parameter_buffer;
    erhe::graphics::Ring_buffer_client       m_draw_indirect_buffer;
    erhe::graphics::Vertex_input_state       m_vertex_input;
    erhe::graphics::Render_pipeline_state    m_pipeline;
    Imgui_settings                           m_imgui_settings;

    ImFontAtlas                              m_font_atlas;
    ImFont*                                  m_primary_font        {nullptr};
    ImFont*                                  m_mono_font           {nullptr};
    ImFont*                                  m_vr_primary_font     {nullptr};
    ImFont*                                  m_vr_mono_font        {nullptr};
    ImFont*                                  m_material_design_font{nullptr};
    ImFont*                                  m_icon_font           {nullptr};
    std::shared_ptr<erhe::graphics::Texture> m_dummy_texture;
    erhe::graphics::Sampler                  m_nearest_sampler;
    erhe::graphics::Sampler                  m_linear_sampler;
    erhe::graphics::Sampler                  m_nearest_mipmap_nearest_sampler;
    erhe::graphics::Sampler                  m_linear_mipmap_nearest_sampler;
    erhe::graphics::Sampler                  m_nearest_mipmap_linear_sampler;
    erhe::graphics::Sampler                  m_linear_mipmap_linear_sampler;
    erhe::graphics::Gpu_timer                m_gpu_timer;

    std::recursive_mutex                     m_mutex;
    std::vector<Imgui_host*>                 m_imgui_hosts;
    const Imgui_host*                        m_current_host{nullptr}; // current context

    std::vector<std::function<void()>> m_at_end_of_frame;

    std::vector<std::shared_ptr<erhe::graphics::Texture>> m_imgui_textures;

    Imgui_host* m_ime_host{nullptr};
};

} // namespace erhe::imgui

void ImGui_ImplErhe_assert_user_error(const bool condition, const char* message);

