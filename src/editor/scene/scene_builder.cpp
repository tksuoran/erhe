#include "scene/scene_builder.hpp"

#include "editor_rendering.hpp"
#include "editor_scenes.hpp"
#include "task_queue.hpp"

#include "tools/brushes/brush.hpp"
#include "parsers/gltf.hpp"
#include "parsers/json_polyhedron.hpp"
#include "parsers/wavefront_obj.hpp"
#include "renderers/mesh_memory.hpp"
#include "scene/material_library.hpp"
#include "scene/scene_root.hpp"
#include "scene/viewport_window.hpp"
#include "scene/viewport_windows.hpp"
#include "windows/settings.hpp"

#include "SkylineBinPack.h" // RectangleBinPack

#include "erhe/configuration/configuration.hpp"
#include "erhe/imgui/imgui_windows.hpp"
#include "erhe/imgui/imgui_windows.hpp"
#include "erhe/rendergraph/rendergraph.hpp"
#include "erhe/geometry/shapes/box.hpp"
#include "erhe/geometry/shapes/cone.hpp"
#include "erhe/geometry/shapes/sphere.hpp"
#include "erhe/geometry/shapes/torus.hpp"
#include "erhe/geometry/shapes/regular_polyhedron.hpp"
#include "erhe/graphics/buffer_transfer_queue.hpp"
#include "erhe/primitive/primitive.hpp"
#include "erhe/primitive/primitive_builder.hpp"
#include "erhe/primitive/material.hpp"
#include "erhe/primitive/material.hpp"
#include "erhe/physics/icollision_shape.hpp"
#include "erhe/physics/iworld.hpp"
#include "erhe/raytrace/iscene.hpp"
#include "erhe/scene_renderer/shadow_renderer.hpp"
#include "erhe/scene/camera.hpp"
#include "erhe/scene/light.hpp"
#include "erhe/scene/mesh.hpp"
#include "erhe/scene/node.hpp"
#include "erhe/scene/scene.hpp"
#include "erhe/scene/transform.hpp"
#include "erhe/toolkit/math_util.hpp"
#include "erhe/toolkit/profile.hpp"
#include "erhe/toolkit/verify.hpp"

#include <fmt/format.h>

#include <glm/gtx/quaternion.hpp>
#include <glm/gtx/rotate_vector.hpp>

#define ERHE_ENABLE_SECOND_CAMERA 1

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
using erhe::scene::Light;
using erhe::scene::Node;
using erhe::scene::Projection;
using erhe::scene::Item_flags;
using erhe::geometry::shapes::make_box;
using erhe::primitive::Normal_style;
using glm::mat3;
using glm::mat4;
using glm::ivec3;
using glm::vec2;
using glm::vec3;
using glm::vec4;

constexpr bool global_instantiate = true;

Scene_builder::Config::Config()
{
    auto ini = erhe::configuration::get_ini("erhe.ini", "scene");
    ini->get("directional_light_intensity", directional_light_intensity);
    ini->get("directional_light_radius",    directional_light_radius);
    ini->get("directional_light_height",    directional_light_height);
    ini->get("directional_light_count",     directional_light_count);
    ini->get("spot_light_intensity",        spot_light_intensity);
    ini->get("spot_light_radius",           spot_light_radius);
    ini->get("spot_light_height",           spot_light_height);
    ini->get("spot_light_count",            spot_light_count);
    ini->get("floor_size",                  floor_size);
    ini->get("instance_count",              instance_count);
    ini->get("instance_gap",                instance_gap);
    ini->get("object_scale",                object_scale);
    ini->get("mass_scale",                  mass_scale);
    ini->get("detail",                      detail);
    ini->get("gltf_files",                  gltf_files);
    ini->get("obj_files",                   obj_files);
    ini->get("floor",                       floor);
    ini->get("sphere",                      sphere);
    ini->get("torus",                       torus);
    ini->get("cylinder",                    cylinder);
    ini->get("cone",                        cone);
    ini->get("platonic_solids",             platonic_solids);
    ini->get("johnson_solids",              johnson_solids);
}

Scene_builder::Scene_builder(
    erhe::graphics::Instance&              graphics_instance,
    erhe::imgui::Imgui_renderer&           imgui_renderer,
    erhe::imgui::Imgui_windows&            imgui_windows,
    erhe::rendergraph::Rendergraph&        rendergraph,
    erhe::scene::Scene_message_bus&        scene_message_bus,
    erhe::scene_renderer::Shadow_renderer& shadow_renderer,
    Editor_context&                        editor_context,
    Editor_rendering&                      editor_rendering,
    Editor_scenes&                         editor_scenes,
    Editor_settings&                       editor_settings,
    Mesh_memory&                           mesh_memory,
    Settings_window&                       settings_window,
    Tools&                                 tools,
    Viewport_config_window&                viewport_config_window,
    Viewport_windows&                      viewport_windows
)
    : m_context{editor_context}
{
    auto content_library = std::make_shared<Content_library>();
    add_default_materials(content_library->materials);

    m_scene_root = std::make_shared<Scene_root>(
        scene_message_bus,
        content_library,
        "Scene"
    );

    setup_cameras(
        graphics_instance,
        imgui_renderer,
        imgui_windows,
        rendergraph,
        shadow_renderer,
        editor_rendering,
        settings_window,
        tools,
        viewport_config_window,
        viewport_windows
    );
    setup_lights   ();
    make_brushes   (graphics_instance, editor_settings, mesh_memory);
    make_mesh_nodes();
    add_room       ();

    editor_scenes.register_scene_root(m_scene_root);
}

void Scene_builder::add_rendertarget_viewports(int count)
{
    static_cast<void>(count);
#if defined(ERHE_GUI_LIBRARY_IMGUI) && 0 //// TODO
    const auto& test_scene_root = get_scene_root();

    if (count >= 1) {
        auto rendertarget_node_1 = std::make_shared<erhe::scene::Node>("RT Node 1");
        auto rendertarget_mesh_1 = std::make_shared<Rendertarget_mesh>(
            1920,  // width
            1080,  // height
            2000.0f // pixels per meter
        );
        rendertarget_mesh_1->mesh_data.layer_id = Mesh_layer_id::rendertarget;
        rendertarget_node_1->attach(rendertarget_mesh_1);
        rendertarget_node_1->set_parent(test_scene_root->scene().get_root_node());

        rendertarget_node_1->set_world_from_node(
            erhe::toolkit::create_look_at(
                glm::vec3{-0.3f, 0.6f, -0.3f},
                glm::vec3{ 0.0f, 0.7f,  0.0f},
                glm::vec3{ 0.0f, 1.0f,  0.0f}
            )
        );

        auto imgui_viewport_1 = std::make_shared<editor::Rendertarget_imgui_viewport>(
            rendertarget_mesh_1.get(),
            "Rendertarget ImGui Viewport 1"
        );

        g_imgui_windows->register_imgui_viewport(imgui_viewport_1);

        g_grid_tool->set_viewport(imgui_viewport_1.get());
        g_grid_tool->show();

#if defined(ERHE_XR_LIBRARY_OPENXR)
        if (g_headset_view != nullptr)
        {
            g_headset_view->set_viewport(imgui_viewport_1.get());
        }
#endif
    }

    if (count >= 2) {
        const auto camera_b = make_camera(
            "Camera B",
            glm::vec3{-7.0f, 1.0f, 0.0f},
            glm::vec3{ 0.0f, 0.5f, 0.0f}
        );
        //auto* camera_node_b = camera_b->get_node();
        camera_b->set_wireframe_color(glm::vec4{ 0.3f, 0.6f, 1.00f, 1.0f });

        auto secondary_viewport_window = g_viewport_windows->create_viewport_window(
            "Secondary Viewport",
            test_scene_root,
            camera_b,
            2 // low MSAA
        );
        auto secondary_imgui_viewport_window = g_viewport_windows->create_imgui_viewport_window(
            secondary_viewport_window
        );

        auto rendertarget_node_2 = std::make_shared<erhe::scene::Node>("RT Node 2");
        auto rendertarget_mesh_2 = std::make_shared<Rendertarget_mesh>(
            1920,
            1080,
            2000.0f
        );
        rendertarget_node_2->attach(rendertarget_mesh_2);
        rendertarget_node_2->set_parent(test_scene_root->scene().get_root_node());

        rendertarget_node_2->set_world_from_node(
            erhe::toolkit::create_look_at(
                glm::vec3{0.3f, 0.6f, -0.3f},
                glm::vec3{0.0f, 0.7f,  0.0f},
                glm::vec3{0.0f, 1.0f,  0.0f}
            )
        );

        auto imgui_viewport_2 = std::make_shared<editor::Rendertarget_imgui_viewport>(
            rendertarget_mesh_2.get(),
            "Rendertarget ImGui Viewport 2"
        );
        g_imgui_windows->register_imgui_viewport(imgui_viewport_2);

        secondary_imgui_viewport_window->set_viewport(imgui_viewport_2.get());
        secondary_imgui_viewport_window->show();

        g_rendergraph->connect(
            erhe::rendergraph::Rendergraph_node_key::window,
            secondary_imgui_viewport_window,
            imgui_viewport_2
        );
    }
#endif
}

auto Scene_builder::make_camera(
    std::string_view name,
    vec3             position,
    vec3             look_at
) -> std::shared_ptr<erhe::scene::Camera>
{
    auto node   = std::make_shared<erhe::scene::Node>(fmt::format("{} node", name));
    auto camera = m_scene_root->content_library()->cameras.make(name);
    camera->projection()->fov_y           = glm::radians(35.0f);
    camera->projection()->projection_type = erhe::scene::Projection::Type::perspective_vertical;
    camera->projection()->z_near          = 0.03f;
    camera->projection()->z_far           = 80.0f;
    camera->enable_flag_bits(Item_flags::content | Item_flags::show_in_ui);
    node->attach(camera);
    node->set_parent(m_scene_root->scene().get_root_node());

    const mat4 m = erhe::toolkit::create_look_at(
        position, // eye
        look_at,  // center
        vec3{0.0f, 1.0f,  0.0f}  // up
    );
    node->set_parent_from_node(m);
    node->enable_flag_bits(Item_flags::content | Item_flags::show_in_ui);

    return camera;
}

void Scene_builder::setup_cameras(
    erhe::graphics::Instance&              graphics_instance,
    erhe::imgui::Imgui_renderer&           imgui_renderer,
    erhe::imgui::Imgui_windows&            imgui_windows,
    erhe::rendergraph::Rendergraph&        rendergraph,
    erhe::scene_renderer::Shadow_renderer& shadow_renderer,
    Editor_rendering&                      editor_rendering,
    Settings_window&                       settings_window,
    Tools&                                 tools,
    Viewport_config_window&                viewport_config_window,
    Viewport_windows&                      viewport_windows
)
{
    const auto& camera_a = make_camera(
        "Camera A",
        vec3{0.0f, 1.0f, 3.0f},
        vec3{0.0f, 0.5f, 0.0f}
    );
    camera_a->projection()->z_far = 64.0f;
    camera_a->set_wireframe_color(glm::vec4{1.0f, 0.6f, 0.3f, 1.0f});

#if defined(ERHE_ENABLE_SECOND_CAMERA)
    const auto& camera_b = make_camera(
        "Camera B",
        vec3{-7.0f, 1.0f, 0.0f},
        vec3{ 0.0f, 0.5f, 0.0f}
    );
    camera_b->set_wireframe_color(glm::vec4{0.3f, 0.6f, 1.00f, 1.0f});
#endif

    //// TODO Read these from ini
    const bool enable_post_processing = true;
    const bool window_viewport        = true;
    bool       window_show            = true;
    {
        auto ini = erhe::configuration::get_ini("erhe.ini", "window");
        ini->get("show", window_show);
    }

    if (!window_show) {
        return;
    }

    const int msaa_sample_count = settings_window.get_msaa_sample_count();
    m_primary_viewport_window = viewport_windows.create_viewport_window(
        graphics_instance,
        rendergraph,
        shadow_renderer,
        editor_rendering,
        tools,
        viewport_config_window,

        "Primary Viewport",
        m_scene_root,
        camera_a,
        std::max(
            2,
            msaa_sample_count
        ), //// TODO Fix rendergraph
        enable_post_processing
    );
    
    if (window_viewport) {
        //auto primary_imgui_viewport_window =
        viewport_windows.create_imgui_viewport_window(
            imgui_renderer,
            imgui_windows,
            rendergraph,
            m_primary_viewport_window
        );
    } else {
        viewport_windows.create_basic_viewport_window(
            rendergraph,
            m_primary_viewport_window
        );
    }
}

auto Scene_builder::make_brush(
    Brush_data&& brush_create_info,
    const bool   instantiate_to_scene
) -> std::shared_ptr<Brush>
{
    auto content_library = m_scene_root->content_library();
    const auto brush = content_library->brushes.make(brush_create_info);
    if (instantiate_to_scene) {
        const std::lock_guard<std::mutex> lock{m_scene_brushes_mutex};

        m_scene_brushes.push_back(brush);
    }
    return brush;
}

auto Scene_builder::make_brush(
    Editor_settings&           editor_settings,
    Mesh_memory&               mesh_memory,
    erhe::geometry::Geometry&& geometry,
    const bool                 instantiate_to_scene
) -> std::shared_ptr<Brush>
{
    return make_brush(
        Brush_data{
            .context         = m_context,
            .editor_settings = editor_settings,
            .build_info      = build_info(mesh_memory),
            .normal_style    = Normal_style::polygon_normals,
            .geometry        = std::make_shared<erhe::geometry::Geometry>(std::move(geometry)),
            .density         = config.mass_scale,
        },
        instantiate_to_scene
    );
}

auto Scene_builder::build_info(Mesh_memory& mesh_memory) -> erhe::primitive::Build_info
{
    return erhe::primitive::Build_info{
        .primitive_types = {
            .fill_triangles  = true,
            .edge_lines      = true,
            .corner_points   = true,
            .centroid_points = true
        },
        .buffer_info = mesh_memory.buffer_info
    };
}

auto Scene_builder::make_brush(
    Editor_settings&                                 editor_settings,
    Mesh_memory&                                     mesh_memory,
    const std::shared_ptr<erhe::geometry::Geometry>& geometry,
    const bool                                       instantiate_to_scene
) -> std::shared_ptr<Brush>
{
    return make_brush(
        Brush_data{
            .context         = m_context,
            .editor_settings = editor_settings,
            .build_info      = build_info(mesh_memory),
            .normal_style    = Normal_style::polygon_normals,
            .geometry        = geometry,
            .density         = config.mass_scale,
        },
        instantiate_to_scene
    );
}

void Scene_builder::make_brushes(
    erhe::graphics::Instance& graphics_instance, 
    Editor_settings&          editor_settings,
    Mesh_memory&              mesh_memory
)
{
    ERHE_PROFILE_FUNCTION();

    std::unique_ptr<ITask_queue> execution_queue;

    const bool parallel_initialization = false; //// TODO
    if (parallel_initialization) {
        const std::size_t thread_count = std::min(
            8U,
            std::max(std::thread::hardware_concurrency() - 0, 1U)
        );
        execution_queue = std::make_unique<Parallel_task_queue>("scene builder", thread_count);
    } else {
        execution_queue = std::make_unique<Serial_task_queue>();
    }

    // Floor
    if (config.floor) {
        auto floor_box_shape = erhe::physics::ICollision_shape::create_box_shape_shared(
            0.5f * vec3{config.floor_size, 1.0f, config.floor_size}
        );

        // Otherwise it will be destructed when leave add_floor() scope
        m_collision_shapes.push_back(floor_box_shape);

        execution_queue->enqueue(
            [this, &floor_box_shape, &editor_settings, &mesh_memory]() {
                ERHE_PROFILE_SCOPE("Floor brush");

                auto floor_geometry = std::make_shared<erhe::geometry::Geometry>(
                    make_box(config.floor_size, 1.0f, config.floor_size)
                );
                floor_geometry->name = "floor";
                floor_geometry->build_edges();

                m_floor_brush = std::make_unique<Brush>(
                    Brush_data{
                        .context         = m_context,
                        .editor_settings = editor_settings,
                        .build_info      = build_info(mesh_memory),
                        .normal_style    = Normal_style::polygon_normals,
                        .geometry        = floor_geometry,
                        .density         = 0.0f,
                        .volume          = 0.0f,
                        .collision_shape = floor_box_shape,
                    }
                );
            }
        );
    }

    constexpr bool anisotropic_test_object = false;

    if (config.gltf_files) {
#if !defined(ERHE_GLTF_LIBRARY_NONE)
        //execution_queue->enqueue(
        //    [this]()
            {
                ERHE_PROFILE_SCOPE("parse gltf files");

                import_gltf(
                    graphics_instance,
                    build_info(mesh_memory),
                    *m_scene_root.get(),
                    "res/assets/sample_models/SimpleSkin.gltf"
                );
            }
        //);
#endif
    }

    if (config.obj_files) {
        execution_queue->enqueue(
            [this, &editor_settings, &mesh_memory]() {
                ERHE_PROFILE_SCOPE("parse .obj files");

                constexpr bool instantiate = true;

                const char* obj_files_names[] = {
                    "res/models/cobra_mk3.obj"
                    //"res/models/teapot.obj",
                    //"res/models/teacup.obj",
                    //"res/models/spoon.obj"
                };
                for (auto* path : obj_files_names) {
                    auto geometries = parse_obj_geometry(path);

                    for (auto& geometry : geometries) {
                        geometry->compute_polygon_normals();
                        // The real teapot is ~33% taller (ratio 4:3)
                        //const mat4 scale_t = erhe::toolkit::create_scale(0.5f, 0.5f * 4.0f / 3.0f, 0.5f);
                        //geometry->transform(scale_t);

                        const mat4 scale_t = erhe::toolkit::create_scale(0.01f);
                        geometry->transform(scale_t);
                        geometry->flip_reversed_polygons();
                        make_brush(
                            Brush_data {
                                .context         = m_context,
                                .editor_settings = editor_settings,
                                .build_info      = build_info(mesh_memory),
                                .normal_style    = Normal_style::polygon_normals,
                                .geometry        = geometry,
                                .density         = config.mass_scale
                            },
                            instantiate
                        );
                    }
                }
            }
        );
    }

    if (config.platonic_solids) {
        execution_queue->enqueue(
            [this, &editor_settings, &mesh_memory]() {
                ERHE_PROFILE_SCOPE("Platonic solids");

                constexpr bool instantiate = global_instantiate;
                const auto scale = config.object_scale;

                make_brush(editor_settings, mesh_memory, make_dodecahedron (scale), instantiate);
                make_brush(editor_settings, mesh_memory, make_icosahedron  (scale), instantiate);
                make_brush(editor_settings, mesh_memory, make_octahedron   (scale), instantiate);
                make_brush(editor_settings, mesh_memory, make_tetrahedron  (scale), instantiate);
                make_brush(editor_settings, mesh_memory, make_cuboctahedron(scale), instantiate);
                make_brush(
                    Brush_data{
                        .context         = m_context,
                        .editor_settings = editor_settings,
                        .build_info      = build_info(mesh_memory),
                        .normal_style    = Normal_style::polygon_normals,
                        .geometry        = std::make_shared<erhe::geometry::Geometry>(make_cube(scale)),
                        .density         = config.mass_scale,
                        .collision_shape = erhe::physics::ICollision_shape::create_box_shape_shared(
                            vec3{scale * 0.5f}
                        )
                    },
                    instantiate
                );
            }
        );
    }

    if (config.sphere) {
        execution_queue->enqueue(
            [this, &editor_settings, &mesh_memory]() {
                ERHE_PROFILE_SCOPE("Sphere");
                constexpr bool instantiate = global_instantiate;

                make_brush(
                    Brush_data{
                        .context         = m_context,
                        .editor_settings = editor_settings,
                        .build_info      = build_info(mesh_memory),
                        .normal_style    = Normal_style::corner_normals,
                        .geometry        = std::make_shared<erhe::geometry::Geometry>(
                            make_sphere(
                                config.object_scale,
                                8 * std::max(1, config.detail), // slice count
                                6 * std::max(1, config.detail)  // stack count
                            )
                        ),
                        .density         = config.mass_scale,
                        .collision_shape = erhe::physics::ICollision_shape::create_sphere_shape_shared(
                            config.object_scale
                        )
                    },
                    instantiate
                );
            }
        );
    }

    if (config.torus) {
        execution_queue->enqueue(
            [this, &editor_settings, &mesh_memory]() {
                ERHE_PROFILE_SCOPE("Torus");

                constexpr bool instantiate = global_instantiate;

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
                    for (int i = 0; i < static_cast<int>(subdivisions); i++) {
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
                const auto torus_geometry = std::make_shared<erhe::geometry::Geometry>(
                    make_torus(
                        major_radius,
                        minor_radius,
                        10 * std::max(1, config.detail),
                        8 * std::max(1, config.detail)
                    )
                );
                make_brush(
                    Brush_data{
                        .context                     = m_context,
                        .editor_settings             = editor_settings,
                        .build_info                  = build_info(mesh_memory),
                        .normal_style                = Normal_style::corner_normals,
                        .geometry                    = torus_geometry,
                        .density                     = config.mass_scale,
                        .collision_volume_calculator = torus_collision_volume_calculator,
                        .collision_shape_generator   = torus_collision_shape_generator,
                    },
                    instantiate
                );

            }
        );
    }

    if (config.cylinder) {
        execution_queue->enqueue(
            [this, &editor_settings, &mesh_memory]() {
                ERHE_PROFILE_SCOPE("Cylinder");

                constexpr bool instantiate = global_instantiate;
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
                    Brush_data{
                        .context         = m_context,
                        .editor_settings = editor_settings,
                        .build_info      = build_info(mesh_memory),
                        .normal_style    = Normal_style::corner_normals,
                        .geometry        = std::make_shared<erhe::geometry::Geometry>(
                            std::move(cylinder_geometry)
                        ),
                        .density         = config.mass_scale,
                        .collision_shape = erhe::physics::ICollision_shape::create_cylinder_shape_shared(
                            erhe::physics::Axis::Y,
                            vec3{scale, scale, scale}
                        )
                    },
                    instantiate
                );
            }
        );
    }

    if (config.cone) {
        execution_queue->enqueue(
            [this, &editor_settings, &mesh_memory]() {
                ERHE_PROFILE_SCOPE("Cone");

                constexpr bool instantiate = global_instantiate;
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
                    Brush_data{
                        .context         = m_context,
                        .editor_settings = editor_settings,
                        .build_info      = build_info(mesh_memory),
                        .normal_style    = Normal_style::corner_normals,
                        .geometry        = std::make_shared<erhe::geometry::Geometry>(
                            std::move(cone_geometry)
                        ),
                        .density         = config.mass_scale
                        // Sadly, Jolt does not have cone shape
                        //erhe::physics::ICollision_shape::create_cone_shape_shared(
                        //    erhe::physics::Axis::Y,
                        //    object_scale,
                        //    2.0f * object_scale
                        //)
                    },
                    instantiate
                );
            }
        );
    }

    if constexpr (anisotropic_test_object) {
        ERHE_PROFILE_SCOPE("test scene for anisotropic debugging");

        auto&       material_library  = m_scene_root->content_library()->materials;
        auto        aniso_material    = material_library.make("aniso", vec3{1.0f, 1.0f, 1.0f}, glm::vec2{0.8f, 0.2f}, 0.0f);
        const float ring_major_radius = 4.0f;
        const float ring_minor_radius = 0.55f; // 0.15f;
        auto        ring_geometry     = make_torus(
            ring_major_radius,
            ring_minor_radius,
            20 * std::max(1, config.detail),
             8 * std::max(1, config.detail)
        );
        ring_geometry.transform(erhe::toolkit::mat4_swap_xy);
        auto rotate_ring_pg = make_primitive(ring_geometry, build_info(mesh_memory));
        const auto shared_geometry = std::make_shared<erhe::geometry::Geometry>(
            std::move(ring_geometry)
        );

        using erhe::scene::Transform;
        auto make_mesh_node =
        [
            &aniso_material,
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
            mesh->enable_flag_bits(
                Item_flags::visible |
                Item_flags::content |
                Item_flags::opaque
            );
            mesh->mesh_data.layer_id = m_scene_root->layers().content()->id;

            auto node = std::make_shared<erhe::scene::Node>(name);
            node->attach              (mesh);
            node->set_parent_from_node(transform);
            node->set_parent          (m_scene_root->scene().get_root_node());
        };

        using namespace erhe::toolkit;
        make_mesh_node("X ring", Transform{} );
        make_mesh_node("Y ring", Transform{create_rotation<float>( glm::pi<float>() / 2.0f, glm::vec3{0.0f, 0.0f, 1.0f})});
        make_mesh_node("Z ring", Transform{create_rotation<float>(-glm::pi<float>() / 2.0f, glm::vec3{0.0f, 1.0f, 0.0f})});
    }

    Json_library library;//("res/polyhedra/johnson.json");
    if (config.johnson_solids) {
        // TODO When tasks can have dependencies we could queue this as well
        ERHE_PROFILE_SCOPE("Johnson solids");

        library = Json_library("res/polyhedra/johnson.json");
        for (const auto& key_name : library.names) {
            execution_queue->enqueue(
                [this, &editor_settings, &mesh_memory, &library, &key_name]() {
                    auto geometry = library.make_geometry(key_name);
                    if (geometry.get_polygon_count() == 0) {
                        return;
                    }
                    geometry.compute_polygon_normals();

                    const auto shared_geometry = std::make_shared<erhe::geometry::Geometry>(
                        std::move(geometry)
                    );

                    make_brush(
                        Brush_data{
                            .context            = m_context,
                            .editor_settings    = editor_settings,
                            .name               = shared_geometry->name,
                            .build_info         = build_info(mesh_memory),
                            .normal_style       = Normal_style::polygon_normals,
                            .geometry_generator = [shared_geometry](){ return shared_geometry; },
                            .density            = config.mass_scale
                        },
                        false
                    );
                }
            );
        }
    }

    execution_queue->wait();

    mesh_memory.gl_buffer_transfer_queue.flush();
}

//auto Scene_builder::buffer_transfer_queue() -> erhe::graphics::Buffer_transfer_queue&
//{
//    return m_mesh_memory.gl_buffer_transfer_queue;
//}

void Scene_builder::add_room()
{
    ERHE_PROFILE_FUNCTION();

    if (!m_floor_brush) {
        return;
    }

    auto& material_library = m_scene_root->content_library()->materials;
    auto floor_material = material_library.make(
        "Floor",
        //vec4{0.02f, 0.02f, 0.02f, 1.0f},
        vec4{0.01f, 0.01f, 0.01f, 1.0f},
        glm::vec2{0.9f, 0.9f},
        0.01f
    );

    // Notably shadow cast is not enabled for floor
    Instance_create_info floor_brush_instance_create_info{
        .node_flags      = Item_flags::visible | Item_flags::content | Item_flags::show_in_ui | Item_flags::lock_viewport_selection | Item_flags::lock_viewport_transform,
        .mesh_flags      = Item_flags::visible | Item_flags::content | Item_flags::opaque | Item_flags::id | Item_flags::show_in_ui | Item_flags::lock_viewport_selection | Item_flags::lock_viewport_transform,
        .scene_root      = m_scene_root.get(),
        .world_from_node = erhe::toolkit::create_translation<float>(0.0f, -0.51f, 0.0f),
        .material        = floor_material,
        .scale           = 1.0f
    };

    auto floor_instance_node = m_floor_brush->make_instance(
        floor_brush_instance_create_info
    );

    floor_instance_node->set_parent(m_scene_root->scene().get_root_node());
}

void Scene_builder::make_mesh_nodes()
{
    ERHE_PROFILE_FUNCTION();

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
                return lhs->get_name() < rhs->get_name();
            }
        );
    }

    std::vector<Pack_entry> pack_entries;

    {
        ERHE_PROFILE_SCOPE("emplace pack");

        for (const auto& brush : m_scene_brushes) {
            for (int i = 0; i < config.instance_count; ++i) {
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
        for (;;) {
            ERHE_PROFILE_SCOPE("iteration");
            max_corner = glm::ivec2{0, 0};
            packer.Init(group_width, group_depth, false);

            bool pack_failed = false;
            for (auto& entry : pack_entries) {
                auto*      brush = entry.brush;
                const vec3 size  = brush->get_bounding_box().diagonal();
                const int  width = static_cast<int>(256.0f * (size.x + gap));
                const int  depth = static_cast<int>(256.0f * (size.z + gap));
                entry.rectangle = packer.Insert(
                    width + 1,
                    depth + 1,
                    rbp::SkylineBinPack::LevelBottomLeft
                );
                if (
                    (entry.rectangle.width  == 0) ||
                    (entry.rectangle.height == 0)
                ) {
                    pack_failed = true;
                    break;
                }
                max_corner.x = std::max(max_corner.x, entry.rectangle.x + entry.rectangle.width);
                max_corner.y = std::max(max_corner.y, entry.rectangle.y + entry.rectangle.height);
            }

            if (!pack_failed) {
                break;
            }

            if (group_width <= group_depth) {
                group_width *= 2;
            } else {
                group_depth *= 2;
            }

            //ERHE_VERIFY(group_width <= 16384);
        }
    }

    {
        ERHE_PROFILE_SCOPE("make instances");

        auto&       material_library = m_scene_root->content_library()->materials;
        const auto& materials        = material_library.entries();
        std::size_t material_index   = 0;

        ERHE_VERIFY(!materials.empty());

        for (auto& entry : pack_entries) {
            // TODO this will lock up if there are no visible materials
            do {
                material_index = (material_index + 1) % materials.size();
            }
            while (!materials.at(material_index)->is_shown_in_ui());

            auto* brush = entry.brush;
            float x     = static_cast<float>(entry.rectangle.x) / 256.0f;
            float z     = static_cast<float>(entry.rectangle.y) / 256.0f;
                  x    += 0.5f * static_cast<float>(entry.rectangle.width ) / 256.0f;
                  z    += 0.5f * static_cast<float>(entry.rectangle.height) / 256.0f;
                  x    -= 0.5f * static_cast<float>(max_corner.x) / 256.0f;
                  z    -= 0.5f * static_cast<float>(max_corner.y) / 256.0f;
            float y     = bottom_y_pos - brush->get_bounding_box().min.y;
            //x -= 0.5f * static_cast<float>(group_width);
            //z -= 0.5f * static_cast<float>(group_depth);
            //const auto& material = m_scene_root->materials().at(material_index);
            const Instance_create_info brush_instance_create_info
            {
                .node_flags =
                    Item_flags::show_in_ui |
                    Item_flags::visible    |
                    Item_flags::content,
                .mesh_flags =
                    Item_flags::show_in_ui |
                    Item_flags::visible    |
                    Item_flags::opaque     |
                    Item_flags::content    |
                    Item_flags::id         |
                    Item_flags::shadow_cast,
                .scene_root      = m_scene_root.get(),
                .world_from_node = erhe::toolkit::create_translation(x, y, z),
                .material        = materials.at(material_index),
                .scale           = 1.0f
            };
            auto instance_node = brush->make_instance(brush_instance_create_info);
            instance_node->set_parent(m_scene_root->scene().get_root_node());

            m_scene_root->scene().sanity_check();
        }
    }
}

void Scene_builder::make_cube_benchmark(Mesh_memory& mesh_memory)
{
    ERHE_PROFILE_FUNCTION();

    m_scene_root->scene().sanity_check();

    auto& material_library = m_scene_root->content_library()->materials;
    auto  material         = material_library.make("cube", vec3{1.0, 1.0f, 1.0f}, glm::vec2{0.3f, 0.4f}, 0.0f);
    auto  cube             = make_cube(0.1f);
    auto  cube_pg          = make_primitive(cube, build_info(mesh_memory), Normal_style::polygon_normals);

    constexpr float scale   = 0.5f;
    constexpr int   x_count = 20;
    constexpr int   y_count = 20;
    constexpr int   z_count = 20;

    const erhe::primitive::Primitive primitive{
        .material              = material,
        .gl_primitive_geometry = cube_pg
    };
    for (int i = 0; i < x_count; ++i) {
        const float x_rel = static_cast<float>(i) - static_cast<float>(x_count) * 0.5f;
        for (int j = 0; j < y_count; ++j) {
            const float y_rel = static_cast<float>(j);
            for (int k = 0; k < z_count; ++k) {
                const float z_rel = static_cast<float>(k) - static_cast<float>(z_count) * 0.5f;
                const vec3 pos{scale * x_rel, 1.0f + scale * y_rel, scale * z_rel};
                auto node = std::make_shared<erhe::scene::Node>();
                auto mesh = std::make_shared<erhe::scene::Mesh>("", primitive);
                mesh->mesh_data.layer_id = m_scene_root->layers().content()->id;
                mesh->enable_flag_bits(Item_flags::content | Item_flags::shadow_cast | Item_flags::opaque);
                node->attach(mesh);
                node->set_world_from_node(erhe::toolkit::create_translation<float>(pos));
                node->set_parent(m_scene_root->scene().get_root_node());
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
    auto node  = std::make_shared<erhe::scene::Node>(fmt::format("{} node", name));
    auto light = m_scene_root->content_library()->lights.make(name);
    light->type      = Light::Type::directional;
    light->color     = color;
    light->intensity = intensity;
    light->range     = 0.0f;
    light->layer_id  = m_scene_root->layers().light()->id;
    light->enable_flag_bits(Item_flags::content | Item_flags::visible | Item_flags::show_in_ui);
    node->attach          (light);
    node->set_parent      (m_scene_root->scene().get_root_node());
    node->enable_flag_bits(Item_flags::content | Item_flags::visible | Item_flags::show_in_ui);

    const mat4 m = erhe::toolkit::create_look_at(
        position,                // eye
        vec3{0.0f, 0.0f, 0.0f},  // center
        vec3{0.0f, 1.0f, 0.0f}   // up
    );
    node->set_parent_from_node(m);

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
    auto node  = std::make_shared<erhe::scene::Node>(fmt::format("{} node", name));
    auto light = m_scene_root->content_library()->lights.make(name);
    light->type             = Light::Type::spot;
    light->color            = color;
    light->intensity        = intensity;
    light->range            = 25.0f;
    light->inner_spot_angle = spot_cone_angle[0];
    light->outer_spot_angle = spot_cone_angle[1];
    light->layer_id         = m_scene_root->layers().light()->id;
    light->enable_flag_bits(Item_flags::content | Item_flags::visible | Item_flags::show_in_ui);
    node->attach          (light);
    node->set_parent      (m_scene_root->scene().get_root_node());
    node->enable_flag_bits(Item_flags::content | Item_flags::visible | Item_flags::show_in_ui);

    const mat4 m = erhe::toolkit::create_look_at(position, target, vec3{0.0f, 0.0f, 1.0f});
    node->set_parent_from_node(m);

    return light;
}

void Scene_builder::setup_lights()
{
    const auto& layers = m_scene_root->layers();
    layers.light()->ambient_light = vec4{0.042f, 0.044f, 0.049f, 0.0f};

#if 0
    make_directional_light(
        "X",
        vec3{1.0f, 0.0f, 0.0f},
        vec3{1.0f, 0.0f, 0.0f},
        2.0f
    );
    make_directional_light(
        "Y",
        vec3{0.0f, 1.0f, 0.0f},
        vec3{0.0f, 1.0f, 0.0f},
        2.0f
    );
    make_directional_light(
        "Z",
        vec3{0.0f, 0.0f, 1.0f},
        vec3{0.0f, 0.0f, 1.0f},
        2.0f
    );
#endif
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

    for (int i = 0; i < config.directional_light_count; ++i) {
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

    for (int i = 0; i < config.spot_light_count; ++i) {
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
    //if (time_d >= 0.0) {
    //    return;
    //}
    const float time        = static_cast<float>(time_d);
    const auto& layers      = m_scene_root->layers();
    const auto& light_layer = *layers.light();
    const auto& lights      = light_layer.lights;
    const int   n_lights    = static_cast<int>(lights.size());
    int         light_index = 0;

    for (auto i = lights.begin(); i != lights.end(); ++i) {
        const auto& l = *i;
        if (l->type == Light::Type::directional) {
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

        l->get_node()->set_parent_from_node(m);

        light_index++;
    }
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
