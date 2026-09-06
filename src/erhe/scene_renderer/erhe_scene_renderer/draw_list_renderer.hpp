#pragma once

#include "erhe_scene_renderer/draw_indirect_buffer.hpp"
#include "erhe_scene_renderer/draw_list.hpp"
#include "erhe_scene_renderer/primitive_buffer.hpp"
#include "erhe_scene_renderer/scene_pass_resources.hpp"

#include <glm/glm.hpp>

#include <cstdint>
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

// The draw-list colour entry point (doc/draw_list_renderer_requirements.md
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
        // .x: 0xffffffffu = no active joint for the joint_weight_ramp debug
        // mode ("missing data" magenta), anything else = one is active and
        // marked per-slot in the joint buffer (see debug_target_joint).
        // .y: 1 = show zero-weight vertices as black.
        const glm::uvec4&                                      debug_joint_indices{0xffffffffu, 0, 0, 0};
        const std::span<glm::vec4>&                            debug_joint_colors{};
        // Active joint for the joint_weight_ramp debug mode, matched per
        // joint slot by Joint_buffer::update(). nullptr = none.
        const erhe::scene::Node*                               debug_target_joint{nullptr};
        const erhe::graphics::Color_blend_state*               color_blend_override{nullptr};
    };

    auto render(const Render_parameters& parameters) -> Draw_statistics;

private:
    erhe::graphics::Device& m_graphics_device;
    Scene_pass_resources&   m_pass_resources;
    Draw_indirect_buffer    m_draw_indirect_buffer;
    Primitive_buffer        m_primitive_buffer;
};

} // namespace erhe::scene_renderer
