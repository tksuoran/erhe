#pragma once

#include "erhe_scene_renderer/draw_indirect_buffer.hpp"
#include "erhe_scene_renderer/draw_list.hpp"
#include "erhe_scene_renderer/primitive_buffer.hpp"
#include "erhe_scene_renderer/scene_pass_resources.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <optional>
#include <span>

namespace erhe {
    class Item_filter;
}
namespace erhe::graphics {
    class Base_render_pipeline;
    class Color_blend_state;
    class Device;
}
namespace erhe::scene {
    class Xformable; using Node = Xformable;
}

namespace erhe::scene_renderer {

class Draw_list_scene;
class Program_interface;

// The draw-list colour entry point (doc/erhe/draw_list_renderer.md
// R8/R8a): the same per-pass prologue / epilogue as the bucket path - camera,
// joints, lights, and the bind of a material set the pass is handed, all of it
// Scene_pass_resources' - but the draws come from the scene's persistent
// Draw_list_scene instead of from re-bucketed mesh spans. Colour purpose only;
// shadow maps go through Shadow_renderer.
//
// A renderer of its own rather than a member of Forward_renderer, so the
// bucket path never names a draw-list type. The two share the prologue by
// both holding a reference to one Scene_pass_resources, which owns no
// per-frame state (begin_pass returns a Pass_state), and they own their own
// per-pass record buffers.
class Draw_list_renderer
{
public:
    Draw_list_renderer(
        erhe::graphics::Device& graphics_device,
        Program_interface&      program_interface,
        Scene_pass_resources&   pass_resources
    );
    ~Draw_list_renderer() noexcept;

    Draw_list_renderer (const Draw_list_renderer&) = delete;
    void operator=     (const Draw_list_renderer&) = delete;

    class Render_parameters
    {
    public:
        Base_render_parameters                                 base;
        Draw_list_scene&                                       draw_list_scene;
        const std::span<erhe::graphics::Base_render_pipeline*> base_render_pipelines;
        std::span<const erhe::scene::Layer_id>                 layers{};
        Draw_blending_selection                                blending;
        Primitive_interface_settings                           primitive_settings{};
        const erhe::Item_filter                                filter{};
        uint32_t                                               shadow_filter{0};
        uint32_t                                               shadow_bias{1};
        uint32_t                                               shadow_technique{0};
        uint32_t                                               shadow_depth_bits{0};
        // ERHE_SPECULAR_ANTI_ALIASING variant axis of the variants that call
        // anisotropic_brdf() (set_specular_anti_aliasing_axis()).
        Specular_anti_aliasing                                 specular_anti_aliasing{Specular_anti_aliasing::normal_kernel};
        // FLAT_DRAW_COLOR variant axis (Color_environment::flat_draw_color).
        bool                                                   flat_draw_color{false};
        // .x: 0xffffffffu = no active joint for the joint_weight_ramp debug
        // mode ("missing data" magenta), anything else = one is active and
        // marked per-slot in the joint buffer (see debug_target_joint).
        // .y: 1 = show zero-weight vertices as black.
        const glm::uvec4&                                      debug_joint_indices{0xffffffffu, 0, 0, 0};
        const std::span<const glm::vec4>&                      debug_joint_colors{};
        // Active joint for the joint_weight_ramp debug mode, matched per
        // joint slot by Joint_buffer::update(). nullptr = none.
        const erhe::scene::Node*                               debug_target_joint{nullptr};
        const erhe::graphics::Color_blend_state*               color_blend_override{nullptr};
        // The pass's culling view in draw_list_scene's view set of the frame
        // (add_color_cull_view() before the frame's classification);
        // Cull_view::none draws unculled (multiview passes).
        Cull_view                                              cull_view{Cull_view::none};
        // The pass's selection in the frame's GPU command generation
        // (add_color_command_selection()); none: CPU-written commands.
        Command_selection_id                                   command_selection{Command_selection_id::none};
    };

    auto render(const Render_parameters& parameters) -> Draw_statistics;

    // Render phase, with the pass's culling view (add_color_cull_view())
    // and before the frame's classification: registers what the color pass
    // draws with this renderer's Primitive_buffer
    // (Draw_list_scene::add_color_command_selection()), for
    // Render_parameters::command_selection. Returns none where
    // Draw_list_scene::add_color_command_selection() does, and while the
    // Draw_indirect_buffer's debug
    // index count clamp is on (the GPU commands carry the raw counts;
    // refused and logged once).
    [[nodiscard]] auto add_color_command_selection(
        Draw_list_scene&                       draw_list_scene,
        uint64_t                               frame_index,
        Cull_view                              cull_view,
        const erhe::Item_filter&               filter,
        std::span<const erhe::scene::Layer_id> layers,
        Draw_blending_selection                blending,
        const Gpu_command_key&                 key
    ) -> Command_selection_id;

    // The inputs of a color pass's culling view: the pass's views and the
    // depth conventions its camera block is written with
    // (Base_render_parameters).
    class Color_cull_view_parameters
    {
    public:
        std::span<const Camera_view_input> views{};
        bool                               reverse_depth{true};
        erhe::math::Depth_range            depth_range  {erhe::math::Depth_range::zero_to_one};
        erhe::math::Coordinate_conventions conventions  {};
        // The view's potentially visible set query box (world space;
        // doc/plans/pvs.md "Runtime consumption"), empty (max < min, the
        // default) for a view without the PVS rule.
        glm::vec3                          pvs_query_min{ 0.0f};
        glm::vec3                          pvs_query_max{-1.0f};
    };
    // Render phase, before the frame's culling classification
    // (doc/erhe/draw_list_renderer.md Q6, doc/plans/culling.md phase 6a):
    // adds the culling view of a single-view color pass - the planes of the
    // view's clip_from_world, built the way Camera_buffer builds the matrix
    // - to draw_list_scene's view set of frame_index and returns it for
    // Render_parameters::cull_view. Multiview passes (and a view without a
    // camera) draw unculled: Cull_view::none.
    [[nodiscard]] static auto add_color_cull_view(
        Draw_list_scene&                  draw_list_scene,
        uint64_t                          frame_index,
        const Color_cull_view_parameters& parameters
    ) -> Cull_view;

    // The clip_from_world of a single-view color pass, built the way
    // Camera_buffer builds it (add_color_cull_view()'s frustum and the
    // occlusion test rasterize with it); empty for multiview passes and a
    // view without a camera.
    [[nodiscard]] static auto get_color_clip_from_world(const Color_cull_view_parameters& parameters) -> std::optional<glm::mat4>;
    // The single-view color pass's camera, homogeneous: (world position, 1)
    // for a perspective projection, (the camera's world +Z axis, 0) for an
    // orthographic one (the direction toward a camera at infinity); empty
    // where get_color_clip_from_world() is.
    [[nodiscard]] static auto get_color_view_eye(const Color_cull_view_parameters& parameters) -> std::optional<glm::vec4>;

    // Render phase, with the pass's culling view and before the frame's
    // classification (doc/plans/occlusion_culling.md): registers the color
    // view as an occlusion view of draw_list_scene
    // (Draw_list_scene::add_occlusion_view()) with the inward near plane of
    // the view's clip_from_world. Returns the view, or none (no occlusion
    // pass this frame: multiview, no camera, no GPU classification with
    // occlusion on).
    [[nodiscard]] static auto add_color_occlusion_view(
        Draw_list_scene&                  draw_list_scene,
        uint64_t                          frame_index,
        Cull_view                         cull_view,
        const Color_cull_view_parameters& parameters,
        const Gpu_command_key&            key
    ) -> Cull_view;

private:
    erhe::graphics::Device& m_graphics_device;
    Scene_pass_resources&   m_pass_resources;
    Draw_indirect_buffer    m_draw_indirect_buffer;
    Primitive_buffer        m_primitive_buffer;
    bool                    m_index_count_clamp_refusal_logged{false};
};

} // namespace erhe::scene_renderer
