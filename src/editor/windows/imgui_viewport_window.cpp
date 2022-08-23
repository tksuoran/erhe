#include "windows/imgui_viewport_window.hpp"

#include "editor_log.hpp"
#include "editor_rendering.hpp"
#include "renderers/id_renderer.hpp"
#include "renderers/programs.hpp"
#include "renderers/render_context.hpp"
#include "scene/scene_root.hpp"
#include "scene/viewport_window.hpp"
#include "tools/selection_tool.hpp"
#include "tools/tools.hpp"
#if defined(ERHE_XR_LIBRARY_OPENXR)
#   include "xr/headset_renderer.hpp"
#endif

#include "erhe/application/configuration.hpp"
#include "erhe/application/imgui/imgui_viewport.hpp"
#include "erhe/application/imgui/imgui_windows.hpp"
#include "erhe/application/view.hpp"
#include "erhe/application/windows/log_window.hpp"
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

using erhe::graphics::Framebuffer;
using erhe::graphics::Texture;

//int Imgui_viewport_window::s_serial = 0;

Imgui_viewport_window::Imgui_viewport_window()
    : erhe::application::Imgui_window{
        "(default constructed Imgui_viewport_window)",
        "(default constructed Imgui_viewport_window)"
    }
    , erhe::application::Texture_rendergraph_node{
        erhe::application::Texture_rendergraph_node_create_info{
            .name                 = std::string{"(default constructed Imgui_viewport_window)"},
            .input_key            = erhe::application::Rendergraph_node_key::viewport,
            .output_key           = erhe::application::Rendergraph_node_key::window,
            .color_format         = gl::Internal_format{0},
            .depth_stencil_format = gl::Internal_format{0}
        }
    }
{
}

// TODO Only really needed when there is no post processing and no MSAA
Imgui_viewport_window::Imgui_viewport_window(
    const std::string_view name,
    Viewport_window*       viewport_window
)
    : erhe::application::Imgui_window            {name, name}
    , erhe::application::Texture_rendergraph_node{
        erhe::application::Texture_rendergraph_node_create_info{
            .name                 = std::string{name},
            .input_key            = erhe::application::Rendergraph_node_key::viewport,
            .output_key           = erhe::application::Rendergraph_node_key::window,
            .color_format         = gl::Internal_format::rgba16f,
            .depth_stencil_format = gl::Internal_format::depth24_stencil8
        }
    }
    , m_viewport_window{viewport_window}
{
    m_viewport.x      = 0;
    m_viewport.y      = 0;
    m_viewport.width  = 0;
    m_viewport.height = 0;

    register_input(
        erhe::application::Resource_routing::Resource_provided_by_consumer,
        "viewport",
        erhe::application::Rendergraph_node_key::viewport
    );

    // "window" is slot / pseudo-resource which allows use rendergraph connection
    // to make Imgui_viewport_window a dependency for Imgui_viewport, forcing
    // correct rendering order (Imgui_viewport_window must be rendered before
    // Imgui_viewport).
    //
    // TODO Imgui_renderer should carry dependencies using Rendergraph.
    register_output(
        erhe::application::Resource_routing::None,
        "window",
        erhe::application::Rendergraph_node_key::window
    );
}

[[nodiscard]] auto Imgui_viewport_window::viewport_window() const -> Viewport_window*
{
    return m_viewport_window;
}

[[nodiscard]] auto Imgui_viewport_window::is_hovered() const -> bool
{
    return m_is_hovered;
}

auto Imgui_viewport_window::consumes_mouse_input() const -> bool
{
    return true;
}

void Imgui_viewport_window::on_begin()
{
#if defined(ERHE_GUI_LIBRARY_IMGUI)
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{0.0f, 0.0f});
#endif
}

void Imgui_viewport_window::on_end()
{
#if defined(ERHE_GUI_LIBRARY_IMGUI)
    ImGui::PopStyleVar();
#endif
}

void Imgui_viewport_window::set_viewport(
    erhe::application::Imgui_viewport* imgui_viewport
)
{
    Imgui_window::set_viewport(imgui_viewport);
}

[[nodiscard]] auto Imgui_viewport_window::get_consumer_input_viewport(
    const erhe::application::Resource_routing resource_routing,
    const int                                 key,
    const int                                 depth
) const -> erhe::scene::Viewport
{
    static_cast<void>(resource_routing); // TODO Validate
    static_cast<void>(depth);
    static_cast<void>(key);
    //ERHE_VERIFY(key == erhe::application::Rendergraph_node_key::window); TODO
    return m_viewport;
}

[[nodiscard]] auto Imgui_viewport_window::get_producer_output_viewport(
    const erhe::application::Resource_routing resource_routing,
    const int                                 key,
    const int                                 depth
) const -> erhe::scene::Viewport
{
    static_cast<void>(resource_routing); // TODO Validate
    static_cast<void>(depth);
    static_cast<void>(key);
    //ERHE_VERIFY(key == erhe::application::Rendergraph_node_key::window); TODO
    return m_viewport;
}

void Imgui_viewport_window::imgui()
{
#if defined(ERHE_GUI_LIBRARY_IMGUI)
    ERHE_PROFILE_FUNCTION

    m_viewport_window->imgui_toolbar();

    const auto size = ImGui::GetContentRegionAvail();

    m_viewport.width  = static_cast<int>(size.x);
    m_viewport.height = static_cast<int>(size.y);

    const auto& color_texture = get_consumer_input_texture(
        erhe::application::Resource_routing::Resource_provided_by_producer,
        erhe::application::Rendergraph_node_key::viewport
    );

    if (color_texture)
    {
        const int texture_width  = color_texture->width();
        const int texture_height = color_texture->height();

        if ((texture_width >= 1) && (texture_height >= 1))
        {
            SPDLOG_LOGGER_TRACE(
                log_render,
                "Imgui_viewport_window::imgui() rendering texture {} {}",
                texture->gl_name(),
                texture->debug_label()
            );
            image(
                color_texture,
                static_cast<int>(size.x),
                static_cast<int>(size.y)
            );
            m_is_hovered = ImGui::IsItemHovered();
            const auto rect_min = ImGui::GetItemRectMin();
            const auto rect_max = ImGui::GetItemRectMax();
            ERHE_VERIFY(m_viewport.width  == static_cast<int>(rect_max.x - rect_min.x));
            ERHE_VERIFY(m_viewport.height == static_cast<int>(rect_max.y - rect_min.y));

            m_viewport_window->set_window_viewport(
                static_cast<int>(rect_min.x),
                static_cast<int>(rect_min.y),
                m_viewport.width,
                m_viewport.height,
                m_is_hovered
            );
        }
    }
    else
    {
        m_is_hovered = false;
        m_viewport_window->set_window_viewport(
            0,
            0,
            0,
            0,
            m_is_hovered
        );
    }

    //m_viewport_config.imgui();

#endif
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


} // namespace editor
