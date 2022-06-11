#include "tools/pointer_context.hpp"
#include "log.hpp"
#include "editor_rendering.hpp"

#include "scene/node_raytrace.hpp"
#include "scene/scene_root.hpp"
#include "renderers/id_renderer.hpp"

#include "erhe/application/time.hpp"
#include "erhe/application/windows/log_window.hpp"
#include "erhe/log/log_fmt.hpp"
#include "erhe/log/log_glm.hpp"
#include "erhe/raytrace/iscene.hpp"
#include "erhe/raytrace/igeometry.hpp"
#include "erhe/raytrace/iinstance.hpp"
#include "erhe/raytrace/ray.hpp"
#include "erhe/scene/camera.hpp"
#include "erhe/scene/scene.hpp"
#include "erhe/toolkit/profile.hpp"
#include "erhe/toolkit/math_util.hpp"

#include <fmt/format.h>
#include <fmt/chrono.h>

#include <gsl/gsl>

namespace editor
{

using erhe::geometry::c_polygon_normals;
using erhe::geometry::Polygon_id;

Pointer_context::Pointer_context()
    : erhe::components::Component{c_label}
    , m_ray_scene_build_timer    {"Ray scene build"}
    , m_ray_traverse_timer       {"Ray traverse"}
{
}

Pointer_context::~Pointer_context() = default;

void Pointer_context::connect()
{
    m_editor_rendering = get<Editor_rendering>();
    m_scene_root       = get<Scene_root>();
    m_viewport_windows = get<Viewport_windows>();
}

void Pointer_context::update_keyboard(
    const bool                   pressed,
    const erhe::toolkit::Keycode code,
    const uint32_t               modifier_mask
)
{
    static_cast<void>(pressed);
    static_cast<void>(code);

    m_shift   = (modifier_mask & erhe::toolkit::Key_modifier_bit_shift) == erhe::toolkit::Key_modifier_bit_shift;
    m_alt     = (modifier_mask & erhe::toolkit::Key_modifier_bit_menu ) == erhe::toolkit::Key_modifier_bit_menu;
    m_control = (modifier_mask & erhe::toolkit::Key_modifier_bit_ctrl ) == erhe::toolkit::Key_modifier_bit_ctrl;
}

void Pointer_context::update_mouse(
    const erhe::toolkit::Mouse_button button,
    const int                         count
)
{
    log_pointer->trace("mouse {} count = {}", static_cast<int>(button), count);
    m_mouse_button[button].pressed  = (count > 0);
    m_mouse_button[button].released = (count == 0);
}

void Pointer_context::update_mouse(
    const double x,
    const double y
)
{
    log_pointer->trace("mouse x = {} y = {}", x, y);
    m_mouse_x = x;
    m_mouse_y = y;
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

                log_pointer->trace("{}: Hit position: {}", slot_names[i], entry.position.value());

                if (entry.raytrace_node != nullptr)
                {
                    auto* node = entry.raytrace_node->get_node();
                    if (node != nullptr)
                    {
                        log_pointer->trace("{}: Hit node: {} {}", slot_names[i], node->node_type(), node->name());
                        const auto* rt_instance = entry.raytrace_node->raytrace_instance();
                        if (rt_instance != nullptr)
                        {
                            log_pointer->trace(
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
                                log_pointer->trace("{}: Hit geometry: {}", slot_names[i], entry.geometry->name);
                            }
                            if (hit.primitive_id < primitive->primitive_geometry.primitive_id_to_polygon_id.size())
                            {
                                const auto polygon_id = primitive->primitive_geometry.primitive_id_to_polygon_id[hit.primitive_id];
                                log_pointer->trace("{}: Hit polygon: {}", slot_names[i], polygon_id);
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
                log_pointer->trace("{}: no hit", slot_names[i]);
            }
        }
    }

    log_pointer->trace("Nearest slot: {}", slot_names[m_nearest_slot]);

    const auto duration = m_ray_traverse_timer.duration().value();
    if (duration >= std::chrono::milliseconds(1))
    {
        log_pointer->trace("ray intersect time: {}", std::chrono::duration_cast<std::chrono::milliseconds>(duration));
    }
    else if (duration >= std::chrono::microseconds(1))
    {
        log_pointer->trace("ray intersect time: {}", std::chrono::duration_cast<std::chrono::microseconds>(duration));
    }
    else
    {
        log_pointer->trace("ray intersect time: {}", duration);
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


void Pointer_context::update_viewport(Viewport_window* viewport_window)
{
    ERHE_PROFILE_FUNCTION

    std::fill(
        m_hover_entries.begin(),
        m_hover_entries.end(),
        Hover_entry{}
    );
    m_position_in_window    .reset();
    m_near_position_in_world.reset();
    m_far_position_in_world .reset();
    m_update_window     = nullptr;

    m_window = viewport_window;

    if (m_window == nullptr)
    {
        return;
    }

    const glm::vec2 position_in_window = m_window->to_scene_content(
        glm::vec2{
            m_mouse_x,
            m_mouse_y
        }
    );

    const bool reverse_depth = m_window->viewport().reverse_depth;
    m_position_in_window     = position_in_window;
    m_near_position_in_world = position_in_world_viewport_depth(reverse_depth ? 1.0f : 0.0f);
    m_far_position_in_world  = position_in_world_viewport_depth(reverse_depth ? 0.0f : 1.0f);

    auto* camera = m_window->camera();
    if (camera == nullptr)
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
    const auto id_renderer     = get<Id_renderer>();
    //const auto scene_root      = get<Scene_root>();
    const bool in_content_area = pointer_in_content_area();
    if (in_content_area && id_renderer)
    {
        const auto id_query = id_renderer->get(
            static_cast<int>(position_in_window.x),
            static_cast<int>(position_in_window.y)
        );

        //m_position_in_window.value().z = id_query.depth;

        m_near_position_in_world = position_in_world_viewport_depth(reverse_depth ? 1.0f : 0.0f);
        m_far_position_in_world  = position_in_world_viewport_depth(reverse_depth ? 0.0f : 1.0f);

        Hover_entry entry;
        entry.position = position_in_world_viewport_depth(id_query.depth);
        entry.valid    = id_query.valid;

        trace_fmt(log_pointer, "position in world = {}", entry.position.value());
        if (id_query.valid)
        {
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
                    const auto polygon_id = static_cast<Polygon_id>(entry.local_index);
                    if (
                        polygon_id < entry.geometry->get_polygon_count()
                    )
                    {
                        trace_fmt(log_pointer, "hover polygon = {}", polygon_id);
                        auto* const polygon_normals = entry.geometry->polygon_attributes().find<glm::vec3>(c_polygon_normals);
                        if (
                            (polygon_normals != nullptr) &&
                            polygon_normals->has(polygon_id)
                        )
                        {
                            const auto local_normal    = polygon_normals->get(polygon_id);
                            const auto world_from_node = entry.mesh->world_from_node();
                            entry.normal = glm::vec3{world_from_node * glm::vec4{local_normal, 0.0f}};
                            trace_fmt(log_pointer, "hover normal = {}", entry.normal.value());
                        }
                    }
                }
            }
            const bool hover_content = id_query.mesh && (entry.mesh->get_visibility_mask() & erhe::scene::Node_visibility::content) == erhe::scene::Node_visibility::content;
            const bool hover_tool    = id_query.mesh && (entry.mesh->get_visibility_mask() & erhe::scene::Node_visibility::tool   ) == erhe::scene::Node_visibility::tool;
            const bool hover_brush   = id_query.mesh && (entry.mesh->get_visibility_mask() & erhe::scene::Node_visibility::brush  ) == erhe::scene::Node_visibility::brush;
            const bool hover_gui     = id_query.mesh && (entry.mesh->get_visibility_mask() & erhe::scene::Node_visibility::gui    ) == erhe::scene::Node_visibility::gui;
            trace_fmt(
                log_pointer,
                "hover mesh = {} primitive = {} local index {} {}{}{}{}",
                entry.mesh ? entry.mesh->name() : "()",
                entry.primitive,
                entry.local_index,
                hover_content ? "content " : "",
                hover_tool    ? "tool "    : "",
                hover_brush   ? "brush "   : "",
                hover_gui     ? "gui "     : ""
            );
            if (hover_content)
            {
                m_hover_entries[content_slot] = entry;
            }
            if (hover_tool)
            {
                m_hover_entries[tool_slot] = entry;
            }
            if (hover_brush)
            {
                m_hover_entries[brush_slot] = entry;
            }
            if (hover_gui)
            {
                m_hover_entries[gui_slot] = entry;
            }
        }
        else
        {
            log_pointer.trace("pointer context hover not valid");
        }
    }
#endif

    m_frame_number = get<erhe::application::Time>()->frame_number();
}


auto Pointer_context::near_position_in_world() const -> nonstd::optional<glm::vec3>
{
    return m_near_position_in_world;
}

auto Pointer_context::far_position_in_world() const -> nonstd::optional<glm::vec3>
{
    return m_far_position_in_world;
}

auto Pointer_context::position_in_world_distance(const float distance) const -> nonstd::optional<glm::vec3>
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

auto Pointer_context::position_in_world_viewport_depth(const double viewport_depth) const -> nonstd::optional<glm::dvec3>
{
    if (!m_position_in_window.has_value())
    {
        return {};
    }

    const double depth_range_near   = 0.0;
    const double depth_range_far    = 1.0;
    const auto   position_in_window = glm::dvec3{
        m_position_in_window.value().x,
        m_position_in_window.value().y,
        viewport_depth
    };
    const auto* const camera                = m_window->camera();
    const auto        vp                    = m_window->viewport();
    const auto        projection_transforms = camera->projection_transforms(vp);
    const glm::mat4   world_from_clip       = projection_transforms.clip_from_world.inverse_matrix();

    return erhe::toolkit::unproject(
        glm::dmat4{world_from_clip},
        position_in_window,
        depth_range_near,
        depth_range_far,
        static_cast<double>(vp.x),
        static_cast<double>(vp.y),
        static_cast<double>(vp.width),
        static_cast<double>(vp.height)
    );
}

auto Pointer_context::position_in_viewport_window() const -> nonstd::optional<glm::vec2>
{
    return m_position_in_window;
}

auto Pointer_context::pointer_in_content_area() const -> bool
{
    return
        (m_window != nullptr) &&
        m_position_in_window.has_value() &&
        (m_position_in_window.value().x >= 0) &&
        (m_position_in_window.value().y >= 0) &&
        (m_position_in_window.value().x < m_window->viewport().width) &&
        (m_position_in_window.value().y < m_window->viewport().height);
}

auto Pointer_context::shift_key_down() const -> bool
{
    return m_shift;
}

auto Pointer_context::control_key_down() const -> bool
{
    return m_control;
}

auto Pointer_context::alt_key_down() const -> bool
{
    return m_alt;
}

auto Pointer_context::mouse_button_pressed(const erhe::toolkit::Mouse_button button) const -> bool
{
    Expects(button < erhe::toolkit::Mouse_button_count);
    return m_mouse_button[static_cast<int>(button)].pressed;
}

auto Pointer_context::mouse_button_released(const erhe::toolkit::Mouse_button button) const -> bool
{
    Expects(button < erhe::toolkit::Mouse_button_count);
    return m_mouse_button[static_cast<int>(button)].released;
}

auto Pointer_context::mouse_x() const -> double
{
    return m_mouse_x;
}
auto Pointer_context::mouse_y() const -> double
{
    return m_mouse_y;
}

auto Pointer_context::get_hover(size_t slot) const -> const Hover_entry&
{
    return m_hover_entries.at(slot);
}

auto Pointer_context::get_nearest_hover() const -> const Hover_entry&
{
    return m_hover_entries.at(m_nearest_slot);
}

auto Pointer_context::window() const -> Viewport_window*
{
    return m_window;
}

auto Pointer_context::last_window() const -> Viewport_window*
{
    return m_last_window;
}

auto Pointer_context::frame_number() const -> uint64_t
{
    return m_frame_number;
}

} // namespace editor
