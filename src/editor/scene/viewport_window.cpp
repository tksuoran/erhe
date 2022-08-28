// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "scene/viewport_window.hpp"

#include "editor_log.hpp"
#include "editor_rendering.hpp"
#include "renderers/id_renderer.hpp"
#include "rendergraph/post_processing.hpp"
#include "renderers/programs.hpp"
#include "renderers/render_context.hpp"
#include "renderers/shadow_renderer.hpp"
#include "scene/scene_root.hpp"
#include "tools/grid_tool.hpp"
#include "tools/selection_tool.hpp"
#include "tools/tools.hpp"
#include "tools/trs_tool.hpp"
#if defined(ERHE_XR_LIBRARY_OPENXR)
#   include "xr/headset_renderer.hpp"
#endif

#include "erhe/application/windows/log_window.hpp"
#include "erhe/application/configuration.hpp"
#include "erhe/application/imgui/imgui_viewport.hpp"
#include "erhe/application/imgui/imgui_windows.hpp"
#include "erhe/application/rendergraph/rendergraph.hpp"
#include "erhe/application/view.hpp"
#include "erhe/geometry/geometry.hpp"
#include "erhe/gl/enum_string_functions.hpp"
#include "erhe/gl/wrapper_functions.hpp"
#include "erhe/graphics/debug.hpp"
#include "erhe/graphics/framebuffer.hpp"
#include "erhe/graphics/opengl_state_tracker.hpp"
#include "erhe/graphics/renderbuffer.hpp"
#include "erhe/graphics/texture.hpp"
#include "erhe/scene/camera.hpp"
#include "erhe/scene/mesh.hpp"
#include "erhe/scene/scene.hpp"
#include "erhe/toolkit/bit_helpers.hpp"
#include "erhe/toolkit/profile.hpp"
#include "erhe/toolkit/verify.hpp"

#if defined(ERHE_GUI_LIBRARY_IMGUI)
#   include <imgui.h>
#endif

namespace editor
{


using erhe::graphics::Vertex_input_state;
using erhe::graphics::Input_assembly_state;
using erhe::graphics::Rasterization_state;
using erhe::graphics::Depth_stencil_state;
using erhe::graphics::Color_blend_state;
using erhe::graphics::Framebuffer;
using erhe::graphics::Renderbuffer;
using erhe::graphics::Texture;


int Viewport_window::s_serial = 0;

Viewport_window::Viewport_window(
    const std::string_view              name,
    const erhe::components::Components& components,
    const std::shared_ptr<Scene_root>&  scene_root,
    erhe::scene::Camera*                camera
)
    : erhe::application::Rendergraph_node{name}
    , m_configuration         {components.get<erhe::application::Configuration    >()}
    , m_pipeline_state_tracker{components.get<erhe::graphics::OpenGL_state_tracker>()}
    , m_editor_rendering      {components.get<Editor_rendering>()}
    , m_grid_tool             {components.get<Grid_tool       >()}
    , m_post_processing       {components.get<Post_processing >()}
    , m_programs              {components.get<Programs        >()}
    , m_trs_tool              {components.get<Trs_tool        >()}
    , m_viewport_config       {components.get<Viewport_config >()}
    , m_name                  {name}
    , m_scene_root            {scene_root}
    , m_camera                {camera}
{
    register_input(
        erhe::application::Resource_routing::Resource_provided_by_producer,
        "shadow_maps",
        erhe::application::Rendergraph_node_key::shadow_maps
    );
    register_output(
        erhe::application::Resource_routing::Resource_provided_by_consumer,
        "viewport",
        erhe::application::Rendergraph_node_key::viewport
    );
}

auto Viewport_window::get_override_shader_stages() const -> erhe::graphics::Shader_stages*
{
    switch (m_shader_stages_variant)
    {
        case Shader_stages_variant::standard:                 return m_programs->standard.get();
        case Shader_stages_variant::debug_depth:              return m_programs->debug_depth.get();
        case Shader_stages_variant::debug_normal:             return m_programs->debug_normal.get();
        case Shader_stages_variant::debug_tangent:            return m_programs->debug_tangent.get();
        case Shader_stages_variant::debug_bitangent:          return m_programs->debug_bitangent.get();
        case Shader_stages_variant::debug_texcoord:           return m_programs->debug_texcoord.get();
        case Shader_stages_variant::debug_vertex_color_rgb:   return m_programs->debug_vertex_color_rgb.get();
        case Shader_stages_variant::debug_vertex_color_alpha: return m_programs->debug_vertex_color_alpha.get();
        case Shader_stages_variant::debug_misc:               return m_programs->debug_misc.get();
        default:                                              return m_programs->standard.get();
    }
}

void Viewport_window::execute_rendergraph_node()
{
    ERHE_PROFILE_FUNCTION

    const auto& output_viewport = get_producer_output_viewport(
        erhe::application::Resource_routing::Resource_provided_by_consumer,
        erhe::application::Rendergraph_node_key::viewport
    );
    const auto& output_framebuffer = get_producer_output_framebuffer(
        erhe::application::Resource_routing::Resource_provided_by_consumer,
        erhe::application::Rendergraph_node_key::viewport
    );

    const GLint output_framebuffer_name = output_framebuffer
        ? output_framebuffer->gl_name()
        : 0;

    if (
        (output_viewport.width < 1) ||
        (output_viewport.height < 1)
    )
    {
        return;
    }

    const Render_context context
    {
        .window                 = this,
        .viewport_config        = m_viewport_config.get(),
        .camera                 = m_camera,
        .viewport               = output_viewport,
        .override_shader_stages = get_override_shader_stages()
    };

#if defined(ERHE_RAYTRACE_LIBRARY_NONE)
    if (m_is_hovered)
    {
        m_editor_rendering->render_id(context);
    }
#endif

    gl::bind_framebuffer(gl::Framebuffer_target::draw_framebuffer, output_framebuffer_name);
    if (output_framebuffer)
    {
        if (!output_framebuffer->check_status())
        {
            return;
        }
    }

    m_editor_rendering->render_viewport_main(context, m_is_hovered);
}

void Viewport_window::clear() const
{
    ERHE_PROFILE_FUNCTION

    m_pipeline_state_tracker->shader_stages.reset();
    m_pipeline_state_tracker->color_blend.execute(Color_blend_state::color_blend_disabled);
    gl::clear_color(
        m_viewport_config->clear_color[0],
        m_viewport_config->clear_color[1],
        m_viewport_config->clear_color[2],
        m_viewport_config->clear_color[3]
    );
    gl::clear_stencil(0);
    gl::clear_depth_f(*m_configuration->depth_clear_value_pointer());
    gl::clear(
        gl::Clear_buffer_mask::color_buffer_bit |
        gl::Clear_buffer_mask::depth_buffer_bit |
        gl::Clear_buffer_mask::stencil_buffer_bit
    );
}

void Viewport_window::set_window_viewport(
    const int x,
    const int y,
    const int width,
    const int height
)
{
    m_window_viewport.x      = x;
    m_window_viewport.y      = y;
    m_window_viewport.width  = width;
    m_window_viewport.height = height;
    m_projection_viewport.x      = 0;
    m_projection_viewport.y      = 0;
    m_projection_viewport.width  = width;
    m_projection_viewport.height = height;
}

void Viewport_window::set_is_hovered(const bool is_hovered)
{
    m_is_hovered = is_hovered;
}

void Viewport_window::set_camera(erhe::scene::Camera* camera)
{
    // TODO Add validation
    m_camera = camera;
}

auto Viewport_window::is_hovered() const -> bool
{
    return m_is_hovered;
}

[[nodiscard]] auto Viewport_window::scene_root() const -> Scene_root*
{
    return m_scene_root.get();
}

auto Viewport_window::window_viewport() const -> const erhe::scene::Viewport&
{
    return m_window_viewport;
}

auto Viewport_window::projection_viewport() const -> const erhe::scene::Viewport&
{
    return m_projection_viewport;
}

auto Viewport_window::camera() const -> erhe::scene::Camera*
{
    return m_camera;
}

auto Viewport_window::to_scene_content(
    const glm::vec2 position_in_root
) const -> glm::vec2
{
    const float content_x      = static_cast<float>(position_in_root.x) - m_window_viewport.x;
    const float content_y      = static_cast<float>(position_in_root.y) - m_window_viewport.y;
    const float content_flip_y = m_window_viewport.height - content_y;
    return {
        content_x,
        content_flip_y
    };
}

auto Viewport_window::project_to_viewport(
    const glm::dvec3 position_in_world
) const -> nonstd::optional<glm::dvec3>
{
    if (m_camera == nullptr)
    {
        return {};
    }
    const auto camera_projection_transforms = m_camera->projection_transforms(m_projection_viewport);
    constexpr double depth_range_near = 0.0;
    constexpr double depth_range_far  = 1.0;
    return erhe::toolkit::project_to_screen_space<double>(
        camera_projection_transforms.clip_from_world.matrix(),
        position_in_world,
        depth_range_near,
        depth_range_far,
        static_cast<double>(m_projection_viewport.x),
        static_cast<double>(m_projection_viewport.y),
        static_cast<double>(m_projection_viewport.width),
        static_cast<double>(m_projection_viewport.height)
    );
}

auto Viewport_window::unproject_to_world(
    const glm::dvec3 position_in_window
) const -> nonstd::optional<glm::dvec3>
{
    if (m_camera == nullptr)
    {
        return {};
    }
    const auto camera_projection_transforms = m_camera->projection_transforms(m_projection_viewport);
    constexpr double depth_range_near = 0.0;
    constexpr double depth_range_far  = 1.0;
    return erhe::toolkit::unproject<double>(
        camera_projection_transforms.clip_from_world.inverse_matrix(),
        position_in_window,
        depth_range_near,
        depth_range_far,
        static_cast<double>(m_projection_viewport.x),
        static_cast<double>(m_projection_viewport.y),
        static_cast<double>(m_projection_viewport.width),
        static_cast<double>(m_projection_viewport.height)
    );
}

#if !defined(ERHE_RAYTRACE_LIBRARY_NONE)
void Pointer_context::raytrace()
{
    ERHE_PROFILE_FUNCTION

    if (m_window == nullptr)
    {
        return;
    }

    const auto pointer_near = position_in_world_viewport_depth(1.0);
    const auto pointer_far  = position_in_world_viewport_depth(0.0);

    if (!pointer_near.has_value() || !pointer_far.has_value())
    {
        return;
    }

    const glm::vec3 origin{pointer_near.value()};

    auto& scene = m_scene_root->raytrace_scene();
    {
        erhe::toolkit::Scoped_timer scoped_timer{m_ray_scene_build_timer};
        scene.commit();
    }

    m_nearest_slot = 0;
    float nearest_t_far = 9999.0f;
    {
        ERHE_PROFILE_SCOPE("raytrace inner");

        erhe::toolkit::Scoped_timer scoped_timer{m_ray_traverse_timer};

        for (size_t i = 0; i < slot_count; ++i)
        {
            auto& entry = m_hover_entries[i];
            erhe::raytrace::Ray ray{
                .origin    = glm::vec3{pointer_near.value()},
                .t_near    = 0.0f,
                .direction = glm::vec3{glm::normalize(pointer_far.value() - pointer_near.value())},
                .time      = 0.0f,
                .t_far     = 9999.0f,
                .mask      = slot_masks[i],
                .id        = 0,
                .flags     = 0
            };
            erhe::raytrace::Hit hit;
            scene.intersect(ray, hit);
            entry.valid = hit.instance != nullptr;
            entry.mask  = slot_masks[i];
            if (entry.valid)
            {
                void* user_data     = hit.instance->get_user_data();
                entry.raytrace_node = reinterpret_cast<Node_raytrace*>(user_data);
                entry.position      = ray.origin + ray.t_far * ray.direction;
                entry.geometry      = nullptr;
                entry.local_index   = 0;

                SPDLOG_LOGGER_TRACE(log_pointer, "{}: Hit position: {}", slot_names[i], entry.position.value());

                if (entry.raytrace_node != nullptr)
                {
                    auto* node = entry.raytrace_node->get_node();
                    if (node != nullptr)
                    {
                        SPDLOG_LOGGER_TRACE(log_pointer, "{}: Hit node: {} {}", slot_names[i], node->node_type(), node->name());
                        const auto* rt_instance = entry.raytrace_node->raytrace_instance();
                        if (rt_instance != nullptr)
                        {
                            SPDLOG_LOGGER_TRACE(
                                log_pointer,
                                "{}: RT instance: {}",
                                slot_names[i],
                                rt_instance->is_enabled()
                                    ? "enabled"
                                    : "disabled"
                            );
                        }
                    }
                    if (is_mesh(node))
                    {
                        entry.mesh = as_mesh(node->shared_from_this());
                        auto* primitive = entry.raytrace_node->raytrace_primitive();
                        entry.primitive = 0; // TODO
                        if (primitive != nullptr)
                        {
                            entry.geometry = entry.raytrace_node->source_geometry().get();
                            if (entry.geometry != nullptr)
                            {
                                SPDLOG_LOGGER_TRACE(log_pointer, "{}: Hit geometry: {}", slot_names[i], entry.geometry->name);
                            }
                            if (hit.primitive_id < primitive->primitive_geometry.primitive_id_to_polygon_id.size())
                            {
                                const auto polygon_id = primitive->primitive_geometry.primitive_id_to_polygon_id[hit.primitive_id];
                                SPDLOG_LOGGER_TRACE(log_pointer, "{}: Hit polygon: {}", slot_names[i], polygon_id);
                                entry.local_index = polygon_id;
                            }
                        }
                    }
                }

                if (ray.t_far < nearest_t_far)
                {
                    m_nearest_slot = i;
                    nearest_t_far = ray.t_far;
                }
            }
            else
            {
                SPDLOG_LOGGER_TRACE(log_pointer, "{}: no hit", slot_names[i]);
            }
        }
    }

    SPDLOG_LOGGER_TRACE(log_pointer, "Nearest slot: {}", slot_names[m_nearest_slot]);

    const auto duration = m_ray_traverse_timer.duration().value();
    if (duration >= std::chrono::milliseconds(1))
    {
        SPDLOG_LOGGER_TRACE(log_pointer, "ray intersect time: {}", std::chrono::duration_cast<std::chrono::milliseconds>(duration));
    }
    else if (duration >= std::chrono::microseconds(1))
    {
        SPDLOG_LOGGER_TRACE(log_pointer, "ray intersect time: {}", std::chrono::duration_cast<std::chrono::microseconds>(duration));
    }
    else
    {
        SPDLOG_LOGGER_TRACE(log_pointer, "ray intersect time: {}", duration);
    }

#if 0
    erhe::scene::Mesh*         hit_mesh      {nullptr};
    erhe::geometry::Geometry*  hit_geometry  {nullptr};
    erhe::geometry::Polygon_id hit_polygon_id{0};
    float                      hit_t         {std::numeric_limits<float>::max()};
    float                      hit_u         {0.0f};
    float                      hit_v         {0.0f};
    const auto& content_layer = m_scene_root->content_layer();
    for (auto& mesh : content_layer.meshes)
    {
        erhe::geometry::Geometry*  geometry  {nullptr};
        erhe::geometry::Polygon_id polygon_id{0};
        float                      t         {std::numeric_limits<float>::max()};
        float                      u         {0.0f};
        float                      v         {0.0f};
        const bool hit = erhe::raytrace::intersect(
            *mesh.get(),
            origin,
            direction,
            geometry,
            polygon_id,
            t,
            u,
            v
        );
        if (hit)
        {
            log->frame_log(
                "hit mesh {}, t = {}, polygon id = {}",
                mesh->name(),
                t,
                static_cast<uint32_t>(polygon_id)
            );
        }
        if (hit && (t < hit_t))
        {
            hit_mesh       = mesh.get();
            hit_geometry   = geometry;
            hit_polygon_id = polygon_id;
            hit_t          = t;
            hit_u          = u;
            hit_v          = v;
        }
    }
    if (hit_t != std::numeric_limits<float>::max())
    {
        pointer_context.raytrace_hit_position = origin + hit_t * direction;
        if (hit_polygon_id < hit_geometry->polygon_count())
        {
            auto* polygon_normals = hit_geometry->polygon_attributes().find<glm::vec3>(c_polygon_normals);
            if ((polygon_normals != nullptr) && polygon_normals->has(hit_polygon_id))
            {
                auto local_normal    = polygon_normals->get(hit_polygon_id);
                auto world_from_node = hit_mesh->world_from_node();
                pointer_context.raytrace_local_index = static_cast<size_t>(hit_polygon_id);
                pointer_context.raytrace_hit_normal = glm::vec3{
                    world_from_node * glm::vec4{local_normal, 0.0f}
                };
            }
        }
    }
#endif
}
#endif

void Viewport_window::reset_pointer_context()
{
    std::fill(
        m_hover_entries.begin(),
        m_hover_entries.end(),
        Hover_entry{}
    );

    m_position_in_viewport  .reset();
    m_near_position_in_world.reset();
    m_far_position_in_world .reset();
}

void Viewport_window::update_pointer_context(
    Id_renderer&    id_renderer,
    const glm::vec2 position_in_viewport
)
{
    ERHE_PROFILE_FUNCTION

    const bool reverse_depth = projection_viewport().reverse_depth;
    m_position_in_viewport   = position_in_viewport;
    m_near_position_in_world = position_in_world_viewport_depth(reverse_depth ? 1.0f : 0.0f);
    m_far_position_in_world  = position_in_world_viewport_depth(reverse_depth ? 0.0f : 1.0f);

    if (m_camera == nullptr)
    {
        return;
    }

#if !defined(ERHE_RAYTRACE_LIBRARY_NONE)
    if (pointer_in_content_area())
    {
        raytrace();
    }
#endif

#if defined(ERHE_RAYTRACE_LIBRARY_NONE)
    const auto id_query = id_renderer.get(
        static_cast<int>(position_in_viewport.x),
        static_cast<int>(position_in_viewport.y)
    );

    m_near_position_in_world = position_in_world_viewport_depth(reverse_depth ? 1.0f : 0.0f);
    m_far_position_in_world  = position_in_world_viewport_depth(reverse_depth ? 0.0f : 1.0f);

    Hover_entry entry;
    entry.position = position_in_world_viewport_depth(id_query.depth);
    entry.valid    = id_query.valid;

    SPDLOG_LOGGER_TRACE(log_pointer, "position in world = {}", entry.position.value());
    if (!id_query.valid)
    {
        SPDLOG_LOGGER_TRACE(log_pointer, "pointer context hover not valid");
        return;
    }

    entry.mesh        = id_query.mesh;
    entry.primitive   = id_query.mesh_primitive_index;
    entry.local_index = id_query.local_index;
    if (entry.mesh)
    {
        const auto& primitive = entry.mesh->mesh_data.primitives[entry.primitive];
        entry.geometry = primitive.source_geometry.get();
        entry.normal   = {};
        if (entry.geometry != nullptr)
        {
            const auto polygon_id = static_cast<erhe::geometry::Polygon_id>(entry.local_index);
            if (polygon_id < entry.geometry->get_polygon_count())
            {
                SPDLOG_LOGGER_TRACE(log_pointer, "hover polygon = {}", polygon_id);
                auto* const polygon_normals = entry.geometry->polygon_attributes().find<glm::vec3>(erhe::geometry::c_polygon_normals);
                if (
                    (polygon_normals != nullptr) &&
                    polygon_normals->has(polygon_id)
                )
                {
                    const auto local_normal    = polygon_normals->get(polygon_id);
                    const auto world_from_node = entry.mesh->world_from_node();
                    entry.normal = glm::vec3{world_from_node * glm::vec4{local_normal, 0.0f}};
                    SPDLOG_LOGGER_TRACE(log_pointer, "hover normal = {}", entry.normal.value());
                }
            }
        }
    }

    using erhe::toolkit::test_all_rhs_bits_set;

    const uint64_t visibility_mask = id_query.mesh ? entry.mesh->get_visibility_mask() : 0;

    const bool hover_content      = id_query.mesh && test_all_rhs_bits_set(visibility_mask, erhe::scene::Node_visibility::content     );
    const bool hover_tool         = id_query.mesh && test_all_rhs_bits_set(visibility_mask, erhe::scene::Node_visibility::tool        );
    const bool hover_brush        = id_query.mesh && test_all_rhs_bits_set(visibility_mask, erhe::scene::Node_visibility::brush       );
    const bool hover_rendertarget = id_query.mesh && test_all_rhs_bits_set(visibility_mask, erhe::scene::Node_visibility::rendertarget);
    SPDLOG_LOGGER_TRACE(
        log_pointer,
        "hover mesh = {} primitive = {} local index {} {}{}{}{}",
        entry.mesh ? entry.mesh->name() : "()",
        entry.primitive,
        entry.local_index,
        hover_content      ? "content "      : "",
        hover_tool         ? "tool "         : "",
        hover_brush        ? "brush "        : "",
        hover_rendertarget ? "rendertarget " : ""
    );
    if (hover_content)
    {
        m_hover_entries[Hover_entry::content_slot] = entry;
    }
    if (hover_tool)
    {
        m_hover_entries[Hover_entry::tool_slot] = entry;
    }
    if (hover_brush)
    {
        m_hover_entries[Hover_entry::brush_slot] = entry;
    }
    if (hover_rendertarget)
    {
        m_hover_entries[Hover_entry::rendertarget_slot] = entry;
    }
    m_scene_root->update_pointer_for_rendertarget_nodes();
#endif
}

auto Viewport_window::near_position_in_world() const -> nonstd::optional<glm::vec3>
{
    return m_near_position_in_world;
}

auto Viewport_window::far_position_in_world() const -> nonstd::optional<glm::vec3>
{
    return m_far_position_in_world;
}

auto Viewport_window::position_in_world_distance(
    const float distance
) const -> nonstd::optional<glm::vec3>
{
    if (
        !m_near_position_in_world.has_value() ||
        !m_far_position_in_world.has_value()
    )
    {
        return {};
    }

    const glm::vec3 p0        = m_near_position_in_world.value();
    const glm::vec3 p1        = m_far_position_in_world.value();
    const glm::vec3 origin    = p0;
    const glm::vec3 direction = glm::normalize(p1 - p0);
    return origin + distance * direction;
}

auto Viewport_window::position_in_world_viewport_depth(
    const double viewport_depth
) const -> nonstd::optional<glm::dvec3>
{
    if (
        !m_position_in_viewport.has_value() ||
        (m_camera == nullptr)
    )
    {
        return {};
    }

    const double depth_range_near     = 0.0;
    const double depth_range_far      = 1.0;
    const auto   position_in_viewport = glm::dvec3{
        m_position_in_viewport.value().x,
        m_position_in_viewport.value().y,
        viewport_depth
    };
    const auto      vp                    = projection_viewport();
    const auto      projection_transforms = m_camera->projection_transforms(vp);
    const glm::mat4 world_from_clip       = projection_transforms.clip_from_world.inverse_matrix();

    return erhe::toolkit::unproject(
        glm::dmat4{world_from_clip},
        position_in_viewport,
        depth_range_near,
        depth_range_far,
        static_cast<double>(vp.x),
        static_cast<double>(vp.y),
        static_cast<double>(vp.width),
        static_cast<double>(vp.height)
    );
}

auto Viewport_window::position_in_viewport() const -> nonstd::optional<glm::vec2>
{
    return m_position_in_viewport;
}

auto Viewport_window::get_hover(size_t slot) const -> const Hover_entry&
{
    return m_hover_entries.at(slot);
}

auto Viewport_window::get_nearest_hover() const -> const Hover_entry&
{
    return m_hover_entries.at(m_nearest_slot);
}

auto Viewport_window::get_shadow_render_node() const -> Shadow_render_node*
{
    const std::weak_ptr<Rendergraph_node>& input_node_weak = get_consumer_input_node(
        erhe::application::Resource_routing::Resource_provided_by_producer,
        erhe::application::Rendergraph_node_key::shadow_maps
    );
    const std::shared_ptr<Rendergraph_node>& input_node_shared = input_node_weak.lock();
    Rendergraph_node*   input_node = input_node_shared.get();
    Shadow_render_node* shadow_render_node = reinterpret_cast<Shadow_render_node*>(input_node);
    return shadow_render_node;
}

auto Viewport_window::get_light_projections() const -> Light_projections*
{
    auto* shadow_render_node = get_shadow_render_node();
    if (shadow_render_node == nullptr)
    {
        return nullptr;
    }
    Light_projections& light_projections = shadow_render_node->light_projections();
    return &light_projections;
}

auto Viewport_window::get_shadow_texture() const -> erhe::graphics::Texture*
{
    const auto* shadow_render_node = get_shadow_render_node();
    if (shadow_render_node == nullptr)
    {
        return nullptr;
    }
    return shadow_render_node->texture().get();
}

void Viewport_window::imgui_toolbar()
{
    if (m_trs_tool)
    {
        m_trs_tool->viewport_toolbar();
    }

    if (m_grid_tool)
    {
        m_grid_tool->viewport_toolbar();
    }

    ImGui::SameLine();
    ImGui::SetNextItemWidth(150.0f);
    scene_root()->camera_combo("Camera", m_camera);

    ImGui::SameLine();
    ImGui::SetNextItemWidth(140.0f);
    ImGui::Combo(
        "Shader",
        reinterpret_cast<int*>(&m_shader_stages_variant),
        c_shader_stages_variant_strings,
        IM_ARRAYSIZE(c_shader_stages_variant_strings),
        IM_ARRAYSIZE(c_shader_stages_variant_strings)
    );

    const auto& post_processing_node = m_post_processing_node.lock();
    if (post_processing_node)
    {
        ImGui::SameLine();
        post_processing_node->imgui_toolbar();
    }
}

void Viewport_window::link_to(std::weak_ptr<erhe::application::Multisample_resolve_node> node)
{
    m_multisample_resolve_node = node;
}

void Viewport_window::link_to(std::weak_ptr<Post_processing_node> node)
{
    m_post_processing_node = node;
}

auto Viewport_window::get_post_processing_node() -> std::shared_ptr<Post_processing_node>
{
    return m_post_processing_node.lock();
}

void Viewport_window::set_final_output(
    std::weak_ptr<erhe::application::Rendergraph_node> node
)
{
    m_final_output = node;
}

auto Viewport_window::get_final_output() -> std::weak_ptr<erhe::application::Rendergraph_node>
{
    return m_final_output;
}

} // namespace editor
