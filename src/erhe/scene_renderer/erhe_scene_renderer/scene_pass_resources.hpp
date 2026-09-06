#pragma once

#include "erhe_scene_renderer/camera_buffer.hpp"
#include "erhe_scene_renderer/glyph_buffer.hpp"
#include "erhe_scene_renderer/joint_buffer.hpp"
#include "erhe_scene_renderer/light_buffer.hpp"
#include "erhe_scene_renderer/material_set.hpp"

#include "erhe_graphics/sampler.hpp"
#include "erhe_math/viewport.hpp"

#include <glm/glm.hpp>

#include <memory>
#include <optional>
#include <span>
#include <string_view>

namespace erhe::graphics {
    class Command_buffer;
    class Device;
    class Render_command_encoder;
    class Render_pass;
    class Texture;
    class Texture_heap;
}
namespace erhe::scene {
    class Xformable; using Node = Xformable;
    class Skin;
}
namespace erhe::ui {
    class Glyph_outline_set;
}

namespace erhe::scene_renderer {

class Program_interface;

// Everything a pass needs that does not depend on how its draws are produced.
// Filled by the caller and consumed by Scene_pass_resources::begin_pass();
// re-bucketed mesh spans, cached draw lists and bare fullscreen triangles all
// describe their pass with this same struct.
class Base_render_parameters
{
public:
    erhe::graphics::Render_command_encoder&                            render_encoder;
    const erhe::graphics::Render_pass*                                 render_pass      {nullptr};
    const erhe::math::Viewport&                                        viewport;
    // Per-eye render inputs. Always non-empty when render actually
    // happens: size 1 for single-view passes (desktop viewports,
    // previews, ID render), size N (>= 2) for multiview (XR).
    // Camera_buffer::update_views and the SHADER_MULTIVIEW_COUNT
    // environment key read directly from this span; the multiview shader
    // pipeline kicks in when views.size() >= 2.
    std::span<const Camera_view_input>                                 views{};
    // Camera exposure applied to all entries written to the Camera
    // UBO this pass. Per-camera, not per-view (both eyes share the
    // same exposure), so it lives here rather than in Camera_view_input.
    float                                                              exposure         {1.0f};
    const glm::vec3                                                    ambient_light    {0.0f};
    const Light_projections*                                           light_projections{nullptr};
    const std::span<const std::shared_ptr<erhe::scene::Skin>>&         skins            {};
    // The material slot space this pass resolves through, already updated for
    // this frame (doc/draw_list_material_set_plan.md D5). The pass binds it
    // and looks slots up in it; it never creates, updates or resets one.
    // Null selects the shared empty set, which is what the passes that carry
    // no materials of their own - the grid / sky branch, the depth
    // visualization window - want.
    //
    // A Material_set rather than a draw-list type on purpose: this struct is
    // shared by the bucket, fullscreen and draw-list entry points alike, and
    // binding materials must not require naming a draw list.
    Material_set*                                                      material_source  {nullptr};
    uint32_t                                                           shader_key_boolean_mask_force_enable {0};
    uint32_t                                                           shader_key_boolean_mask_force_disable{0};
    uint64_t                                                           frame_number     {0};
    bool                                                               reverse_depth    {true};
    erhe::math::Depth_range                                            depth_range      {erhe::math::Depth_range::zero_to_one};
    erhe::math::Coordinate_conventions                                 conventions{};
    // Grid composition pass settings (cell sizes, line widths,
    // per-level colors, axis label settings) written to the camera
    // UBO; ignored by passes that do not draw the grid.
    const Grid_parameters                                              grid_parameters  {};
    // Sky composition pass settings (horizon / zenith / ground colors,
    // checker pattern) written to the camera UBO; ignored by passes
    // that do not draw the sky.
    const Sky_parameters                                               sky_parameters   {};
    const std::string_view                                             debug_label{};
};

// The per-pass environment resources of the standard.{vert,frag} shader pair:
// the buffers and the texture heap a pass must update and bind before any draw,
// plus the lightmap / DDGI state those binds and the shader-variant keys read.
//
// Nothing here knows how the draws are produced. That is the point: the
// bucket path (Forward_renderer::render), the draw-list path
// (Draw_list_renderer::render -> Draw_list_scene::draw_color) and the
// fullscreen path (Forward_renderer::draw_primitives) share this prologue and
// differ only afterwards, so it must not name any of them. Extracted from
// Forward_renderer, which owned it and therefore forced every consumer of the
// shared prologue to depend on the renderer that happened to host it.
//
// Note on lifetime: begin_pass() / end_pass() may be called several times
// within one frame, and several times within one pass - Shadow_renderer runs
// the sequence once per light, with a different camera each time - so the
// returned Pass_state carries that call's ring ranges rather than the
// resources object holding per-frame state of its own.
class Scene_pass_resources
{
public:
    // glyph_outline_set is optional: pass nullptr from executables that never
    // draw glyphs (the Glyph_buffer falls back to an empty but bindable
    // buffer, same as when the outline set is invalid).
    //
    // empty_material_set is the shared set a pass with no materials of its own
    // binds (D8). It exists for the texture heap rather than the buffer:
    // giving each such pass its own would add descriptor-set pools for
    // permanently empty content. Owned by the application, not here.
    Scene_pass_resources(
        erhe::graphics::Device&            graphics_device,
        erhe::graphics::Command_buffer&    init_command_buffer,
        Program_interface&                 program_interface,
        const erhe::ui::Glyph_outline_set* glyph_outline_set,
        Material_set&                      empty_material_set
    );
    ~Scene_pass_resources() noexcept;

    Scene_pass_resources (const Scene_pass_resources&) = delete;
    Scene_pass_resources& operator=(const Scene_pass_resources&) = delete;

    // Per-pass shared bindings (camera / joint / light ring ranges);
    // begin_pass() updates + binds them, end_pass() releases them after the
    // draws. Materials are not here: their storage is persistent and owned by
    // the Material_set, so a pass binds and unbinds it and holds nothing.
    class Pass_state
    {
    public:
        std::optional<erhe::graphics::Ring_buffer_range> camera_range{};
        erhe::graphics::Ring_buffer_range                joint_range{};
        erhe::graphics::Ring_buffer_range                light_range{};
        // The set begin_pass() bound, for end_pass() to unbind. Never null
        // after begin_pass().
        Material_set*                                    material_source{nullptr};
    };

    auto begin_pass(
        const Base_render_parameters& base,
        const glm::uvec4&             debug_joint_indices,
        const std::span<glm::vec4>&   debug_joint_colors,
        const erhe::scene::Node*      debug_target_joint
    ) -> Pass_state;
    void end_pass(Pass_state& state, erhe::graphics::Render_command_encoder& render_encoder);

    // Baked lightmap atlas sampled by standard.frag through s_lightmap
    // (doc/lightmap_baking_plan.md phase 5). Null binds the black fallback;
    // per-primitive lightmap_scale_offset gates sampling per draw.
    void set_lightmap_texture(const std::shared_ptr<erhe::graphics::Texture>& texture) { m_lightmap_texture = texture; }
    // Viewport lightmap filtering: bicubic B-spline reconstruction when
    // true (the default), plain bilinear when false.
    void set_lightmap_bicubic(const bool enabled) { m_lightmap_bicubic = enabled; }

    // DDGI probe volume sampled by standard.frag (doc/ddgi-plan.md phase 6).
    // A default-constructed Ddgi_parameters (or null textures) means no
    // volume: the USE_DDGI variant axis stays off and the flat ambient term
    // is used, exactly as before DDGI existed.
    void set_ddgi(
        const Ddgi_parameters&                          parameters,
        const std::shared_ptr<erhe::graphics::Texture>& irradiance_texture,
        const std::shared_ptr<erhe::graphics::Texture>& distance_texture,
        const std::shared_ptr<erhe::graphics::Texture>& probe_data_texture
    )
    {
        m_ddgi                    = parameters;
        m_ddgi_irradiance_texture = irradiance_texture;
        m_ddgi_distance_texture   = distance_texture;
        m_ddgi_probe_data_texture = probe_data_texture;
    }
    void clear_ddgi()
    {
        m_ddgi = Ddgi_parameters{};
        m_ddgi_irradiance_texture.reset();
        m_ddgi_distance_texture  .reset();
        m_ddgi_probe_data_texture.reset();
    }

    // Read by the shader-variant key derivation in the owning renderer, which
    // must select the USE_DDGI / lightmap axes from the same state begin_pass()
    // binds.
    [[nodiscard]] auto get_ddgi            () const -> const Ddgi_parameters& { return m_ddgi; }
    [[nodiscard]] auto get_lightmap_bicubic() const -> bool                   { return m_lightmap_bicubic; }

    // The buffers, for the parts of a pass that are not the prologue: filling
    // per-primitive ranges, binding the per-light control buffer, and the
    // fullscreen path's hand-rolled variant of the sequence.
    [[nodiscard]] auto get_camera_buffer  () -> Camera_buffer&                 { return m_camera_buffer; }
    [[nodiscard]] auto get_glyph_buffer   () -> Glyph_buffer&                  { return m_glyph_buffer; }
    [[nodiscard]] auto get_joint_buffer   () -> Joint_buffer&                  { return m_joint_buffer; }
    [[nodiscard]] auto get_light_buffer   () -> Light_buffer&                  { return m_light_buffer; }
    // The set a pass would bind for the given source: the source itself, or
    // the shared empty set when it is null.
    [[nodiscard]] auto resolve_material_source(Material_set* material_source) -> Material_set&;
    [[nodiscard]] auto get_lightmap_texture         () const -> erhe::graphics::Texture* { return m_lightmap_texture.get(); }
    [[nodiscard]] auto get_ddgi_irradiance_texture  () const -> erhe::graphics::Texture* { return m_ddgi_irradiance_texture.get(); }
    [[nodiscard]] auto get_ddgi_distance_texture    () const -> erhe::graphics::Texture* { return m_ddgi_distance_texture.get(); }
    [[nodiscard]] auto get_ddgi_probe_data_texture  () const -> erhe::graphics::Texture* { return m_ddgi_probe_data_texture.get(); }

private:
    erhe::graphics::Device&                       m_graphics_device;
    Program_interface&                            m_program_interface;
    Camera_buffer                                 m_camera_buffer;
    Glyph_buffer                                  m_glyph_buffer;
    Joint_buffer                                  m_joint_buffer;
    Light_buffer                                  m_light_buffer;
    // The material buffer, the texture heap and the fallback texture / sampler
    // pair all moved to Material_set: a heap handle baked into a material
    // record is only meaningful in the heap it was allocated from, and with
    // persistence only for as long as that heap keeps the allocation, so the
    // two are reset and rewritten together in the one place either is written.
    Material_set&                                 m_empty_material_set;
    std::shared_ptr<erhe::graphics::Texture>      m_lightmap_texture;
    Ddgi_parameters                               m_ddgi{};
    std::shared_ptr<erhe::graphics::Texture>      m_ddgi_irradiance_texture;
    std::shared_ptr<erhe::graphics::Texture>      m_ddgi_distance_texture;
    std::shared_ptr<erhe::graphics::Texture>      m_ddgi_probe_data_texture;
    bool                                          m_lightmap_bicubic{true};
};

} // namespace erhe::scene_renderer
