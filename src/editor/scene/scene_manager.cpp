#include "scene/scene_manager.hpp"
#include "parsers/json_polyhedron.hpp"
#include "parsers/wavefront_obj.hpp"
#include "renderers/programs.hpp"
#include "scene/brush.hpp"
#include "scene/debug_draw.hpp"
#include "scene/node_physics.hpp"
#include "log.hpp"

#include "SkylineBinPack.h" // RectangleBinPack

#include "erhe/geometry/operation/catmull_clark_subdivision.hpp"
#include "erhe/geometry/operation/clone.hpp"
#include "erhe/geometry/shapes/box.hpp"
#include "erhe/geometry/shapes/cone.hpp"
#include "erhe/geometry/shapes/disc.hpp"
#include "erhe/geometry/shapes/sphere.hpp"
#include "erhe/geometry/shapes/torus.hpp"
#include "erhe/geometry/shapes/regular_polyhedron.hpp"
#include "erhe/graphics/buffer.hpp"
#include "erhe/primitive/primitive.hpp"
#include "erhe/primitive/primitive_builder.hpp"
#include "erhe/primitive/material.hpp"
#include "erhe/physics/world.hpp"
#include "erhe/scene/camera.hpp"
#include "erhe/scene/light.hpp"
#include "erhe/scene/mesh.hpp"
#include "erhe/scene/node.hpp"
#include "erhe/scene/scene.hpp"
#include "erhe/toolkit/math_util.hpp"
#include "erhe/toolkit/tracy_client.hpp"

#include "glm/gtx/color_space.hpp"

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
    : Component("Scene_manager")
{
}

void Scene_manager::connect()
{
    m_programs   = require<Programs  >();
    m_debug_draw = require<Debug_draw>();
}

void Scene_manager::initialize_component()
{
    ZoneScoped;

    m_scene = std::make_unique<erhe::scene::Scene>();

    m_physics_world = std::make_unique<erhe::physics::World>();
    if (m_debug_draw)
    {
        m_physics_world->bullet_dynamics_world.setDebugDrawer(m_debug_draw.get());
    }

    static constexpr gl::Buffer_storage_mask storage_mask{gl::Buffer_storage_mask::map_write_bit};

    size_t vertex_byte_count  = 256 * 1024 * 1024;
    size_t index_byte_count   =  64 * 1024 * 1024;
    m_buffer_info.index_type    = gl::Draw_elements_type::unsigned_int;
    m_buffer_info.vertex_buffer = make_shared<Buffer>(gl::Buffer_target::array_buffer,
                                                      vertex_byte_count,
                                                      storage_mask);
    m_buffer_info.index_buffer = make_shared<Buffer>(gl::Buffer_target::element_array_buffer,
                                                     index_byte_count,
                                                     storage_mask);

    m_buffer_info.vertex_buffer->set_debug_label("Scene Manager Vertex");
    m_buffer_info.index_buffer ->set_debug_label("Scene Manager Index");

    m_format_info.want_fill_triangles       = true;
    m_format_info.want_edge_lines           = true;
    m_format_info.want_centroid_points      = true;
    m_format_info.want_corner_points        = true;
    m_format_info.want_position             = true;
    m_format_info.want_normal               = true;
    m_format_info.want_normal_smooth        = true;
    m_format_info.want_tangent              = true;
    m_format_info.want_bitangent            = true;
    m_format_info.want_texcoord             = true;
    m_format_info.want_color                = true;
    m_format_info.want_id                   = true;
    m_format_info.normal_style              = Normal_style::corner_normals;
    m_format_info.vertex_attribute_mappings = &m_programs->attribute_mappings;

    Primitive_builder::prepare_vertex_format(m_format_info, m_buffer_info);

    add_scene();
}

auto Scene_manager::make_mesh_node(const string&                  name,
                                   shared_ptr<Primitive_geometry> primitive_geometry,
                                   shared_ptr<Material>           material,
                                   shared_ptr<Layer>              target_layer,
                                   Node*                          parent,
                                   vec3                           position)
-> shared_ptr<Mesh>
{
    ZoneScoped;

    mat4 transform = erhe::toolkit::create_translation(position);

    auto* layer = target_layer.get();
    if (layer == nullptr)
    {
        layer = m_content_layer.get();
    }
    VERIFY(layer != nullptr);

    auto mesh = make_shared<Mesh>(name);
    mesh->primitives.emplace_back(primitive_geometry, material);
    //layer->meshes.push_back(mesh);

    auto node = make_shared<Node>();
    node->parent = parent;
    node->transforms.parent_from_node.set(transform);
    node->update();
    //node->attach(mesh);
    //m_scene->nodes.push_back(node);

    attach(node, mesh, {});

    return mesh;
}

auto Scene_manager::camera() -> ICamera&
{
    auto* camera = m_camera.get();
    assert(camera != nullptr);
    return *camera;
}

auto Scene_manager::camera() const -> const Camera&
{
    auto* camera = m_camera.get();
    assert(camera != nullptr);
    return *camera;
}

auto Scene_manager::materials() -> vector<shared_ptr<Material>>&
{
    return m_materials;
}

auto Scene_manager::materials() const -> const vector<shared_ptr<Material>>&
{
    return m_materials;
}

auto Scene_manager::vertex_buffer() -> Buffer*
{
    return m_buffer_info.vertex_buffer.get();
}

auto Scene_manager::index_buffer() -> Buffer*
{
    return m_buffer_info.index_buffer.get();
}

auto Scene_manager::index_type() -> gl::Draw_elements_type
{
    return m_buffer_info.index_type;
}

auto Scene_manager::vertex_format() -> shared_ptr<Vertex_format>
{
    return m_buffer_info.vertex_format;
}

auto Scene_manager::make_brush(Geometry&&                   geometry,
                               Normal_style                 normal_style,
                               shared_ptr<btCollisionShape> collision_shape)
-> Brush&
{
    ZoneScoped;

    auto shared_geometry = make_shared<Geometry>(move(geometry));
    return make_brush(shared_geometry, normal_style, collision_shape);
}

auto Scene_manager::make_brush(const shared_ptr<Geometry>&  geometry,
                               Normal_style                 normal_style,
                               shared_ptr<btCollisionShape> collision_shape)
-> Brush&
{
    ZoneScoped;

    geometry->build_edges();
    geometry->compute_polygon_normals();
    geometry->compute_tangents();
    geometry->compute_polygon_centroids();
    geometry->compute_point_normals(c_point_normals_smooth);
    Brush::Create_info create_info{geometry,
                                   m_format_info,
                                   m_buffer_info,
                                   normal_style,
                                   1.0f, // density
                                   geometry->volume(),
                                   collision_shape};

    if (m_instantiate_brush_to_scene)
    {
        m_scene_brushes.emplace_back(m_brushes.size());
    }

    return m_brushes.emplace_back(create_info);
}

auto Scene_manager::make_brush(const shared_ptr<Geometry>& geometry,
                               Normal_style                normal_style,
                               Collision_volume_calculator collision_volume_calculator,
                               Collision_shape_generator   collision_shape_generator)
-> Brush&
{
    ZoneScoped;

    geometry->build_edges();
    geometry->compute_polygon_normals();
    geometry->compute_tangents();
    geometry->compute_polygon_centroids();
    geometry->compute_point_normals(c_point_normals_smooth);
    Brush::Create_info create_info{geometry,
                                   m_format_info,
                                   m_buffer_info,
                                   normal_style,
                                   1.0f, // density
                                   collision_volume_calculator,
                                   collision_shape_generator};

    if (m_instantiate_brush_to_scene)
    {
        m_scene_brushes.emplace_back(m_brushes.size());
    }

    return m_brushes.emplace_back(create_info);
}

void Scene_manager::make_brushes()
{
    ZoneScoped;

    // TODO This is slow:
    // - Parsing obj is slow (~100 ms)
    // - Generating Johnson solids is slow (~70 ms)
    // - Generating floor is slow (see add_floor()) (~60 ms)
    // These tasks should be executed in parallel using threads.
    // Some sort of task system.
    if constexpr (false)
    {
        ZoneScopedN("test scene for brush");

        instantiate_brush_to_scene(true);
        make_brush(make_cube(2.0),
                   Normal_style::polygon_normals,
                   make_shared<btBoxShape>(btVector3(1.0f, 1.0f, 1.0f)));
        return;
    }

    if constexpr (true) // teapot
    {
        ZoneScopedN("teapot from .obj");

        instantiate_brush_to_scene(false);
        auto geometry = parse_obj_geometry("res/models/teapot.obj");
        geometry.compute_polygon_normals();
        // The real teapot is ~33% taller (ratio 4:3)
        mat4 scale_t = erhe::toolkit::create_scale(0.5f, 0.5f * 4.0f / 3.0f, 0.5f);
        geometry.transform(scale_t);
        make_brush(move(geometry), Normal_style::point_normals);
    }

    if constexpr (true)
    {
        ZoneScopedN("platonic solids");

        instantiate_brush_to_scene(false); // true
        float original_scale = 1.0f;
        make_brush(make_dodecahedron (original_scale), Normal_style::polygon_normals);
        make_brush(make_icosahedron  (original_scale), Normal_style::polygon_normals);
        make_brush(make_octahedron   (original_scale), Normal_style::polygon_normals);
        make_brush(make_tetrahedron  (original_scale), Normal_style::polygon_normals);
        make_brush(make_cuboctahedron(original_scale), Normal_style::polygon_normals);
        make_brush(make_cube         (original_scale),
                   Normal_style::polygon_normals,
                   make_shared<btBoxShape>(btVector3(original_scale * 0.5f, original_scale * 0.5f, original_scale * 0.5f)));
    }

    if constexpr (true)
    {
        ZoneScopedN("Round shapes");

        instantiate_brush_to_scene(true);

        make_brush(make_sphere(1.0f, 12 * 4, 4 * 6),
                   Normal_style::polygon_normals,
                   make_shared<btSphereShape>(1.0f));

        instantiate_brush_to_scene(true);
        float major_radius = 1.0f;
        float minor_radius = 0.25f;
        auto torus_collision_volume_calculator = [=](float scale) -> float
        {
            return torus_volume(major_radius * scale, minor_radius * scale);
        };

        auto torus_collision_shape_generator = [major_radius, minor_radius](float scale) -> std::shared_ptr<btCollisionShape>
        {
            ZoneScopedN("torus_collision_shape_generator");

            auto torus_shape = make_shared<btCompoundShape>();

            double subdivisions        = 16.0;
            double scaled_major_radius = major_radius * scale;
            double scaled_minor_radius = minor_radius * scale;
            double major_circumference = 2.0 * SIMD_PI * scaled_major_radius;
            double capsule_length      = major_circumference / subdivisions;
            btVector3 forward(btScalar(0.0), btScalar(1.0), btScalar(0.0));
            btVector3 side(btScalar(scaled_major_radius), btScalar(0.0), btScalar(0.0));

            btCapsuleShapeZ* shape = new btCapsuleShapeZ(btScalar(scaled_minor_radius), btScalar(capsule_length));
            btTransform t;
            for (int rel = 0; rel < (int)subdivisions; rel++)
            {
                btScalar     angle    = btScalar((rel * 2.0 * SIMD_PI) / subdivisions);
                btVector3    position = side.rotate(forward, angle);
                btQuaternion q(forward, angle);
                t.setIdentity();
                t.setOrigin  (position);
                t.setRotation(q);
                torus_shape->addChildShape(t, shape);
            }
            return torus_shape;
        };

        make_brush(make_shared<Geometry>(move(make_torus(major_radius, minor_radius, 42, 32))),
                   Normal_style::polygon_normals,
                   torus_collision_volume_calculator,
                   torus_collision_shape_generator);

        instantiate_brush_to_scene(false);

        make_brush(make_cylinder(-1.0f, 1.0f, 1.0f, true, true, 32, 2),
                   Normal_style::polygon_normals,
                   make_shared<btCylinderShapeX>(btVector3(1.0f, 1.0f, 1.0f)));

        make_brush(make_cone(-1.0f, 1.0f, 1.0f, true, 42, 4),
                   Normal_style::polygon_normals,
                   make_shared<btConeShapeX>(1.0f, 2.0f));
    }

    if constexpr (false)
    {
        ZoneScopedN("test scene for anisotropic debugging");

        auto x_material = make_shared<Material>("x", vec4(1.000f, 0.000f, 0.0f, 1.0f), 0.3f, 0.0f, 0.3f);
        auto y_material = make_shared<Material>("y", vec4(0.228f, 1.000f, 0.0f, 1.0f), 0.3f, 0.0f, 0.3f);
        auto z_material = make_shared<Material>("z", vec4(0.000f, 0.228f, 1.0f, 1.0f), 0.3f, 0.0f, 0.3f);

        add(x_material);
        add(y_material);
        add(z_material);

        float ring_major_radius = 4.0f;
        float ring_minor_radius = 0.55f; // 0.15f;
        auto  ring_geometry     = make_torus(ring_major_radius, ring_minor_radius, 80, 32);
        ring_geometry.transform(erhe::toolkit::mat4_swap_xy);
        ring_geometry.reverse_polygons();
        //auto ring_geometry = make_shared<Geometry>(move(ring_geometry));
        auto rotate_ring_pg = make_primitive_shared(ring_geometry, m_format_info, m_buffer_info);

        vec3 pos{0.0f, 0.0f, 0.0f};
        auto x_rotate_ring_mesh = make_mesh_node("X ring", rotate_ring_pg, x_material, {}, nullptr, pos);
        auto y_rotate_ring_mesh = make_mesh_node("Y ring", rotate_ring_pg, y_material, {}, nullptr, pos);
        auto z_rotate_ring_mesh = make_mesh_node("Z ring", rotate_ring_pg, z_material, {}, nullptr, pos);

        x_rotate_ring_mesh->node()->transforms.parent_from_node.set         ( mat4(1));
        y_rotate_ring_mesh->node()->transforms.parent_from_node.set_rotation( pi<float>() / 2.0f, vec3(0.0f, 0.0f, 1.0f));
        z_rotate_ring_mesh->node()->transforms.parent_from_node.set_rotation(-pi<float>() / 2.0f, vec3(0.0f, 1.0f, 0.0f));
        return;
    }

    if constexpr (true)
    {
        ZoneScopedN("Johnson solids");

        instantiate_brush_to_scene(false);

        Json_library library("res/polyhedra/johnson.json");
        for (const auto& key_name : library.names)
        {
            auto geometry = library.make_geometry(key_name);
            if (geometry.polygon_count() == 0)
            {
                continue;
            }
            geometry.compute_polygon_normals();

            make_brush(move(geometry),
                       Normal_style::polygon_normals);
        }
    }
}

void Scene_manager::make_materials()
{
    if constexpr (true) // White default material
    {
        auto m = make_shared<Material>(fmt::format("Default Material", m_materials.size()),
                                       vec4(1.0f, 1.0f, 1.0f, 1.0f),
                                       0.50f,
                                       0.00f,
                                       0.50f);
        add(m);
        //return;
    }

    for (size_t i = 0, end = 10; i < end; ++i)
    {
        float rel        = static_cast<float>(i) / static_cast<float>(end);
        float hue        = rel * 360.0f;
        float saturation = 0.9f;
        float value      = 1.0f;
        float R, G, B;
        erhe::toolkit::hsv_to_rgb(hue, saturation, value, R, G, B);
        auto m = make_shared<Material>(fmt::format("Hue {}", hue),
                                       vec4(R, G, B, 1.0f),
                                       1.00f,
                                       0.95f,
                                       0.70f);
        add(m);
    }
}

void Scene_manager::add_floor()
{
    ZoneScoped;

    auto floor_material = make_shared<Material>("Floor",
                                                vec4(0.4f, 0.4f, 0.4f, 1.0f),
                                                0.5f,
                                                0.8f);
    add(floor_material);

    shared_ptr<Geometry> floor_geometry = std::make_shared<Geometry>(std::move(make_box(vec3(40.0f, 1.0f, 40.0f),
                                                                                        ivec3(40, 1, 40))));
    floor_geometry->name = "floor";
    floor_geometry->build_edges();
    auto box_shape = make_shared<btBoxShape>(btVector3{20.0f, 0.5f, 20.0f});

    // Otherwise it will be destructed when leave add_floor() scope
    physics_world().collision_shapes.push_back(box_shape);

    Brush::Create_info brush_create_info{floor_geometry,
                                         m_format_info,
                                         m_buffer_info,
                                         Normal_style::polygon_normals,
                                         0.0f, // density for static object
                                         0.0f, // volume for static object
                                         box_shape};
    Brush floor_brush{brush_create_info};
    auto instance = floor_brush.make_instance(*this, {}, erhe::toolkit::create_translation(0, -0.5001f, 0.0f), floor_material, 1.0f);
    attach(instance.node, instance.mesh, instance.node_physics);
}

void Scene_manager::attach(std::shared_ptr<erhe::scene::Node> node,
                           std::shared_ptr<erhe::scene::Mesh> mesh,
                           std::shared_ptr<Node_physics>      node_physics)
{
    m_content_layer->meshes.push_back(mesh);
    m_scene->nodes.push_back(node);
    node->attach(mesh);
    if (node_physics)
    {
        node->attach(node_physics);
        m_physics_world->add_rigid_body(&node_physics->rigid_body);
    }
}

void Scene_manager::detach(std::shared_ptr<erhe::scene::Node> node,
                           std::shared_ptr<erhe::scene::Mesh> mesh,
                           std::shared_ptr<Node_physics>      node_physics)
{
    auto& meshes = content_layer()->meshes;
    auto& nodes  = scene().nodes;
    auto& world  = physics_world();

    node->detach(mesh);
    if (node_physics)
    {
        node->detach(node_physics);
        world.remove_rigid_body(&node_physics->rigid_body);
    }
    meshes.erase(std::remove(meshes.begin(), meshes.end(), mesh), meshes.end());
    nodes.erase(std::remove(nodes.begin(), nodes.end(), node), nodes.end());
}

void Scene_manager::make_mesh_nodes()
{
    ZoneScoped;

    struct Pack_entry
    {
        Pack_entry() = default;
        Pack_entry(size_t brush_index)
            : brush_index{brush_index}
            , rectangle  {0, 0, 0, 0}
        {
        }

        size_t    brush_index;
        rbp::Rect rectangle;
    };

    vector<Pack_entry> pack_entries;
    for (size_t i : m_scene_brushes)
    {
        pack_entries.emplace_back(i);
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
            Brush& brush = m_brushes[entry.brush_index];
            vec3 size = brush.primitive_geometry->bounding_box_max - brush.primitive_geometry->bounding_box_min;
            int width = static_cast<int>(size.x + 0.5f);
            int depth = static_cast<int>(size.z + 0.5f);
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
        auto& brush              = m_brushes[entry.brush_index];
        auto  primitive_geometry = brush.primitive_geometry;
        float x = static_cast<float>(entry.rectangle.x) + 0.5f * static_cast<float>(entry.rectangle.width);
        float z = static_cast<float>(entry.rectangle.y) + 0.5f * static_cast<float>(entry.rectangle.height);
        float y = -primitive_geometry->bounding_box_min.y;
        x -= 0.5f * static_cast<float>(group_width);
        z -= 0.5f * static_cast<float>(group_depth);
        shared_ptr<Material> material = m_materials.at(material_index);

        auto instance = brush.make_instance(*this,
                                            {},
                                            erhe::toolkit::create_translation(x, y, z),
                                            material,
                                            1.0f);
        attach(instance.node,
               instance.mesh,
               instance.node_physics);
        material_index = (material_index + 1) % m_materials.size();
    }
}

auto Scene_manager::make_directional_light(const string& name,
                                           vec3          position,
                                           vec3          color,
                                           float         intensity,
                                           Layer*        layer)
-> shared_ptr<Light>
{
    auto l = make_shared<Light>(name);
    l->type                          = Light::Type::directional;
    l->color                         = color;
    l->intensity                     = intensity;
    l->range                         =  60.0f;
    l->projection()->projection_type = Projection::Type::orthogonal;
    l->projection()->ortho_left      = -25.0f;
    l->projection()->ortho_width     =  50.0f;
    l->projection()->ortho_bottom    = -25.0f;
    l->projection()->ortho_height    =  50.0f;
    l->projection()->z_near          =  20.0f;
    l->projection()->z_far           =  60.0f;

    if (layer == nullptr)
    {
        layer = m_content_layer.get();
    }
    layer->lights.emplace_back(l);

    auto node = make_shared<Node>();
    m_scene->nodes.emplace_back(node);
    mat4 m = erhe::toolkit::create_look_at(position,                 // eye
                                           vec3(0.0f,  0.0f, 0.0f),  // center
                                           vec3(0.0f,  0.0f, 1.0f)); // up
    node->transforms.parent_from_node.set(m);
    node->update();
    node->attach(l);

    return l;
}

auto Scene_manager::make_spot_light(const string& name,
                                    vec3          position,
                                    vec3          target,
                                    vec3          color,
                                    float         intensity,
                                    vec2          spot_cone_angle)
-> shared_ptr<Light>
{
    auto l = make_shared<Light>(name);
    l->type                          = Light::Type::spot;
    l->color                         = color;
    l->intensity                     = intensity;
    l->range                         = 25.0f;
    l->inner_spot_angle              = spot_cone_angle[0];
    l->outer_spot_angle              = spot_cone_angle[1];
    l->projection()->projection_type = Projection::Type::perspective;//orthogonal;
    l->projection()->fov_x           = l->outer_spot_angle;
    l->projection()->fov_y           = l->outer_spot_angle;
    l->projection()->z_near          =   1.0f;
    l->projection()->z_far           = 100.0f;

    add(l);

    auto node = make_shared<Node>();
    m_scene->nodes.emplace_back(node);
    mat4 m = erhe::toolkit::create_look_at(position, target, vec3(0.0f, 0.0f, 1.0f));
    node->transforms.parent_from_node.set(m);
    node->update();
    node->attach(l);

    return l;
}

void Scene_manager::make_punctual_light_nodes()
{
    size_t directional_light_count = 1;
    for (size_t i = 0; i < directional_light_count; ++i)
    {
        float rel = i / static_cast<float>(directional_light_count);
        float h   = rel * 90.0f;
        float s   = directional_light_count == 1 ? 0.0f : 1.0f;
        float v   = 1.0f;
        float r, g, b;
        erhe::toolkit::hsv_to_rgb(h, s, v, r, g, b);

        float x = 30.0f * cos(rel * 2.0f * pi<float>());
        float z = 30.0f * sin(rel * 2.0f * pi<float>());
        //if (i == 0)
        //{
        //    x = 0;
        //    z = 0;
        //    r = 1.0;
        //    g = 1.0;
        //    b = 1.0;
        //}

        vec3   color     = vec3(r, g, b);
        float  intensity = (8.0f / static_cast<float>(directional_light_count));
        string name      = fmt::format("Directional light {}", i);
        vec3   position  = vec3(   x, 30.0f, z);
        //vec3        position  = vec3( 10.0f, 10.0f, 10.0f);
        make_directional_light(name,
                               position,
                               color,
                               intensity);
    }

    int spot_light_count = 0;
    for (int i = 0; i < spot_light_count; ++i)
    {
        float rel   = static_cast<float>(i) / static_cast<float>(spot_light_count);
        float t     = std::pow(rel, 0.5f);
        float theta = t * 6.0f;
        float R     = 0.5f + 20.0f * t;
        float h     = fract(theta) * 360.0f;
        float s     = 0.9f;
        float v     = 1.0f;
        float r, g, b;

        erhe::toolkit::hsv_to_rgb(h, s, v, r, g, b);

        vec3   color     = vec3(r, g, b);
        float  intensity = 100.0f;
        string name      = fmt::format("Spot {}", i);

        float x_pos = R * sin(t * 6.0f * 2.0f * pi<float>());
        float z_pos = R * cos(t * 6.0f * 2.0f * pi<float>());

        vec3 position        = vec3(x_pos, 10.0f, z_pos);
        vec3 target          = vec3(x_pos * 0.5, 0.0f, z_pos * 0.5f);
        vec2 spot_cone_angle = vec2(pi<float>() / 5.0f,
                                    pi<float>() / 4.0f);
        make_spot_light(name, position, target, color, intensity, spot_cone_angle);
    }
}

void Scene_manager::update_fixed_step(double dt)
{
    // TODO
    // Physics should mostly run in a separate thread.
    m_physics_world->update_fixed_step(dt);
}

void Scene_manager::update_once_per_frame(double time_d)
{
    ZoneScoped;

    float time        = static_cast<float>(time_d);
    auto& lights      = m_content_layer->lights;
    int   n_lights    = static_cast<int>(lights.size());
    int   light_index = 0;

    for (auto i = lights.begin(); i != lights.end(); ++i)
    {
        auto l = *i;
        if (l->type == Light::Type::directional)
        {
            continue;
        }

        float rel = static_cast<float>(light_index) / static_cast<float>(n_lights);
        float t   = 0.5f * time + rel * pi<float>() * 7.0f;
        float R   = 4.0f;
        float r   = 8.0f;

        auto eye = vec3(R * std::sin(rel + t * 0.52f),
                        8.0f,
                        R * std::cos(rel + t * 0.71f));

        auto center = vec3(r * std::sin(rel + t * 0.35f),
                           0.0f,
                           r * std::cos(rel + t * 0.93f));

        auto m = erhe::toolkit::create_look_at(eye,
                                               center,
                                               vec3(0.0f, 0.0f, 1.0f)); // up

        l->node()->transforms.parent_from_node.set(m);
        l->node()->update();

        light_index++;
    }
}

void Scene_manager::add_scene()
{
    ZoneScoped;

    // Layer configuration
    m_content_layer   = make_shared<Layer>();
    m_selection_layer = make_shared<Layer>();
    m_tool_layer      = make_shared<Layer>();
    m_brush_layer     = make_shared<Layer>();
    m_scene->layers.push_back(m_content_layer);
    m_scene->layers.push_back(m_selection_layer);
    m_scene->layers.push_back(m_tool_layer);
    m_scene->layers.push_back(m_brush_layer);
    m_all_layers.push_back(m_content_layer);
    m_all_layers.push_back(m_selection_layer);
    m_all_layers.push_back(m_tool_layer);
    m_all_layers.push_back(m_brush_layer);
    m_content_layers.push_back(m_content_layer);
    m_selection_layers.push_back(m_selection_layer);
    m_tool_layers.push_back(m_tool_layer);
    m_brush_layers.push_back(m_brush_layer);

    m_content_layer->ambient_light = vec4(0.1f, 0.15f, 0.2f, 0.0f);
    m_tool_layer->ambient_light = vec4(0.0f, 0.0f, 0.0f, 0.0f);
    auto tool_light = make_directional_light("Tool layer directional light",
                                             vec3(1.0f, 1.0f, 1.0f),
                                             vec3(1.0f, 1.0f, 1.0f), 5.0f,
                                             m_tool_layer.get());
    tool_light->cast_shadow = false;

    make_brushes();
    make_materials();
    make_mesh_nodes();
    make_punctual_light_nodes();
    add_floor();
    initialize_cameras();
}

void Scene_manager::initialize_cameras()
{
    m_camera = make_shared<Camera>("camera");
    m_camera->projection()->fov_y           = erhe::toolkit::degrees_to_radians(35.0f);
    m_camera->projection()->projection_type = Projection::Type::perspective_vertical;
    m_camera->projection()->z_near          = 0.03f;
    m_camera->projection()->z_far           = 200.0f;
    m_scene->cameras.emplace_back(m_camera);

    auto node = make_shared<Node>();
    m_scene->nodes.emplace_back(node);
    //mat4 m = erhe::toolkit::create_look_at(vec3(1.0f, 7.0f, 1.0f),
    mat4 m = erhe::toolkit::create_look_at(vec3(10.0f, 7.0f, 10.0f),
                                           vec3(0.0f, 0.0f, 0.0f),
                                           vec3(0.0f, 1.0f, 0.0f));
    node->transforms.parent_from_node.set(m);
    node->update();
    node->attach(m_camera);
}

auto Scene_manager::add(shared_ptr<Material> material)
-> shared_ptr<Material>
{
    VERIFY(material);
    material->index = m_materials.size();
    m_materials.push_back(material);
    log_materials.trace("material {} is {}\n", material->index, material->name);
    return material;
}

auto Scene_manager::add(shared_ptr<Mesh> mesh)
-> shared_ptr<Mesh>
{
    m_content_layer->meshes.push_back(mesh);
    return mesh;
}

auto Scene_manager::add(shared_ptr<Light> light)
-> shared_ptr<Light>
{
    m_content_layer->lights.push_back(light);
    return light;
}

auto Scene_manager::brushes() -> vector<Brush>&
{
    return m_brushes;
}

auto Scene_manager::brushes() const -> const vector<Brush>&
{
    return m_brushes;
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

struct Light_comparator
{
    inline auto operator()(const shared_ptr<Light>& lhs,
                           const shared_ptr<Light>& rhs) -> bool
    {
        return sort_value(lhs->type) < sort_value(rhs->type);
    }
};

}

void Scene_manager::sort_lights()
{
    sort(m_content_layer->lights.begin(),
         m_content_layer->lights.end(),
         Light_comparator());
    sort(m_tool_layer->lights.begin(),
         m_tool_layer->lights.end(),
         Light_comparator());
}

auto Scene_manager::scene() -> Scene&
{
    return *m_scene.get();
}

void Scene_manager::debug_render()
{
    m_physics_world->bullet_dynamics_world.debugDrawWorld();
}

auto Scene_manager::make_primitive_geometry(Geometry&    geometry,
                                            Normal_style normal_style)
-> shared_ptr<Primitive_geometry>
{
    auto format_info = m_format_info;
    format_info.normal_style = normal_style;
    return make_primitive_shared(geometry, format_info, m_buffer_info);
}

//auto Scene_manager::make_convex_shape(Geometry& geometry) -> btConvexHullShape&
//{
//    return m_convex_shapes.emplace_back(
//        reinterpret_cast<const btScalar*>(
//            geometry.point_attributes().find<vec3>(
//                c_point_locations
//            )->values.data()),
//        static_cast<int>(geometry.point_count()),
//        static_cast<int>(sizeof(vec3))
//    );
//}


} // namespace editor
