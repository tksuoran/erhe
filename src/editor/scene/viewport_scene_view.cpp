// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "scene/viewport_scene_view.hpp"

#include "ImViewGuizmo.h"
#include "config/generated/viewport_config_data.hpp"
#include "config/generated/editor_settings_config.hpp"
#include "app_context.hpp"
#include "editor_log.hpp"
#include "app_message_bus.hpp"
#include "app_rendering.hpp"
#include "app_scenes.hpp"
#include "config/generated/editor_settings_config.hpp"
#include "config/generated/sky_config.hpp"
#include "renderers/composition_pass.hpp"
#include "renderers/id_renderer.hpp"
#include "renderers/programs.hpp"
#include "renderers/render_style.hpp"
#include "renderers/render_context.hpp"
#include "renderers/sky_renderer.hpp"
#include "rendergraph/shadow_render_node.hpp"
#include "scene/scene_root.hpp"
#include "scene/viewport_scene_views.hpp"
#include "time.hpp"
#include "tools/mesh_component_selection_tool.hpp"
#include "tools/selection_tool.hpp"
#include "tools/tools.hpp"
#include "transform/transform_tool.hpp"
#include "windows/viewport_config_window.hpp"
#include "windows/scene_view_config_window.hpp"

#include "erhe_defer/defer.hpp"
#include "erhe_geometry/geometry.hpp"
#include "erhe_graphics/command_buffer.hpp"
#include "erhe_graphics/compute_command_encoder.hpp"
#include "erhe_graphics/render_command_encoder.hpp"
#include "erhe_graphics/render_pass.hpp"
#include "erhe_graphics/scoped_debug_group.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_hash/xxhash.hpp"
#include "erhe_imgui/imgui_renderer.hpp"
#include "erhe_imgui/imgui_helpers.hpp"
#include "erhe_log/log_glm.hpp"
#include "erhe_math/math_util.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_renderer/debug_renderer.hpp"
#include "erhe_renderer/text_renderer.hpp"
#include "erhe_rendergraph/rendergraph.hpp"
#include "erhe_rendergraph/rendergraph_node.hpp"
#include "erhe_scene/camera.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_primitive/buffer_mesh.hpp"
#include "erhe_primitive/primitive.hpp"
#include "erhe_scene_renderer/content_wide_line_renderer.hpp"
#include "erhe_scene_renderer/forward_renderer.hpp"
#include "erhe_scene_renderer/joint_buffer.hpp"
#include "erhe_scene_renderer/primitive_buffer.hpp"
#include "erhe_utility/bit_helpers.hpp"

#include <algorithm>

#include <glm/gtx/matrix_operation.hpp>

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>

using erhe::geometry::mesh_facet_normalf;
using erhe::geometry::to_glm_vec3;

// #include "IconsMaterialDesignIcons.h"
#define ICON_MDI_AXIS_ARROW                               "\xf3\xb0\xb5\x89" // U+F0D49
#define ICON_MDI_CAMERA                                   "\xf3\xb0\x84\x80" // U+F0100
#define ICON_MDI_DOTS_TRIANGLE                            "\xf3\xb1\x97\xbe" // U+F15FE
#define ICON_MDI_EYE                                      "\xf3\xb0\x88\x88" // U+F0208
#define ICON_MDI_PALETTE                                  "\xf3\xb0\x8f\x98" // U+F03D8
#define ICON_MDI_EYE_SETTINGS_OUTLINE                     "\xf3\xb0\xa1\xae" // U+F086E

namespace editor {

void Face_id_registry::clear()
{
    m_bases.clear();
    m_next_base = 1u;
}

void Face_id_registry::add_mesh(const erhe::scene::Mesh& mesh)
{
    if (m_bases.find(&mesh) != m_bases.end()) {
        return; // already assigned this frame
    }
    const std::vector<erhe::scene::Mesh_primitive>& primitives = mesh.get_primitives();
    std::vector<uint32_t> bases;
    bases.reserve(primitives.size());
    for (const erhe::scene::Mesh_primitive& mesh_primitive : primitives) {
        bases.push_back(m_next_base);
        // Space the next base by this primitive's fill index count, an upper
        // bound on its facet count, so base + facet never reaches the next base.
        uint32_t span = 1u;
        if (mesh_primitive.primitive) {
            const erhe::primitive::Buffer_mesh* buffer_mesh = mesh_primitive.primitive->get_renderable_mesh();
            if (buffer_mesh != nullptr) {
                const erhe::primitive::Index_range index_range = buffer_mesh->index_range(erhe::primitive::Primitive_mode::polygon_fill);
                span = static_cast<uint32_t>(std::max<std::size_t>(index_range.index_count, std::size_t{1}));
            }
        }
        m_next_base += span;
    }
    m_bases.emplace(&mesh, std::move(bases));
}

auto Face_id_registry::get_face_id_base(const erhe::scene::Mesh& mesh, const std::size_t primitive_index) const -> uint32_t
{
    const auto it = m_bases.find(&mesh);
    if ((it == m_bases.end()) || (primitive_index >= it->second.size())) {
        return 0u; // unregistered -> EDGE_LINES_FROM_ID treats it as "no edge"
    }
    return it->second[primitive_index];
}

using erhe::graphics::Vertex_input_state;
using erhe::graphics::Input_assembly_state;
using erhe::graphics::Rasterization_state;
using erhe::graphics::Depth_stencil_state;
using erhe::graphics::Color_blend_state;
using erhe::graphics::Render_pass;
using erhe::graphics::Renderbuffer;
using erhe::graphics::Texture;

int Viewport_scene_view::s_serial = 0;

Viewport_scene_view::Viewport_scene_view(
    Editor_settings_store*                      editor_settings_store,
    const Viewport_config_data&                 viewport_config_data,
    App_context&                                context,
    erhe::rendergraph::Rendergraph&             rendergraph,
    Tools&                                      tools,
    const std::string_view                      name,
    const char*                                 ini_label,
    const std::shared_ptr<Scene_root>&          scene_root,
    const std::shared_ptr<erhe::scene::Camera>& camera,
    int                                         msaa_sample_count
)
    : Scene_view              {context, editor_settings_store, name, make_viewport_config(viewport_config_data)}
    , Texture_rendergraph_node{
        erhe::rendergraph::Texture_rendergraph_node_create_info{
            .rendergraph          = rendergraph,
            .debug_label          = erhe::utility::Debug_label{"Viewport window"},
            .output_key           = erhe::rendergraph::Rendergraph_node_key::viewport_texture,
            .color_format         = erhe::dataformat::Format::format_16_vec4_float,
            .depth_stencil_format = erhe::dataformat::Format::format_d32_sfloat_s8_uint,
            .sample_count         = msaa_sample_count
        }
    }
    , m_name           {name}
    , m_ini_label      {ini_label}
    , m_tool_scene_root{tools.get_tool_scene_root()}
    , m_camera         {camera}
{
    set_scene_root(scene_root);

    m_navigation_gizmo = std::make_unique<ImViewGuizmo::Context>();

    // We need shadows rendered before
    register_input("shadow_maps", erhe::rendergraph::Rendergraph_node_key::shadow_maps);

    // We need rendertarget texture(s) rendered before
    register_input("rendertarget texture", erhe::rendergraph::Rendergraph_node_key::rendertarget_texture);

    // Apply the persisted shader_debug / renderer_choice immediately: unlike
    // scene / camera they have no scene dependency, so they do not need the
    // deferred name resolution done in resolve_pending_scene_and_camera().
    // m_pending_scene_and_camera was staged by the Scene_view constructor.
    if (m_scene_and_camera_restore_pending) {
        const int shader_debug_value = m_pending_scene_and_camera.shader_debug;
        if ((shader_debug_value >= 0) && (shader_debug_value < IM_ARRAYSIZE(erhe::scene_renderer::c_shader_debug_strings))) {
            m_shader_debug = static_cast<erhe::scene_renderer::Shader_debug>(shader_debug_value);
        }
        const int renderer_value = m_pending_scene_and_camera.renderer_choice;
        if ((renderer_value >= 0) && (renderer_value < IM_ARRAYSIZE(c_renderer_choice_strings))) {
            m_renderer_choice = static_cast<Renderer_choice>(renderer_value);
        }
    }
}

Viewport_scene_view::~Viewport_scene_view() noexcept
{
    m_context.scene_views->erase(this);
}

void Viewport_scene_view::execute_rendergraph_node(erhe::graphics::Command_buffer& command_buffer)
{
    ERHE_PROFILE_FUNCTION();

    if ((m_projection_viewport.width < 1) || (m_projection_viewport.height < 1)) {
        return;
    }

    bool do_render = true;
    const std::shared_ptr<Scene_root>& scene_root = get_scene_root();
    if (!scene_root) {
        do_render = false;
    }
    std::shared_ptr<erhe::scene::Camera> camera = m_camera.lock();
    if (!camera) {
        do_render = false;
    }

    // 1-element views span for the single-view path. Declared in this
    // outer scope so the backing object outlives every use of
    // context.views (Render_context's views field is a span pointing
    // here; it is read by Forward_renderer, Content_wide_line_renderer,
    // and downstream renderables for the rest of this function).
    erhe::scene_renderer::Camera_view_input single_view_input{};
    if (camera) {
        single_view_input = erhe::scene_renderer::Camera_view_input{
            .projection = camera->projection(),
            .node       = camera->get_node(),
            .viewport   = m_projection_viewport
        };
    }
    Render_context context{
        .command_buffer      = &command_buffer,
        .encoder             = nullptr, // filled in later once we start render pass
        .app_context         = m_context,
        .scene_view          = *this,
        .viewport_config     = m_viewport_config,
        .camera              = camera.get(),
        .viewport_scene_view = this,
        .viewport            = m_projection_viewport,
        .shader_debug        = m_shader_debug,
        .views               = camera
            ? std::span<const erhe::scene_renderer::Camera_view_input>(&single_view_input, 1)
            : std::span<const erhe::scene_renderer::Camera_view_input>{}
    };

    // The ID renderer always runs when the viewport is hovered -- it is
    // the only source of correct picks for skinned meshes (the raytrace
    // BVH is rest-pose only). The id_renderer.enabled config knob is
    // repurposed inside App_rendering::render_id to switch the ID pass
    // between skinned-only (default) and skinned + static (force-id).
    // It also runs when a region selection scan is pending, so a programmatic
    // (non-hovered) scan still renders the ID pass it reads back.
    const bool scan_pending = (m_context.id_renderer != nullptr) && m_context.id_renderer->has_pending_scan();
    if (do_render && (m_is_scene_view_hovered || scan_pending)) {
        m_context.app_rendering->render_id(context);
    }

    erhe::graphics::Device& graphics_device = m_rendergraph.get_graphics_device();

    // Set when the ID-buffer edge-line method is active this frame: the wide-line
    // renderer runs in id mode, the registry is rebuilt, and an edge-id pre-pass
    // is rendered before the main pass for the polygon-fill shader to sample.
    bool use_id_edge_lines = false;

    if (do_render) {
        const erhe::renderer::View debug_view = erhe::renderer::Debug_renderer::view_from_camera(
            *context.camera, context.viewport, get_conventions()
        );
        m_context.debug_renderer->begin_frame(
            context.viewport,
            std::span<const erhe::renderer::View>(&debug_view, 1)
        );
        if (
            (m_context.content_wide_line_renderer != nullptr) &&
            m_context.content_wide_line_renderer->is_enabled()
        ) {
            // Push the editor-global content edge-line config (method + bias) to
            // the renderer each frame; it is edited in the Settings window.
            const Content_edge_lines_config& cel = m_context.editor_settings->content_edge_lines;
            // The ID-buffer method is compute-only (id mode runs in the compute
            // expansion) and needs the Id_renderer that owns the face-ID buffer.
            use_id_edge_lines =
                cel.use_id_buffer &&
                (m_context.id_renderer != nullptr) &&
                m_context.content_wide_line_renderer->uses_compute();
            m_context.content_wide_line_renderer->set_id_mode(use_id_edge_lines);
            if (use_id_edge_lines) {
                // Tent geometry with NO toward-camera lift: each half-quad's depth
                // equals its own face plane, so the edge-id pass's depth test picks
                // the frontmost face's edge per pixel.
                m_context.content_wide_line_renderer->set_use_tent(true);
                m_context.content_wide_line_renderer->set_line_bias_margin(0.0f);
                m_context.content_wide_line_renderer->set_line_bias_clamp(0.0f);
            } else {
                m_context.content_wide_line_renderer->set_use_tent(cel.use_tent);
                m_context.content_wide_line_renderer->set_line_bias_margin(cel.line_bias_margin);
                m_context.content_wide_line_renderer->set_line_bias_clamp(cel.line_bias_clamp);
            }
            m_context.content_wide_line_renderer->begin_frame();
        }

        m_context.tools        ->render_viewport_tools(context);
        m_context.app_rendering->render_viewport_renderables(context);

        if (m_context.debug_renderer->use_compute()) {
            {
                erhe::graphics::Scoped_debug_group debug_group{command_buffer, "debug_renderer->compute()"};
                erhe::graphics::Compute_command_encoder compute_encoder = graphics_device.make_compute_command_encoder(command_buffer);
                m_context.debug_renderer->compute(compute_encoder);
            }
            // Compute -> vertex barrier paired with the dispatches
            // inside debug_renderer->compute(). Both bits required: the
            // post-unification debug-line vertex shader reads triangles
            // via SSBO (shader_storage_barrier_bit), and some debug-line
            // paths still consume the vertex buffer through the input
            // assembler (vertex_attrib_array_barrier_bit). Must be
            // emitted after the compute encoder scope ends; on Metal the
            // cb cannot be split while the compute encoder is open.
            command_buffer.memory_barrier(
                erhe::graphics::Memory_barrier_mask::vertex_attrib_array_barrier_bit |
                erhe::graphics::Memory_barrier_mask::shader_storage_barrier_bit
            );
        }

        // Content wide line compute path, all edge line passes
        if (m_context.content_wide_line_renderer != nullptr && m_context.content_wide_line_renderer->is_enabled()) {
            const auto content_scene_root = context.scene_view.get_scene_root();
            if (content_scene_root) {
                erhe::scene::Scene* scene = content_scene_root->get_hosted_scene();
                if (scene != nullptr) {
                    erhe::graphics::Scoped_debug_group content_wide_line_renderer_debug_group{command_buffer, "content_wide_line_renderer"};

                    // Helper to feed meshes from a composition pass to the content wide line renderer
                    auto feed_pass = [&](const Composition_pass* pass) {
                        if (pass == nullptr) {
                            return;
                        }
                        auto& data = pass->data;
                        if ((data.primitive_mode != erhe::primitive::Primitive_mode::edge_lines) || !data.enabled) {
                            return;
                        }
                        erhe::scene_renderer::Primitive_interface_settings settings;
                        if (data.primitive_settings.has_value()) {
                            settings = data.primitive_settings.value();
                        } else if (data.get_appearance) {
                            settings = get_primitive_settings(data.get_appearance(context), data.primitive_mode);
                        }
                        const glm::vec4 color      = settings.constant_color0;
                        const float     line_width = settings.constant_size;
                        const auto&     filter     = data.filter;
                        // In id mode every edge goes to one group (0) and carries
                        // the face-id base provider; the color is unused (the
                        // compute writes face ids). In color mode keep the pass's
                        // selection group and no provider.
                        const uint32_t  group      = use_id_edge_lines ? 0u : data.content_wide_line_group;
                        const erhe::scene_renderer::Face_id_base_provider* provider =
                            use_id_edge_lines ? &m_face_id_registry : nullptr;

                        for (const auto layer_id : data.mesh_layers) {
                            const auto mesh_layer = scene->get_mesh_layer_by_id(layer_id);
                            if (mesh_layer) {
                                for (const auto& mesh : mesh_layer->meshes) {
                                    if (filter(mesh->get_flag_bits())) {
                                        if (use_id_edge_lines) {
                                            m_face_id_registry.add_mesh(*mesh);
                                            // Collect this content mesh for the seed
                                            // pass: it renders exactly the visible
                                            // content the edge method covers.
                                            m_seed_meshes.push_back(mesh);
                                        }
                                        m_context.content_wide_line_renderer->add_mesh(
                                            *m_context.mesh_memory,
                                            *mesh,
                                            color,
                                            line_width,
                                            group,
                                            provider
                                        );
                                    }
                                }
                            }
                        }
                    };

                    if (use_id_edge_lines) {
                        // ID-buffer method: rebuild the per-frame face-id base
                        // registry and feed only the regular content edge passes
                        // (both selection states into one id group). The animated
                        // selection / translucent outlines are a separate visual
                        // and stay on the wide-line path; they are simply off here.
                        m_face_id_registry.clear();
                        m_seed_meshes.clear(); // capacity kept; refilled by feed_pass
                        {
                            erhe::graphics::Scoped_debug_group feed_debug_group{command_buffer, "edge_id_not_selected"};
                            feed_pass(m_context.app_rendering->edge_lines_not_selected.get());
                        }
                        {
                            erhe::graphics::Scoped_debug_group feed_debug_group{command_buffer, "edge_id_selected"};
                            feed_pass(m_context.app_rendering->edge_lines_selected.get());
                        }
                    } else {
                    // Feed selection outline with animated color/width. The
                    // outline appearance is editor-global (Selection_outline_style),
                    // shared by all scene views; edited in the Settings window.
                    if (m_context.app_rendering->selection_outline) {
                        const Selection_outline_style& sel_outline = m_context.editor_settings->selection_outline;
                        const int64_t   t0_ns         = m_context.time->get_host_system_time_ns();
                        const double    t0            = static_cast<double>(t0_ns) / 1'000'000'000.0;
                        const float     period        = 1.0f / sel_outline.selection_highlight_frequency;
                        const float     t1            = static_cast<float>(::fmod(t0, period));
                        const float     t2            = static_cast<float>(0.5f + (2.0f * std::abs(2.0f * (t1 / period - std::floor(t1 / period + 0.5f))) - 1.0f) * 0.5f);
                        const glm::vec4 outline_color = glm::mix(sel_outline.selection_highlight_low, sel_outline.selection_highlight_high, t2);
                        const float     outline_width = sel_outline.selection_highlight_width_low * (1.0f - t2) + sel_outline.selection_highlight_width_high * t2;

                        // Temporarily override primitive_settings for the animated outline
                        m_context.app_rendering->selection_outline->data.primitive_settings = erhe::scene_renderer::Primitive_interface_settings{
                            .color_source    = erhe::scene_renderer::Primitive_color_source::constant_color,
                            .constant_color0 = outline_color,
                            .size_source     = erhe::scene_renderer::Primitive_size_source::constant_size,
                            .constant_size   = outline_width
                        };
                        erhe::graphics::Scoped_debug_group feed_debug_group{command_buffer, "selection_outline"};
                        feed_pass(m_context.app_rendering->selection_outline.get());
                    }

                    // Feed regular edge line passes
                    {
                        erhe::graphics::Scoped_debug_group feed_debug_group{command_buffer, "opaque_edge_lines_not_selected"};
                        feed_pass(m_context.app_rendering->edge_lines_not_selected.get());
                    }
                    {
                        erhe::graphics::Scoped_debug_group feed_debug_group{command_buffer, "opaque_edge_lines_selected"};
                        feed_pass(m_context.app_rendering->edge_lines_selected.get());
                    }
                    {
                        erhe::graphics::Scoped_debug_group feed_debug_group{command_buffer, "translucent_outline"};
                        feed_pass(m_context.app_rendering->translucent_outline.get());
                    }
                    } // end else (color edge-line feed)
                }
            }

            {
                // Cache per-frame view + depth conventions on the renderer
                // so both compute() and render() consume the same Per_view_camera
                // entries without rebuilding them.
                m_context.content_wide_line_renderer->set_view_params(
                    context.views,
                    get_reverse_depth(),
                    get_depth_range(),
                    get_conventions()
                );

                // Hand the per-frame joint UBO/SSBO range over to the renderer.
                // Forward_renderer's later render() will call Joint_buffer::update()
                // again with its own debug joint indices/colors; both updates
                // allocate disjoint ring ranges. The renderer holds this range
                // until end_frame() releases it -- the geometry-shader backend
                // (future) needs the joint UBO bound at render time, which is
                // after the compute encoder closes.
                const auto          scene_root_joints = context.scene_view.get_scene_root();
                erhe::scene::Scene* scene_for_joints  = scene_root_joints ? scene_root_joints->get_hosted_scene() : nullptr;
                if ((scene_for_joints != nullptr) && (m_context.forward_renderer != nullptr)) {
                    erhe::scene_renderer::Joint_buffer& joint_buffer = m_context.forward_renderer->get_joint_buffer();
                    erhe::graphics::Ring_buffer_range   joint_buffer_range = joint_buffer.update(
                        glm::uvec4{0, 0, 0, 0}, {}, scene_for_joints->get_skins()
                    );
                    m_context.content_wide_line_renderer->set_joint_buffer(&joint_buffer, std::move(joint_buffer_range));
                }

                // ID-buffer edge-line method: render the SEED face-ID buffer
                // (visible content fill -> frontmost-visible-face id per pixel) in
                // its own render pass BEFORE the edge compute + edge-id pass, so the
                // seed-masked edge-id draw can reject edge fragments that do not land
                // on their own face's visible surface. Run order per the handoff:
                // seed -> edge compute -> edge-id (masked) -> fill.
                if (
                    use_id_edge_lines &&
                    (m_context.id_renderer != nullptr) &&
                    !m_seed_meshes.empty() &&
                    (context.camera != nullptr)
                ) {
                    erhe::scene_renderer::Joint_buffer* seed_joint_buffer =
                        (m_context.forward_renderer != nullptr) ? &m_context.forward_renderer->get_joint_buffer() : nullptr;
                    std::span<const std::shared_ptr<erhe::scene::Skin>> seed_skins{};
                    const std::shared_ptr<Scene_root>& seed_scene_root = context.scene_view.get_scene_root();
                    if (seed_scene_root) {
                        erhe::scene::Scene* seed_scene = seed_scene_root->get_hosted_scene();
                        if (seed_scene != nullptr) {
                            seed_skins = seed_scene->get_skins();
                        }
                    }
                    m_context.id_renderer->render_content_seed(
                        Id_renderer::Seed_render_parameters{
                            .command_buffer        = command_buffer,
                            .viewport              = context.viewport,
                            .camera                = *context.camera,
                            .meshes                = m_seed_meshes,
                            .face_id_base_provider = &m_face_id_registry,
                            .reverse_depth         = get_reverse_depth(),
                            .depth_range           = get_depth_range(),
                            .conventions           = get_conventions(),
                            .joint_buffer          = seed_joint_buffer,
                            .skins                 = seed_skins
                        }
                    );
                }

                // Compute pre-pass + compute->vertex barrier only on the
                // compute backend. The geometry-shader backend (used when
                // the device does not expose compute shaders, e.g. OpenGL
                // 4.1 on macOS) expands lines in its geometry shader at
                // render time: compute() is a no-op, there is no
                // compute->vertex hazard, and glMemoryBarrier does not
                // exist there.
                if (m_context.content_wide_line_renderer->uses_compute()) {
                    {
                        erhe::graphics::Compute_command_encoder compute_encoder = graphics_device.make_compute_command_encoder(command_buffer);
                        m_context.content_wide_line_renderer->compute(compute_encoder);
                    }
                    // Compute -> vertex barrier. The unified content-line
                    // vertex shader reads pre-transformed triangles via SSBO
                    // (shader_storage_barrier_bit); legacy / non-content-wide-
                    // line paths may still read through the input assembler
                    // (vertex_attrib_array_barrier_bit). Must be emitted after
                    // the compute encoder scope ends.
                    command_buffer.memory_barrier(
                        erhe::graphics::Memory_barrier_mask::vertex_attrib_array_barrier_bit |
                        erhe::graphics::Memory_barrier_mask::shader_storage_barrier_bit
                    );
                }
            }
        }

        // ID-buffer edge-line method: now that the edge ribbons are expanded,
        // render them into the face-ID buffer (its own pass, before the main
        // viewport pass) and hand the buffer + matching base provider to the
        // composition passes via the render context.
        if (use_id_edge_lines && (m_context.id_renderer != nullptr)) {
            m_context.id_renderer->render_content_edge_id(
                command_buffer,
                context.viewport,
                *m_context.content_wide_line_renderer
            );
            context.edge_id_texture       = m_context.id_renderer->get_edge_id_texture();
            context.face_id_base_provider = &m_face_id_registry;
        }
    }

    // Shadow-texel debug visualization sync. Texel_renderer samples the shadow
    // map in the VERTEX stage (textureSize/textureLod in shadow_debug.vert). The
    // shadow render pass's producer->consumer barrier only makes the shadow map
    // available to fragment-stage reads (the forward renderer samples it there),
    // so the vertex-stage read is a read-after-write hazard. Insert an extra
    // barrier (outside the render pass) making prior writes visible to
    // vertex-stage texture fetches -- only when the visualization is active, so
    // the common path pays no extra synchronization. Mirrors the content-wide-
    // line compute->vertex barrier above.
    if (m_debug_visualizations.is_shadow_debug_enabled()) {
        command_buffer.memory_barrier(erhe::graphics::Memory_barrier_mask::texture_fetch_barrier_bit);
    }

    // Generate the atmosphere LUTs once (must be outside the render pass: it
    // issues compute dispatches + image barriers) when the physically-based
    // sky mode is active.
    if ((m_context.sky_renderer != nullptr) &&
        (m_context.editor_settings != nullptr) &&
        (m_context.editor_settings->sky.mode == 1)) {
        m_context.sky_renderer->ensure_luts(graphics_device, command_buffer);
    }

    m_render_target.update(m_projection_viewport.width, m_projection_viewport.height, nullptr);

    ERHE_VERIFY(m_render_target.get_render_pass());

    erhe::graphics::Render_command_encoder encoder = graphics_device.make_render_command_encoder(command_buffer);
    erhe::graphics::Scoped_render_pass scoped_render_pass{*m_render_target.get_render_pass(), command_buffer};
    context.encoder     = &encoder;
    context.render_pass = m_render_target.get_render_pass();

    // Starting render encoder clears render target texture(s)
    if (!do_render) {
        // ending render encoder applies multisample resolve, if applicabale
        //m_context.debug_renderer->release();
        return;
    }

    // TODO This would be? needed for basic (non-ImGui) viewports?
    // encoder.set_scissor_rect(context.viewport.x, context.viewport.y, context.viewport.width, context.viewport.height);

    m_context.app_message_bus->render_scene_view.send_message(
        Render_scene_view_message{
            .scene_view = this
        }
    );

    m_context.app_rendering ->render_viewport_main(context);
    m_context.app_rendering ->render_viewport_renderables(context); // This time with render encoder set
    m_context.debug_renderer->render(encoder, *m_render_target.get_render_pass(), context.viewport);
    m_context.debug_renderer->end_frame();
    if (m_context.content_wide_line_renderer != nullptr && m_context.content_wide_line_renderer->is_enabled()) {
        m_context.content_wide_line_renderer->end_frame();
    }
    m_context.text_renderer ->render(encoder, *m_render_target.get_render_pass(), context.viewport);
}

void Viewport_scene_view::set_window_viewport(erhe::math::Viewport viewport)
{
    // Needed for get_viewport_from_window()
    m_window_viewport = viewport;
    m_projection_viewport.x      = 0;
    m_projection_viewport.y      = 0;
    m_projection_viewport.width  = viewport.width;
    m_projection_viewport.height = viewport.height;
}

void Viewport_scene_view::set_is_scene_view_hovered(const bool is_hovered)
{
    // log_frame->debug("{}->set_is_scene_view_hovered({})", get_name(), m_is_scene_view_hovered);
    m_is_scene_view_hovered = is_hovered;
}

void Viewport_scene_view::set_camera(const std::shared_ptr<erhe::scene::Camera>& camera)
{
    // TODO Add validation
    m_camera = camera;
}

auto Viewport_scene_view::is_scene_view_hovered() const -> bool
{
    // log_frame->debug("{}->is_scene_view_hovered() = {}", get_name(), m_is_scene_view_hovered);
    return m_is_scene_view_hovered;
}

auto Viewport_scene_view::get_window_viewport() const -> const erhe::math::Viewport&
{
    return m_window_viewport;
}

auto Viewport_scene_view::get_projection_viewport() const -> const erhe::math::Viewport&
{
    return m_projection_viewport;
}

auto Viewport_scene_view::get_camera() const -> std::shared_ptr<erhe::scene::Camera>
{
    return m_camera.lock();
}

auto Viewport_scene_view::get_perspective_scale() const -> float
{
    const auto camera = m_camera.lock();
    if (!camera) {
        return 1.0f;
    }
    const erhe::scene::Camera_projection_transforms camera_projection_transforms_ = camera->projection_transforms(m_projection_viewport, get_reverse_depth(), get_depth_range(), get_conventions());
    const glm::mat4 clip_from_view = camera_projection_transforms_.clip_from_camera.get_matrix();
    const glm::mat2 projection_top_left_2x2{clip_from_view};
    const glm::mat2 inverted_top_left_2x2 = glm::inverse(projection_top_left_2x2);
    const float x = inverted_top_left_2x2[0][0];
    const float y = inverted_top_left_2x2[1][1];
    const float w = static_cast<float>(m_projection_viewport.width);
    const float h = static_cast<float>(m_projection_viewport.height);
    const float vp_scale = 1000.0f / std::min(w, h);
    return std::min(x, y) * vp_scale;
}

auto Viewport_scene_view::get_rendergraph_node() -> erhe::rendergraph::Rendergraph_node*
{
    return this;
}

auto Viewport_scene_view::as_viewport_scene_view() -> Viewport_scene_view*
{
    return this;
}

auto Viewport_scene_view::as_viewport_scene_view() const -> const Viewport_scene_view*
{
    return this;
}

auto Viewport_scene_view::get_viewport_from_window(const glm::vec2 window_position) const -> glm::vec2
{
    const float content_x   = static_cast<float>(window_position.x) - m_window_viewport.x;
    const float content_y   = static_cast<float>(window_position.y) - m_window_viewport.y;
    const auto& conventions = m_context.graphics_device->get_info().coordinate_conventions;
    // Window coordinates are top-left origin. OpenGL viewport is bottom-left, so Y must be flipped.
    // Metal/Vulkan viewports are top-left, so no flip is needed.
    const float viewport_y = (conventions.framebuffer_origin == erhe::math::Framebuffer_origin::bottom_left)
        ? (m_window_viewport.height - content_y)
        : content_y;
    return glm::vec2{content_x, viewport_y};
}

auto Viewport_scene_view::project_to_viewport(const glm::vec3 position_in_world) const -> std::optional<glm::vec3>
{
    const auto camera = m_camera.lock();
    if (!camera) {
        return {};
    }
    const auto camera_projection_transforms = camera->projection_transforms(m_projection_viewport, get_reverse_depth(), get_depth_range(), get_conventions());
    constexpr float depth_range_near = 0.0f;
    constexpr float depth_range_far  = 1.0f;
    return erhe::math::project_to_screen_space<float>(
        camera_projection_transforms.clip_from_world.get_matrix(),
        position_in_world,
        depth_range_near,
        depth_range_far,
        static_cast<float>(m_projection_viewport.x),
        static_cast<float>(m_projection_viewport.y),
        static_cast<float>(m_projection_viewport.width),
        static_cast<float>(m_projection_viewport.height),
        get_conventions()
    );
}

auto Viewport_scene_view::unproject_to_world(const glm::vec3 position_in_window) const -> std::optional<glm::vec3>
{
    const auto camera = m_camera.lock();
    if (!camera) {
        return {};
    }
    const auto camera_projection_transforms = camera->projection_transforms(m_projection_viewport, get_reverse_depth(), get_depth_range(), get_conventions());
    constexpr float depth_range_near = 0.0f;
    constexpr float depth_range_far  = 1.0f;
    return erhe::math::unproject<float>(
        camera_projection_transforms.clip_from_world.get_inverse_matrix(),
        position_in_window,
        depth_range_near,
        depth_range_far,
        static_cast<float>(m_projection_viewport.x),
        static_cast<float>(m_projection_viewport.y),
        static_cast<float>(m_projection_viewport.width),
        static_cast<float>(m_projection_viewport.height),
        get_conventions()
    );
}

void Viewport_scene_view::update_pointer_2d_position(const glm::vec2 position_in_viewport)
{
    ERHE_PROFILE_FUNCTION();

    m_position_in_viewport = position_in_viewport;
}

void Viewport_scene_view::request_cursor_relative_hold(bool relative_hold_enable)
{
    m_relative_hold_enable = relative_hold_enable;
}

auto Viewport_scene_view::get_cursor_relative_hold() const -> bool
{
    return m_relative_hold_enable;
}

void Viewport_scene_view::update_hover(bool ray_only)
{
    // Reverse Z: near=1.0, far=0.0; Forward Z: near=0.0, far=1.0
    const float near_depth             = get_reverse_depth() ? 1.0f : 0.0f;
    const float far_depth              = get_reverse_depth() ? 0.0f : 1.0f;
    const auto  near_position_in_world = get_position_in_world_viewport_depth(near_depth);
    const auto  far_position_in_world  = get_position_in_world_viewport_depth(far_depth);

    // log_input_frame->info("Viewport_scene_view::update_hover");

    const auto camera = m_camera.lock();
    if (!near_position_in_world.has_value() || !far_position_in_world.has_value() || !camera || !m_is_scene_view_hovered) {
        reset_control_transform();
        reset_hover_slots();
        return;
    }

    set_world_from_control(near_position_in_world.value(), far_position_in_world.value());

    if (ray_only) {
        return;
    }

    const auto scene_root = m_scene_root.lock();
    if (scene_root) {
        scene_root->update_pointer_for_rendertarget_meshes(this);
    }

    // Hybrid picker: raytrace handles static meshes (its BVH is correct
    // for the rest-pose surface which equals the displayed surface for
    // non-skinned content), the ID renderer handles skinned meshes
    // (rasterizing the posed surface that the user actually sees). Both
    // paths run every frame and the per-slot result is merged by ray-t
    // so the closer hit wins. With id_renderer.enabled set to true the
    // ID pass additionally covers static meshes; the merge then chooses
    // whichever path returns the closer hit (they should agree on the
    // same surface).
    update_hover_with_raytrace();
    if (m_context.id_renderer != nullptr) {
        update_hover_with_id_render();
    }

    update_grid_hover();

    if (m_hover_update_pending) {
        m_context.app_message_bus->hover_mesh.send_message(
            Hover_mesh_message{}
        );
        m_hover_update_pending = false;
    }
}

void Viewport_scene_view::update_hover_with_id_render()
{
    if (!m_position_in_viewport.has_value()) {
        reset_hover_slots();
        return;
    }
    const auto position_in_viewport = m_position_in_viewport.value();
    const auto id_query = m_context.id_renderer->get(
        static_cast<int>(position_in_viewport.x),
        static_cast<int>(position_in_viewport.y)
    );
    if (!id_query.valid) {
        SPDLOG_LOGGER_TRACE(log_scene_view, "pointer context hover not valid");
        return;
    }

    Hover_entry entry{
        .valid                      = id_query.valid,
        .scene_mesh_weak            = id_query.mesh,
        .scene_mesh_primitive_index = id_query.index_of_gltf_primitive_in_mesh,
        .position                   = get_position_in_world_viewport_depth(id_query.depth),
        .triangle                   = static_cast<uint32_t>(id_query.triangle_id) // TODO Consider these types
    };
    // log_controller_ray->trace("id_query.triangle_id = {}", id_query.triangle_id);

    // log_controller_ray->trace("position in world = {}", entry.position.value());

    std::shared_ptr<erhe::scene::Mesh> scene_mesh = entry.scene_mesh_weak.lock();
    if (scene_mesh) {
        const erhe::scene::Node* node = scene_mesh->get_node();
        ERHE_VERIFY(node != nullptr);
        const erhe::scene::Mesh_primitive& mesh_primitive = scene_mesh->get_primitives()[entry.scene_mesh_primitive_index];
        const erhe::primitive::Primitive&  primitive      = *mesh_primitive.primitive.get();
        const std::shared_ptr<erhe::primitive::Primitive_shape> shape = primitive.get_shape_for_raytrace();
        if (shape) {
            entry.geometry = shape->get_geometry();
            if (entry.geometry) {
                const GEO::Mesh& geo_mesh = entry.geometry->get_mesh();
                entry.facet = shape->get_mesh_facet_from_triangle(entry.triangle);
                if (entry.facet != GEO::NO_INDEX) {
                    // log_controller_ray->trace("hover facet = {}", entry.facet);

                    const bool       negative_determinant   = (node->get_flag_bits() & erhe::Item_flags::negative_determinant) == erhe::Item_flags::negative_determinant;
                    const GEO::vec3f facet_normal           = mesh_facet_normalf(geo_mesh, entry.facet);
                    const glm::vec3  local_normal           = to_glm_vec3(facet_normal);
                    const glm::mat4  world_from_node        = node->world_from_node();
                    const glm::mat4  normal_world_from_node = glm::transpose(glm::adjugate(world_from_node));
                    const glm::vec3  normal_in_world        = glm::vec3{normal_world_from_node * glm::vec4{local_normal, 0.0f}};
                    const glm::vec3  unit_normal            = glm::normalize(normal_in_world);
                    entry.normal = negative_determinant ? -unit_normal : unit_normal;
                    // log_controller_ray->trace("hover normal = {}", entry.normal.value());
                //} else {
                //    log_controller_ray->trace("hover facet = missing triangle to facet mapping");
                }
            }
        }
    }

    using namespace erhe::utility;

    const uint64_t flags = (id_query.mesh != nullptr) && scene_mesh ? scene_mesh->get_flag_bits() : 0;

    const bool hover_content      = id_query.mesh && test_bit_set(flags, erhe::Item_flags::content     );
    const bool hover_tool         = id_query.mesh && test_bit_set(flags, erhe::Item_flags::tool        );
    const bool hover_brush        = id_query.mesh && test_bit_set(flags, erhe::Item_flags::brush       );
    const bool hover_rendertarget = id_query.mesh && test_bit_set(flags, erhe::Item_flags::rendertarget);
    std::shared_ptr<erhe::scene::Mesh> entry_scene_mesh = entry.scene_mesh_weak.lock();
    // log_controller_ray->debug(
    //     "hover mesh = {} primitive index = {} facet {} {}{}{}{}",
    //     entry_scene_mesh ? entry_scene_mesh->get_name() : "()",
    //     entry.scene_mesh_primitive_index,
    //     entry.facet,
    //     hover_content      ? "content "      : "",
    //     hover_tool         ? "tool "         : "",
    //     hover_brush        ? "brush "        : "",
    //     hover_rendertarget ? "rendertarget " : ""
    // );

    // Merge into the slot the mesh's role flag matches. The raytrace
    // path ran first and may have populated other slots; merge_hover
    // only overrides them when the candidate is closer along the
    // picking ray. Slots where the candidate role does not match are
    // intentionally left alone so the raytrace result stands.
    if (hover_content     ) { merge_hover(Hover_entry::content_slot     , entry); }
    if (hover_tool        ) { merge_hover(Hover_entry::tool_slot        , entry); }
    if (hover_brush       ) { merge_hover(Hover_entry::brush_slot       , entry); }
    if (hover_rendertarget) { merge_hover(Hover_entry::rendertarget_slot, entry); }
}

auto Viewport_scene_view::get_position_in_viewport() const -> std::optional<glm::vec2>
{
    return m_position_in_viewport;
}

auto Viewport_scene_view::get_position_in_world_viewport_depth(const float viewport_depth) const -> std::optional<glm::vec3>
{
    const auto camera = m_camera.lock();
    if (!m_position_in_viewport.has_value() || !camera) {
        return {};
    }

    const float depth_range_near     = 0.0f;
    const float depth_range_far      = 1.0f;
    const auto  position_in_viewport = glm::vec3{
        m_position_in_viewport.value().x,
        m_position_in_viewport.value().y,
        viewport_depth
    };
    const erhe::math::Viewport&                     vp                    = get_projection_viewport();
    const erhe::scene::Camera_projection_transforms projection_transforms = camera->projection_transforms(vp, get_reverse_depth(), get_depth_range(), get_conventions());
    const glm::mat4                                 world_from_clip       = projection_transforms.clip_from_world.get_inverse_matrix();

    return erhe::math::unproject<float>(
        glm::mat4{world_from_clip},
        position_in_viewport,
        depth_range_near,
        depth_range_far,
        static_cast<float>(vp.x),
        static_cast<float>(vp.y),
        static_cast<float>(vp.width),
        static_cast<float>(vp.height),
        get_conventions()
    );
}

auto Viewport_scene_view::get_shadow_render_node() const -> Shadow_render_node*
{
    Rendergraph_node*   input_node         = get_consumer_input_node(erhe::rendergraph::Rendergraph_node_key::shadow_maps);
    Shadow_render_node* shadow_render_node = static_cast<Shadow_render_node*>(input_node);
    return shadow_render_node;
}

auto Viewport_scene_view::get_show_navigation_gizmo() const -> bool
{
    return m_show_navigation_gizmo;
}

auto Viewport_scene_view::get_navigation_gizmo() -> ImViewGuizmo::Context&
{
    return *m_navigation_gizmo;
}

void Viewport_scene_view::viewport_toolbar()
{
    ImGui::PushID("Viewport_scene_view::viewport_toolbar()");
    ERHE_DEFER( ImGui::PopID(); );

    ImFont* icon_font = m_context.imgui_renderer->material_design_font();
    const float font_size =
        m_context.imgui_renderer->get_imgui_settings().scale_factor *
        m_context.imgui_renderer->get_imgui_settings().material_design_font_size;

    icon_button(
        icon_font,
        font_size,
        ICON_MDI_AXIS_ARROW "##navigation_gizmo",
        "N##navigation_gizmo",
        "Show/Hide Navigation Gizmo",
        m_show_navigation_gizmo
    );

    popup_button(
        icon_font,
        font_size,
        ICON_MDI_EYE "##ViewportVisualStyle",                      // icon
        "Visual Style",                                            // title
        ImGui::GetID("ViewportVisualStylePopup"),                  // popup_id
        m_show_visual_style_popup,                                 // is_open
        [this]() { Viewport_config_window::imgui(get_config()); }  // content_fn
    );
    popup_button(
        icon_font,
        font_size,
        ICON_MDI_DOTS_TRIANGLE "##ViewportSceneAndCamera",              // icon
        "Scene and Camera",                                             // title
        ImGui::GetID("ViewportSceneAndCameraPopup"),                    // popup_id
        m_show_scene_and_camera_popup,                                  // is_open
        [this]() { Scene_view_config_window::imgui(m_context, *this); } // content_fn
    );
    popup_button(
        icon_font,
        font_size,
        ICON_MDI_EYE_SETTINGS_OUTLINE "##ViewportDebugVisualizations", // icon
        "Debug Visualization",                                         // title
        ImGui::GetID("ViewportDebugVisualizations"),                   // popup_id
        m_show_debug_visualizations_popup,                             // is_open
        [this]() { m_debug_visualizations.imgui(*this, m_context); }   // content_fn
    );

    // Shader debug visualization selector. Lives directly on the toolbar
    // (rather than behind the "Scene and Camera" popup) so the per-viewport
    // debug mode is one click away. The combo preview shows the active mode;
    // the tooltip names the control.
    {
        int shader_debug_int = static_cast<int>(get_shader_debug());
        if (erhe::imgui::combo_fit_width(
                "##ViewportShaderDebug",
                &shader_debug_int,
                erhe::scene_renderer::c_shader_debug_strings,
                IM_ARRAYSIZE(erhe::scene_renderer::c_shader_debug_strings)
            ))
        {
            set_shader_debug(static_cast<erhe::scene_renderer::Shader_debug>(shader_debug_int));
        }
        ImGui::SetItemTooltip("Shader Debug");
    }

    m_context.selection_tool->viewport_toolbar();
    m_context.transform_tool->viewport_toolbar();
    if (m_context.mesh_component_selection_tool != nullptr) {
        m_context.mesh_component_selection_tool->viewport_toolbar();
    }
    //// m_context.grid_tool->viewport_toolbar(hovered);
    //// TODO m_physics_window.viewport_toolbar(hovered);
}

void Viewport_scene_view::set_shader_debug(erhe::scene_renderer::Shader_debug shader_debug)
{
    m_shader_debug = shader_debug;
}

auto Viewport_scene_view::get_shader_debug() const -> erhe::scene_renderer::Shader_debug
{
    return m_shader_debug;
}

void Viewport_scene_view::set_renderer_choice(Renderer_choice choice)
{
    m_renderer_choice = choice;
}

auto Viewport_scene_view::get_renderer_choice() const -> Renderer_choice
{
    return m_renderer_choice;
}

void Viewport_scene_view::write_scene_and_camera_settings(Scene_and_camera_settings& out) const
{
    Scene_view::write_scene_and_camera_settings(out);
    if (m_scene_and_camera_restore_pending) {
        // See Scene_view::write_scene_and_camera_settings: preserve verbatim
        // until the selection has been resolved.
        out.camera_name = m_pending_scene_and_camera.camera_name;
    } else {
        std::shared_ptr<erhe::scene::Camera> camera = m_camera.lock();
        out.camera_name = camera ? camera->get_name() : std::string{};
    }
    out.shader_debug    = static_cast<int>(m_shader_debug);
    out.renderer_choice = static_cast<int>(m_renderer_choice);
}

auto Viewport_scene_view::resolve_pending_scene_and_camera() -> bool
{
    if (!Scene_view::resolve_pending_scene_and_camera()) {
        return false; // scene not loaded yet; base asked to retry
    }
    const std::string& camera_name = m_pending_scene_and_camera.camera_name;
    std::shared_ptr<Scene_root> scene_root = get_scene_root();
    if (!camera_name.empty() && scene_root) {
        std::shared_ptr<erhe::scene::Camera> camera = find_camera_in_scene(*scene_root, camera_name);
        if (camera) {
            set_camera(camera);
        }
    }
    return true;
}

auto Viewport_scene_view::get_closest_point_on_line(const glm::vec3 P0, const glm::vec3 P1) -> std::optional<glm::vec3>
{
    ERHE_PROFILE_FUNCTION();

    using vec2 = glm::vec2;
    using vec3 = glm::vec3;

    const auto position_in_viewport_opt = get_position_in_viewport();
    if (!position_in_viewport_opt.has_value()) {
        return {};
    }

    const auto ss_P0_opt = project_to_viewport(P0);
    const auto ss_P1_opt = project_to_viewport(P1);
    if (
        !ss_P0_opt.has_value() ||
        !ss_P1_opt.has_value()
    ) {
        return {};
    }

    const vec3 ss_P0      = ss_P0_opt.value();
    const vec3 ss_P1      = ss_P1_opt.value();
    const auto ss_closest = erhe::math::closest_point<float>(
        vec2{ss_P0},
        vec2{ss_P1},
        vec2{position_in_viewport_opt.value()}
    );

    if (ss_closest.has_value()) {
        const float near_depth = get_reverse_depth() ? 1.0f : 0.0f;
        const float far_depth  = get_reverse_depth() ? 0.0f : 1.0f;
        const auto R0_opt = unproject_to_world(vec3{ss_closest.value(), near_depth});
        const auto R1_opt = unproject_to_world(vec3{ss_closest.value(), far_depth});
        if (R0_opt.has_value() && R1_opt.has_value()) {
            const auto R0 = R0_opt.value();
            const auto R1 = R1_opt.value();
            const auto closest_points_r = erhe::math::closest_points<float>(P0, P1, R0, R1);
            if (closest_points_r.has_value()) {
                return closest_points_r.value().P;
            }
        }
    } else {
        const float near_depth2 = get_reverse_depth() ? 1.0f : 0.0f;
        const float far_depth2  = get_reverse_depth() ? 0.0f : 1.0f;
        const auto Q0_opt = get_position_in_world_viewport_depth(near_depth2);
        const auto Q1_opt = get_position_in_world_viewport_depth(far_depth2);
        if (Q0_opt.has_value() && Q1_opt.has_value()) {
            const auto Q0 = Q0_opt.value();
            const auto Q1 = Q1_opt.value();
            const auto closest_points_q = erhe::math::closest_points<float>(P0, P1, Q0, Q1);
            if (closest_points_q.has_value()) {
                return closest_points_q.value().P;
            }
        }
    }

    return {};
}

auto Viewport_scene_view::get_closest_point_on_plane(const glm::vec3 N, const glm::vec3 P) -> std::optional<glm::vec3>
{
    ERHE_PROFILE_FUNCTION();

    using vec3 = glm::vec3;

    const float near_depth = get_reverse_depth() ? 1.0f : 0.0f;
    const float far_depth  = get_reverse_depth() ? 0.0f : 1.0f;
    const auto Q0_opt = get_position_in_world_viewport_depth(near_depth);
    const auto Q1_opt = get_position_in_world_viewport_depth(far_depth);
    if (
        !Q0_opt.has_value() ||
        !Q1_opt.has_value()
    ) {
        return {};
    }

    const vec3 Q0 = Q0_opt.value();
    const vec3 Q1 = Q1_opt.value();
    const vec3 v  = normalize(Q1 - Q0);

    const auto intersection = erhe::math::intersect_plane<float>(N, P, Q0, v);
    if (!intersection.has_value()) {
        return {};
    }

    const vec3 drag_point_new_position_in_world = Q0 + intersection.value() * v;
    return drag_point_new_position_in_world;
}

}
