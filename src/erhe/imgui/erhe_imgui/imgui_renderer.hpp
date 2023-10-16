#pragma once

#include "erhe_graphics/debug.hpp"
#include "erhe_graphics/fragment_outputs.hpp"
#include "erhe_graphics/gpu_timer.hpp"
#include "erhe_graphics/shader_resource.hpp"
#include "erhe_graphics/vertex_attribute_mappings.hpp"
#include "erhe_graphics/vertex_format.hpp"
#include "erhe_renderer/multi_buffer.hpp"

#include <imgui/imgui.h>

#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string_view>
#include <vector>

namespace erhe::graphics {
    class Instance;
    class Sampler;
    class Texture;
    class Vertex_attribute_mappings;
    class Vertex_input_state;
}
namespace erhe::window {
    class Context_window;
}

namespace erhe::imgui {

class Imgui_settings
{
public:
    std::string primary_font{"res/fonts/SourceSansPro-Regular.otf"};
    std::string mono_font   {"res/fonts/SourceCodePro-Semibold.otf"};
    float       font_size   {17.0f};
    float       vr_font_size{22.0f};
};

class Imgui_viewport;

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

class Multi_pipeline
{
public:
    static constexpr std::size_t s_frame_resources_count = 4;

    explicit Multi_pipeline(const std::string_view name);

    void next_frame();
    void allocate(
        const erhe::graphics::Vertex_attribute_mappings& attribute_mappings,
        const erhe::graphics::Vertex_format&             vertex_format,
        erhe::graphics::Shader_stages*                   shader_stages,
        erhe::renderer::Multi_buffer&                    vertex_buffer,
        erhe::renderer::Multi_buffer&                    index_buffer
    );

    [[nodiscard]] auto current_pipeline() -> erhe::graphics::Pipeline&;

protected:
    std::vector<
        std::unique_ptr<
            erhe::graphics::Vertex_input_state
        >
    >                            m_vertex_inputs;
    std::array<
        erhe::graphics::Pipeline,
        s_frame_resources_count
    >                            m_pipelines;
    std::size_t                  m_current_slot{0};
    std::string                  m_name;
};

class Imgui_program_interface
{
public:
    explicit Imgui_program_interface(
        erhe::graphics::Instance& graphics_instance
    );

    void next_frame();

    // scale, translation, clip rectangle, texture indices
    static constexpr std::size_t s_max_draw_count     =   6'000;
    static constexpr std::size_t s_max_index_count    = 300'000;
    static constexpr std::size_t s_max_vertex_count   = 800'000;
    static constexpr std::size_t s_texture_unit_count = 32; // for non bindless textures

    erhe::graphics::Shader_resource     draw_parameter_block;
    erhe::graphics::Shader_resource     draw_parameter_struct;
    Imgui_draw_parameter_struct_offsets draw_parameter_struct_offsets{};
    Imgui_draw_parameter_block_offsets  block_offsets                {};

    erhe::graphics::Fragment_outputs          fragment_outputs;
    erhe::graphics::Vertex_attribute_mappings attribute_mappings;
    erhe::graphics::Vertex_format             vertex_format;
    erhe::graphics::Shader_resource           default_uniform_block; // containing sampler uniforms for non bindless textures
    erhe::graphics::Shader_stages             shader_stages;
    erhe::renderer::Multi_buffer              vertex_buffer;
    erhe::renderer::Multi_buffer              index_buffer;
    erhe::renderer::Multi_buffer              draw_parameter_buffer;
    erhe::renderer::Multi_buffer              draw_indirect_buffer;
    Multi_pipeline                            pipeline;
};

class Imgui_renderer final
{
public:
    Imgui_renderer(
        erhe::graphics::Instance& graphics_instance,
        Imgui_settings&           settings
    );

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
        glm::vec2                                       uv0        = {0.0f, 1.0f},
        glm::vec2                                       uv1        = {1.0f, 0.0f},
        glm::vec4                                       tint_color = {1.0f, 1.0f, 1.0f, 1.0f},
        bool                                            linear     = true
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

    void use(
        const std::shared_ptr<erhe::graphics::Texture>& texture,
        const uint64_t                                  handle
    );
    void render_draw_data();

    void at_end_of_frame(std::function<void()>&& func);
    void next_frame     ();

    auto primary_font   () const -> ImFont*;
    auto mono_font      () const -> ImFont*;
    auto vr_primary_font() const -> ImFont*;
    auto vr_mono_font   () const -> ImFont*;

    void make_current             (const Imgui_viewport* imgui_viewport);
    void register_imgui_viewport  (Imgui_viewport* viewport);
    void unregister_imgui_viewport(Imgui_viewport* viewport);
    [[nodiscard]] auto get_imgui_viewports() const -> const std::vector<Imgui_viewport*>&;
    void lock_mutex();
    void unlock_mutex();

private:
    void apply_font_config_changes(const Imgui_settings& settings);

    erhe::graphics::Instance&                m_graphics_instance;
    Imgui_program_interface                  m_imgui_program_interface;
    ImFontAtlas                              m_font_atlas;
    ImFont*                                  m_primary_font   {nullptr};
    ImFont*                                  m_mono_font      {nullptr};
    ImFont*                                  m_vr_primary_font{nullptr};
    ImFont*                                  m_vr_mono_font   {nullptr};
    std::shared_ptr<erhe::graphics::Texture> m_dummy_texture;
    std::shared_ptr<erhe::graphics::Texture> m_font_texture;
    erhe::graphics::Sampler                  m_nearest_sampler;
    erhe::graphics::Sampler                  m_linear_sampler;
    erhe::graphics::Sampler                  m_linear_mipmap_linear_sampler;
    erhe::graphics::Gpu_timer                m_gpu_timer;

    std::recursive_mutex                     m_mutex;
    std::vector<Imgui_viewport*>             m_imgui_viewports;
    const Imgui_viewport*                    m_current_viewport{nullptr}; // current context

    std::set<std::shared_ptr<erhe::graphics::Texture>> m_used_textures;
    std::set<uint64_t>                                 m_used_texture_handles;

    std::vector<std::function<void()>> m_at_end_of_frame;
};

} // namespace erhe::imgui

void ImGui_ImplErhe_assert_user_error(const bool condition, const char* message);

