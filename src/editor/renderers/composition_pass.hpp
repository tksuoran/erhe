#pragma once

#include "erhe_renderer/enums.hpp"
#include "erhe_scene_renderer/camera_buffer.hpp"
#include "erhe_scene_renderer/primitive_buffer.hpp"
#include "erhe_scene_renderer/mesh_memory.hpp"

#include "erhe_item/item.hpp"
#include "erhe_primitive/enums.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_utility/debug_label.hpp"

#include <glm/glm.hpp>

#include <functional>
#include <optional>

namespace erhe::graphics { class Base_render_pipeline; class Color_blend_state; }
namespace erhe::scene    { using Layer_id = uint64_t; }

struct Render_style_data;
struct Render_style_appearance;

namespace editor {

class Render_context;
class Scene_root;

class Composition_pass_data
{
public:
    bool                                                                   enabled{true}; // TODO consider using Item visibility flag
    // Tool / rendertarget overlay meshes must not be scaled by camera exposure
    // (issue #230). When set, Composition_pass::render forces exposure = 1.0
    // instead of camera->get_exposure(), so the gizmo handles and the hotbar
    // rendertarget mesh look identical regardless of the camera exposure.
    bool                                                                   ignore_exposure{false};
    // Overlay passes (tool / rendertarget) are rendered AFTER post-processing
    // when it is enabled (in a separate render pass that loads the content depth
    // attachment), so bloom / tonemap do not affect them. When post-processing
    // is disabled they render inline in the content pass. Composer::render uses
    // this to split the content phase from the overlay phase.
    bool                                                                   overlay{false};
    // ID-buffer edge-line method: this pass is a lit content polygon-fill pass
    // that should paint edge lines from the face-ID buffer when the method is
    // active (Render_context::edge_id_texture set). Gated so the method is only
    // applied where Primitive_buffer also stamps the face-id base into
    // primitive.color (these passes get the Face_id_base_provider).
    bool                                                                   edge_lines_from_id_capable{false};
    // ID-buffer edge-line method, corner-cap overlay: this pass renders the
    // expanded fill soup (primitive_mode solid_wireframe) with the
    // EDGE_LINES_CORNER_CAP variant force-enabled, painting the edge color near
    // real shared vertices to fill the gaps the per-pixel id match leaves there.
    // When set, the pass's edge-line color AND width are sourced from its
    // appearance (edge_lines mode) and forwarded to the camera UBO (the cap reads
    // edge_line_color + edge_line_width + vp_y_sign); no face-ID texture needed.
    bool                                                                   edge_lines_corner_cap{false};
    uint32_t                                                               content_wide_line_group{0};
    std::vector<erhe::scene::Layer_id>                                     mesh_layers{};
    std::size_t                                                            non_mesh_vertex_count{0};
    std::vector<erhe::graphics::Base_render_pipeline*>                     base_render_pipelines{};
    erhe::scene_renderer::Blending_mode_policy                             blending_mode_policy{erhe::scene_renderer::Blending_mode_policy::not_set};
    erhe::primitive::Primitive_mode                                        primitive_mode{erhe::primitive::Primitive_mode::polygon_fill};
    erhe::Item_filter                                                      filter{};
    // When set, overrides the per-bucket color-blend state in Forward_renderer
    // (otherwise chosen by blend mode). Used by the selection stencil-mask pass
    // to write depth/stencil only (color_writes_disabled).
    const erhe::graphics::Color_blend_state*                               color_blend_override{nullptr};
    std::shared_ptr<Scene_root>                                            override_scene_root{};
    uint32_t                                                               shader_key_force_enable_mask{0};
    uint32_t                                                               shader_key_force_disable_mask{0};
    erhe::graphics::Shader_stages*                                         shader_stages{nullptr};
    // Force a SHADER_DEBUG variant for this pass's own meshes, instead of taking
    // the scene view's debug mode. The default path can only ever DROP the view's
    // mode per mesh (its filter is content-only), never turn one on - which is
    // what a pass needs when the variant IS the intended look rather than a debug
    // view. Used by the bone pass: Shader_debug::vdotn is exactly the N.V shading
    // solid bones want, so no new shader or Shader_key axis is needed.
    // When set, shader_debug_override_filter selects which meshes it applies to.
    std::optional<erhe::scene_renderer::Shader_debug>                      shader_debug_override{};
    erhe::Item_filter                                                      shader_debug_override_filter{};
    // Grid settings (cell sizes, line widths, per-level colors, axis
    // label settings) forwarded to the camera UBO; read by grid.frag.
    erhe::scene_renderer::Grid_parameters                                  grid_parameters{};
    // Sky settings (horizon / zenith / ground colors, checker pattern)
    // forwarded to the camera UBO; read by sky.frag.
    erhe::scene_renderer::Sky_parameters                                   sky_parameters{};
    std::optional<erhe::scene_renderer::Primitive_interface_settings>      primitive_settings{};
    std::function<void()>                                                  begin{};
    std::function<void()>                                                  end{};
    std::function<const Render_style_data&(const Render_context& context)> get_render_style{};
    // Editor-global appearance (colors / widths / color sources) for this pass's
    // primitive_mode, selected to match get_render_style (Default vs Selection
    // appearance). Read via get_primitive_settings when primitive_settings is
    // not explicitly set. The matching per-view Render_style_data still gates
    // visibility through get_render_style / is_primitive_mode_enabled.
    std::function<const Render_style_appearance&(const Render_context& context)> get_appearance{};
    // Optional render-time activation predicate, evaluated against the current
    // viewport's Render_context (e.g. to gate on a render-style flag). The pass
    // is skipped when this is set and returns false. Distinct from
    // get_render_style / is_primitive_mode_enabled, which gate on the pass's own
    // primitive_mode.
    std::function<bool(const Render_context& context)>                     is_enabled{};
};

// Why a pass did or did not draw on its last render() call. Recorded so the
// composition pass state can be inspected after the fact (Composition Passes
// window, MCP get_composition_passes) - a pass that returns early emits no
// debug marker at all, so a frame capture cannot distinguish "skipped" from
// "drew nothing", which makes a mis-gated pass look like missing geometry.
enum class Composition_pass_result : unsigned int {
    never_rendered = 0,      // render() has not been called yet
    disabled,                // data.enabled is false
    is_enabled_false,        // the is_enabled(context) predicate returned false
    no_scene_root,           // no scene root to render
    primitive_mode_disabled, // the render style has this primitive_mode off
    no_mesh_layers,          // none of data.mesh_layers resolved in the scene
    submitted,               // reached the draw submission (Forward_renderer::render)
    submitted_draw_lists     // reached the draw submission through the scene's Draw_list_scene
};

[[nodiscard]] auto c_str(Composition_pass_result result) -> const char*;

class Composition_pass : public erhe::Item<erhe::Item_base, erhe::Item_base, Composition_pass>
{
public:
    explicit Composition_pass(const Composition_pass&);
    Composition_pass& operator=(const Composition_pass&);
    ~Composition_pass() noexcept override;

    explicit Composition_pass(std::string_view name);

    virtual void render(const Render_context& context);
    void imgui();

    // Implements Item_base
    static constexpr std::string_view static_type_name{"Composition_pass"};
    [[nodiscard]] static constexpr auto get_static_type() -> uint64_t { return erhe::Item_type::composition_pass; }

    Composition_pass_data data;

    // Outcome of the most recent render() call, and the scene view it was for.
    // Written once per render() (a plain store, not a per-frame sync).
    [[nodiscard]] auto get_last_result          () const -> Composition_pass_result { return m_last_result; }
    [[nodiscard]] auto get_last_scene_view_name () const -> const std::string&      { return m_last_scene_view_name; }
    [[nodiscard]] auto get_last_mesh_count      () const -> std::size_t             { return m_last_mesh_count; }
    // Entries drawn by the draw-list path in the most recent render() (0 when
    // the pass went through Forward_renderer::render()).
    [[nodiscard]] auto get_last_draw_list_entry_count() const -> std::size_t        { return m_last_draw_list_entry_count; }

private:
    std::vector<std::span<const std::shared_ptr<erhe::scene::Mesh>>> m_mesh_spans;
    Composition_pass_result                                         m_last_result{Composition_pass_result::never_rendered};
    std::string                                                     m_last_scene_view_name{};
    std::size_t                                                     m_last_mesh_count{0};
    std::size_t                                                     m_last_draw_list_entry_count{0};
};

}
