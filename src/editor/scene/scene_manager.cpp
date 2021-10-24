#include "scene/scene_manager.hpp"
#include "graphics/gl_context_provider.hpp"
#include "parsers/json_polyhedron.hpp"
#include "parsers/wavefront_obj.hpp"
#include "renderers/mesh_memory.hpp"
#include "renderers/programs.hpp"
#include "scene/brush.hpp"
#include "scene/debug_draw.hpp"
#include "scene/helpers.hpp"
#include "scene/node_physics.hpp"
#include "scene/scene_root.hpp"
#include "windows/brushes.hpp"
#include "log.hpp"

#include "SkylineBinPack.h" // RectangleBinPack

#include "erhe/geometry/shapes/box.hpp"
#include "erhe/geometry/shapes/cone.hpp"
#include "erhe/geometry/shapes/disc.hpp"
#include "erhe/geometry/shapes/sphere.hpp"
#include "erhe/geometry/shapes/torus.hpp"
#include "erhe/geometry/shapes/regular_polyhedron.hpp"
#include "erhe/graphics/buffer.hpp"
#include "erhe/graphics/buffer_transfer_queue.hpp"
#include "erhe/primitive/primitive.hpp"
#include "erhe/primitive/primitive_builder.hpp"
#include "erhe/primitive/material.hpp"
#include "erhe/primitive/material.hpp"
#include "erhe/physics/icollision_shape.hpp"
#include "erhe/physics/iworld.hpp"
#include "erhe/scene/camera.hpp"
#include "erhe/scene/light.hpp"
#include "erhe/scene/mesh.hpp"
#include "erhe/scene/node.hpp"
#include "erhe/scene/scene.hpp"
#include "erhe/toolkit/math_util.hpp"
#include "erhe/toolkit/tracy_client.hpp"

#include "mango/core/thread.hpp"

#include <glm/gtx/quaternion.hpp>
#include <glm/gtx/color_space.hpp>
#include <glm/gtx/rotate_vector.hpp>

namespace editor
{

using namespace erhe::graphics;
using namespace erhe::geometry;
using namespace erhe::geometry::shapes;
using namespace erhe::scene;
using namespace erhe::primitive;
using namespace std;
using namespace glm;


Scene_manager::Scene_manager()
    : Component(c_name)
{
}

Scene_manager::~Scene_manager() = default;

void Scene_manager::connect()
{
    require<Gl_context_provider>();

    m_brushes     = require<Brushes>();
    m_mesh_memory = require<Mesh_memory>();
    m_scene_root  = require<Scene_root>();
}

void Scene_manager::initialize_component()
{
    ZoneScoped;

    Scoped_gl_context gl_context{Component::get<Gl_context_provider>().get()};

    m_scene_root = Component::get<Scene_root>();

    initialize_camera();
    add_scene();
}

void Scene_manager::set_view_camera(std::shared_ptr<erhe::scene::ICamera> camera)
{
    m_view_camera = camera;
}

auto Scene_manager::get_view_camera() const -> std::shared_ptr<erhe::scene::ICamera>
{
    return m_view_camera;
}

void Scene_manager::initialize_camera()
{
    m_camera = make_shared<erhe::scene::Camera>("Camera");
    m_camera->projection()->fov_y           = erhe::toolkit::degrees_to_radians(35.0f);
    m_camera->projection()->projection_type = erhe::scene::Projection::Type::perspective_vertical;
    m_camera->projection()->z_near          = 0.03f;
    m_camera->projection()->z_far           = 200.0f;
    m_scene_root->scene().cameras.push_back(m_camera);

    auto node = make_shared<erhe::scene::Node>("Camera");
    m_scene_root->scene().nodes.emplace_back(node);
    //const glm::mat4 identity{1.0f};
    //node->transforms.parent_from_node.set(identity);
    node->transforms.parent_from_node.set_translation(0.0f, 1.65f, 0.0f);
    node->update();
    node->attach(m_camera);

    set_view_camera(m_camera);
}

auto Scene_manager::build_info_set() -> erhe::primitive::Build_info_set&
{
    return m_mesh_memory->build_info_set;
};

void Scene_manager::make_brushes()
{
    ZoneScoped;
    const Brush_create_context brush_create_context{build_info_set()};
    mango::ConcurrentQueue     execution_queue;
    //mango::SerialQueue         execution_queue;

    auto floor_box_shape = erhe::physics::ICollision_shape::create_box_shape_shared(glm::vec3{20.0f, 0.5f, 20.0f});

    // Otherwise it will be destructed when leave add_floor() scope
    m_collision_shapes.push_back(floor_box_shape);
    //m_scene_root->physics_world().collision_shapes.push_back(floor_box_shape);

    // Floor
    execution_queue.enqueue(
        [this, &floor_box_shape]()
        {
            ZoneScopedN("Floor brush");

            Brush_create_context context{build_info_set()}; //, Normal_style::polygon_normals};
            context.normal_style = Normal_style::polygon_normals;

            auto floor_geometry = std::make_shared<erhe::geometry::Geometry>(
                std::move(
                    make_box(
                        vec3(40.0f, 1.0f, 40.0f),
                        ivec3(40, 1, 40)
                    )
                )
            );
            floor_geometry->name = "floor";
            floor_geometry->build_edges();

            const Brush::Create_info brush_create_info{
                floor_geometry,
                build_info_set(),
                Normal_style::polygon_normals,
                0.0f, // density for static object
                0.0f, // volume for static object
                floor_box_shape
            };
            m_floor_brush = make_unique<Brush>(brush_create_info);
        }
    );

    // Teapot
    if constexpr (false)
    {
        execution_queue.enqueue(
            [this]()
            {
                ZoneScopedN("teapot from .obj");

                const Brush_create_context context{build_info_set(), Normal_style::point_normals};
                constexpr bool instantiate = true;

                const char* teaset[] = {
                    "res/models/teapot.obj",
                    "res/models/teacup.obj",
                    "res/models/spoon.obj"
                };
                for (auto* path : teaset)
                {
                    auto geometries = parse_obj_geometry(path);

                    for (auto& geometry : geometries)
                    {
                        geometry->compute_polygon_normals();
                        // The real teapot is ~33% taller (ratio 4:3)
                        const mat4 scale_t = erhe::toolkit::create_scale(0.5f, 0.5f * 4.0f / 3.0f, 0.5f);
                        geometry->transform(scale_t);
                        make_brush(instantiate, move(geometry), context);
                    }
                }
            }
        );
    }

    // Platonic solids
    if constexpr (false)
    {
        execution_queue.enqueue(
            [this]()
            {
                ZoneScopedN("Platonic solids");

                const Brush_create_context context{build_info_set(), Normal_style::polygon_normals};
                constexpr bool instantiate = true;

                constexpr float original_scale = 1.0f;
                //make_brush(instantiate, make_dodecahedron (original_scale), context);
                //make_brush(instantiate, make_icosahedron  (original_scale), context);
                //make_brush(instantiate, make_octahedron   (original_scale), context);
                make_brush(instantiate, make_tetrahedron  (original_scale), context);
                //make_brush(instantiate, make_cuboctahedron(original_scale), context);
                make_brush(
                    instantiate,
                    make_cube(original_scale),
                    context,
                    erhe::physics::ICollision_shape::create_box_shape_shared(glm::vec3(original_scale * 0.5f))
                );
            }
        );
    }

    // Sphere
    if constexpr (true)
    {
        execution_queue.enqueue(
            [this]()
            {
                ZoneScopedN("Sphere");

                const Brush_create_context context{build_info_set(), Normal_style::polygon_normals};
                constexpr bool instantiate = true;

                make_brush(
                    instantiate,
                    make_box(1.0f, 2.0f, 0.5f),
                    context,
                    erhe::physics::ICollision_shape::create_box_shape_shared(glm::vec3(0.5f, 1.0f, 0.25f))
                );

                make_brush(
                    instantiate,
                    make_sphere(1.0f, 12 * 4, 4 * 6),
                    context,
                    erhe::physics::ICollision_shape::create_sphere_shape_shared(1.0f)
                );
                make_brush(
                    instantiate,
                    make_sphere(1.0f, 12 * 3, 3 * 6),
                    context,
                    erhe::physics::ICollision_shape::create_sphere_shape_shared(1.0f)
                );
                make_brush(
                    instantiate,
                    make_sphere(1.0f, 12 * 2, 2 * 6),
                    context,
                    erhe::physics::ICollision_shape::create_sphere_shape_shared(1.0f)
                );
            }
        );
    }

#if 0
        execution_queue.enqueue(
            [this]()
            {
                ZoneScopedN("Torus");

                const Brush_create_context context{build_info_set(), Normal_style::polygon_normals};
                constexpr bool instantiate = true;

                constexpr float major_radius = 1.0f;
                constexpr float minor_radius = 0.25f;
                auto torus_collision_volume_calculator = [=](float scale) -> float
                {
                    return torus_volume(major_radius * scale, minor_radius * scale);
                };

                auto torus_collision_shape_generator = [major_radius, minor_radius](float scale)
                -> std::shared_ptr<erhe::physics::ICollision_shape>
                {
                    ZoneScopedN("torus_collision_shape_generator");

                    auto torus_shape = erhe::physics::ICollision_shape::create_compound_shape_shared();

                    const double     subdivisions        = 16.0;
                    const double     scaled_major_radius = major_radius * scale;
                    const double     scaled_minor_radius = minor_radius * scale;
                    const double     major_circumference = 2.0 * glm::pi<double>() * scaled_major_radius;
                    const double     capsule_length      = major_circumference / subdivisions;
                    const glm::dvec3 forward{0.0, 1.0, 0.0};
                    const glm::dvec3 side   {scaled_major_radius, 0.0, 0.0};

                    // TODO Fix new
                    auto shape = erhe::physics::ICollision_shape::create_capsule_shape_shared(
                        erhe::physics::Axis::Z,
                        static_cast<float>(scaled_minor_radius),
                        static_cast<float>(capsule_length)
                    );
                    for (int rel = 0; rel < (int)subdivisions; rel++)
                    {
                        const double     angle    = (rel * 2.0 * glm::two_pi<double>()) / subdivisions;
                        const glm::dvec3 position = glm::rotate(side, angle, forward);
                        const glm::dquat q        = glm::angleAxis(angle, forward);
                        const glm::dmat3 m        = glm::toMat3(q);

                        torus_shape->add_child_shape(shape.get(), glm::mat3{m}, glm::vec3{position});
                    }
                    return torus_shape;
                };

                make_brush(
                    instantiate,
                    make_shared<erhe::geometry::Geometry>(move(make_torus(major_radius, minor_radius, 42, 32))),
                    context,
                    torus_collision_volume_calculator,
                    torus_collision_shape_generator
                );
            }
        );
#endif

    // cylinder and cone
    if constexpr (true)
    {
        execution_queue.enqueue(
            [this]()
            {
                ZoneScopedN("Cylinder");

                const Brush_create_context context{build_info_set(), Normal_style::polygon_normals};
                constexpr bool instantiate = true;

                make_brush(
                    instantiate,
                    make_cylinder(-1.0f, 1.0f, 1.0f, true, true, 32, 2),
                    context,
                    erhe::physics::ICollision_shape::create_cylinder_shape_shared(
                        erhe::physics::Axis::X,
                        glm::vec3(1.0f, 1.0f, 1.0f)
                    )
                );
            }
        );

        execution_queue.enqueue(
            [this]()
            {
                ZoneScopedN("Cone");

                const Brush_create_context context{build_info_set(), Normal_style::polygon_normals};
                constexpr bool instantiate = true;

                make_brush(
                    instantiate,
                    make_cone(-1.0f, 1.0f, 1.0f, true, 42, 4),
                    context,
                    erhe::physics::ICollision_shape::create_cone_shape_shared(erhe::physics::Axis::X, 1.0f, 2.0f)
                );
            }
        );
    }

    // Test scene for anisotropic debugging
    if constexpr (false)
    {
        ZoneScopedN("test scene for anisotropic debugging");

        auto x_material = m_scene_root->make_material("x", vec4(1.000f, 0.000f, 0.0f, 1.0f), 0.3f, 0.0f, 0.3f);
        auto y_material = m_scene_root->make_material("y", vec4(0.228f, 1.000f, 0.0f, 1.0f), 0.3f, 0.0f, 0.3f);
        auto z_material = m_scene_root->make_material("z", vec4(0.000f, 0.228f, 1.0f, 1.0f), 0.3f, 0.0f, 0.3f);

        const float ring_major_radius = 4.0f;
        const float ring_minor_radius = 0.55f; // 0.15f;
        auto        ring_geometry     = make_torus(ring_major_radius, ring_minor_radius, 80, 32);
        ring_geometry.transform(erhe::toolkit::mat4_swap_xy);
        ring_geometry.reverse_polygons();
        //auto ring_geometry = make_shared<Geometry>(move(ring_geometry));
        auto rotate_ring_pg = make_primitive_shared(ring_geometry, build_info_set().gl);

        const vec3 pos{0.0f, 0.0f, 0.0f};
        auto x_rotate_ring_mesh = m_scene_root->make_mesh_node("X ring", rotate_ring_pg, x_material, nullptr, pos);
        auto y_rotate_ring_mesh = m_scene_root->make_mesh_node("Y ring", rotate_ring_pg, y_material, nullptr, pos);
        auto z_rotate_ring_mesh = m_scene_root->make_mesh_node("Z ring", rotate_ring_pg, z_material, nullptr, pos);

        x_rotate_ring_mesh->node()->transforms.parent_from_node.set         ( mat4(1));
        y_rotate_ring_mesh->node()->transforms.parent_from_node.set_rotation( pi<float>() / 2.0f, vec3(0.0f, 0.0f, 1.0f));
        z_rotate_ring_mesh->node()->transforms.parent_from_node.set_rotation(-pi<float>() / 2.0f, vec3(0.0f, 1.0f, 0.0f));
    }

    // Johnson solids
    if constexpr (false)
    {
        execution_queue.enqueue(
            [this]()
            {
                ZoneScopedN("Johnson solids");

                const Brush_create_context context{build_info_set(), Normal_style::polygon_normals};
                constexpr bool instantiate = true;

                const Json_library library("res/polyhedra/johnson.json");
                for (const auto& key_name : library.names)
                {
                    auto geometry = library.make_geometry(key_name);
                    if (geometry.polygon_count() == 0)
                    {
                        continue;
                    }
                    geometry.compute_polygon_normals();

                    make_brush(instantiate, move(geometry), context);
                }
            }
        );
    }

    execution_queue.wait();

    buffer_transfer_queue().flush();
}

auto Scene_manager::buffer_transfer_queue()
-> erhe::graphics::Buffer_transfer_queue&
{
    Expects(m_mesh_memory->gl_buffer_transfer_queue);

    return *m_mesh_memory->gl_buffer_transfer_queue.get();
}

void Scene_manager::add_floor()
{
    ZoneScoped;

    auto floor_material = m_scene_root->make_material(
        "Floor",
        vec4(0.4f, 0.4f, 0.4f, 1.0f),
        0.5f,
        0.8f
    );

    auto instance = m_floor_brush->make_instance(
        m_scene_root->content_layer(),
        m_scene_root->scene(),
        m_scene_root->physics_world(),
        {},
        erhe::toolkit::create_translation(0, -0.5001f, 0.0f),
        floor_material,
        1.0f
    );
    instance.mesh->visibility_mask |= 
        (INode_attachment::c_visibility_content     |
         INode_attachment::c_visibility_shadow_cast | // Optional for flat floor
         INode_attachment::c_visibility_id);
    attach(
        m_scene_root->content_layer(),
        m_scene_root->scene(),
        m_scene_root->physics_world(),
        instance.node,
        instance.mesh,
        instance.node_physics
    );
}

void Scene_manager::make_mesh_nodes()
{
    ZoneScoped;

    class Pack_entry
    {
    public:
        Pack_entry() = default;
        explicit Pack_entry(Brush* brush)
            : brush    {brush}
            , rectangle{0, 0, 0, 0}
        {
        }

        Brush*    brush{nullptr};
        rbp::Rect rectangle;
    };

    std::lock_guard<std::mutex> lock(m_scene_brushes_mutex);

    vector<Pack_entry> pack_entries;
    for (auto brush : m_scene_brushes)
    {
        pack_entries.emplace_back(brush.get());
    }

    rbp::SkylineBinPack packer;
    int group_width = 2;
    int group_depth = 2;
    for (;;)
    {
        packer.Init(group_width, group_depth, false);

        bool pack_failed = false;
        for (auto& entry : pack_entries)
        {
            const auto* brush = entry.brush;
            VERIFY(brush->primitive_geometry);
            const vec3 size = brush->primitive_geometry->bounding_box_max - brush->primitive_geometry->bounding_box_min;
            const int width = static_cast<int>(size.x + 0.5f);
            const int depth = static_cast<int>(size.z + 0.5f);
            entry.rectangle = packer.Insert(width + 1, depth + 1, rbp::SkylineBinPack::LevelBottomLeft);
            if ((entry.rectangle.width  == 0) ||
                (entry.rectangle.height == 0))
            {
                pack_failed = true;
                break;
            }
        }

        if (!pack_failed)
        {
            break;
        }

        if (group_width <= group_depth)
        {
            group_width *= 2;
        }
        else
        {
            group_depth *= 2;
        }

        VERIFY(group_width <= 16384);
    }

    size_t material_index = 0;
    for (auto& entry : pack_entries)
    {
        auto*      brush              = entry.brush;
        const auto primitive_geometry = brush->primitive_geometry;
        float      x = static_cast<float>(entry.rectangle.x) + 0.5f * static_cast<float>(entry.rectangle.width);
        float      z = static_cast<float>(entry.rectangle.y) + 0.5f * static_cast<float>(entry.rectangle.height);
        float      y = -primitive_geometry->bounding_box_min.y + 1.0f;
        x -= 0.5f * static_cast<float>(group_width);
        z -= 0.5f * static_cast<float>(group_depth);
        auto material = m_scene_root->materials().at(material_index);
        auto instance = brush->make_instance(
            m_scene_root->content_layer(),
            m_scene_root->scene(),
            m_scene_root->physics_world(),
            {},
            erhe::toolkit::create_translation(x, y, z),
            material,
            1.0f
        );
        instance.mesh->visibility_mask |= 
            (INode_attachment::c_visibility_content     |
             INode_attachment::c_visibility_shadow_cast |
             INode_attachment::c_visibility_id);

        attach(
            m_scene_root->content_layer(),
            m_scene_root->scene(),
            m_scene_root->physics_world(),
            instance.node,
            instance.mesh,
            instance.node_physics
        );
        material_index = (material_index + 1) % m_scene_root->materials().size();
    }
}

auto Scene_manager::make_directional_light(
    string_view name,
    vec3        position,
    vec3        color,
    float       intensity)
-> shared_ptr<Light>
{
    auto light = make_shared<Light>(name);
    light->type                          = Light::Type::directional;
    light->color                         = color;
    light->intensity                     = intensity;
    light->range                         =  60.0f;
    light->projection()->projection_type = Projection::Type::orthogonal;
    light->projection()->ortho_left      = -25.0f;
    light->projection()->ortho_width     =  50.0f;
    light->projection()->ortho_bottom    = -25.0f;
    light->projection()->ortho_height    =  50.0f;
    light->projection()->z_near          =  20.0f;
    light->projection()->z_far           =  60.0f;

    auto node = make_shared<Node>(name);
    mat4 m = erhe::toolkit::create_look_at(
        position,                 // eye
        vec3(0.0f,  0.0f, 0.0f),  // center
        vec3(0.0f,  0.0f, 1.0f)   // up
    );
    node->transforms.parent_from_node.set(m);

    attach(
        m_scene_root->content_layer(),
        m_scene_root->scene(),
        node,
        light
    );

    return light;
}

auto Scene_manager::make_spot_light(
    string_view name,
    vec3        position,
    vec3        target,
    vec3        color,
    float       intensity,
    vec2        spot_cone_angle)
-> shared_ptr<Light>
{
    auto light = make_shared<Light>(name);
    light->type                          = Light::Type::spot;
    light->color                         = color;
    light->intensity                     = intensity;
    light->range                         = 25.0f;
    light->inner_spot_angle              = spot_cone_angle[0];
    light->outer_spot_angle              = spot_cone_angle[1];
    light->projection()->projection_type = Projection::Type::perspective;//orthogonal;
    light->projection()->fov_x           = light->outer_spot_angle;
    light->projection()->fov_y           = light->outer_spot_angle;
    light->projection()->z_near          =   1.0f;
    light->projection()->z_far           = 100.0f;

    auto node = make_shared<Node>(name);
    const mat4 m = erhe::toolkit::create_look_at(position, target, vec3(0.0f, 0.0f, 1.0f));
    node->transforms.parent_from_node.set(m);

    attach(
        m_scene_root->content_layer(),
        m_scene_root->scene(),
        node,
        light
    );

    return light;
}

void Scene_manager::make_punctual_light_nodes()
{
    constexpr size_t directional_light_count = 1;
    for (size_t i = 0; i < directional_light_count; ++i)
    {
        const float rel = i / static_cast<float>(directional_light_count);
        const float h   = rel * 90.0f;
        const float s   = (directional_light_count == 1) ? 0.0f : 1.0f;
        const float v   = 1.0f;
        float r, g, b;
        erhe::toolkit::hsv_to_rgb(h, s, v, r, g, b);

        const float x = 30.0f * cos(rel * 2.0f * pi<float>());
        const float z = 30.0f * sin(rel * 2.0f * pi<float>());

        const vec3   color     = vec3(r, g, b);
        const float  intensity = (8.0f / static_cast<float>(directional_light_count));
        const string name      = fmt::format("Directional light {}", i);
        const vec3   position  = vec3(   x, 30.0f, z);
        make_directional_light(
            name,
            position,
            color,
            intensity
        );
    }

    int spot_light_count = 0;
    for (int i = 0; i < spot_light_count; ++i)
    {
        const float rel   = static_cast<float>(i) / static_cast<float>(spot_light_count);
        const float t     = std::pow(rel, 0.5f);
        const float theta = t * 6.0f;
        const float R     = 0.5f + 20.0f * t;
        const float h     = fract(theta) * 360.0f;
        const float s     = 0.9f;
        const float v     = 1.0f;
        float r, g, b;

        erhe::toolkit::hsv_to_rgb(h, s, v, r, g, b);

        const vec3   color     = vec3(r, g, b);
        const float  intensity = 100.0f;
        const string name      = fmt::format("Spot {}", i);

        const float x_pos = R * sin(t * 6.0f * 2.0f * pi<float>());
        const float z_pos = R * cos(t * 6.0f * 2.0f * pi<float>());

        const vec3 position        = vec3(x_pos, 10.0f, z_pos);
        const vec3 target          = vec3(x_pos * 0.5, 0.0f, z_pos * 0.5f);
        const vec2 spot_cone_angle = vec2(pi<float>() / 5.0f,
                                          pi<float>() / 4.0f);
        make_spot_light(name, position, target, color, intensity, spot_cone_angle);
    }
}

void Scene_manager::update_fixed_step(const erhe::components::Time_context& time_context)
{
    // TODO
    // Physics should mostly run in a separate thread.
    m_scene_root->physics_world().update_fixed_step(time_context.dt);
}

void Scene_manager::update_once_per_frame(const erhe::components::Time_context& time_context)
{
    ZoneScoped;

    buffer_transfer_queue().flush();
    animate_lights(time_context.time);
}

void Scene_manager::animate_lights(double time_d)
{
    const float time        = static_cast<float>(time_d);
    const auto& lights      = m_scene_root->content_layer().lights;
    const int   n_lights    = static_cast<int>(lights.size());
    int         light_index = 0;

    for (auto i = lights.begin(); i != lights.end(); ++i)
    {
        auto l = *i;
        if (l->type == Light::Type::directional)
        {
            continue;
        }

        const float rel = static_cast<float>(light_index) / static_cast<float>(n_lights);
        const float t   = 0.5f * time + rel * pi<float>() * 7.0f;
        const float R   = 4.0f;
        const float r   = 8.0f;

        const auto eye = vec3(
            R * std::sin(rel + t * 0.52f),
            8.0f,
            R * std::cos(rel + t * 0.71f)
        );

        const auto center = vec3(
            r * std::sin(rel + t * 0.35f),
            0.0f,
            r * std::cos(rel + t * 0.93f)
        );

        const auto m = erhe::toolkit::create_look_at(
            eye,
            center,
            vec3(0.0f, 0.0f, 1.0f) // up
        );

        l->node()->transforms.parent_from_node.set(m);
        l->node()->update();

        light_index++;
    }
}

void Scene_manager::add_scene()
{
    ZoneScoped;

    m_scene_root->content_layer().ambient_light = vec4(0.1f, 0.15f, 0.2f, 0.0f);

    make_brushes();
    make_mesh_nodes();
    make_punctual_light_nodes();
    add_floor();
}

namespace
{

int sort_value(Light::Type light_type)
{
    switch (light_type)
    {
        case Light::Type::directional: return 0;
        case Light::Type::point:       return 1;
        case Light::Type::spot:        return 2;
        default: return 3;
    }
}

class Light_comparator
{
public:
    inline auto operator()(const shared_ptr<Light>& lhs,
                           const shared_ptr<Light>& rhs) -> bool
    {
        return sort_value(lhs->type) < sort_value(rhs->type);
    }
};

}

void Scene_manager::sort_lights()
{
    sort(
        m_scene_root->content_layer().lights.begin(),
        m_scene_root->content_layer().lights.end(),
        Light_comparator()
    );
    sort(
        m_scene_root->tool_layer()->lights.begin(),
        m_scene_root->tool_layer()->lights.end(),
        Light_comparator()
    );
}

} // namespace editor
