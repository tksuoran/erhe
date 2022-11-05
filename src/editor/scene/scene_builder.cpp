#include "scene/scene_builder.hpp"
#include "editor_log.hpp"
#include "editor_rendering.hpp"
#include "editor_scenes.hpp"
#include "rendertarget_node.hpp"
#include "rendertarget_imgui_viewport.hpp"
#include "task_queue.hpp"

#include "brushes/brush.hpp"
#include "brushes/brushes.hpp"
#include "parsers/gltf.hpp"
#include "parsers/json_polyhedron.hpp"
#include "parsers/wavefront_obj.hpp"
#include "rendergraph/shadow_render_node.hpp"
#include "renderers/mesh_memory.hpp"
#include "renderers/programs.hpp"
#include "renderers/shadow_renderer.hpp"
#include "scene/debug_draw.hpp"
#include "scene/helpers.hpp"
#include "scene/material_library.hpp"
#include "scene/node_physics.hpp"
#include "scene/scene_root.hpp"
#include "scene/viewport_window.hpp"
#include "scene/viewport_windows.hpp"
#include "tools/fly_camera_tool.hpp"
#include "tools/grid_tool.hpp"
#include "tools/trs_tool.hpp"
#include "windows/debug_view_window.hpp"
#include "windows/imgui_viewport_window.hpp"
#if defined(ERHE_XR_LIBRARY_OPENXR)
#   include "xr/headset_view.hpp"
#endif

#include "SkylineBinPack.h" // RectangleBinPack

#include "erhe/application/configuration.hpp"
#include "erhe/application/imgui/imgui_windows.hpp"
#include "erhe/application/imgui/imgui_windows.hpp"
#include "erhe/application/imgui/window_imgui_viewport.hpp"
#include "erhe/application/rendergraph/rendergraph.hpp"
#include "erhe/application/rendergraph/rendergraph_node.hpp"
#include "erhe/application/graphics/gl_context_provider.hpp"
#include "erhe/geometry/shapes/box.hpp"
#include "erhe/geometry/shapes/cone.hpp"
#include "erhe/geometry/shapes/disc.hpp"
#include "erhe/geometry/shapes/sphere.hpp"
#include "erhe/geometry/shapes/torus.hpp"
#include "erhe/geometry/shapes/regular_polyhedron.hpp"
#include "erhe/graphics/buffer.hpp"
#include "erhe/graphics/buffer_transfer_queue.hpp"
#include "erhe/graphics/renderbuffer.hpp"
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
#include "erhe/scene/transform.hpp"
#include "erhe/toolkit/math_util.hpp"
#include "erhe/toolkit/profile.hpp"

#include <glm/gtx/quaternion.hpp>
#include <glm/gtx/color_space.hpp>
#include <glm/gtx/rotate_vector.hpp>

namespace editor
{

using erhe::geometry::shapes::make_dodecahedron;
using erhe::geometry::shapes::make_icosahedron;
using erhe::geometry::shapes::make_octahedron;
using erhe::geometry::shapes::make_tetrahedron;
using erhe::geometry::shapes::make_cuboctahedron;
using erhe::geometry::shapes::make_cube;
using erhe::geometry::shapes::make_cone;
using erhe::geometry::shapes::make_sphere;
using erhe::geometry::shapes::make_torus;
using erhe::geometry::shapes::torus_volume;
using erhe::geometry::shapes::make_cylinder;
using erhe::scene::Projection;
using erhe::scene::Light;
using erhe::scene::Node;
using erhe::scene::Node_visibility;
using erhe::geometry::shapes::make_box;
using erhe::primitive::Normal_style;
using glm::mat3;
using glm::mat4;
using glm::ivec3;
using glm::vec2;
using glm::vec3;
using glm::vec4;

Scene_builder::Scene_builder()
    : Component{c_type_name}
{
}

Scene_builder::~Scene_builder() noexcept
{
}

void Scene_builder::declare_required_components()
{
    require<erhe::application::Configuration      >();
    require<erhe::application::Gl_context_provider>();
    require<erhe::application::Imgui_windows      >();
    require<erhe::application::Rendergraph        >();
    require<Debug_view_window>();
    require<Editor_rendering >();
    require<Editor_scenes    >();
    require<Fly_camera_tool  >();
    require<Shadow_renderer  >();
    require<Viewport_windows >();
    m_brushes     = require<Brushes    >();
    m_mesh_memory = require<Mesh_memory>();
}

void Scene_builder::initialize_component()
{
    ERHE_PROFILE_FUNCTION

    {
        const erhe::application::Scoped_gl_context gl_context{
            Component::get<erhe::application::Gl_context_provider>()
        };

        m_scene_root = std::make_shared<Scene_root>("Test scene");
        m_scene_root->material_library()->add_default_materials();

        setup_scene();
    }

    const auto& editor_scenes = get<Editor_scenes>();
    editor_scenes->register_scene_root(m_scene_root);
}

void Scene_builder::add_rendertarget_viewports(int count)
{
#if defined(ERHE_GUI_LIBRARY_IMGUI)
    const auto& rendergraph             = get<erhe::application::Rendergraph  >();
    const auto& imgui_windows           = get<erhe::application::Imgui_windows>();
    const auto& primary_viewport_window = get_primary_viewport_window();
    const auto& test_scene_root         = get_scene_root();

    if (count >= 1)
    {
        auto rendertarget_node_1 = test_scene_root->create_rendertarget_node(
            *m_components,
            *primary_viewport_window.get(),
            1920,
            1080,
            2000.0
        );

        test_scene_root->scene().add_to_mesh_layer(
            *test_scene_root->layers().rendertarget(),
            rendertarget_node_1
        );

        rendertarget_node_1->set_world_from_node(
            erhe::toolkit::create_look_at(
                glm::vec3{-0.3f, 0.6f, -0.3f},
                glm::vec3{ 0.0f, 0.7f,  0.0f},
                glm::vec3{ 0.0f, 1.0f,  0.0f}
            )
        );

        auto imgui_viewport_1 = std::make_shared<editor::Rendertarget_imgui_viewport>(
            rendertarget_node_1.get(),
            "Rendertarget ImGui Viewport 1",
            *m_components
        );

        imgui_windows->register_imgui_viewport(imgui_viewport_1);

        const auto& grid_tool = get<editor::Grid_tool>();
        grid_tool->set_viewport(imgui_viewport_1.get());
        grid_tool->show();

#if defined(ERHE_XR_LIBRARY_OPENXR)
        const auto& headset_view = get<editor::Headset_view>();
        if (headset_view)
        {
            headset_view->set_viewport(imgui_viewport_1.get());
        }
#endif
    }

    if (count >= 2)
    {
        const auto camera_b = make_camera(
            "Camera B",
            glm::vec3{-7.0f, 1.0f, 0.0f},
            glm::vec3{ 0.0f, 0.5f, 0.0f}
        );
        camera_b->node_data.wireframe_color = glm::vec4{0.3f, 0.6f, 1.00f, 1.0f};

        const auto& viewport_windows = get<editor::Viewport_windows>();
        auto secondary_viewport_window = viewport_windows->create_viewport_window(
            "Secondary Viewport",
            test_scene_root,
            camera_b,
            2 // low MSAA
        );
        auto secondary_imgui_viewport_window = viewport_windows->create_imgui_viewport_window(
            secondary_viewport_window
        );
        //secondary_viewport_window->set_post_processing_enable(false); // TODO Post processing currently only handles one viewport

        auto rendertarget_node_2 = test_scene_root->create_rendertarget_node(
            *m_components,
            *primary_viewport_window.get(),
            1920,
            1080,
            2000.0
        );

        test_scene_root->scene().add_to_mesh_layer(
            *test_scene_root->layers().rendertarget(),
            rendertarget_node_2
        );

        rendertarget_node_2->set_world_from_node(
            erhe::toolkit::create_look_at(
                glm::vec3{0.3f, 0.6f, -0.3f},
                glm::vec3{0.0f, 0.7f,  0.0f},
                glm::vec3{0.0f, 1.0f,  0.0f}
            )
        );

        auto imgui_viewport_2 = std::make_shared<editor::Rendertarget_imgui_viewport>(
            rendertarget_node_2.get(),
            "Rendertarget ImGui Viewport 2",
            *m_components
        );
        imgui_windows->register_imgui_viewport(imgui_viewport_2);

        secondary_imgui_viewport_window->set_viewport(imgui_viewport_2.get());
        secondary_imgui_viewport_window->show();

        // const auto& window_imgui_viewport = imgui_windows->get_window_viewport();

        rendergraph->connect(
            erhe::application::Rendergraph_node_key::window,
            secondary_imgui_viewport_window,
            imgui_viewport_2
        );
    }

    //secondary_viewport_window->show();
#endif
}

auto Scene_builder::make_camera(
    std::string_view name,
    vec3             position,
    vec3             look_at
) -> std::shared_ptr<erhe::scene::Camera>
{
    auto camera = std::make_shared<erhe::scene::Camera>(name);
    camera->projection()->fov_y           = glm::radians(35.0f);
    camera->projection()->projection_type = erhe::scene::Projection::Type::perspective_vertical;
    camera->projection()->z_near          = 0.03f;
    camera->projection()->z_far           = 80.0f;

    m_scene_root->scene().add(camera);

    const mat4 m = erhe::toolkit::create_look_at(
        position, // eye
        look_at,  // center
        vec3{0.0f, 1.0f,  0.0f}  // up
    );
    camera->set_parent_from_node(m);

    return camera;
}

void Scene_builder::setup_cameras()
{
    const auto& camera_a = make_camera(
        "Camera A",
        vec3{0.0f, 1.0f, 3.0f},
        vec3{0.0f, 0.5f, 0.0f}
    );
    camera_a->projection()->z_far = 64.0f;
    camera_a->node_data.wireframe_color = glm::vec4{1.0f, 0.6f, 0.3f, 1.0f};

    //const auto& camera_b = make_camera(
    //    "Camera B",
    //    vec3{-7.0f, 1.0f, 0.0f},
    //    vec3{ 0.0f, 0.5f, 0.0f}
    //);
    //camera_b->node_data.wireframe_color = glm::vec4{0.3f, 0.6f, 1.00f, 1.0f};

    const auto& configuration = get<erhe::application::Configuration>();
    if (configuration->window.show)
    {
        const auto& viewport_windows = get<Viewport_windows>();
        m_primary_viewport_window = viewport_windows->create_viewport_window(
            "Primary Viewport",
            m_scene_root,
            camera_a,
            std::min(2, configuration->graphics.msaa_sample_count), //// TODO Fix rendergraph
            configuration->graphics.post_processing
        );

        if (configuration->imgui.window_viewport)
        {
            //auto primary_imgui_viewport_window =
            viewport_windows->create_imgui_viewport_window(
                m_primary_viewport_window
            );
        }
        else
        {
            viewport_windows->create_basic_viewport_window(
                m_primary_viewport_window
            );
        }
    }

    const auto& shadow_renderer = get<Shadow_renderer>();
    const auto& render_graph    = get<erhe::application::Rendergraph>();

    if (configuration->window.show)
    {
        const auto shadow_render_node = shadow_renderer->create_node_for_viewport(m_primary_viewport_window);
        render_graph->register_node(shadow_render_node);
        render_graph->connect(
            erhe::application::Rendergraph_node_key::shadow_maps,
            shadow_render_node,
            m_primary_viewport_window
        );

        const auto& debug_view_window = get<Debug_view_window>();
        auto depth_to_color_node = std::make_shared<Depth_to_color_rendergraph_node>(
            *m_components
        );
        render_graph->register_node(depth_to_color_node);
        render_graph->connect(
            erhe::application::Rendergraph_node_key::shadow_maps,
            shadow_render_node,
            depth_to_color_node
        );
        render_graph->connect(
            erhe::application::Rendergraph_node_key::depth_visualization,
            depth_to_color_node,
            debug_view_window
        );
        render_graph->register_node(debug_view_window);

        const auto& imgui_windows         = get<erhe::application::Imgui_windows>();
        const auto& window_imgui_viewport = imgui_windows->get_window_viewport();
        //render_graph->register_node(window_imgui_viewport);

        if (window_imgui_viewport)
        {
            render_graph->connect(
                erhe::application::Rendergraph_node_key::window,
                debug_view_window,
                window_imgui_viewport
            );
        }

    }

    // TODO debug_view_window should also depend on debug_view_render_node
}

auto Scene_builder::build_info() -> erhe::primitive::Build_info&
{
    return m_mesh_memory->build_info;
};

void Scene_builder::make_brushes()
{
    ERHE_PROFILE_FUNCTION

    std::unique_ptr<ITask_queue> execution_queue;

    const auto& configuration = get<erhe::application::Configuration>();

    if (configuration->threading.parallel_initialization)
    {
        const std::size_t thread_count = std::min(
            8U,
            std::max(std::thread::hardware_concurrency() - 0, 1U)
        );
        execution_queue = std::make_unique<Parallel_task_queue>("scene builder", thread_count);
    }
    else
    {
        execution_queue = std::make_unique<Serial_task_queue>();
    }

    const auto& config = configuration->scene;
    auto floor_box_shape = erhe::physics::ICollision_shape::create_box_shape_shared(
        0.5f * vec3{config.floor_size, 1.0f, config.floor_size}
    );
    //auto table_box_shape = erhe::physics::ICollision_shape::create_box_shape_shared(vec3{1.0f, 0.5f, 0.5f});

    // Otherwise it will be destructed when leave add_floor() scope
    m_collision_shapes.push_back(floor_box_shape);
    //m_collision_shapes.push_back(table_box_shape);
    //m_scene_root->physics_world().collision_shapes.push_back(floor_box_shape);

    // Floor
    if (config.floor)
    {
        execution_queue->enqueue(
            [this, /*floor_size,*/ &floor_box_shape/*, &table_box_shape*/, &config]()
            {
                ERHE_PROFILE_SCOPE("Floor brush");

                Brush_create_context context{
                    .build_info   = build_info(),
                    .normal_style = Normal_style::corner_normals,
                    .density      = config.mass_scale
                };
                context.normal_style = Normal_style::polygon_normals;

                auto floor_geometry = std::make_shared<erhe::geometry::Geometry>(
                    make_box(
                        vec3{config.floor_size, 1.0f, config.floor_size},
                        //ivec3{static_cast<int>(floor_size), 1, static_cast<int>(floor_size)}
                        ivec3{config.floor_div, 1, config.floor_div}
                    )
                );
                floor_geometry->name = "floor";
                floor_geometry->build_edges();

                m_floor_brush = std::make_unique<Brush>(
                    Brush::Create_info{
                        .geometry        = floor_geometry,
                        .build_info      = build_info(),
                        .normal_style    = Normal_style::polygon_normals,
                        .density         = 0.0f,
                        .volume          = 0.0f,
                        .collision_shape = floor_box_shape
                    }
                );

                //auto table_geometry = std::make_shared<erhe::geometry::Geometry>(
                //    make_box(
                //        vec3{2.0f, 1.0f, 1.0f},
                //        ivec3{10, 1, 10}
                //    )
                //);
                //table_geometry->name = "table";
                //table_geometry->build_edges();
                //m_table_brush = make_unique<Brush>(
                //    Brush::Create_info{
                //        .geometry        = table_geometry,
                //        .build_info_set  = build_info_set(),
                //        .normal_style    = Normal_style::polygon_normals,
                //        .density         = 0.0f,
                //        .volume          = 0.0f,
                //        .collision_shape = table_box_shape
                //    }
                //);
            }
        );
    }

    constexpr bool anisotropic_test_object = false;

    if (config.gltf_files)
    {
#if !defined(ERHE_GLTF_LIBRARY_NONE)
        //execution_queue->enqueue(
        //    [this]()
            {
                ERHE_PROFILE_SCOPE("parse gltf files");

                //const Brush_create_context context{build_info_set(), Normal_style::polygon_normals};
                //constexpr bool instantiate = true;

                const char* files_names[] = {
                    "res/models/SM_Deccer_Cubes.gltf"
                    //"res/models/MetalRoughSpheresNoTextures.gltf"
                    //"res/models/Suzanne.gltf"
                };
                for (auto* path : files_names)
                {
                    parse_gltf(m_scene_root, build_info(), path);

                    //for (auto& geometry : geometries)
                    //{
                    //    geometry->compute_polygon_normals();
                    //    make_brush(instantiate, move(geometry), context);
                    //}
                }
            }
        //);
#endif
    }

    if (config.obj_files)
    {
        execution_queue->enqueue(
            [this, &config]()
            {
                ERHE_PROFILE_SCOPE("parse .obj files");

                const Brush_create_context context{
                    .build_info   = build_info(),
                    .normal_style = Normal_style::polygon_normals,
                    .density      = config.mass_scale
                };
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
                        make_brush(instantiate, geometry, context);
                    }
                }
            }
        );
    }

    if (config.platonic_solids)
    {
        execution_queue->enqueue(
            [this, &config]()
            {
                ERHE_PROFILE_SCOPE("Platonic solids");

                const Brush_create_context context{
                    .build_info   = build_info(),
                    .normal_style = Normal_style::polygon_normals,
                    .density      = config.mass_scale
                };
                constexpr bool instantiate = true;
                const auto scale = config.object_scale;

                make_brush(instantiate, make_dodecahedron (scale), context);
                make_brush(instantiate, make_icosahedron  (scale), context);
                make_brush(instantiate, make_octahedron   (scale), context);
                make_brush(instantiate, make_tetrahedron  (scale), context);
                make_brush(instantiate, make_cuboctahedron(scale), context);
                make_brush(
                    instantiate,
                    make_cube(scale),
                    context,
                    erhe::physics::ICollision_shape::create_box_shape_shared(
                        vec3{scale * 0.5f}
                    )
                );
            }
        );
    }

    if (config.sphere)
    {
        execution_queue->enqueue(
            [this, &config]()
            {
                ERHE_PROFILE_SCOPE("Sphere");

                //const Brush_create_context context{build_info_set(), Normal_style::polygon_normals};
                const Brush_create_context context{
                    .build_info   = build_info(),
                    .normal_style = Normal_style::corner_normals,
                    .density      = config.mass_scale
                };
                constexpr bool instantiate = true;

                make_brush(
                    instantiate,
                    make_sphere(
                        config.object_scale,
                        8 * std::max(1, config.detail), // slice count
                        6 * std::max(1, config.detail)  // stack count
                    ),
                    context,
                    erhe::physics::ICollision_shape::create_sphere_shape_shared(config.object_scale)
                );
            }
        );
    }

    if (config.torus)
    {
        execution_queue->enqueue(
            [this, &config]()
            {
                ERHE_PROFILE_SCOPE("Torus");

                //const Brush_create_context context{build_info_set(), Normal_style::polygon_normals};
                const Brush_create_context context{
                    .build_info   = build_info(),
                    .normal_style = Normal_style::corner_normals,
                    .density      = config.mass_scale
                };
                constexpr bool instantiate = true;

                const float major_radius = 1.0f  * config.object_scale;
                const float minor_radius = 0.25f * config.object_scale;

                auto torus_collision_volume_calculator = [=](float scale) -> float
                {
                    return torus_volume(major_radius * scale, minor_radius * scale);
                };

                auto torus_collision_shape_generator = [major_radius, minor_radius](float scale)
                -> std::shared_ptr<erhe::physics::ICollision_shape>
                {
                    ERHE_PROFILE_SCOPE("torus_collision_shape_generator");

                    erhe::physics::Compound_shape_create_info torus_shape_create_info;

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

                        torus_shape_create_info.children.emplace_back(
                            erhe::physics::Compound_child{
                                .shape = capsule,
                                .transform = erhe::physics::Transform{
                                    mat3{m},
                                    vec3{position}
                                }
                            }
                            //capsule,
                            //erhe::physics::Transform{
                            //    mat3{m},
                            //    vec3{position}
                            //}
                        );
                    }
                    return erhe::physics::ICollision_shape::create_compound_shape_shared(torus_shape_create_info);
                };

                make_brush(
                    instantiate,
                    std::make_shared<erhe::geometry::Geometry>(
                        make_torus(
                            major_radius,
                            minor_radius,
                            10 * std::max(1, config.detail),
                             8 * std::max(1, config.detail)
                        )
                    ),
                    context,
                    torus_collision_volume_calculator,
                    torus_collision_shape_generator
                );
            }
        );
    }

    if (config.cylinder)
    {
        execution_queue->enqueue(
            [this, &config]()
            {
                ERHE_PROFILE_SCOPE("Cylinder");

                const Brush_create_context context{
                    .build_info   = build_info(),
                    .normal_style = Normal_style::corner_normals, // Normal_style::polygon_normals
                    .density      = config.mass_scale
                };
                constexpr bool instantiate = true;
                const float scale = config.object_scale;
                auto cylinder_geometry = make_cylinder(
                    -1.0f * scale,
                     1.0f * scale,
                     1.0f * scale,
                    true,
                    true,
                    9 * std::max(1, config.detail),
                    std::max(1, config.detail)
                ); // always axis = x
                cylinder_geometry.transform(erhe::toolkit::mat4_swap_xy);

                make_brush(
                    instantiate,
                    std::move(cylinder_geometry),
                    context,
                    erhe::physics::ICollision_shape::create_cylinder_shape_shared(
                        erhe::physics::Axis::Y,
                        vec3{scale, scale, scale}
                    )
                );
            }
        );
    }

    if (config.cone)
    {
        execution_queue->enqueue(
            [this, &config]()
            {
                ERHE_PROFILE_SCOPE("Cone");

                const Brush_create_context context{
                    .build_info   = build_info(),
                    .normal_style = Normal_style::corner_normals,
                    .density      = config.mass_scale
                };
                constexpr bool instantiate = true;
                auto cone_geometry = make_cone( // always axis = x
                    -config.object_scale,             // min x
                    config.object_scale,              // max x
                    config.object_scale,              // bottom radius
                    true,                             // use bottm
                    10 * std::max(1, config.detail),  // slice count
                     5 * std::max(1, config.detail)   // stack count
                );
                cone_geometry.transform(erhe::toolkit::mat4_swap_xy); // convert to axis = y

                make_brush(
                    instantiate,
                    std::move(cone_geometry),
                    context
                    //erhe::physics::ICollision_shape::create_cone_shape_shared(
                    //    erhe::physics::Axis::Y,
                    //    object_scale,
                    //    2.0f * object_scale
                    //)
                );
            }
        );
    }

    if constexpr (anisotropic_test_object)
    {
        ERHE_PROFILE_SCOPE("test scene for anisotropic debugging");

        const auto& material_library  = m_scene_root->material_library();
        auto        aniso_material    = material_library->make_material("aniso", vec3{1.0f, 1.0f, 1.0f}, glm::vec2{0.8f, 0.2f}, 0.0f);
        const float ring_major_radius = 4.0f;
        const float ring_minor_radius = 0.55f; // 0.15f;
        auto        ring_geometry     = make_torus(
            ring_major_radius,
            ring_minor_radius,
            20 * std::max(1, config.detail),
             8 * std::max(1, config.detail)
        );
        ring_geometry.transform(erhe::toolkit::mat4_swap_xy);
        auto rotate_ring_pg = make_primitive(ring_geometry, build_info());
        const auto shared_geometry = std::make_shared<erhe::geometry::Geometry>(
            std::move(ring_geometry)
        );

        using erhe::scene::Transform;
        auto make_mesh_node =
        [
            &aniso_material,
            //&ring_geometry,
            &rotate_ring_pg,
            &shared_geometry,
            this
        ]
        (
            const char*      name,
            const Transform& transform
        )
        {
            auto mesh = std::make_shared<erhe::scene::Mesh>(name);
            mesh->mesh_data.primitives.push_back(
                erhe::primitive::Primitive{
                    .material              = aniso_material,
                    .gl_primitive_geometry = rotate_ring_pg,
                    .rt_primitive_geometry = {},
                    .rt_vertex_buffer      = {},
                    .rt_index_buffer       = {},
                    .source_geometry       = shared_geometry,
                    .normal_style          = erhe::primitive::Normal_style::point_normals
                }
            );
            mesh->set_visibility_mask (Node_visibility::visible | Node_visibility::content);
            mesh->set_parent_from_node(transform);

            const auto& layers = m_scene_root->layers();

            m_scene_root->scene().add_to_mesh_layer(
                *layers.content(),
                mesh
            );

        };

        make_mesh_node("X ring", Transform{} );
        make_mesh_node("Y ring", Transform::create_rotation( glm::pi<float>() / 2.0f, glm::vec3{0.0f, 0.0f, 1.0f}));
        make_mesh_node("Z ring", Transform::create_rotation(-glm::pi<float>() / 2.0f, glm::vec3{0.0f, 1.0f, 0.0f}));
    }

    Json_library library;//("res/polyhedra/johnson.json");
    if (config.johnson_solids)
    {
        // TODO When tasks can have dependencies we could queue this as well
        library = Json_library("res/polyhedra/johnson.json");
        {
            ERHE_PROFILE_SCOPE("Johnson solids");

            const Brush_create_context context{
                .build_info   = build_info(),
                .normal_style = Normal_style::polygon_normals,
                .density      = config.mass_scale
            };

            {
                ERHE_PROFILE_SCOPE("make brushes");

                for (const auto& key_name : library.names)
                {
                    execution_queue->enqueue(
                        [this, &library, &key_name, &context]()
                        {
                            constexpr bool instantiate = true;
                            auto geometry = library.make_geometry(key_name);
                            if (geometry.get_polygon_count() == 0)
                            {
                                return;
                            }
                            geometry.compute_polygon_normals();

                            make_brush(instantiate, std::move(geometry), context);
                        }
                    );
                }
            }
        }
    }

    execution_queue->wait();

    buffer_transfer_queue().flush();
}

auto Scene_builder::buffer_transfer_queue() -> erhe::graphics::Buffer_transfer_queue&
{
    Expects(m_mesh_memory->gl_buffer_transfer_queue);

    return *m_mesh_memory->gl_buffer_transfer_queue.get();
}

void Scene_builder::add_room()
{
    ERHE_PROFILE_FUNCTION

    if (!m_floor_brush)
    {
        return;
    }

    const auto& material_library = m_scene_root->material_library();
    const auto& layers           = m_scene_root->layers();

    auto floor_material = material_library->make_material(
        "Floor",
        //vec4{0.02f, 0.02f, 0.02f, 1.0f},
        vec4{0.01f, 0.01f, 0.01f, 1.0f},
        glm::vec2{0.9f, 0.9f},
        0.01f
    );
    //auto table_material = m_scene_root->make_material(
    //    "Table",
    //    vec4{0.2f, 0.2f, 0.2f, 1.0f},
    //    0.5f,
    //    0.8f
    //);

    // Notably shadow cast is not enabled for floor
    Instance_create_info floor_brush_instance_create_info
    {
        .node_visibility_flags = Node_visibility::visible | Node_visibility::content | Node_visibility::id,
        .scene_root            = m_scene_root.get(),
        .world_from_node       = erhe::toolkit::create_translation<float>(0.0f, -0.51f, 0.0f),
        .material              = floor_material,
        .scale                 = 1.0f
    };

    auto floor_instance = m_floor_brush->make_instance(
        floor_brush_instance_create_info
    );
    //auto table_instance = m_table_brush->make_instance(
    //    erhe::toolkit::create_translation<float>(0.0f, 0.5f, 0.0f),
    //    table_material,
    //    1.0f
    //);
    //floor_instance.mesh->visibility_mask() |=
    //    (Node_visibility::content     |
    //     //Node_visibility::shadow_cast |
    //     Node_visibility::id);

    m_scene_root->scene().add_to_mesh_layer(
        *layers.content(),
        floor_instance.mesh
    );

    //add_to_scene_layer(m_scene_root->scene(), m_scene_root->content_layer(), table_instance.mesh);
    if (floor_instance.node_physics)
    {
        add_to_physics_world(m_scene_root->physics_world(), floor_instance.node_physics);
    }
    //if (table_instance.node_physics)
    //{
    //    add_to_physics_world(m_scene_root->physics_world(), table_instance.node_physics);
    //}
    if (floor_instance.node_raytrace)
    {
        add_to_raytrace_scene(m_scene_root->raytrace_scene(), floor_instance.node_raytrace);
    }
    //if (table_instance.node_raytrace)
    //{
    //    add_to_raytrace_scene(m_scene_root->raytrace_scene(), table_instance.node_raytrace);
    //}
}

void Scene_builder::make_mesh_nodes()
{
    ERHE_PROFILE_FUNCTION

    const auto& config = get<erhe::application::Configuration>()->scene;

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

    const std::lock_guard<std::mutex> lock{m_scene_brushes_mutex};

    {
        ERHE_PROFILE_SCOPE("sort");

        std::sort(
            m_scene_brushes.begin(),
            m_scene_brushes.end(),
            [](
                const std::shared_ptr<Brush>& lhs,
                const std::shared_ptr<Brush>& rhs
            )
            {
                return lhs->name() < rhs->name();
            }
        );
    }

    std::vector<Pack_entry> pack_entries;

    {
        ERHE_PROFILE_SCOPE("emplace pack");

        for (const auto& brush : m_scene_brushes)
        {
            for (int i = 0; i < config.instance_count; ++i)
            {
                pack_entries.emplace_back(brush.get());
            }
        }
    }

    constexpr float bottom_y_pos = 0.01f;

    glm::ivec2 max_corner;
    {
        rbp::SkylineBinPack packer;
        const float gap = config.instance_gap;
        int group_width = 2;
        int group_depth = 2;
        ERHE_PROFILE_SCOPE("pack");
        for (;;)
        {
            ERHE_PROFILE_SCOPE("iteration");
            max_corner = glm::ivec2{0, 0};
            packer.Init(group_width, group_depth, false);

            bool pack_failed = false;
            for (auto& entry : pack_entries)
            {
                const auto* brush = entry.brush;
                const vec3  size  = brush->gl_primitive_geometry.bounding_box.diagonal();
                const int   width = static_cast<int>(256.0f * (size.x + gap));
                const int   depth = static_cast<int>(256.0f * (size.z + gap));
                entry.rectangle = packer.Insert(
                    width + 1,
                    depth + 1,
                    rbp::SkylineBinPack::LevelBottomLeft
                );
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

            //ERHE_VERIFY(group_width <= 16384);
        }
    }

    {
        ERHE_PROFILE_SCOPE("make instances");

        const auto& material_library = m_scene_root->material_library();
        const auto& materials        = material_library->materials();
        std::size_t material_index   = 0;

        for (auto& entry : pack_entries)
        {
            // TODO this will lock up if there are no visible materials
            do
            {
                material_index = (material_index + 1) % materials.size();
            }
            while (!materials.at(material_index)->visible);

            auto* brush = entry.brush;
            float x     = static_cast<float>(entry.rectangle.x) / 256.0f;
            float z     = static_cast<float>(entry.rectangle.y) / 256.0f;
                  x    += 0.5f * static_cast<float>(entry.rectangle.width ) / 256.0f;
                  z    += 0.5f * static_cast<float>(entry.rectangle.height) / 256.0f;
                  x    -= 0.5f * static_cast<float>(max_corner.x) / 256.0f;
                  z    -= 0.5f * static_cast<float>(max_corner.y) / 256.0f;
            float y     = bottom_y_pos - brush->gl_primitive_geometry.bounding_box.min.y;
            //x -= 0.5f * static_cast<float>(group_width);
            //z -= 0.5f * static_cast<float>(group_depth);
            //const auto& material = m_scene_root->materials().at(material_index);
            const Instance_create_info brush_instance_create_info
            {
                .node_visibility_flags = (
                    erhe::scene::Node_visibility::visible |
                    erhe::scene::Node_visibility::content |
                    erhe::scene::Node_visibility::id      |
                    erhe::scene::Node_visibility::shadow_cast
                ),
                .scene_root      = m_scene_root.get(),
                .world_from_node = erhe::toolkit::create_translation(x, y, z),
                .material        = materials.at(material_index),
                .scale           = 1.0f
            };
            auto instance = brush->make_instance(brush_instance_create_info);
            m_scene_root->add_instance(instance);

            m_scene_root->scene().sanity_check();
        }
    }
}

void Scene_builder::make_cube_benchmark()
{
    ERHE_PROFILE_FUNCTION

    m_scene_root->scene().sanity_check();

    const auto& material_library = m_scene_root->material_library();
    auto material = material_library->make_material("cube", vec3{1.0, 1.0f, 1.0f}, glm::vec2{0.3f, 0.4f}, 0.0f);
    auto cube     = make_cube(0.1f);
    auto cube_pg  = make_primitive(cube, build_info(), Normal_style::polygon_normals);

    constexpr float scale   = 0.5f;
    constexpr int   x_count = 20;
    constexpr int   y_count = 20;
    constexpr int   z_count = 20;

    const erhe::primitive::Primitive primitive{
        .material              = material,
        .gl_primitive_geometry = cube_pg
    };
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
                auto mesh = std::make_shared<erhe::scene::Mesh>("", primitive);
                mesh->set_world_from_node(erhe::toolkit::create_translation<float>(pos));
                m_scene_root->add(mesh);
                mesh->set_visibility_mask(
                    Node_visibility::content |
                    Node_visibility::shadow_cast
                );
            }
        }
    }

    m_scene_root->scene().sanity_check();
}

auto Scene_builder::make_directional_light(
    const std::string_view name,
    const vec3             position,
    const vec3             color,
    const float            intensity
) -> std::shared_ptr<Light>
{
    auto light = std::make_shared<Light>(name);
    light->type                          = Light::Type::directional;
    light->color                         = color;
    light->intensity                     = intensity;
    light->range                         =   0.0f;
    //light->projection()->projection_type = Projection::Type::orthogonal;
    //light->projection()->ortho_left      = -10.0f;
    //light->projection()->ortho_width     =  20.0f;
    //light->projection()->ortho_bottom    = -10.0f;
    //light->projection()->ortho_height    =  20.0f;
    //light->projection()->z_near          =   5.0f;
    //light->projection()->z_far           =  20.0f;

    const auto& layers = m_scene_root->layers();
    m_scene_root->scene().add_to_light_layer(
        *layers.light(),
        light
    );

    const mat4 m = erhe::toolkit::create_look_at(
        position,                // eye
        vec3{0.0f, 0.0f, 0.0f},  // center
        vec3{0.0f, 1.0f, 0.0f}   // up
    );
    light->set_parent_from_node(m);

    return light;
}

auto Scene_builder::make_spot_light(
    const std::string_view name,
    const vec3             position,
    const vec3             target,
    const vec3             color,
    const float            intensity,
    const vec2             spot_cone_angle
) -> std::shared_ptr<Light>
{
    auto light = std::make_shared<Light>(name);
    light->type             = Light::Type::spot;
    light->color            = color;
    light->intensity        = intensity;
    light->range            = 25.0f;
    light->inner_spot_angle = spot_cone_angle[0];
    light->outer_spot_angle = spot_cone_angle[1];
    const auto& layers = m_scene_root->layers();
    m_scene_root->scene().add_to_light_layer(
        *layers.light(),
        light
    );

    const mat4 m = erhe::toolkit::create_look_at(position, target, vec3{0.0f, 0.0f, 1.0f});
    light->set_parent_from_node(m);

    return light;
}

void Scene_builder::setup_lights()
{
    const auto& config = get<erhe::application::Configuration>()->scene;

    const auto& layers = m_scene_root->layers();
    layers.light()->ambient_light = vec4{0.042f, 0.044f, 0.049f, 0.0f};

    //make_directional_light(
    //    "Key",
    //    vec3{10.0f, 10.0f, 10.0f},
    //    vec3{1.0f, 0.9f, 0.8f},
    //    2.0f
    //);
    //make_directional_light(
    //    "Fill",
    //    vec3{-10.0f, 5.0f, -10.0f},
    //    vec3{0.8f, 0.9f, 1.0f},
    //    1.0f
    //);
    //make_spot_light(
    //    "Spot",
    //    vec3{0.0f, 1.0f, 0.0f}, // position
    //    vec3{0.0f, 0.0f, 0.0f}, // target
    //    vec3{0.0f, 1.0f, 0.0f}, // color
    //    10.0f,                  // intensity
    //    vec2{                   // cone angles
    //        glm::pi<float>() * 0.125f,
    //        glm::pi<float>() * 0.25f
    //    }
    //);

    for (int i = 0; i < config.directional_light_count; ++i)
    {
        const float rel = static_cast<float>(i) / static_cast<float>(config.directional_light_count);
        const float R   = config.directional_light_radius;
        const float h   = rel * 360.0f;
        const float s   = (config.directional_light_count > 1) ? 0.5f : 0.0f;
        const float v   = 1.0f;
        float r, g, b;

        erhe::toolkit::hsv_to_rgb(h, s, v, r, g, b);

        const vec3        color     = vec3{r, g, b};
        const float       intensity = config.directional_light_intensity / static_cast<float>(config.directional_light_count);
        const std::string name      = fmt::format("Directional light {}", i);
        const float       x_pos     = R * std::sin(rel * glm::two_pi<float>() + 1.0f / 7.0f);
        const float       z_pos     = R * std::cos(rel * glm::two_pi<float>() + 1.0f / 7.0f);
        const vec3        position  = vec3{x_pos, config.directional_light_height, z_pos};
        make_directional_light(name, position, color, intensity);
    }

    for (int i = 0; i < config.spot_light_count; ++i)
    {
        const float rel   = static_cast<float>(i) / static_cast<float>(config.spot_light_count);
        const float theta = rel * glm::two_pi<float>();
        const float R     = config.spot_light_radius;
        const float h     = rel * 360.0f;
        const float s     = (config.spot_light_count > 1) ? 0.9f : 0.0f;
        const float v     = 1.0f;
        float r, g, b;

        erhe::toolkit::hsv_to_rgb(h, s, v, r, g, b);

        const vec3        color           = vec3{r, g, b};
        const float       intensity       = config.spot_light_intensity;
        const std::string name            = fmt::format("Spot {}", i);
        const float       x_pos           = R * std::sin(theta);
        const float       z_pos           = R * std::cos(theta);
        const vec3        position        = vec3{x_pos, config.spot_light_height, z_pos};
        const vec3        target          = vec3{x_pos * 0.1f, 0.0f, z_pos * 0.1f};
        const vec2        spot_cone_angle = vec2{
            glm::pi<float>() / 5.0f,
            glm::pi<float>() / 4.0f
        };
        make_spot_light(name, position, target, color, intensity, spot_cone_angle);
    }
}

void Scene_builder::animate_lights(const double time_d)
{
    if (time_d >= 0.0)
    {
        return;
    }
    const float time        = static_cast<float>(time_d);
    const auto& layers      = m_scene_root->layers();
    const auto& light_layer = *layers.light();
    const auto& lights      = light_layer.lights;
    const int   n_lights    = static_cast<int>(lights.size());
    int         light_index = 0;

    for (auto i = lights.begin(); i != lights.end(); ++i)
    {
        const auto& l = *i;
        if (l->type == Light::Type::directional)
        {
            continue;
        }

        const float rel = static_cast<float>(light_index) / static_cast<float>(n_lights);
        const float t   = 0.5f * time + rel * glm::pi<float>() * 7.0f;
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

void Scene_builder::setup_scene()
{
    ERHE_PROFILE_FUNCTION

    setup_cameras();
    setup_lights();
    make_brushes();
    make_mesh_nodes();
    add_room();
    //make_cube_benchmark();
}

[[nodiscard]] auto Scene_builder::get_scene_root() const -> std::shared_ptr<Scene_root>
{
    return m_scene_root;
}

[[nodiscard]] auto Scene_builder::get_primary_viewport_window() const -> std::shared_ptr<Viewport_window>
{
    return m_primary_viewport_window;
}

} // namespace editor
