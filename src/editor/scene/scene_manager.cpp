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
#include "tools/fly_camera_tool.hpp"
#include "windows/brushes.hpp"
#include "windows/materials.hpp"
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
#include "erhe/toolkit/profile.hpp"

#include <mango/core/thread.hpp>

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

    require<Materials>();
    m_brushes     = require<Brushes>();
    m_mesh_memory = require<Mesh_memory>();
    m_scene_root  = require<Scene_root>();
}

void Scene_manager::initialize_component()
{
    ERHE_PROFILE_FUNCTION

    Scoped_gl_context gl_context{Component::get<Gl_context_provider>()};

    m_scene_root = Component::get<Scene_root>();

    setup_scene();
}

//void Scene_manager::set_view_camera(std::shared_ptr<erhe::scene::ICamera> camera)
//{
//    m_view_camera = camera;
//}

//auto Scene_manager::get_view_camera() const -> std::shared_ptr<erhe::scene::ICamera>
//{
//    return m_view_camera;
//}

auto Scene_manager::make_camera(
    std::string_view name,
    glm::vec3        position,
    glm::vec3        look_at
) -> std::shared_ptr<erhe::scene::Camera>
{
    auto camera = make_shared<erhe::scene::Camera>(name);
    camera->projection()->fov_y           = erhe::toolkit::degrees_to_radians(35.0f);
    camera->projection()->projection_type = erhe::scene::Projection::Type::perspective_vertical;
    camera->projection()->z_near          = 0.03f;
    camera->projection()->z_far           = 200.0f;
    m_scene_root->scene().cameras.push_back(camera);

    auto& scene = m_scene_root->scene();
    scene.nodes.emplace_back(camera);
    scene.nodes_sorted = false;

    const mat4 m = erhe::toolkit::create_look_at(
        position, // eye
        look_at,  // center
        vec3{0.0f, 1.0f,  0.0f}  // up
    );
    camera->set_parent_from_node(m);

    return camera;
}

void Scene_manager::setup_cameras()
{
    auto camera_a = make_camera("Camera A", glm::vec3{ 1.0f, 4.00f, 12.0f});
    auto camera_b = make_camera("Camera B", glm::vec3{-1.0f, 1.65f,  4.0f});

    get<Fly_camera_tool>()->set_camera(camera_a.get());
}

auto Scene_manager::build_info_set() -> erhe::primitive::Build_info_set&
{
    return m_mesh_memory->build_info_set;
};

void Scene_manager::make_brushes()
{
    ERHE_PROFILE_FUNCTION

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
            ERHE_PROFILE_SCOPE("Floor brush");

            Brush_create_context context{build_info_set()}; //, Normal_style::polygon_normals};
            context.normal_style = Normal_style::polygon_normals;

            auto floor_geometry = std::make_shared<erhe::geometry::Geometry>(
                make_box(
                    vec3{40.0f, 1.0f, 40.0f},
                    //ivec3{40, 1, 40}
                    ivec3{1, 1, 1}
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

    constexpr bool obj_files               = true;
    constexpr bool platonic_solids         = true;
    constexpr bool sphere                  = true;
    constexpr bool torus                   = true;
    constexpr bool cylinder                = true;
    constexpr bool cone                    = true;
    constexpr bool anisotropic_test_object = false;
    constexpr bool johnson_solids          = false;

    if constexpr (obj_files)
    {
        execution_queue.enqueue(
            [this]()
            {
                ERHE_PROFILE_SCOPE("parse .obj files");

                const Brush_create_context context{build_info_set(), Normal_style::polygon_normals};
                constexpr bool instantiate = true;

                const char* obj_files_names[] = {
                    "res/models/cobra_mk3.obj"
                    //"res/models/teapot.obj",
                    //"res/models/teacup.obj",
                    //"res/models/spoon.obj"
                };
                for (auto* path : obj_files_names)
                {
                    auto geometries = parse_obj_geometry(path);

                    for (auto& geometry : geometries)
                    {
                        geometry->compute_polygon_normals();
                        // The real teapot is ~33% taller (ratio 4:3)
                        //const mat4 scale_t = erhe::toolkit::create_scale(0.5f, 0.5f * 4.0f / 3.0f, 0.5f);
                        //geometry->transform(scale_t);

                        const mat4 scale_t = erhe::toolkit::create_scale(0.01f);
                        geometry->transform(scale_t);
                        geometry->flip_reversed_polygons();
                        make_brush(instantiate, move(geometry), context);
                    }
                }
            }
        );
    }

    if constexpr (platonic_solids)
    {
        execution_queue.enqueue(
            [this]()
            {
                ERHE_PROFILE_SCOPE("Platonic solids");

                const Brush_create_context context{build_info_set(), Normal_style::polygon_normals};
                constexpr bool instantiate = true;

                constexpr float original_scale = 1.0f;
                make_brush(instantiate, make_dodecahedron (original_scale), context);
                make_brush(instantiate, make_icosahedron  (original_scale), context);
                make_brush(instantiate, make_octahedron   (original_scale), context);
                make_brush(instantiate, make_tetrahedron  (original_scale), context);
                make_brush(instantiate, make_cuboctahedron(original_scale), context);
                make_brush(
                    instantiate,
                    make_cube(original_scale),
                    context,
                    erhe::physics::ICollision_shape::create_box_shape_shared(glm::vec3{original_scale * 0.5f})
                );
                //make_brush(
                //    instantiate,
                //    make_box(
                //        vec3{1.0f, 1.0f, 1.0f},
                //        ivec3{3, 3, 3}
                //    ),
                //    context,
                //    erhe::physics::ICollision_shape::create_box_shape_shared(glm::vec3{original_scale * 0.5f})
                //);
            }
        );
    }

    if constexpr (sphere)
    {
        execution_queue.enqueue(
            [this]()
            {
                ERHE_PROFILE_SCOPE("Sphere");

                //const Brush_create_context context{build_info_set(), Normal_style::polygon_normals};
                const Brush_create_context context{build_info_set(), Normal_style::corner_normals};
                constexpr bool instantiate = true;

                make_brush(
                    instantiate,
                    make_sphere(1.0f, 24 * 4, 6 * 4),
                    context,
                    erhe::physics::ICollision_shape::create_sphere_shape_shared(1.0f)
                );
            }
        );
    }

    if constexpr (torus)
    {
        execution_queue.enqueue(
            [this]()
            {
                ERHE_PROFILE_SCOPE("Torus");

                //const Brush_create_context context{build_info_set(), Normal_style::polygon_normals};
                const Brush_create_context context{build_info_set(), Normal_style::corner_normals};
                constexpr bool instantiate = true;

                constexpr float major_radius = 1.0f;
                constexpr float minor_radius = 0.25f;
                auto torus_collision_volume_calculator = [=](float scale) -> float
                {
                    return torus_volume(major_radius * scale, minor_radius * scale);
                };

                auto torus_collision_shape_generator = [](float scale)
                -> std::shared_ptr<erhe::physics::ICollision_shape>
                {
                    ERHE_PROFILE_SCOPE("torus_collision_shape_generator");

                    auto torus_shape = erhe::physics::ICollision_shape::create_compound_shape_shared();

                    const double     subdivisions        = 16.0;
                    const double     scaled_major_radius = major_radius * scale;
                    const double     scaled_minor_radius = minor_radius * scale;
                    const double     major_circumference = glm::two_pi<double>() * scaled_major_radius;
                    const double     capsule_length      = major_circumference / subdivisions;
                    const glm::dvec3 forward{0.0, 1.0, 0.0};
                    const glm::dvec3 side   {scaled_major_radius, 0.0, 0.0};

                    auto capsule = erhe::physics::ICollision_shape::create_capsule_shape_shared(
                        erhe::physics::Axis::Z,
                        static_cast<float>(scaled_minor_radius),
                        static_cast<float>(capsule_length)
                    );
                    for (int i = 0; i < static_cast<int>(subdivisions); i++)
                    {
                        const double     rel      = static_cast<double>(i) / subdivisions;
                        const double     theta    = rel * glm::two_pi<double>();
                        const glm::dvec3 position = glm::rotate(side, theta, forward);
                        const glm::dquat q        = glm::angleAxis(theta, forward);
                        const glm::dmat3 m        = glm::toMat3(q);

                        torus_shape->add_child_shape(
                            capsule,
                            erhe::physics::Transform{
                                glm::mat3{m},
                                glm::vec3{position}
                            }
                        );
                    }
                    return torus_shape;
                };

                make_brush(
                    instantiate,
                    make_shared<erhe::geometry::Geometry>(
                        make_torus(major_radius, minor_radius, 42, 32)
                    ),
                    context,
                    torus_collision_volume_calculator,
                    torus_collision_shape_generator
                );
            }
        );
    }

    if constexpr (cylinder)
    {
        execution_queue.enqueue(
            [this]()
            {
                ERHE_PROFILE_SCOPE("Cylinder");

                //const Brush_create_context context{build_info_set(), Normal_style::polygon_normals};
                const Brush_create_context context{build_info_set(), Normal_style::corner_normals};
                constexpr bool instantiate = true;
                //auto cylinder_geometry = make_cylinder(-1.0f, 1.0f, 1.0f, true, true, 32, 2); // always axis = x
                auto cylinder_geometry = make_cylinder(-1.0f, 1.0f, 1.0f, true, true, 8, 1); // always axis = x
                cylinder_geometry.transform(erhe::toolkit::mat4_swap_xy);                    // convert to axis = y

                make_brush(
                    instantiate,
                    std::move(cylinder_geometry),
                    context,
                    erhe::physics::ICollision_shape::create_cylinder_shape_shared(
                        erhe::physics::Axis::Y,
                        glm::vec3{1.0f, 1.0f, 1.0f}
                    )
                );
            }
        );
    }

    if constexpr (cone)
    {
        execution_queue.enqueue(
            [this]()
            {
                ERHE_PROFILE_SCOPE("Cone");

                //const Brush_create_context context{build_info_set(), Normal_style::polygon_normals};
                const Brush_create_context context{build_info_set(), Normal_style::corner_normals};
                constexpr bool instantiate = true;
                auto cone_geometry = make_cone(-1.0f, 1.0f, 1.0f, true, 42, 4); // always axis = x
                cone_geometry.transform(erhe::toolkit::mat4_swap_xy);           // convert to axis = y

                make_brush(
                    instantiate,
                    std::move(cone_geometry),
                    context,
                    erhe::physics::ICollision_shape::create_cone_shape_shared(
                        erhe::physics::Axis::Y, 1.0f, 2.0f
                    )
                );
            }
        );
    }

    if constexpr (anisotropic_test_object)
    {
        ERHE_PROFILE_SCOPE("test scene for anisotropic debugging");

        auto x_material = m_scene_root->make_material("x", vec4{1.000f, 0.000f, 0.0f, 1.0f}, 0.3f, 0.0f, 0.3f);
        auto y_material = m_scene_root->make_material("y", vec4{0.228f, 1.000f, 0.0f, 1.0f}, 0.3f, 0.0f, 0.3f);
        auto z_material = m_scene_root->make_material("z", vec4{0.000f, 0.228f, 1.0f, 1.0f}, 0.3f, 0.0f, 0.3f);

        const float ring_major_radius = 4.0f;
        const float ring_minor_radius = 0.55f; // 0.15f;
        auto        ring_geometry     = make_torus(ring_major_radius, ring_minor_radius, 80, 32);
        ring_geometry.transform(erhe::toolkit::mat4_swap_xy);
        auto rotate_ring_pg = make_primitive_shared(ring_geometry, build_info_set().gl);

        const vec3 pos{20.0f, 0.0f, 0.0f};
        auto x_rotate_ring_mesh = m_scene_root->make_mesh_node("X ring", rotate_ring_pg, x_material, nullptr, pos);
        auto y_rotate_ring_mesh = m_scene_root->make_mesh_node("Y ring", rotate_ring_pg, y_material, x_rotate_ring_mesh.get());
        auto z_rotate_ring_mesh = m_scene_root->make_mesh_node("Z ring", rotate_ring_pg, z_material, x_rotate_ring_mesh.get());

        // x_rotate_ring_mesh identity
        y_rotate_ring_mesh->set_parent_from_node(Transform::create_rotation( pi<float>() / 2.0f, vec3{0.0f, 0.0f, 1.0f}));
        z_rotate_ring_mesh->set_parent_from_node(Transform::create_rotation(-pi<float>() / 2.0f, vec3{0.0f, 1.0f, 0.0f}));

        x_rotate_ring_mesh->visibility_mask() |= 
            (Node::c_visibility_content     |
             Node::c_visibility_shadow_cast |
             Node::c_visibility_id);
        y_rotate_ring_mesh->visibility_mask() |= 
            (Node::c_visibility_content     |
             Node::c_visibility_shadow_cast |
             Node::c_visibility_id);
        z_rotate_ring_mesh->visibility_mask() |= 
            (Node::c_visibility_content     |
             Node::c_visibility_shadow_cast |
             Node::c_visibility_id);

    }

    if constexpr (johnson_solids)
    {
        execution_queue.enqueue(
            [this]()
            {
                ERHE_PROFILE_SCOPE("Johnson solids");

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

auto Scene_manager::buffer_transfer_queue() -> erhe::graphics::Buffer_transfer_queue&
{
    Expects(m_mesh_memory->gl_buffer_transfer_queue);

    return *m_mesh_memory->gl_buffer_transfer_queue.get();
}

void Scene_manager::add_floor()
{
    ERHE_PROFILE_FUNCTION

    auto floor_material = m_scene_root->make_material(
        "Floor",
        vec4{0.4f, 0.4f, 0.4f, 1.0f},
        0.5f,
        0.8f
    );

    auto instance = m_floor_brush->make_instance(
        erhe::toolkit::create_translation(0, -0.5001f, 0.0f), // local to parent
        floor_material, // material
        1.0f            // scale
    );
    instance.mesh->visibility_mask() |= 
        (Node::c_visibility_content     |
         Node::c_visibility_shadow_cast | // Optional for flat floor
         Node::c_visibility_id);

    add_to_scene_layer(
        m_scene_root->scene(),
        m_scene_root->content_layer(),
        instance.mesh
    );
    if (instance.node_physics)
    {
        add_to_physics_world(
            m_scene_root->physics_world(),
            instance.node_physics
        );
    }
}

void Scene_manager::make_mesh_nodes()
{
    ERHE_PROFILE_FUNCTION

    m_scene_root->scene().sanity_check();

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

    std::sort(
        m_scene_brushes.begin(),
        m_scene_brushes.end(),
        [](const std::shared_ptr<Brush>& lhs, const std::shared_ptr<Brush>& rhs)
        {
            return lhs->name() < rhs->name();
        }
    );

    vector<Pack_entry> pack_entries;
    for (auto brush : m_scene_brushes)
    {
        pack_entries.emplace_back(brush.get());
    }

    rbp::SkylineBinPack packer;
    int group_width = 2;
    int group_depth = 2;

    const float gap = 1.0f;

    glm::ivec2 max_corner;
    for (;;)
    {
        max_corner = glm::ivec2{0, 0};
        packer.Init(group_width, group_depth, false);

        bool pack_failed = false;
        for (auto& entry : pack_entries)
        {
            const auto* brush = entry.brush;
            VERIFY(brush->primitive_geometry);
            const vec3 size = brush->primitive_geometry->bounding_box_max - brush->primitive_geometry->bounding_box_min;
            const int width = static_cast<int>(256.0f * (size.x + gap));
            const int depth = static_cast<int>(256.0f * (size.z + gap));
            entry.rectangle = packer.Insert(width + 1, depth + 1, rbp::SkylineBinPack::LevelBottomLeft);
            if (
                (entry.rectangle.width  == 0) ||
                (entry.rectangle.height == 0)
            )
            {
                pack_failed = true;
                break;
            }
            max_corner.x = std::max(max_corner.x, entry.rectangle.x + entry.rectangle.width);
            max_corner.y = std::max(max_corner.y, entry.rectangle.y + entry.rectangle.height);
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
        float      x  = static_cast<float>(entry.rectangle.x) / 256.0f;
        float      z  = static_cast<float>(entry.rectangle.y) / 256.0f;
                   x += 0.5f * static_cast<float>(entry.rectangle.width ) / 256.0f;
                   z += 0.5f * static_cast<float>(entry.rectangle.height) / 256.0f;
                   x -= 0.5f * static_cast<float>(max_corner.x) / 256.0f;
                   z -= 0.5f * static_cast<float>(max_corner.y) / 256.0f;
        float      y  = -primitive_geometry->bounding_box_min.y;
        //x -= 0.5f * static_cast<float>(group_width);
        //z -= 0.5f * static_cast<float>(group_depth);
        auto material = m_scene_root->materials().at(material_index);
        auto instance = brush->make_instance(
            erhe::toolkit::create_translation(x, y, z),
            material,
            1.0f
        );
        instance.mesh->visibility_mask() |= 
            (Node::c_visibility_content     |
             Node::c_visibility_shadow_cast |
             Node::c_visibility_id);

        add_to_scene_layer(
            m_scene_root->scene(),
            m_scene_root->content_layer(),
            instance.mesh
        );

        if (instance.node_physics)
        {
            add_to_physics_world(
                m_scene_root->physics_world(),
                instance.node_physics
            );
        }

        material_index = (material_index + 1) % m_scene_root->materials().size();

        m_scene_root->scene().sanity_check();
    }
}

void Scene_manager::make_cube_benchmark()
{
    ERHE_PROFILE_FUNCTION

    m_scene_root->scene().sanity_check();

    auto material = m_scene_root->make_material("cube", vec4{1.0, 1.000f, 1.0f, 1.0f}, 0.3f, 0.0f, 0.3f);
    auto cube     = make_cube(0.1f);
    auto cube_pg  = make_primitive_shared(cube, build_info_set().gl, Normal_style::polygon_normals);
    //auto cube     = make_sphere(0.1f, 24 * 1, 6 * 1);
    //auto cube_pg  = make_primitive_shared(cube, build_info_set().gl, Normal_style::point_normals);

    constexpr float scale = 0.5f;
    constexpr int   x_count = 20;
    constexpr int   y_count = 20;
    constexpr int   z_count = 20;
    for (int i = 0; i < x_count; ++i)
    {
        const float x_rel = static_cast<float>(i) - static_cast<float>(x_count) * 0.5f;
        for (int j = 0; j < y_count; ++j)
        {
            const float y_rel = static_cast<float>(j);
            for (int k = 0; k < z_count; ++k)
            {
                const float z_rel = static_cast<float>(k) - static_cast<float>(z_count) * 0.5f;
                const vec3 pos{scale * x_rel, 1.0f + scale * y_rel, scale * z_rel};
                auto mesh = m_scene_root->make_mesh_node("", cube_pg, material, nullptr, pos);
                mesh->visibility_mask() |= 
                    (Node::c_visibility_content     |
                     Node::c_visibility_shadow_cast |
                     Node::c_visibility_id);
            }
        }
    }

    m_scene_root->scene().sanity_check();
}

auto Scene_manager::make_directional_light(
    string_view name,
    vec3        position,
    vec3        color,
    float       intensity
) -> shared_ptr<Light>
{
    auto light = make_shared<Light>(name);
    light->type                          = Light::Type::directional;
    light->color                         = color;
    light->intensity                     = intensity;
    light->range                         =  80.0f;
    light->projection()->projection_type = Projection::Type::orthogonal;
    light->projection()->ortho_left      = -40.0f;
    light->projection()->ortho_width     =  80.0f;
    light->projection()->ortho_bottom    = -40.0f;
    light->projection()->ortho_height    =  80.0f;
    light->projection()->z_near          =  2.0f;
    light->projection()->z_far           =  60.0f;

    mat4 m = erhe::toolkit::create_look_at(
        position,                 // eye
        vec3{0.0f,  0.0f, 0.0f},  // center
        vec3{0.0f,  1.0f, 0.0f}   // up
    );
    light->set_parent_from_node(m);

    add_to_scene_layer(
        m_scene_root->scene(),
        *m_scene_root->light_layer().get(),
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
    vec2        spot_cone_angle
) -> shared_ptr<Light>
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

    const mat4 m = erhe::toolkit::create_look_at(position, target, vec3{0.0f, 0.0f, 1.0f});
    light->set_parent_from_node(m);

    add_to_scene_layer(
        m_scene_root->scene(),
        *m_scene_root->light_layer().get(),
        light
    );

    return light;
}

void Scene_manager::setup_lights()
{
    m_scene_root->light_layer()->ambient_light = vec4{0.033f, 0.055f, 0.077f, 0.0f};

    //make_directional_light(
    //    "Key",
    //    glm::vec3{10.0f, 10.0f, 10.0f},
    //    glm::vec3{1.0f, 0.9f, 0.8f},
    //    2.0f
    //);
    //make_directional_light(
    //    "Fill",
    //    glm::vec3{-10.0f, 5.0f, -10.0f},
    //    glm::vec3{0.8f, 0.9f, 1.0f},
    //    1.0f
    //);

    int directional_light_count = 2;
    for (int i = 0; i < directional_light_count; ++i)
    {
        const float rel = static_cast<float>(i) / static_cast<float>(directional_light_count);
        const float R   = 1.0f;
        const float h   = rel * 360.0f;
        const float s   = 0.9f;
        const float v   = 1.0f;
        float r, g, b;

        erhe::toolkit::hsv_to_rgb(h, s, v, r, g, b);

        const vec3   color     = vec3{r, g, b};
        const float  intensity = 5.0f / static_cast<float>(directional_light_count);
        const string name      = fmt::format("Directional light {}", i);
        const float  x_pos     = R * sin(rel * two_pi<float>());
        const float  z_pos     = R * cos(rel * two_pi<float>());
        const vec3   position  = vec3{x_pos, 10.0f, z_pos};
        make_directional_light(name, position, color, intensity);
    }   

    int spot_light_count = 0;
    for (int i = 0; i < spot_light_count; ++i)
    {
        const float rel   = static_cast<float>(i) / static_cast<float>(spot_light_count);
        const float theta = rel * two_pi<float>();
        const float R     = 0.5f + 20.0f * rel;
        const float h     = fract(theta) * 360.0f;
        const float s     = 0.9f;
        const float v     = 1.0f;
        float r, g, b;

        erhe::toolkit::hsv_to_rgb(h, s, v, r, g, b);

        const vec3   color           = vec3{r, g, b};
        const float  intensity       = 100.0f;
        const string name            = fmt::format("Spot {}", i);
        const float  x_pos           = R * sin(rel * 6.0f * two_pi<float>());
        const float  z_pos           = R * cos(rel * 6.0f * two_pi<float>());
        const vec3   position        = vec3{x_pos, 10.0f, z_pos};
        const vec3   target          = vec3{x_pos * 0.5, 0.0f, z_pos * 0.5f};
        const vec2   spot_cone_angle = vec2{
            pi<float>() / 5.0f,
            pi<float>() / 4.0f
        };
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
    ERHE_PROFILE_FUNCTION

    buffer_transfer_queue().flush();

    static_cast<void>(time_context);
    animate_lights(time_context.time);
}

void Scene_manager::animate_lights(double time_d)
{
    const float time        = static_cast<float>(time_d);
    const auto& light_layer = *m_scene_root->light_layer().get();
    const auto& lights      = light_layer.lights;
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

        const auto eye = vec3{
            R * std::sin(rel + t * 0.52f),
            8.0f,
            R * std::cos(rel + t * 0.71f)
        };

        const auto center = vec3{
            r * std::sin(rel + t * 0.35f),
            0.0f,
            r * std::cos(rel + t * 0.93f)
        };

        const auto m = erhe::toolkit::create_look_at(
            eye,
            center,
            vec3{0.0f, 1.0f, 0.0f} // up
        );

        l->set_parent_from_node(m);

        light_index++;
    }
}

void Scene_manager::setup_scene()
{
    ERHE_PROFILE_FUNCTION

    setup_cameras();
    setup_lights();
    make_brushes();
    make_mesh_nodes();
    add_floor();
    //make_cube_benchmark();
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
    inline auto operator()(
        const shared_ptr<Light>& lhs,
        const shared_ptr<Light>& rhs
    ) -> bool
    {
        return sort_value(lhs->type) < sort_value(rhs->type);
    }
};

}

void Scene_manager::sort_lights()
{
    sort(
        m_scene_root->light_layer()->lights.begin(),
        m_scene_root->light_layer()->lights.end(),
        Light_comparator()
    );
}

} // namespace editor
