#include "scene/scene_manager.hpp"
#include "parsers/json_polyhedron.hpp"
#include "parsers/wavefront_obj.hpp"
#include "renderers/programs.hpp"
#include "log.hpp"

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
#include "erhe/primitive/material.hpp"
#include "erhe/scene/camera.hpp"
#include "erhe/scene/mesh.hpp"
#include "erhe/scene/node.hpp"
#include "erhe/scene/light.hpp"
#include "erhe/toolkit/math_util.hpp"

#include "erhe_tracy.hpp"

#include "glm/gtx/color_space.hpp"

namespace editor
{

using namespace erhe::graphics;
using namespace erhe::geometry;
using namespace erhe::scene;
using namespace erhe::primitive;
using namespace std;
using namespace glm;

Scene_manager::Geometry_entry::Geometry_entry(erhe::geometry::Geometry&&                        geometry,
                                              erhe::primitive::Primitive_geometry::Normal_style normal_style)
    : geometry    {std::make_shared<erhe::geometry::Geometry>(std::move(geometry))}
    , normal_style{normal_style}
{
}

Scene_manager::Geometry_entry::Geometry_entry(const std::shared_ptr<erhe::geometry::Geometry>&  geometry,
                                              erhe::primitive::Primitive_geometry::Normal_style normal_style)
    : geometry    {geometry}
    , normal_style{normal_style}
{
}

Scene_manager::Scene_manager()
    : Component("Scene_manager")
{
}

void Scene_manager::connect()
{
    m_programs = require<Programs>();
}

void Scene_manager::initialize_component()
{
    ZoneScoped;

    m_format_info = Primitive_builder::Format_info();
    m_buffer_info = Primitive_builder::Buffer_info();

    static constexpr gl::Buffer_storage_mask storage_mask{gl::Buffer_storage_mask::map_write_bit};

    size_t vertex_byte_count = 256 * 1024 * 1024;
    size_t index_byte_count  =  64 * 1024 * 1024;
    m_buffer_info.index_type    = gl::Draw_elements_type::unsigned_int;
    m_buffer_info.vertex_buffer = std::make_shared<Buffer>(gl::Buffer_target::array_buffer,
                                                           vertex_byte_count,
                                                           storage_mask);
    m_buffer_info.index_buffer = std::make_shared<Buffer>(gl::Buffer_target::element_array_buffer,
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
    m_format_info.normal_style              = Primitive_geometry::Normal_style::corner_normals;
    m_format_info.vertex_attribute_mappings = &m_programs->attribute_mappings;

    Primitive_builder::prepare_vertex_format(m_format_info, m_buffer_info);

    add_scene();
}

auto Scene_manager::make_primitive_geometry(const Geometry_entry& entry)
-> std::shared_ptr<Primitive_geometry>
{
    ZoneScoped;

    m_format_info.normal_style = entry.normal_style;
    auto res = make_primitive_shared(*entry.geometry.get(), m_format_info, m_buffer_info);
    res->source_geometry     = entry.geometry;
    res->source_normal_style = m_format_info.normal_style;
    m_primitive_geometries.push_back(res);
    return res;
}

auto Scene_manager::make_primitive_geometry(const std::shared_ptr<Geometry>& geometry)
-> std::shared_ptr<Primitive_geometry>
{
    ZoneScoped;

    if (geometry.get() == nullptr)
    {
        return {};
    }
    m_format_info.normal_style = Primitive_geometry::Normal_style::corner_normals;
    auto res = make_primitive_shared(*geometry.get(), m_format_info, m_buffer_info);
    res->source_geometry     = geometry;
    res->source_normal_style = m_format_info.normal_style;
    m_primitive_geometries.push_back(res);
    return res;
}

auto Scene_manager::make_primitive_geometry(Geometry&& geometry, Primitive_geometry::Normal_style normal_style)
-> std::shared_ptr<Primitive_geometry>
{
    ZoneScoped;

    m_format_info.normal_style = normal_style;
    auto res = make_primitive_shared(geometry, m_format_info, m_buffer_info);
    res->source_geometry     = std::make_shared<Geometry>(std::move(geometry));
    res->source_normal_style = m_format_info.normal_style;
    m_primitive_geometries.push_back(res);
    return res;
}

auto Scene_manager::make_mesh_node(const std::string&             name,
                                   shared_ptr<Primitive_geometry> primitive_geometry,
                                   std::shared_ptr<Material>      material,
                                   std::shared_ptr<Layer>         target_layer,
                                   Node*                          parent,
                                   vec3                           position)
-> shared_ptr<Mesh>
{
    ZoneScoped;

    mat4 transform = erhe::toolkit::create_translation(position);

    auto node = make_shared<Node>();
    node->parent = parent;
    node->transforms.parent_from_node.set(transform);
    node->update();
    m_scene.nodes.push_back(node);

    auto* layer = target_layer.get();
    if (layer == nullptr)
    {
        layer = m_content_layer.get();
    }
    VERIFY(layer != nullptr);

    auto mesh = make_shared<Mesh>(name, node);
    mesh->primitives.emplace_back(primitive_geometry, material);
    mesh->node->reference_count++;
    layer->meshes.push_back(mesh);

    return mesh;
}

auto Scene_manager::camera() -> erhe::scene::ICamera&
{
    auto* camera = m_camera.get();
    assert(camera != nullptr);
    return *camera;
}

auto Scene_manager::camera() const -> const erhe::scene::Camera&
{
    auto* camera = m_camera.get();
    assert(camera != nullptr);
    return *camera;
}

auto Scene_manager::materials() -> std::vector<std::shared_ptr<Material>>&
{
    return m_materials;
}

auto Scene_manager::materials() const -> const std::vector<std::shared_ptr<Material>>&
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

auto Scene_manager::vertex_format() -> std::shared_ptr<Vertex_format>
{
    return m_buffer_info.vertex_format;
}

void Scene_manager::make_geometry(Geometry&&                       geometry,
                                  Primitive_geometry::Normal_style normal_style)
{
    ZoneScoped;

    geometry.compute_polygon_normals();
    geometry.compute_tangents();
    geometry.build_edges();
    geometry.compute_polygon_centroids();
    geometry.compute_point_normals(c_point_normals_smooth);
    m_geometries.emplace_back(std::move(geometry), normal_style);
}

void Scene_manager::make_geometry(const std::shared_ptr<Geometry>& geometry,
                                  Primitive_geometry::Normal_style normal_style)
{
    ZoneScoped;

    geometry->compute_polygon_normals();
    geometry->compute_tangents();
    geometry->build_edges();
    geometry->compute_polygon_centroids();
    geometry->compute_point_normals(c_point_normals_smooth);
    m_geometries.emplace_back(geometry, normal_style);
}

void Scene_manager::make_geometries()
{
    ZoneScoped;

    if constexpr (true) // test scene six platonic solids
    {
        if constexpr (true) // teapot
        {
            auto geometry = parse_obj_geometry("res/models/teapot.obj");
            geometry.compute_polygon_normals();
            // The real teapot is ~33% taller (ratio 4:3)
            mat4 scale_t = erhe::toolkit::create_scale(0.5f, 0.5f * 4.0f / 3.0f, 0.5f);
            geometry.transform(scale_t);
            make_geometry(std::move(geometry), Primitive_geometry::Normal_style::point_normals);
        }

        float scale = 1.0f;
        make_geometry(shapes::make_dodecahedron(scale),  Primitive_geometry::Normal_style::polygon_normals);
        make_geometry(shapes::make_icosahedron(scale),   Primitive_geometry::Normal_style::polygon_normals);
        make_geometry(shapes::make_octahedron(scale),    Primitive_geometry::Normal_style::polygon_normals);
        make_geometry(shapes::make_tetrahedron(scale),   Primitive_geometry::Normal_style::polygon_normals);
        make_geometry(shapes::make_cube(scale),          Primitive_geometry::Normal_style::polygon_normals);
        make_geometry(shapes::make_cuboctahedron(scale), Primitive_geometry::Normal_style::polygon_normals);
    }

    if constexpr (false) // test scene for anisotropy debugging
    {
        auto x_material = std::make_shared<erhe::primitive::Material>("x", glm::vec4(1.000f, 0.000f, 0.0f, 1.0f), 0.3f, 0.0f, 0.3f);
        auto y_material = std::make_shared<erhe::primitive::Material>("y", glm::vec4(0.228f, 1.000f, 0.0f, 1.0f), 0.3f, 0.0f, 0.3f);
        auto z_material = std::make_shared<erhe::primitive::Material>("z", glm::vec4(0.000f, 0.228f, 1.0f, 1.0f), 0.3f, 0.0f, 0.3f);

        add(x_material);
        add(y_material);
        add(z_material);

        float ring_major_radius = 4.0f;
        float ring_minor_radius = 0.55f; // 0.15f;
        auto  ring_geometry     = erhe::geometry::shapes::make_torus(ring_major_radius, ring_minor_radius, 80, 32);
        ring_geometry.transform(erhe::toolkit::mat4_swap_xy);
        ring_geometry.reverse_polygons();
        auto ring_geometry_shared = std::make_shared<Geometry>(std::move(ring_geometry));
        auto rotate_ring_pg = make_primitive_geometry(ring_geometry_shared);

        glm::vec3 pos{0.0f, 0.0f, 0.0f};
        auto x_rotate_ring_mesh = make_mesh_node("X ring", rotate_ring_pg, x_material, {}, nullptr, pos);
        auto y_rotate_ring_mesh = make_mesh_node("Y ring", rotate_ring_pg, y_material, {}, nullptr, pos);
        auto z_rotate_ring_mesh = make_mesh_node("Z ring", rotate_ring_pg, z_material, {}, nullptr, pos);

        x_rotate_ring_mesh->node->transforms.parent_from_node.set         ( glm::mat4(1));
        y_rotate_ring_mesh->node->transforms.parent_from_node.set_rotation( glm::pi<float>() / 2.0f, glm::vec3(0.0f, 0.0f, 1.0f));
        z_rotate_ring_mesh->node->transforms.parent_from_node.set_rotation(-glm::pi<float>() / 2.0f, glm::vec3(0.0f, 1.0f, 0.0f));
        return;
    }

    if constexpr (true) // Round shapes
    {
        make_geometry(shapes::make_sphere(1.0f, 12 * 4, 4 * 6));
        make_geometry(shapes::make_torus(0.6f, 0.3f, 42, 32));
        make_geometry(shapes::make_cylinder(-1.0f, 1.0f, 1.0f, true, true, 32, 2));
        make_geometry(shapes::make_cone(-1.0f, 1.0f, 1.0f, true, 42, 4));
    }

    if constexpr (false) // Catmull-clark subdivision testing
    {
        Geometry dodecahedron  = shapes::make_dodecahedron(1.0);
        Geometry dodecahedron0 = operation::catmull_clark_subdivision(dodecahedron);
        Geometry dodecahedron1 = operation::catmull_clark_subdivision(dodecahedron0);
        //Geometry dodecahedron2 = std::move(operation::catmull_clark(std::move(dodecahedron1)));
        //Geometry dodecahedron3 = std::move(operation::catmull_clark(std::move(dodecahedron2)));
        //Geometry dodecahedron4 = std::move(operation::catmull_clark(std::move(dodecahedron3)));

        dodecahedron1.compute_tangents();

        make_geometry(std::move(dodecahedron1), Primitive_geometry::Normal_style::polygon_normals);
    }

    if constexpr (false) // Johnson solid 127
    {
        auto geometry = make_json_polyhedron("res/polyhedra/127.json");
        geometry.compute_polygon_normals();
        make_geometry(std::move(geometry), Primitive_geometry::Normal_style::polygon_normals);
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
        return;
    }

    for (float z = -15.0f; z < 15.1f; z += 5.0f)
    {
        float rel = (z + 15.0f) / 30.0f;
        float h   = rel * 360.0f;
        float s   = 0.9f;
        float v   = 1.0f;
        float R, G, B;
        erhe::toolkit::hsv_to_rgb(h, s, v, R, G, B);
        auto m = make_shared<Material>(fmt::format("Material {}", m_materials.size()),
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

    auto floor_geometry_entry = Geometry_entry
    {
        shapes::make_box(vec3(40.0f, 1.0f, 40.0f),
                              ivec3(1, 1, 1),
                              1.0f),
        Primitive_geometry::Normal_style::polygon_normals
    };

    floor_geometry_entry.geometry->build_edges();

    make_mesh_node("Floor",
                   make_primitive_geometry(floor_geometry_entry),
                   floor_material,
                   m_content_layer,
                   nullptr,
                   vec3(0, -0.5001f, 0.0f));
}

void Scene_manager::make_mesh_nodes()
{
    ZoneScoped;

    float min_x = 0.0f;
    float max_x = 0.0f;
    float gap   = 0.5f;

    size_t geometry_index = 0;
    for (auto& entry : m_geometries)
    {
        auto  primitive_geometry = make_primitive_geometry(entry);

        vec3  min   = primitive_geometry->bounding_box_min;
        vec3  max   = primitive_geometry->bounding_box_max;
        float width = max.x - min.x;

        float x;
        if (fabs(min_x) < fabs(max_x))
        {
            min_x -= 0.5f * gap;
            min_x -= width * 0.5f;
            x = min_x;
            min_x -= width * 0.5f;
            min_x -= 0.5f * gap;
        }
        else
        {
            max_x += 0.5f * gap;
            max_x += width * 0.5f;
            x = max_x;
            max_x += width * 0.5f;
            max_x += 0.5f * gap;
        }

        size_t material_index = 0;
        float z = 0.0f; // XXX only one material/mesh node for debugging
        //for (float z = -15.0f; z < 15.1f; z += 5.0f)
        {
            shared_ptr<Material> material = m_materials.at(material_index);

            auto m = make_mesh_node(entry.geometry->name(),
                                    primitive_geometry,
                                    material,
                                    m_content_layer,
                                    nullptr,
                                    vec3(x, -min.y, z));
            ++material_index;
        }
        ++geometry_index;
    }
}

auto Scene_manager::make_directional_light(const std::string&  name,
                                           glm::vec3           position,
                                           glm::vec3           color,
                                           float               intensity,
                                           erhe::scene::Layer* layer)
-> std::shared_ptr<erhe::scene::Light>
{
    auto node = make_shared<erhe::scene::Node>();
    m_scene.nodes.emplace_back(node);
    mat4 m = erhe::toolkit::create_look_at(position,                 // eye
                                           vec3(0.0f,  0.0f, 0.0f),  // center
                                           vec3(0.0f,  0.0f, 1.0f)); // up
    node->transforms.parent_from_node.set(m);
    node->update();
    node->reference_count++;

    auto l = make_shared<Light>(name, node);
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
    return l;
}

auto Scene_manager::make_spot_light(const std::string& name,
                                    vec3               position,
                                    vec3               target,
                                    vec3               color,
                                    float              intensity,
                                    vec2               spot_cone_angle)
-> std::shared_ptr<erhe::scene::Light>
{
    auto node = make_shared<erhe::scene::Node>();
    m_scene.nodes.emplace_back(node);
    mat4 m = erhe::toolkit::create_look_at(position, target, vec3(0.0f, 0.0f, 1.0f));
    node->transforms.parent_from_node.set(m);
    node->update();
    node->reference_count++;

    auto l = make_shared<Light>(name, node);
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

        float x = 30.0f * cos(rel * 2.0f * glm::pi<float>());
        float z = 30.0f * sin(rel * 2.0f * glm::pi<float>());
        //if (i == 0)
        //{
        //    x = 0;
        //    z = 0;
        //    r = 1.0;
        //    g = 1.0;
        //    b = 1.0;
        //}

        vec3        color     = vec3(r, g, b);
        float       intensity = (8.0f / static_cast<float>(directional_light_count));
        std::string name      = fmt::format("Directional light {}", i);
        vec3        position  = vec3(   x, 30.0f, z);
        //vec3        position  = vec3( 10.0f, 10.0f, 10.0f);
        make_directional_light(name,
                               position,
                               color,
                               intensity);
    }

    int spot_light_count = 4;
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

        vec3        color     = vec3(r, g, b);
        float       intensity = 100.0f;
        std::string name      = fmt::format("Spot {}", i);

        float x_pos = R * sin(t * 6.0f * 2.0f * glm::pi<float>());
        float z_pos = R * cos(t * 6.0f * 2.0f * glm::pi<float>());

        vec3 position        = vec3(x_pos, 10.0f, z_pos);
        vec3 target          = vec3(x_pos * 0.5, 0.0f, z_pos * 0.5f);
        vec2 spot_cone_angle = vec2(glm::pi<float>() / 5.0f,
                                    glm::pi<float>() / 4.0f);
        make_spot_light(name, position, target, color, intensity, spot_cone_angle);
    }
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
        if (l->type == erhe::scene::Light::Type::directional)
        {
            continue;
        }

        float rel = static_cast<float>(light_index) / static_cast<float>(n_lights);
        float t   = 0.5f * time + rel * glm::pi<float>() * 7.0f;
        float R   = 4.0f;
        float r   = 8.0f;

        auto eye = glm::vec3(R * std::sin(rel + t * 0.52f),
                             8.0f,
                             R * std::cos(rel + t * 0.71f));

        auto center = glm::vec3(r * std::sin(rel + t * 0.35f),
                                0.0f,
                                r * std::cos(rel + t * 0.93f));

        auto m = erhe::toolkit::create_look_at(eye,
                                               center,
                                               glm::vec3(0.0f, 0.0f, 1.0f)); // up

        l->node()->transforms.parent_from_node.set(m);
        l->node()->update();

        light_index++;
    }
}

void Scene_manager::add_scene()
{
    ZoneScoped;

    // Layer configuration
    m_content_layer   = std::make_shared<erhe::scene::Layer>();
    m_selection_layer = std::make_shared<erhe::scene::Layer>();
    m_tool_layer      = std::make_shared<erhe::scene::Layer>();
    m_brush_layer     = std::make_shared<erhe::scene::Layer>();
    m_scene.layers.push_back(m_content_layer);
    m_scene.layers.push_back(m_selection_layer);
    m_scene.layers.push_back(m_tool_layer);
    m_scene.layers.push_back(m_brush_layer);
    m_all_layers.push_back(m_content_layer);
    m_all_layers.push_back(m_selection_layer);
    m_all_layers.push_back(m_tool_layer);
    m_all_layers.push_back(m_brush_layer);
    m_content_layers.push_back(m_content_layer);
    m_selection_layers.push_back(m_selection_layer);
    m_tool_layers.push_back(m_tool_layer);
    m_brush_layers.push_back(m_brush_layer);

    m_content_layer->ambient_light = glm::vec4(0.1f, 0.15f, 0.2f, 0.0f);
    m_tool_layer->ambient_light = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);
    auto tool_light = make_directional_light("Tool layer directional light",
                                             glm::vec3(1.0f, 1.0f, 1.0f),
                                             glm::vec3(1.0f, 1.0f, 1.0f), 5.0f,
                                             m_tool_layer.get());
    tool_light->cast_shadow = false;

    make_geometries();
    make_materials();
    make_mesh_nodes();
    make_punctual_light_nodes();
    add_floor();
    initialize_cameras();
}

void Scene_manager::initialize_cameras()
{
    auto node = make_shared<erhe::scene::Node>();
    m_scene.nodes.emplace_back(node);
    glm::mat4 m = erhe::toolkit::create_look_at(glm::vec3(10.0f, 7.0f, 10.0f),
                                                glm::vec3(0.0f, 0.0f, 0.0f),
                                                glm::vec3(0.0f, 1.0f, 0.0f));
    node->transforms.parent_from_node.set(m);
    node->update();
    node->reference_count++;

    m_camera = make_shared<Camera>("camera", node);
    m_camera->projection()->fov_y           = erhe::toolkit::degrees_to_radians(35.0f);
    m_camera->projection()->projection_type = Projection::Type::perspective_vertical;
    m_camera->projection()->z_near          = 0.03f;
    m_camera->projection()->z_far           = 200.0f;
    m_scene.cameras.emplace_back(m_camera);
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

auto Scene_manager::geometries() -> std::vector<Scene_manager::Geometry_entry>&
{
    return m_geometries;
}

auto Scene_manager::geometries() const -> const std::vector<Scene_manager::Geometry_entry>&
{
    return m_geometries;
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
    inline auto operator()(const std::shared_ptr<erhe::scene::Light>& lhs,
                           const std::shared_ptr<erhe::scene::Light>& rhs) -> bool
    {
        return sort_value(lhs->type) < sort_value(rhs->type);
    }
};

}

void Scene_manager::sort_lights()
{
    std::sort(m_content_layer->lights.begin(),
              m_content_layer->lights.end(),
              Light_comparator());
    std::sort(m_tool_layer->lights.begin(),
              m_tool_layer->lights.end(),
              Light_comparator());
}

auto Scene_manager::scene() -> erhe::scene::Scene&
{
    return m_scene;
}


} // namespace editor
