#pragma once

#include "erhe_dataformat/vertex_format.hpp"
#include "erhe_graphics/bind_group_layout.hpp"
#include "erhe_graphics/fragment_outputs.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/render_pipeline.hpp"
#include "erhe_graphics/render_pipeline_state.hpp"
#include "erhe_graphics/ring_buffer_client.hpp"
#include "erhe_graphics/shader_resource.hpp"
#include "erhe_graphics/shader_stages.hpp"
#include "erhe_graphics/sampler.hpp"
#include "erhe_graphics/state/vertex_input_state.hpp"
#include "erhe_graphics/texture_reference.hpp"
#include "erhe_utility/debug_label.hpp"

#include <imgui/imgui.h>

#include <functional>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace erhe::graphics {
    class Command_buffer;
    class Device;
    class Render_command_encoder;
    class Render_pass;
    class Sampler;
    class Texture;
    class Texture_heap;
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
    float       scale_factor             { 1.0f};
    float       font_size                {24.0f};
    float       vr_font_size             {24.0f};
    float       material_design_font_size{0.0f};
    float       icon_font_size           {0.0f};
};

class Imgui_draw_data_copy;
class Imgui_host;

// A texture ImGui manages (the font atlas pages). The ImGui phase creates it
// and gives ImGui its id at once (Imgui_renderer::copy_draw_data); the render
// thread creates the GPU texture when it runs the create request. Only the
// render thread reads or writes `texture`.
class Imgui_texture : public erhe::graphics::Texture_reference
{
public:
    ~Imgui_texture() noexcept override;

    [[nodiscard]] auto get_referenced_texture() const -> const erhe::graphics::Texture* override;

    std::shared_ptr<erhe::graphics::Texture> texture;
};

enum class Imgui_texture_request_kind : unsigned int {
    create = 0,
    update,
    destroy
};

// One ImGui texture request, copied by the ImGui phase of frame_index and run
// by the render thread when it records that frame (or the first frame it
// records after it). pixels holds whole rows of the ImTextureData:
// rows y .. y + height of a pitch of `pitch` bytes; the upload starts at
// column x.
class Imgui_texture_request
{
public:
    Imgui_texture_request_kind     kind           {Imgui_texture_request_kind::create};
    uint64_t                       frame_index    {0};
    std::shared_ptr<Imgui_texture> texture        {};
    erhe::dataformat::Format       format         {erhe::dataformat::Format::format_undefined};
    int                            texture_width  {0};
    int                            texture_height {0};
    int                            x              {0};
    int                            y              {0};
    int                            width          {0};
    int                            height         {0};
    int                            bytes_per_pixel{0};
    int                            pitch          {0};
    std::vector<std::uint8_t>      pixels         {};
};

class Imgui_draw_parameter_block_offsets
{
public:
    std::size_t scale                      {0}; // vec2
    std::size_t translate                  {0}; // vec2
    std::size_t clip_rotation              {0}; // vec4 (column-major mat2: c0=xy, c1=zw)
    std::size_t draw_parameter_struct_array{0}; // struct
};

class Imgui_draw_parameter_struct_offsets
{
public:
    std::size_t clip_rect   {0}; // vec4
    std::size_t texture     {0}; // uvec2
    std::size_t array_layer {0}; // uint
    std::size_t padding     {0}; // uint
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
};

class Draw_texture_parameters
{
public:
    std::shared_ptr<erhe::graphics::Texture_reference> texture_reference{};
    ImGuiID                                            id               {0};
    int                                                width            {0};
    int                                                height           {0};
    // Identity UVs show uploaded image content upright on every backend:
    // the loaders and Image_transfer write the image's top row into texture
    // row 0, which V = 0 samples regardless of the device's texture origin.
    // Render-target content depends on the texture origin; draw it with
    // Imgui_renderer::get_rtt_uv0() / get_rtt_uv1().
    glm::vec2                                          uv0              {0.0f, 0.0f};
    glm::vec2                                          uv1              {1.0f, 1.0f};
    glm::vec4                                          background_color {0.0f, 0.0f, 0.0f, 0.0f};
    glm::vec4                                          tint_color       {1.0f, 1.0f, 1.0f, 1.0f};
    erhe::graphics::Filter                             filter           {erhe::graphics::Filter::nearest};
    erhe::graphics::Sampler_mipmap_mode                mipmap_mode      {erhe::graphics::Sampler_mipmap_mode::not_mipmapped};
    int                                                array_layer      {-1}; // -1 = sampler2D, >= 0 = sampler2DArray layer
    int                                                lod              {-1}; // -1 = use mipmap_mode as-is, >= 0 = snap to exact mip via sampler min_lod == max_lod
    erhe::utility::Debug_label                         debug_label      {};
};

class Imgui_renderer final
{
public:
    Imgui_renderer(
        erhe::graphics::Device&         graphics_device,
        erhe::graphics::Command_buffer& init_command_buffer,
        Imgui_settings&                 settings
    );
    ~Imgui_renderer() noexcept;

    static constexpr std::size_t s_uivec4_size = 4 * sizeof(uint32_t); // for non bindless textures
    static constexpr std::size_t s_uvec2_size  = 2 * sizeof(uint32_t);
    static constexpr std::size_t s_vec4_size   = 4 * sizeof(float);

    // Public API
    [[nodiscard]] auto get_rtt_uv0() const -> glm::vec2; // UV for top-left when displaying render-to-texture content
    [[nodiscard]] auto get_rtt_uv1() const -> glm::vec2; // UV for bottom-right when displaying render-to-texture content
    [[nodiscard]] auto get_font_atlas() -> ImFontAtlas*;
    void use_as_backend_renderer_on_context(ImGuiContext* imgui_context);

    void on_font_config_changed(Imgui_settings& settings);

    auto image       (Draw_texture_parameters&& parameters) -> bool;
    auto image_button(Draw_texture_parameters&& parameters) -> bool;

    // The ImGui phase (main thread), right after ImGui::Render() of the
    // current context, through Imgui_host::copy_draw_data(): turns the
    // context's texture requests (ImTextureData create / update / destroy)
    // into Imgui_texture_request entries for the render thread, so every
    // command's texture id is known, then deep-copies the context's
    // ImDrawData into `out` (cleared and refilled, capacity kept) stamped
    // with the ImGui phase's frame index. A context that rendered nothing
    // this frame leaves `out` stamped and not valid.
    void copy_draw_data(Imgui_draw_data_copy& out);

    // Render thread: runs the texture requests the ImGui phase queued for
    // the frame being recorded and earlier, in order. Before any render pass
    // begins - uploads use Blit_command_encoder which Vulkan does not permit
    // inside a render pass. The first ImGui host node of a frame runs them;
    // the others find the queue empty.
    void update_draw_data_textures(erhe::graphics::Command_buffer& command_buffer);

    // transform applies the presentation pre-rotation (Android landscape on a
    // portrait-native panel) to the final swapchain pass: it rotates clip space
    // and swaps the framebuffer viewport for 90/270. Defaults to identity, which
    // every offscreen / render-to-texture host (and all desktop platforms) uses.
    // Render thread: draws a host's draw data copy of the frame being
    // recorded (Imgui_host::get_draw_data()).
    void render_draw_data(
        erhe::graphics::Render_command_encoder& encoder,
        const erhe::graphics::Render_pass&      render_pass,
        const Imgui_draw_data_copy&             draw_data,
        erhe::graphics::Surface_transform       transform = erhe::graphics::Surface_transform::identity
    );

    // Main thread, the start of the ImGui phase: frame_index is the device
    // frame index of the frame being drawn (the application passes it, so
    // the ImGui phase reads no device state another thread advances). The
    // draw data copies and the texture references image() / image_button()
    // keep alive are filed under it.
    void begin_frame    (uint64_t frame_index);

    // The ImGui phase's frame index (begin_frame); main thread.
    [[nodiscard]] auto get_imgui_frame_index() const -> uint64_t;
    // The device frame the calling render thread records
    // (Device::get_frame_index(), role-keyed).
    [[nodiscard]] auto get_render_frame_index() const -> uint64_t;
    [[nodiscard]] auto get_number_of_frames_in_flight() const -> std::size_t;
    // Render thread, after the frame's ImGui hosts drew: releases the texture
    // references render_draw_data() displaced.
    void next_frame     ();

    // The ImGui host rendergraph nodes call wait_for_draw_data() before they
    // read the frame's draw data (update_draw_data_textures /
    // render_draw_data); it runs the function the application set, which
    // blocks until the main thread's ImGui phase has published the draw
    // data of the frame being rendered (doc/editor/render_thread.md
    // section 2.5). Without a function set it returns at once.
    void set_draw_data_wait(std::function<void()> wait);
    void wait_for_draw_data();

    // Drops every texture reference held for drawing (see
    // m_retained_texture_references). A reference can be a Rendergraph_node
    // owned by the application, so at teardown the application calls this -
    // with the GPU idle, before it destroys those owners - instead of letting
    // ~Imgui_renderer drop the last reference after the owner is gone.
    void release_texture_references();

    auto primary_font        () const -> ImFont*;
    auto mono_font           () const -> ImFont*;
    auto vr_primary_font     () const -> ImFont*;
    auto vr_mono_font        () const -> ImFont*;
    auto material_design_font() const -> ImFont*;
    auto icon_font           () const -> ImFont*;

    void make_current         (const Imgui_host* imgui_host);
    [[nodiscard]] auto get_current_host() const -> const Imgui_host*;
    void register_imgui_host  (Imgui_host* viewport);
    void unregister_imgui_host(Imgui_host* viewport);
    [[nodiscard]] auto get_imgui_hosts   () const -> const std::vector<Imgui_host*>&;
    [[nodiscard]] auto get_imgui_settings() const -> const Imgui_settings&;

    void set_ime_data(ImGuiViewport* viewport, ImGuiPlatformImeData* data);

    void lock_mutex();
    void unlock_mutex();

private:
    // One device frame in flight: the texture references image() /
    // image_button() pushed while the ImGui phase drew that frame. The ImGui
    // phase appends; the render thread moves them into
    // m_retained_texture_references when it draws the frame.
    class Frame_slot
    {
    public:
        std::vector<std::shared_ptr<erhe::graphics::Texture_reference>> draw_texture_references;
    };

    void queue_texture_request  (ImTextureData* tex);
    void push_texture_rect      (Imgui_texture_request_kind kind, const std::shared_ptr<Imgui_texture>& texture, ImTextureData* tex, const ImTextureRect& rect);
    void run_texture_request    (Imgui_texture_request& request, erhe::graphics::Command_buffer& command_buffer);
    void push_draw_texture_reference(const std::shared_ptr<erhe::graphics::Texture_reference>& texture_reference);

    [[nodiscard]] auto get_sampler(const Erhe_ImTextureID& texture_id) const -> const erhe::graphics::Sampler&;
    [[nodiscard]] auto get_sampler(
        erhe::graphics::Filter              filter,
        erhe::graphics::Sampler_mipmap_mode mipmap_mode
    ) const -> const erhe::graphics::Sampler&;
    [[nodiscard]] auto get_lod_clamped_sampler(
        erhe::graphics::Filter              filter,
        erhe::graphics::Sampler_mipmap_mode mipmap_mode,
        int                                 lod
    ) const -> const erhe::graphics::Sampler&;

    static constexpr std::size_t s_max_draw_count     =    64'000;
    static constexpr std::size_t s_max_index_count    = 2'400'000;
    static constexpr std::size_t s_max_vertex_count   = 2'400'000;

    erhe::graphics::Device&                       m_graphics_device;
    Imgui_program_interface                       m_imgui_program_interface;
    erhe::graphics::Bind_group_layout             m_bind_group_layout;
    erhe::graphics::Shader_stages                 m_shader_stages;
    erhe::graphics::Ring_buffer_client            m_vertex_buffer;
    erhe::graphics::Ring_buffer_client            m_index_buffer;
    erhe::graphics::Ring_buffer_client            m_draw_parameter_buffer;
    erhe::graphics::Ring_buffer_client            m_draw_indirect_buffer;
    erhe::graphics::Vertex_input_state            m_vertex_input;
    erhe::graphics::Base_render_pipeline          m_pipeline;
    Imgui_settings                                m_imgui_settings;
    std::unique_ptr<erhe::graphics::Texture_heap> m_texture_heap;

    ImFontAtlas                              m_font_atlas;
    ImFont*                                  m_primary_font        {nullptr};
    ImFont*                                  m_mono_font           {nullptr};
    ImFont*                                  m_vr_primary_font     {nullptr};
    ImFont*                                  m_vr_mono_font        {nullptr};
    ImFont*                                  m_material_design_font{nullptr};
    ImFont*                                  m_icon_font           {nullptr};
    std::shared_ptr<erhe::graphics::Texture> m_dummy_texture;
    std::shared_ptr<erhe::graphics::Texture> m_dummy_array_texture;
    erhe::graphics::Sampler                  m_nearest_sampler;
    erhe::graphics::Sampler                  m_linear_sampler;
    erhe::graphics::Sampler                  m_nearest_mipmap_nearest_sampler;
    erhe::graphics::Sampler                  m_linear_mipmap_nearest_sampler;
    erhe::graphics::Sampler                  m_nearest_mipmap_linear_sampler;
    erhe::graphics::Sampler                  m_linear_mipmap_linear_sampler;

    // Lazy cache of samplers with (min_lod, max_lod) clamped to a specific mip
    // level. Keyed by (filter, mipmap_mode, lod) packed into a uint64_t so the
    // Draw_texture_parameters::lod knob can snap sampling to an exact mip of a
    // full mipmapped texture without creating a per-mip Texture view
    // (originally needed on the since-removed GL 4.1 path, where
    // ARB_texture_view was unavailable).
    mutable std::unordered_map<uint64_t, std::unique_ptr<erhe::graphics::Sampler>> m_lod_clamped_samplers;

    // TODO Re-add a per-render-pass GPU timer; the imgui draw render pass
    // is owned by the caller (passed in to render_draw_data), not here.

    std::recursive_mutex                     m_mutex;
    std::vector<Imgui_host*>                 m_imgui_hosts;
    const Imgui_host*                        m_current_host{nullptr}; // current context

    std::function<void()>              m_draw_data_wait;

    // ImGui phase (main thread): the frame it draws, and the live ImGui
    // textures (ImTextureData with an id).
    uint64_t                                    m_imgui_frame_index{0};
    std::vector<std::shared_ptr<Imgui_texture>> m_imgui_textures;

    // Texture requests from the ImGui phase, oldest first; the render thread
    // runs and removes those of the frame it records and earlier.
    std::mutex                         m_texture_request_mutex;
    std::vector<Imgui_texture_request> m_texture_requests;

    std::vector<Frame_slot> m_frame_slots; // one per device frame in flight

    // Render thread: the references of the frame drawn last.
    std::vector<std::shared_ptr<erhe::graphics::Texture_reference>> m_retained_texture_references;
    // Texture references displaced from m_retained_texture_references during
    // render_draw_data(); released in next_frame(). render_draw_data() runs
    // inside Rendergraph::execute() (under the rendergraph mutex), and dropping
    // the last reference there can destroy a Rendergraph_node (e.g. a viewport
    // overlay node of a closed scene), whose destructor re-locks the
    // rendergraph mutex to unregister itself.
    std::vector<std::shared_ptr<erhe::graphics::Texture_reference>> m_expired_texture_references;

    Imgui_host* m_ime_host{nullptr};
};

} // namespace erhe::imgui

void ImGui_ImplErhe_assert_user_error(const bool condition, const char* message);

