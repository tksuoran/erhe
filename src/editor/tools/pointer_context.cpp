#include "tools/pointer_context.hpp"
#include "editor_time.hpp"
#include "log.hpp"
#include "rendering.hpp"

#include "scene/node_raytrace.hpp"
#include "scene/scene_root.hpp"
#include "renderers/id_renderer.hpp"
#include "windows/log_window.hpp"

#include "erhe/log/log_glm.hpp"
#include "erhe/raytrace/iscene.hpp"
#include "erhe/raytrace/igeometry.hpp"
#include "erhe/raytrace/iinstance.hpp"
#include "erhe/scene/camera.hpp"
#include "erhe/scene/scene.hpp"
#include "erhe/toolkit/math_util.hpp"

#include <gsl/gsl>

namespace editor
{

using erhe::geometry::c_polygon_normals;
using erhe::geometry::Polygon_id;

Pointer_context::Pointer_context()
    : erhe::components::Component{c_name}
{
}

Pointer_context::~Pointer_context() = default;

void Pointer_context::connect()
{
    m_editor_rendering = get<Editor_rendering>();
    m_log_window       = get<Log_window>();
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
    log_input_events.trace("mouse {} count = {}\n", static_cast<int>(button), count);
    m_mouse_button[button].pressed  = (count > 0);
    m_mouse_button[button].released = (count == 0);
}

void Pointer_context::update_mouse(
    const double x,
    const double y
)
{
    log_input_events.trace("mouse x = {} y = {}\n", x, y);
    m_mouse_x = x;
    m_mouse_y = y;
}


void Pointer_context::raytrace()
{
    if (m_window == nullptr)
    {
        return;
    }

    const auto log = get<Log_window>();

    const auto pointer_near = position_in_world(1.0);
    //if (pointer_near.has_value())
    //{
    //    log->frame_log("Near: {}", glm::vec3{pointer_near.value()});
    //}
    const auto pointer_far  = position_in_world(0.0);
    //if (pointer_far.has_value())
    //{
    //    log->frame_log("Far: {}", glm::vec3{pointer_far.value()});
    //}

    if (!pointer_near.has_value() || !pointer_far.has_value())
    {
        return;
    }

    //log->frame_log("Direction: {}", glm::vec3{direction});

    const glm::vec3 origin{pointer_near.value()};

    erhe::raytrace::Ray ray{
        .origin    = glm::vec3{pointer_near.value()},
        .t_near    = 0.0f,
        .direction = glm::vec3{glm::normalize(pointer_far.value() - pointer_near.value())},
        .time      = 0.0f,
        .t_far     = 9999.0f,
        .mask      = 0,
        .id        = 0,
        .flags     = 0
    };
    erhe::raytrace::Hit hit{};

    auto& scene = m_scene_root->raytrace_scene();
    scene.commit();
    scene.intersect(ray, hit);
    //log->frame_log("Ray near t: {}",    ray.t_near);
    //log->frame_log("Ray near: {}",      ray.origin + ray.t_near * ray.direction);
    //log->frame_log("Ray far t: {}",     ray.t_far);
    //log->frame_log("Ray far: {}",       ray.origin + ray.t_far * ray.direction);
    //log->frame_log("Hit UV: {}",        hit.uv);
    //log->frame_log("Hit primitive: {}", hit.primitive_id);
    //if (hit.geometry != nullptr)
    //{
    //    log->frame_log("Hit geometry: {}", hit.geometry->debug_label());
    //}
    if (hit.instance != nullptr)
    {
        //log->frame_log("Hit instance: {}", hit.instance->debug_label());
        void* user_data     = hit.instance->get_user_data();
        auto* node_raytrace = reinterpret_cast<Node_raytrace*>(user_data);
        m_raytrace_node         = node_raytrace;
        m_raytrace_hit_position = ray.origin + ray.t_far * ray.direction;
        log->frame_log("Hit position: {}", m_raytrace_hit_position.value());
        if (node_raytrace != nullptr)
        {
            auto* primitive = node_raytrace->raytrace_primitive();
            if (primitive != nullptr)
            {
                auto* geometry = primitive->geometry.get();
                if (geometry != nullptr)
                {
                    log->frame_log("Hit geometry: {}", geometry->name);
                }
                if (hit.primitive_id < primitive->primitive_geometry.primitive_id_to_polygon_id.size())
                {
                    const auto polygon_id = primitive->primitive_geometry.primitive_id_to_polygon_id[hit.primitive_id];
                    log->frame_log("Hit polygon: {}", polygon_id);
                }
            }
        }
    }
    else
    {
        m_raytrace_node = nullptr;
        m_raytrace_hit_position.reset();
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

void Pointer_context::update_viewport(Viewport_window* viewport_window)
{
    m_hover_mesh.reset();
    m_hover_geometry    = nullptr;
    m_hover_layer       = nullptr;
    m_position_in_window    .reset();
    m_position_in_world     .reset();
    m_near_position_in_world.reset();
    m_far_position_in_world .reset();
    m_hover_primitive   = 0;
    m_hover_local_index = 0;
    m_hover_tool        = false;
    m_hover_content     = false;
    m_hover_gui         = false;
    m_hover_valid       = false;
    m_update_window     = nullptr;
    m_raytrace_hit_position.reset();

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

    m_position_in_window = glm::vec3{position_in_window, 1.0f};

    auto* camera = m_window->camera();
    if (camera == nullptr)
    {
        return;
    }

#if 1
    if (pointer_in_content_area())
    {
        raytrace();
    }
#endif

    const auto id_renderer     = get<Id_renderer>();
    //const auto scene_root      = get<Scene_root>();
    const bool in_content_area = pointer_in_content_area();
    //m_log_window->frame_log("in_content_area = {}", in_content_area);
    if (in_content_area && id_renderer)
    {
        const auto mesh_primitive = id_renderer->get(
            static_cast<int>(position_in_window.x),
            static_cast<int>(position_in_window.y),
            m_position_in_window.value().z
        );
        m_position_in_world      = position_in_world(m_position_in_window.value().z);
        m_near_position_in_world = position_in_world(0.0f);
        m_far_position_in_world  = position_in_world(1.0f);
        m_hover_valid = mesh_primitive.valid;
        //m_log_window->frame_log("position in world = {}", m_position_in_world.value());
        if (m_hover_valid && (mesh_primitive.layer != nullptr))
        {
            m_hover_mesh        = mesh_primitive.mesh;
            m_hover_layer       = mesh_primitive.layer;
            m_hover_primitive   = mesh_primitive.mesh_primitive_index;
            m_hover_local_index = mesh_primitive.local_index;
            m_hover_tool        = (m_hover_layer->flags & erhe::scene::Node::c_visibility_tool   ) == erhe::scene::Node::c_visibility_tool;
            m_hover_content     = (m_hover_layer->flags & erhe::scene::Node::c_visibility_content) == erhe::scene::Node::c_visibility_content;
            m_hover_gui         = (m_hover_layer->flags & erhe::scene::Node::c_visibility_gui    ) == erhe::scene::Node::c_visibility_gui;
            m_log_window->frame_log(
                "hover mesh = {} primitive = {} local index {} tool = {} content = {} gui = {}",
                m_hover_mesh ? m_hover_mesh->name() : "()",
                m_hover_primitive,
                m_hover_local_index,
                m_hover_tool,
                m_hover_content,
                m_hover_gui
            );
            if (m_hover_mesh)
            {
                const auto& primitive = m_hover_mesh->data.primitives[m_hover_primitive];
                m_hover_geometry = primitive.source_geometry.get();
                m_hover_normal   = {};
                if (m_hover_geometry != nullptr)
                {
                    const auto polygon_id = static_cast<Polygon_id>(m_hover_local_index);
                    if (
                        polygon_id < m_hover_geometry->get_polygon_count()
                    )
                    {
                        m_log_window->frame_log("hover polygon = {}", polygon_id);
                        auto* const polygon_normals = m_hover_geometry->polygon_attributes().find<glm::vec3>(c_polygon_normals);
                        if (
                            (polygon_normals != nullptr) &&
                            polygon_normals->has(polygon_id)
                        )
                        {
                            const auto local_normal    = polygon_normals->get(polygon_id);
                            const auto world_from_node = m_hover_mesh->world_from_node();
                            m_hover_normal = glm::vec3{world_from_node * glm::vec4{local_normal, 0.0f}};
                            //m_log_window->frame_log("hover normal = {}", m_hover_normal.value());
                        }
                    }
                }
            }
        }
        //else
        //{
        //    m_log_window->tail_log("pointer context hover not valid");
        //}
        // else mesh etc. contain latest valid values
    }
    else
    {
        //m_log_window->frame_log("hover not content area or no id renderer");
        m_hover_valid       = false;
        m_hover_mesh        = nullptr;
        m_hover_primitive   = 0;
        m_hover_local_index = 0;
    }

    m_frame_number = get<Editor_time>()->frame_number();
}

auto Pointer_context::position_in_world() const -> nonstd::optional<glm::vec3>
{
    return m_position_in_world;
}

auto Pointer_context::near_position_in_world() const -> nonstd::optional<glm::vec3>
{
    return m_near_position_in_world;
}

auto Pointer_context::far_position_in_world() const -> nonstd::optional<glm::vec3>
{
    return m_far_position_in_world;
}

auto Pointer_context::raytrace_node() const -> Node_raytrace*
{
    return m_raytrace_node;
}

auto Pointer_context::raytrace_hit_position() const -> nonstd::optional<glm::vec3>
{
    return m_raytrace_hit_position;
}

auto Pointer_context::position_in_world(const double viewport_depth) const -> nonstd::optional<glm::dvec3>
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
    Expects(button < erhe::toolkit::Mouse_button_count && button >= 0);
    return m_mouse_button[static_cast<int>(button)].pressed;
}

auto Pointer_context::mouse_button_released(const erhe::toolkit::Mouse_button button) const -> bool
{
    Expects(button < erhe::toolkit::Mouse_button_count && button >= 0);
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

auto Pointer_context::hovering_over_tool() const -> bool
{
    return m_hover_valid && m_hover_tool;
}

auto Pointer_context::hovering_over_content() const -> bool
{
    return m_hover_valid && m_hover_content;
}

auto Pointer_context::hovering_over_gui() const -> bool
{
    return m_hover_valid && m_hover_gui;
}

auto Pointer_context::hover_normal() const -> nonstd::optional<glm::vec3>
{
    return m_hover_normal;
}

auto Pointer_context::hover_mesh() const -> std::shared_ptr<erhe::scene::Mesh>
{
    return m_hover_mesh;
}

auto Pointer_context::hover_primitive() const -> size_t
{
    return m_hover_primitive;
}

auto Pointer_context::hover_local_index() const -> size_t
{
    return m_hover_local_index;
}

auto Pointer_context::hover_geometry() const -> erhe::geometry::Geometry*
{
    return m_hover_geometry;
}

auto Pointer_context::window() const -> Viewport_window*
{
    return m_window;
}

auto Pointer_context::last_window() const -> Viewport_window*
{
    return m_last_window;
}

auto Pointer_context::position_in_viewport_window() const -> nonstd::optional<glm::vec3>
{
    return m_position_in_window;
}

auto Pointer_context::frame_number() const -> uint64_t
{
    return m_frame_number;
}

} // namespace editor
