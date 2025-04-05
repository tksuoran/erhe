#pragma once

#include "erhe_dataformat/vertex_format.hpp"
#include "erhe_graphics/fragment_outputs.hpp"
#include "erhe_graphics/gpu_timer.hpp"
#include "erhe_graphics/shader_resource.hpp"
#include "erhe_graphics/shader_stages.hpp"
#include "erhe_graphics/sampler.hpp"
#include "erhe_graphics/state/vertex_input_state.hpp"
#include "erhe_graphics/pipeline.hpp"
#include "erhe_renderer/gpu_ring_buffer.hpp"

#include <imgui/imgui.h>

#include <functional>
#include <memory>
#include <mutex>
#include <set>
#include <vector>

namespace erhe::graphics {
    class Instance;
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
    std::string primary_font  {"res/fonts/SourceSansPro-Regular.otf"};
    std::string mono_font     {"res/fonts/SourceCodePro-Semibold.otf"};
    std::string icon_font     {"res/fonts/materialdesignicons-webfont.ttf"};
    float       font_size     {24.0f};
    float       vr_font_size  {24.0f};
    float       icon_font_size{24.0f};
};

class Imgui_host;

class Imgui_draw_parameter_block_offsets
{
public:
    std::size_t scale                      {0}; // vec2
    std::size_t translate                  {0}; // vec2
    std::size_t draw_parameter_struct_array{0}; // struct
};

// TODO Merge texture and texture_indices
class Imgui_draw_parameter_struct_offsets
{
public:
    std::size_t clip_rect      {0}; // vec4
    std::size_t texture        {0}; // uvec2   for bindless textures
    std::size_t extra          {0}; // uvec2   for bindless textures
    std::size_t texture_indices{0}; // uint[4] for non bindless textures
};

class Imgui_program_interface
{
public:
    static constexpr std::size_t s_texture_unit_count = 32; // for non bindless textures

    explicit Imgui_program_interface(erhe::graphics::Instance& graphics_instance);

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
    Imgui_renderer(erhe::graphics::Instance& graphics_instance, Imgui_settings& settings);

    static constexpr std::size_t s_uivec4_size = 4 * sizeof(uint32_t); // for non bindless textures
    static constexpr std::size_t s_uvec2_size  = 2 * sizeof(uint32_t);
    static constexpr std::size_t s_vec4_size   = 4 * sizeof(float);

    // Public API
    [[nodiscard]] auto get_font_atlas() -> ImFontAtlas*;
    void use_as_backend_renderer_on_context(ImGuiContext* imgui_context);

    void on_font_config_changed(Imgui_settings& settings);

    auto image(
        const std::shared_ptr<erhe::graphics::Texture>& texture,
        int                                             width,
        int                                             height,
        glm::vec2                                       uv0              = {0.0f, 1.0f},
        glm::vec2                                       uv1              = {1.0f, 0.0f},
        glm::vec4                                       background_color = {0.0f, 0.0f, 0.0f, 0.0f},
        glm::vec4                                       tint_color       = {1.0f, 1.0f, 1.0f, 1.0f},
        bool                                            linear           = true
    ) -> bool;

    auto image_button(
        uint32_t                                        id,
        const std::shared_ptr<erhe::graphics::Texture>& texture,
        int                                             width,
        int                                             height,
        glm::vec2                                       uv0              = {0.0f, 1.0f},
        glm::vec2                                       uv1              = {1.0f, 0.0f},
        glm::vec4                                       background_color = {0.0f, 0.0f, 0.0f, 0.0f},
        glm::vec4                                       tint_color       = {1.0f, 1.0f, 1.0f, 1.0f},
        bool                                            linear           = true
    ) -> bool;

    void use(const std::shared_ptr<erhe::graphics::Texture>& texture, const uint64_t handle);
    void render_draw_data();

    void at_end_of_frame(std::function<void()>&& func);
    void next_frame     ();

    auto primary_font   () const -> ImFont*;
    auto mono_font      () const -> ImFont*;
    auto vr_primary_font() const -> ImFont*;
    auto vr_mono_font   () const -> ImFont*;
    auto icon_font      () const -> ImFont*;

    void make_current         (const Imgui_host* imgui_host);
    void register_imgui_host  (Imgui_host* viewport);
    void unregister_imgui_host(Imgui_host* viewport);
    [[nodiscard]] auto get_imgui_hosts() const -> const std::vector<Imgui_host*>&;

    void set_ime_data(ImGuiViewport* viewport, ImGuiPlatformImeData* data);

    void lock_mutex();
    void unlock_mutex();

private:
    void apply_font_config_changes(const Imgui_settings& settings);

    static constexpr std::size_t s_max_draw_count     =    64'000;
    static constexpr std::size_t s_max_index_count    = 2'400'000;
    static constexpr std::size_t s_max_vertex_count   = 2'400'000;

    erhe::graphics::Instance&                m_graphics_instance;
    Imgui_program_interface                  m_imgui_program_interface;
    erhe::graphics::Shader_stages            m_shader_stages;
    erhe::renderer::GPU_ring_buffer          m_vertex_buffer;
    erhe::renderer::GPU_ring_buffer          m_index_buffer;
    erhe::renderer::GPU_ring_buffer          m_draw_parameter_buffer;
    erhe::renderer::GPU_ring_buffer          m_draw_indirect_buffer;
    erhe::graphics::Vertex_input_state       m_vertex_input;
    erhe::graphics::Pipeline                 m_pipeline;

    ImFontAtlas                              m_font_atlas;
    ImFont*                                  m_primary_font   {nullptr};
    ImFont*                                  m_mono_font      {nullptr};
    ImFont*                                  m_vr_primary_font{nullptr};
    ImFont*                                  m_vr_mono_font   {nullptr};
    ImFont*                                  m_icon_font      {nullptr};
    std::shared_ptr<erhe::graphics::Texture> m_dummy_texture;
    std::shared_ptr<erhe::graphics::Texture> m_font_texture;
    erhe::graphics::Sampler                  m_nearest_sampler;
    erhe::graphics::Sampler                  m_linear_sampler;
    erhe::graphics::Sampler                  m_linear_mipmap_linear_sampler;
    erhe::graphics::Gpu_timer                m_gpu_timer;

    std::recursive_mutex                     m_mutex;
    std::vector<Imgui_host*>                 m_imgui_hosts;
    const Imgui_host*                        m_current_host{nullptr}; // current context

    std::set<std::shared_ptr<erhe::graphics::Texture>> m_used_textures;
    std::set<uint64_t>                                 m_used_texture_handles;

    std::vector<std::function<void()>> m_at_end_of_frame;

    Imgui_host* m_ime_host{nullptr};
};

} // namespace erhe::imgui

void ImGui_ImplErhe_assert_user_error(const bool condition, const char* message);

