#include "scene/scene_builder.hpp"

#include "app_settings.hpp"

#include "brushes/brush.hpp"
#include "brushes/brush_placement.hpp"
#include "operations/ambient_light_operation.hpp"
#include "operations/compound_operation.hpp"
#include "operations/item_insert_remove_operation.hpp"
#include "operations/operation_stack.hpp"
#include "operations/scene_builder_floor_resources_operation.hpp"
#include "operations/scene_builder_viewport_resources_operation.hpp"

#include "erhe_physics/icollision_shape.hpp"
#include "erhe_primitive/material.hpp"

#include <algorithm>
#include "parsers/json_polyhedron.hpp"
#include "erhe_scene_renderer/mesh_memory.hpp"
#include "content_library/content_library.hpp"
#include "scene/scene_root.hpp"

#include "SkylineBinPack.h" // RectangleBinPack

#include "config/generated/add_cameras_args.hpp"
#include "config/generated/add_lights_args.hpp"
#include "config/generated/add_room_args.hpp"
#include "config/generated/scene_config.hpp"
#include "app_context.hpp"
#include "erhe_graphics/command_buffer.hpp"
#include "erhe_verify/verify.hpp"
#include "erhe_geometry/shapes/capsule.hpp"
#include "erhe_geometry/shapes/cone.hpp"
#include "erhe_geometry/shapes/sphere.hpp"
#include "erhe_geometry/shapes/torus.hpp"
#include "erhe_geometry/shapes/box.hpp"
#include "erhe_geometry/shapes/regular_polyhedron.hpp"
#include "erhe_graphics/buffer_transfer_queue.hpp"
#include "erhe_math/math_util.hpp"
#include "erhe_physics/icollision_shape.hpp"
#include "erhe_primitive/primitive.hpp"
#include "erhe_primitive/primitive_builder.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_scene/camera.hpp"
#include "erhe_scene/light.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_verify/verify.hpp"

#include <fmt/format.h>

#include <glm/gtx/quaternion.hpp>
#include <glm/gtx/rotate_vector.hpp>

#include <taskflow/taskflow.hpp>

using erhe::geometry::to_geo_mat4f;
using erhe::geometry::transform;

#define ERHE_ENABLE_SECOND_CAMERA 1

namespace editor {

using erhe::scene::Light;
using erhe::scene::Node;
using erhe::scene::Projection;
using erhe::Item_flags;
using erhe::primitive::Normal_style;
using glm::mat3;
using glm::mat4;
using glm::ivec3;
using glm::vec2;
using glm::vec3;
using glm::vec4;

Scene_builder::Scene_builder(
    const Scene_config&                scene_config,
    const bool                         enable_post_processing,
    std::shared_ptr<Content_library>   content_library,
    tf::Executor&                      executor,
    App_context&                       context,
    App_settings&                      app_settings,
    erhe::scene_renderer::Mesh_memory& mesh_memory
)
    : m_context                {context}
    , m_scene_config           {scene_config}
    , m_enable_post_processing{enable_post_processing}
{
    ERHE_PROFILE_FUNCTION();
    // The scene_root is assigned later via set_scene_root() (the scene.create
    // startup command builds it); only the content library -- into which the
    // brushes are built below -- is needed at construction time.
    m_content_library = content_library;

    ERHE_VERIFY(context.current_command_buffer != nullptr);

    // Default cameras / lights / floor instance are added later via the
    // undoable scene.* commands queued through Operation_stack, which is
    // not yet wired into App_context at construction time. The default
    // commands.json invokes them at startup (during the init-cb phase).
    //
    // Brushes, in contrast, are eagerly built here: they populate the
    // content library only (no scene instances, no Operation_stack
    // involvement), so they are safe to construct at this point. Which
    // brush families get built is gated by scene_config flags
    // (make_platonic_solid_brushes / make_johnson_solid_brushes /
    // make_curved_brushes) in editor_settings.json. mass_scale / detail
    // use the member defaults; ensure_brushes() becomes a no-op for any
    // later scene.add_* invocation because m_brushes_built is set here.
    make_brushes(app_settings, mesh_memory, executor);
    m_brushes_built = true;
    mesh_memory.flush(*context.current_command_buffer);
}

Scene_builder::~Scene_builder() noexcept
{
}

auto Scene_builder::get_content_library() const -> std::shared_ptr<Content_library>
{
    return m_content_library;
}

auto Scene_builder::make_camera(std::string_view name, vec3 position, vec3 look_at, float z_near, float z_far, float exposure, float shadow_range) -> std::shared_ptr<erhe::scene::Node>
{
    std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> scene_lock{m_scene_root->item_host_mutex};

    std::shared_ptr<erhe::scene::Node>   node   = std::make_shared<erhe::scene::Node>(name);
    std::shared_ptr<erhe::scene::Camera> camera = std::make_shared<erhe::scene::Camera>(name);
    camera->projection()->fov_y           = glm::radians(35.0f);
    camera->projection()->projection_type = erhe::scene::Projection::Type::perspective_vertical;
    camera->projection()->z_near          = z_near;
    camera->projection()->z_far           = z_far;
    camera->enable_flag_bits(Item_flags::content | Item_flags::show_in_ui | Item_flags::show_debug_visualizations);
    camera->set_exposure(exposure);
    camera->set_shadow_range(shadow_range);
    node->attach(camera);

    const mat4 m = erhe::math::create_look_at(
        position, // eye
        look_at,  // center
        vec3{0.0f, 1.0f,  0.0f}  // up
    );
    node->set_parent_from_node(m);
    node->enable_flag_bits(Item_flags::content | Item_flags::show_in_ui);

    return node;
}

void Scene_builder::add_cameras(const Add_cameras_args& args)
{
    const float camera_distance  = args.camera_distance;
    const float camera_elevation = args.camera_elevation;

    std::shared_ptr<erhe::scene::Node> camera_a_node = make_camera(
        "Camera A",
        vec3{0.0f, camera_elevation, camera_distance},
        vec3{0.0f, 0.25f, 0.0f},
        0.03f,
        64.0f,
        args.camera_exposure,
        args.shadow_range
    );

#if defined(ERHE_ENABLE_SECOND_CAMERA)
    std::shared_ptr<erhe::scene::Node> camera_b_node = make_camera(
        "Camera B",
        vec3{-7.0f, 1.0f, 0.0f},
        vec3{ 0.0f, 0.5f, 0.0f},
        0.01f,
        64.0f,
        args.camera_exposure,
        args.shadow_range
    );
#endif

    const std::shared_ptr<erhe::scene::Node>& root_node = m_scene_root->get_scene().get_root_node();
    std::vector<std::shared_ptr<Operation>> operations;
    operations.reserve(3);
    operations.push_back(
        std::make_shared<Item_insert_remove_operation>(
            Item_insert_remove_operation::Parameters{
                .context = m_context,
                .item    = camera_a_node,
                .parent  = root_node,
                .mode    = Item_insert_remove_operation::Mode::insert
            }
        )
    );
#if defined(ERHE_ENABLE_SECOND_CAMERA)
    operations.push_back(
        std::make_shared<Item_insert_remove_operation>(
            Item_insert_remove_operation::Parameters{
                .context = m_context,
                .item    = camera_b_node,
                .parent  = root_node,
                .mode    = Item_insert_remove_operation::Mode::insert
            }
        )
    );
#endif

    // The default desktop viewport is rendergraph + imgui plumbing tied
    // to camera_a, not a scene-graph insert. On OpenXR the headset path
    // renders the scene, so the desktop viewport is skipped. When the
    // viewport IS created, it goes through
    // Scene_builder_viewport_resources_operation so undo tears down the
    // viewport / viewport_window / post_processing_node / shadow_render
    // node together with the camera insertion.
    const bool create_default_viewport = !m_context.OpenXR && m_scene_config.imgui_window_scene_view;
    if (create_default_viewport) {
        std::shared_ptr<erhe::scene::Camera> camera_a;
        for (const std::shared_ptr<erhe::scene::Node_attachment>& attachment : camera_a_node->get_attachments()) {
            camera_a = std::dynamic_pointer_cast<erhe::scene::Camera>(attachment);
            if (camera_a) {
                break;
            }
        }
        ERHE_VERIFY(camera_a);

        operations.push_back(
            std::make_shared<Scene_builder_viewport_resources_operation>(
                Scene_builder_viewport_resources_operation::Parameters{
                    .scene_root             = m_scene_root,
                    .camera                 = camera_a,
                    .name                   = "Default Viewport",
                    .enable_post_processing = m_enable_post_processing
                }
            )
        );
    }

    m_context.operation_stack->queue(
        std::make_shared<Compound_operation>(
            Compound_operation::Parameters{.operations = std::move(operations)}
        )
    );
}

auto Scene_builder::make_brush(Content_library_node& folder, Brush_data&& brush_create_info) -> std::shared_ptr<Brush>
{
    // TODO This is very crude locking.
    //      We could be able to do more in parallel - check if we can do more fine grained locking.
    const std::shared_ptr<Content_library>& content_library = m_content_library;
    std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{content_library->mutex};

    return folder.make<Brush>(brush_create_info);
}

auto Scene_builder::build_info(erhe::scene_renderer::Mesh_memory& mesh_memory) -> erhe::primitive::Build_info
{
    return erhe::primitive::Build_info{
        .primitive_types = {
            .fill_triangles  = true,
            .fill_triangles_expanded = true,
            .edge_lines      = true,
            .corner_points   = true,
            .centroid_points = true
        },
        .buffer_info = mesh_memory.make_primitive_buffer_info()
    };
}

auto Scene_builder::make_brush(
    Content_library_node&                            folder,
    App_settings&                                    app_settings,
    erhe::scene_renderer::Mesh_memory&               mesh_memory,
    const std::shared_ptr<erhe::geometry::Geometry>& geometry
) -> std::shared_ptr<Brush>
{
    return make_brush(
        folder,
        Brush_data{
            .context      = m_context,
            .app_settings = app_settings,
            .build_info   = build_info(mesh_memory),
            .normal_style = Normal_style::polygon_normals,
            .geometry     = geometry,
            .density      = m_mass_scale,
        }
    );
}

void Scene_builder::make_platonic_solid_brushes(
    App_settings&                      app_settings,
    erhe::scene_renderer::Mesh_memory& mesh_memory
)
{
    ERHE_PROFILE_FUNCTION();

    Content_library_node& brushes = get_brushes();

    const auto scale = 1.0f;

    m_platonic_solids_folder = brushes.make_folder("Platonic Solids");
    auto& folder = *m_platonic_solids_folder.get();

    const uint64_t flags =
        erhe::geometry::Geometry::process_flag_connect |
        erhe::geometry::Geometry::process_flag_build_edges |
        erhe::geometry::Geometry::process_flag_compute_facet_centroids |
        erhe::geometry::Geometry::process_flag_compute_smooth_vertex_normals |
        erhe::geometry::Geometry::process_flag_generate_facet_texture_coordinates |
        erhe::geometry::Geometry::process_flag_generate_tangents;

    auto make_platonic_solid = [this, &folder, &app_settings, &mesh_memory, flags](const char* name, std::function<void(GEO::Mesh&)> builder)
    {
        auto new_geometry = std::make_shared<erhe::geometry::Geometry>(name);
        builder(new_geometry->get_mesh());
        new_geometry->process({.flags = flags});

        std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_brush_mutex};
        m_platonic_solids.push_back(make_brush(folder, app_settings, mesh_memory, new_geometry));
    };

    make_platonic_solid("dodecahedron",  [scale](GEO::Mesh& mesh){ erhe::geometry::shapes::make_dodecahedron (mesh, scale); });
    make_platonic_solid("icosahedron",   [scale](GEO::Mesh& mesh){ erhe::geometry::shapes::make_icosahedron  (mesh, scale); });
    make_platonic_solid("octahedron",    [scale](GEO::Mesh& mesh){ erhe::geometry::shapes::make_octahedron   (mesh, scale); });
    make_platonic_solid("cuboctahedron", [scale](GEO::Mesh& mesh){ erhe::geometry::shapes::make_cuboctahedron(mesh, scale); });
    make_platonic_solid("tetrahedron",   [scale](GEO::Mesh& mesh){ erhe::geometry::shapes::make_tetrahedron  (mesh, scale); });

    auto cube = std::make_shared<erhe::geometry::Geometry>("cube");
    erhe::geometry::shapes::make_cube(cube->get_mesh(), scale);
    cube->process({.flags = flags});
    m_platonic_solids.push_back(make_brush(
        folder,
        Brush_data{
            .context         = m_context,
            .app_settings    = app_settings,
            .build_info      = build_info(mesh_memory),
            .normal_style    = Normal_style::polygon_normals,
            .geometry        = cube,
            .density         = m_mass_scale,
            .collision_shape = erhe::physics::ICollision_shape::create_box_shape_shared(vec3{scale * 0.5f})
        }
    ));
}

void Scene_builder::make_sphere_brushes(
    App_settings&                      app_settings,
    erhe::scene_renderer::Mesh_memory& mesh_memory
)
{
    ERHE_PROFILE_FUNCTION();

    Content_library_node& brushes = get_brushes();
    std::shared_ptr<erhe::geometry::Geometry> sphere = std::make_shared<erhe::geometry::Geometry>("sphere");
    erhe::geometry::shapes::make_sphere(
        sphere->get_mesh(),
        1.0f, //config.object_scale,
        8 * std::max(1, m_detail), // slice count
        6 * std::max(1, m_detail)  // stack count
    );
    const uint64_t flags =
        erhe::geometry::Geometry::process_flag_connect |
        erhe::geometry::Geometry::process_flag_build_edges |
        erhe::geometry::Geometry::process_flag_generate_facet_texture_coordinates;
    sphere->process({.flags = flags});

    m_sphere_brush = make_brush(
        brushes,
        Brush_data{
            .context         = m_context,
            .app_settings    = app_settings,
            .build_info      = build_info(mesh_memory),
            .normal_style    = Normal_style::corner_normals,
            .geometry        = sphere,
            .density         = m_mass_scale,
            .collision_shape = erhe::physics::ICollision_shape::create_sphere_shape_shared(
                1.0f // config.object_scale
            )
        }
    );
}

void Scene_builder::make_torus_brushes(
    App_settings&                      app_settings,
    erhe::scene_renderer::Mesh_memory& mesh_memory
)
{
    ERHE_PROFILE_FUNCTION();

    Content_library_node& brushes = get_brushes();

    const float major_radius = 1.0f ; // * config.object_scale;
    const float minor_radius = 0.25f; // * config.object_scale;

    auto torus_collision_volume_calculator = [=](float scale) -> float {
        return erhe::math::torus_volume(major_radius * scale, minor_radius * scale);
    };

    auto torus_collision_shape_generator = [major_radius, minor_radius](float scale) -> std::shared_ptr<erhe::physics::ICollision_shape> {
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
                    .shape     = capsule,
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
    std::shared_ptr<erhe::geometry::Geometry> torus_geometry = std::make_shared<erhe::geometry::Geometry>("torus");
    erhe::geometry::shapes::make_torus(
        torus_geometry->get_mesh(),
        major_radius,
        minor_radius,
        10 * std::max(1, m_detail),
         8 * std::max(1, m_detail)
    );
    const uint64_t flags =
        erhe::geometry::Geometry::process_flag_connect |
        erhe::geometry::Geometry::process_flag_build_edges | 
        erhe::geometry::Geometry::process_flag_generate_facet_texture_coordinates;
    torus_geometry->process({.flags = flags});
    m_torus_brush = make_brush(
        brushes,
        Brush_data{
            .context                     = m_context,
            .app_settings                = app_settings,
            .build_info                  = build_info(mesh_memory),
            .normal_style                = Normal_style::corner_normals,
            .geometry                    = torus_geometry,
            .density                     = m_mass_scale,
            .collision_volume_calculator = torus_collision_volume_calculator,
            .collision_shape_generator   = torus_collision_shape_generator,
        }
    );
}

void Scene_builder::make_cylinder_brushes(
    App_settings&                      app_settings, 
    erhe::scene_renderer::Mesh_memory& mesh_memory
)
{
    ERHE_PROFILE_FUNCTION();

    Content_library_node& brushes = get_brushes();

    const float scale = 1.0f; //config.object_scale;
    std::size_t index = 0;
    for (float h = 0.1f; h < 1.1f; h += 0.9f) {
        std::shared_ptr<erhe::geometry::Geometry> cylinder_geometry = std::make_shared<erhe::geometry::Geometry>("cylinder");
        erhe::geometry::shapes::make_cylinder(
            cylinder_geometry->get_mesh(),
            -h * scale,
             h * scale,
            1.0f * scale,
            true,
            true,
            9 * std::max(1, m_detail), // slice count
            1 * std::max(1, m_detail)  // stack count
        ); // always axis = x
        transform(*cylinder_geometry.get(), *cylinder_geometry.get(), to_geo_mat4f(erhe::math::mat4_swap_xy));
        const uint64_t flags =
            erhe::geometry::Geometry::process_flag_connect |
            erhe::geometry::Geometry::process_flag_build_edges |
            erhe::geometry::Geometry::process_flag_generate_facet_texture_coordinates;
        cylinder_geometry->process({.flags = flags});
        m_cylinder_brush[index++] = make_brush(
            brushes,
            Brush_data{
                .context         = m_context,
                .app_settings    = app_settings,
                .build_info      = build_info(mesh_memory),
                .normal_style    = Normal_style::corner_normals,
                .geometry        = cylinder_geometry,
                .density         = m_mass_scale,
                .collision_shape = erhe::physics::ICollision_shape::create_cylinder_shape_shared(
                    erhe::physics::Axis::Y,
                    vec3{h * scale, scale, h * scale}
                )
            }
        );
    }
}

void Scene_builder::make_cone_brushes(
    App_settings&                      app_settings, 
    erhe::scene_renderer::Mesh_memory& mesh_memory
)
{
    ERHE_PROFILE_FUNCTION();

    Content_library_node& brushes = get_brushes();

    std::shared_ptr<erhe::geometry::Geometry> cone_geometry = std::make_shared<erhe::geometry::Geometry>("cone");
    erhe::geometry::shapes::make_cone( // always axis = x
        cone_geometry->get_mesh(),
        -1.0f, // * config.object_scale,    // min x
         1.0f, // * config.object_scale,    // max x
         1.0f, // * config.object_scale,    // bottom radius
        true,                               // use bottm
        10 * std::max(1, m_detail),         // slice count
         5 * std::max(1, m_detail)          // stack count
    );
    transform(*cone_geometry.get(), *cone_geometry.get(), to_geo_mat4f(erhe::math::mat4_swap_xy)); // convert to axis = y
    const uint64_t flags =
        erhe::geometry::Geometry::process_flag_connect |
        erhe::geometry::Geometry::process_flag_build_edges |
        erhe::geometry::Geometry::process_flag_generate_facet_texture_coordinates;
    cone_geometry->process({.flags = flags});

    m_cone_brush = make_brush(
        brushes,
        Brush_data{
            .context         = m_context,
            .app_settings    = app_settings,
            .build_info      = build_info(mesh_memory),
            .normal_style    = Normal_style::corner_normals,
            .geometry        = cone_geometry,
            .density         = m_mass_scale
            // Sadly, Jolt does not have cone shape
            //erhe::physics::ICollision_shape::create_cone_shape_shared(
            //    erhe::physics::Axis::Y,
            //    object_scale,
            //    2.0f * object_scale
            //)
        }
    );
}

void Scene_builder::make_capsule_brushes(
    App_settings&                      app_settings,
    erhe::scene_renderer::Mesh_memory& mesh_memory
)
{
    ERHE_PROFILE_FUNCTION();

    Content_library_node& brushes = get_brushes();

    const float radius = 1.0f;
    const float length = 2.0f; // cylinder mid-section length; total height = length + 2 * radius

    std::shared_ptr<erhe::geometry::Geometry> capsule_geometry = std::make_shared<erhe::geometry::Geometry>("capsule");
    erhe::geometry::shapes::make_capsule( // axis = y
        capsule_geometry->get_mesh(),
        radius,
        length,
        8 * std::max(1, m_detail), // slice count
        3 * std::max(1, m_detail)  // hemisphere stack count
    );
    const uint64_t flags =
        erhe::geometry::Geometry::process_flag_connect |
        erhe::geometry::Geometry::process_flag_build_edges |
        erhe::geometry::Geometry::process_flag_generate_facet_texture_coordinates;
    capsule_geometry->process({.flags = flags});

    m_capsule_brush = make_brush(
        brushes,
        Brush_data{
            .context         = m_context,
            .app_settings    = app_settings,
            .build_info      = build_info(mesh_memory),
            .normal_style    = Normal_style::corner_normals,
            .geometry        = capsule_geometry,
            .density         = m_mass_scale,
            .collision_shape = erhe::physics::ICollision_shape::create_capsule_shape_shared(
                erhe::physics::Axis::Y,
                radius,
                length
            )
        }
    );
}

void Scene_builder::make_json_brushes(
    App_settings&                      app_settings,
    erhe::scene_renderer::Mesh_memory& mesh_memory,
    tf::Taskflow*                      tf,
    Json_library&                      library
)
{
    ERHE_PROFILE_FUNCTION();

    Content_library_node& brushes = get_brushes();

    static constexpr uint64_t process_flags =
        erhe::geometry::Geometry::process_flag_connect |
        erhe::geometry::Geometry::process_flag_build_edges |
        erhe::geometry::Geometry::process_flag_compute_facet_centroids |
        erhe::geometry::Geometry::process_flag_compute_smooth_vertex_normals |
        erhe::geometry::Geometry::process_flag_generate_facet_texture_coordinates |
        erhe::geometry::Geometry::process_flag_generate_tangents;

    m_johnson_solids_folder = brushes.make_folder("Johnson Solids");
    auto& folder = *m_johnson_solids_folder.get();

    for (const auto& key_name : library.names) {
        auto op = [this, &app_settings, &mesh_memory, &library, &key_name, &folder]() {
            std::shared_ptr<erhe::geometry::Geometry> geometry = std::make_shared<erhe::geometry::Geometry>(key_name);
            const bool ok = library.make_geometry(*geometry.get(), key_name);
            if (!ok || (geometry->get_mesh().facets.nb() == 0)) {
                return;
            }
            geometry->process({
                .flags = process_flags,
                .facet_texcoord_usage_index = 1,
                .tangent_texcoord_usage_index = 1
            });

            auto brush = make_brush(
                folder,
                Brush_data{
                    .context      = m_context,
                    .app_settings = app_settings,
                    .name         = "", //// TODO shared_geometry->name,
                    .build_info   = build_info(mesh_memory),
                    .normal_style = Normal_style::polygon_normals,
                    .geometry     = geometry,
                    .density      = m_mass_scale
                }
            );

            std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_brush_mutex};
            m_johnson_solids.push_back(brush);
        };
        if (tf != nullptr) {
            tf->emplace(op);
        } else {
            op();
        }
    }
}

auto Scene_builder::get_brushes() -> Content_library_node&
{
    auto content_library = m_content_library;
    return *(content_library->brushes.get());
}

void Scene_builder::make_brushes(
    App_settings&                      app_settings,
    erhe::scene_renderer::Mesh_memory& mesh_memory,
    tf::Executor&                      executor
)
{
    ERHE_PROFILE_FUNCTION();

    const bool  make_johnson_solid_brushes   = m_scene_config.make_johnson_solid_brushes;
    const bool  make_platonic_solid_brushes_ = m_scene_config.make_platonic_solid_brushes;
    const bool  make_curved_brushes          = m_scene_config.make_curved_brushes;
    // Floor brush construction has moved into Scene_builder::add_room so
    // the floor_size / floor_height / floor args from scene.add_room can
    // drive it.

    Json_library library{"res/editor/polyhedra/johnson.json"};
    if (executor.num_workers() > 1) {
        tf::Taskflow tf;
        if (make_platonic_solid_brushes_) {
            tf.emplace([this, &app_settings, &mesh_memory]() { make_platonic_solid_brushes(app_settings, mesh_memory); }).name("Platonic Solid Brushes");
        }
        if (make_curved_brushes) {
            tf.emplace([this, &app_settings, &mesh_memory]() { make_sphere_brushes        (app_settings, mesh_memory); }).name("Sphere Brushes");
            tf.emplace([this, &app_settings, &mesh_memory]() { make_torus_brushes         (app_settings, mesh_memory); }).name("Torus Brushes");
            tf.emplace([this, &app_settings, &mesh_memory]() { make_cylinder_brushes      (app_settings, mesh_memory); }).name("Cylinder Brushes");
            tf.emplace([this, &app_settings, &mesh_memory]() { make_cone_brushes          (app_settings, mesh_memory); }).name("Cone Brushes");
            tf.emplace([this, &app_settings, &mesh_memory]() { make_capsule_brushes       (app_settings, mesh_memory); }).name("Capsule Brushes");
        }
        if (make_johnson_solid_brushes) {
            make_json_brushes(app_settings, mesh_memory, &tf, library);
        }

        tf::Future<void> future = executor.run(tf);
        future.wait();
    } else {
        if (make_platonic_solid_brushes_) {
            make_platonic_solid_brushes(app_settings, mesh_memory);
        }
        if (make_curved_brushes) {
            make_sphere_brushes        (app_settings, mesh_memory);
            make_torus_brushes         (app_settings, mesh_memory);
            make_cylinder_brushes      (app_settings, mesh_memory);
            make_cone_brushes          (app_settings, mesh_memory);
            make_capsule_brushes       (app_settings, mesh_memory);
        }
        if (make_johnson_solid_brushes) {
            make_json_brushes(app_settings, mesh_memory, nullptr, library);
        }
    }

    auto sort_folder = [](Content_library_node& folder)
    {
        std::vector<std::shared_ptr<erhe::Hierarchy>>& entries = folder.get_mutable_children();

        std::sort(
            entries.begin(),
            entries.end(),
            [](const std::shared_ptr<erhe::Hierarchy>& lhs, const std::shared_ptr<erhe::Hierarchy>& rhs)
            {
                return lhs->get_name() < rhs->get_name();
            }
        );
    };

    if (make_platonic_solid_brushes_) {
        sort_folder(*m_platonic_solids_folder.get());
    }
    if (make_johnson_solid_brushes) {
        sort_folder(*m_johnson_solids_folder.get());
        m_johnson_solids_folder->disable_flag_bits(erhe::Item_flags::expand);
    }

    ERHE_VERIFY(m_context.current_command_buffer != nullptr);
    mesh_memory.flush(*m_context.current_command_buffer);
}

void Scene_builder::add_room(const Add_room_args& args)
{
    ERHE_PROFILE_FUNCTION();

    if (!args.floor) {
        return;
    }

    const float floor_size   = args.floor_size;
    const float floor_height = args.floor_height;

    // Build a fresh floor brush from the args. The brush, its collision
    // shape and the floor material are tracked as side resources of the
    // add_room operation: the Scene_builder_floor_resources_operation
    // below pushes them into m_floor_brushes / m_collision_shapes /
    // content_library.materials inside its execute(), and pops them on
    // undo so that an undo of add_room does not leak the brush.
    std::shared_ptr<erhe::physics::ICollision_shape> floor_box_shape =
        erhe::physics::ICollision_shape::create_box_shape_shared(
            0.5f * vec3{floor_size, std::max(floor_height, 0.10f), floor_size}
        );

    std::shared_ptr<erhe::geometry::Geometry> floor_geometry = std::make_shared<erhe::geometry::Geometry>("floor");
    erhe::geometry::shapes::make_box(floor_geometry->get_mesh(), floor_size, floor_height, floor_size);
    const uint64_t flags =
        erhe::geometry::Geometry::process_flag_connect |
        erhe::geometry::Geometry::process_flag_build_edges |
        erhe::geometry::Geometry::process_flag_compute_smooth_vertex_normals |
        erhe::geometry::Geometry::process_flag_generate_facet_texture_coordinates;
    floor_geometry->process({.flags = flags});

    ERHE_VERIFY(m_context.app_settings != nullptr);
    ERHE_VERIFY(m_context.mesh_memory  != nullptr);
    std::shared_ptr<Brush> floor_brush = std::make_shared<Brush>(
        Brush_data{
            .context         = m_context,
            .app_settings    = *m_context.app_settings,
            .build_info      = build_info(*m_context.mesh_memory),
            .normal_style    = Normal_style::polygon_normals,
            .geometry        = floor_geometry,
            .density         = 0.0f,
            .volume          = 0.0f,
            .collision_shape = floor_box_shape,
        }
    );

    // Construct the floor material without inserting it into the
    // content library; the resources operation will install it on
    // execute() and remove it on undo().
    std::shared_ptr<erhe::primitive::Material> floor_material =
        std::make_shared<erhe::primitive::Material>(
            erhe::primitive::Material_create_info{
                .name = "Floor",
                .data = {
                    .base_color = glm::vec3{0.07f, 0.07f, 0.07f},
                    //.base_color = glm::vec3{0.27f, 0.27f, 0.27f}, // temp for shadow debug with single directional light source
                    .roughness  = glm::vec2{0.9f, 0.9f},
                    .metallic   = 0.01f // TODO 0.0f ?
                }
            }
        );

    std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> scene_lock{m_scene_root->item_host_mutex};

    // Notably shadow cast is not enabled for floor
    erhe::math::Aabb aabb = floor_brush->get_bounding_box();

    Instance_create_info floor_brush_instance_create_info{
        .node_flags      = Item_flags::visible | Item_flags::content | Item_flags::show_in_ui | Item_flags::lock_viewport_selection | Item_flags::lock_viewport_transform | Item_flags::expand,
        .mesh_flags      = Item_flags::visible | Item_flags::content | Item_flags::id | Item_flags::show_in_ui | Item_flags::lock_viewport_selection | Item_flags::lock_viewport_transform | Item_flags::lock_edit, // | Item_flags::shadow_cast,
        .scene_root      = m_scene_root.get(),
        .world_from_node = erhe::math::create_translation<float>(0.0f, -aabb.max.y - 0.001f, 0.0f),
        .material        = floor_material,
        .scale           = 1.0f,
        .motion_mode     = erhe::physics::Motion_mode::e_static
    };

    std::shared_ptr<erhe::scene::Node> floor_instance_node = floor_brush->make_instance(
        floor_brush_instance_create_info
    );
    floor_instance_node->set_lock_edit(true);

    std::vector<std::shared_ptr<Operation>> operations;
    // Register the brush / collision shape / material first; the node
    // insert then references the now-installed material. Undo runs in
    // reverse order, removing the node before the resources, so the
    // teardown order matches the build order.
    operations.push_back(
        std::make_shared<Scene_builder_floor_resources_operation>(
            Scene_builder_floor_resources_operation::Parameters{
                .builder         = this,
                .brush           = floor_brush,
                .collision_shape = floor_box_shape,
                .material        = floor_material
            }
        )
    );
    operations.push_back(
        std::make_shared<Item_insert_remove_operation>(
            Item_insert_remove_operation::Parameters{
                .context = m_context,
                .item    = floor_instance_node,
                .parent  = m_scene_root->get_scene().get_root_node(),
                .mode    = Item_insert_remove_operation::Mode::insert
            }
        )
    );

    m_context.operation_stack->queue(
        std::make_shared<Compound_operation>(
            Compound_operation::Parameters{.operations = std::move(operations)}
        )
    );
}

void Scene_builder::ensure_brushes(const float mass_scale, const int detail)
{
    if (m_brushes_built) {
        return;
    }
    m_mass_scale    = mass_scale;
    m_detail        = detail;
    m_brushes_built = true;

    ERHE_VERIFY(m_context.app_settings != nullptr);
    ERHE_VERIFY(m_context.mesh_memory  != nullptr);
    ERHE_VERIFY(m_context.executor     != nullptr);
    make_brushes(*m_context.app_settings, *m_context.mesh_memory, *m_context.executor);
    if (m_context.current_command_buffer != nullptr) {
        m_context.mesh_memory->flush(*m_context.current_command_buffer);
    }
}

void Scene_builder::add_platonic_solids(const Make_mesh_config& config)
{
    ensure_brushes(config.mass_scale, config.detail);
    make_mesh_nodes(config, m_platonic_solids);
}

void Scene_builder::add_johnson_solids(const Make_mesh_config& config)
{
    ensure_brushes(config.mass_scale, config.detail);
    make_mesh_nodes(config, m_johnson_solids);
}

void Scene_builder::add_curved_shapes(const Make_mesh_config& config)
{
    ensure_brushes(config.mass_scale, config.detail);
    std::vector<std::shared_ptr<Brush>> brushes;
    brushes.push_back(m_sphere_brush);
    brushes.push_back(m_cylinder_brush[0]);
    brushes.push_back(m_cylinder_brush[1]);
    brushes.push_back(m_cone_brush);
    brushes.push_back(m_capsule_brush);
    brushes.push_back(m_torus_brush);
    make_mesh_nodes(config, brushes);
}

void Scene_builder::add_torus_chain(const Make_mesh_config& config, bool connected)
{
    ensure_brushes(config.mass_scale, config.detail);
    std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> scene_lock{m_scene_root->item_host_mutex};

    const float major_radius = 1.0f  * config.object_scale;
    const float minor_radius = 0.25f * config.object_scale;

    auto&       material_library = m_content_library->materials;
    const auto& materials        = material_library->get_all<erhe::primitive::Material>();
    std::size_t material_index   = 0;

    float x = 0.0f;
    float y = major_radius + minor_radius;
    float z = 0.0f;

    float x_stride = connected
        ? major_radius - 2.0f * minor_radius + major_radius
        : 3 * major_radius;
    glm::mat4 alternate_rotate[2] = {
        glm::mat4{1.0f},
        erhe::math::create_rotation<float>(glm::pi<float>() / 2.0f, glm::vec3{1.0f, 0.0f, 0.0f})
    };

    const std::shared_ptr<erhe::scene::Node>& root_node = m_scene_root->get_scene().get_root_node();
    std::vector<std::shared_ptr<Operation>> operations;
    operations.reserve(config.instance_count);

    const std::shared_ptr<Brush>& brush = m_torus_brush;
    for (int i = 0; i < config.instance_count; ++i) {
        const Instance_create_info brush_instance_create_info{
            .node_flags      = Item_flags::show_in_ui | Item_flags::visible | Item_flags::content,
            .mesh_flags      = Item_flags::show_in_ui | Item_flags::visible | Item_flags::content | Item_flags::id | Item_flags::shadow_cast,
            .scene_root      = m_scene_root.get(),
            .world_from_node = erhe::math::create_translation(x, y, z) * alternate_rotate[connected ? (i & 1) : 0],
            .material        = config.material ? config.material : materials.at(material_index),
            .scale           = config.object_scale
        };
        std::shared_ptr<erhe::scene::Node> instance_node = brush->make_instance(brush_instance_create_info);
        instance_node->set_name(fmt::format("{}.{}", instance_node->get_name(), i + 1));
        operations.push_back(
            std::make_shared<Item_insert_remove_operation>(
                Item_insert_remove_operation::Parameters{
                    .context = m_context,
                    .item    = instance_node,
                    .parent  = root_node,
                    .mode    = Item_insert_remove_operation::Mode::insert
                }
            )
        );
        x = x + x_stride;
    }

    if (!operations.empty()) {
        m_context.operation_stack->queue(
            std::make_shared<Compound_operation>(
                Compound_operation::Parameters{.operations = std::move(operations)}
            )
        );
    }
}

void Scene_builder::make_mesh_nodes(const Make_mesh_config& config, std::vector<std::shared_ptr<Brush>>& brushes)
{
    ERHE_PROFILE_FUNCTION();

#if !defined(NDEBUG)
    m_scene_root->get_scene().sanity_check();
#endif

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
        int       instance_number{0};
    };

    std::sort(
        brushes.begin(),
        brushes.end(),
        [](const std::shared_ptr<Brush>& lhs, const std::shared_ptr<Brush>& rhs){
            return lhs->get_name() < rhs->get_name();
        }
    );

    std::vector<Pack_entry> pack_entries;

    // TODO Orient brushes using a specificit face - largest maybe?

    for (const auto& brush : brushes) {
        for (int i = 0; i < config.instance_count; ++i) {
            const erhe::math::Aabb& bounding_box = brush->get_bounding_box();
            ERHE_VERIFY(bounding_box.is_valid());
            pack_entries.emplace_back(brush.get());
            pack_entries.back().instance_number = i;
        }
    }

    constexpr float bottom_y_pos = 0.001f;

    glm::ivec2 max_corner;
    {
        rbp::SkylineBinPack packer;
        const float gap = config.instance_gap;
        int group_width = 500;
        int group_depth = 500;
        for (;;) {
            max_corner = glm::ivec2{0, 0};
            packer.Init(group_width, group_depth, false);

            bool pack_failed = false;
            for (auto& entry : pack_entries) {
                auto*      brush = entry.brush;
                const vec3 size  = brush->get_bounding_box().diagonal() * config.object_scale;
                const int  width = static_cast<int>(256.0f * (size.x + gap));
                const int  depth = static_cast<int>(256.0f * (size.z + gap));
                entry.rectangle = packer.Insert(width + 1, depth + 1, rbp::SkylineBinPack::LevelBottomLeft);
                if ((entry.rectangle.width == 0) || (entry.rectangle.height == 0)) {
                    pack_failed = true;
                    break;
                }
                max_corner.x = std::max(max_corner.x, entry.rectangle.x + entry.rectangle.width);
                max_corner.y = std::max(max_corner.y, entry.rectangle.y + entry.rectangle.height);
            }

            if (!pack_failed) {
                break;
            }

            group_width = group_width + group_width / 2;
            group_depth = group_depth + group_depth / 2;

            //ERHE_VERIFY(group_width <= 16384);
        }
    }

    {
        ERHE_PROFILE_SCOPE("make instances");

        auto&       material_library = m_content_library->materials;
        const auto& materials        = material_library->get_all<erhe::primitive::Material>();
        std::size_t material_index   = 0;

        std::size_t visible_material_count = 0;
        for (const auto& material : materials) {
            if (material->is_shown_in_ui()) {
                ++visible_material_count;
            }
        }

        ERHE_VERIFY(!materials.empty());
        ERHE_VERIFY(visible_material_count > 0);

        const std::shared_ptr<erhe::scene::Node>& root_node = m_scene_root->get_scene().get_root_node();
        std::vector<std::shared_ptr<Operation>> operations;
        operations.reserve(pack_entries.size());

        for (auto& entry : pack_entries) {
            // TODO this will lock up if there are no visible materials
            do {
                material_index = (material_index + 1) % materials.size();
            } while (!materials.at(material_index)->is_shown_in_ui());

            auto* brush = entry.brush;

            const vec3 size  = brush->get_bounding_box().diagonal();

            float x = static_cast<float>(entry.rectangle.x) / 256.0f;
            float z = static_cast<float>(entry.rectangle.y) / 256.0f;
            float w = static_cast<float>(entry.rectangle.width ) / 256.0f;
            float h = static_cast<float>(entry.rectangle.height) / 256.0f;
            log_startup->trace(
                "brush {} w = {} h = {} rect x = {} y = {} w = {} h = {}",
                brush->get_name(), size.x, size.y, x, z, w, h
            );
            x += 0.5f * w;
            z += 0.5f * h;
            x -= 0.5f * static_cast<float>(max_corner.x) / 256.0f;
            z -= 0.5f * static_cast<float>(max_corner.y) / 256.0f;
            const float y = bottom_y_pos - (brush->get_bounding_box().min.y * config.object_scale);
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
                    Item_flags::content    |
                    Item_flags::id         |
                    Item_flags::shadow_cast,
                .scene_root      = m_scene_root.get(),
                .world_from_node = erhe::math::create_translation(x, y, z),
                .material        = materials.at(material_index),
                .scale           = config.object_scale
            };
            std::shared_ptr<erhe::scene::Node> instance_node = brush->make_instance(brush_instance_create_info);

            std::shared_ptr<erhe::Item_base> shared_brush_item_base = brush->shared_from_this();
            std::shared_ptr<Brush>           shared_brush           = std::dynamic_pointer_cast<Brush>(shared_brush_item_base);
            std::shared_ptr<Brush_placement> brush_placement        = std::make_shared<Brush_placement>(shared_brush, GEO::NO_FACET, GEO::NO_CORNER);
            instance_node->attach(brush_placement);
            brush_placement->enable_flag_bits(
                erhe::Item_flags::brush      |
                erhe::Item_flags::visible    |
                erhe::Item_flags::no_message |
                erhe::Item_flags::show_in_ui
            );

            if (config.instance_count > 1) {
                instance_node->set_name(fmt::format("{}.{}", instance_node->get_name(), entry.instance_number + 1));
            }
            operations.push_back(
                std::make_shared<Item_insert_remove_operation>(
                    Item_insert_remove_operation::Parameters{
                        .context = m_context,
                        .item    = instance_node,
                        .parent  = root_node,
                        .mode    = Item_insert_remove_operation::Mode::insert
                    }
                )
            );
        }

        if (!operations.empty()) {
            m_context.operation_stack->queue(
                std::make_shared<Compound_operation>(
                    Compound_operation::Parameters{.operations = std::move(operations)}
                )
            );
        }
    }
}

void Scene_builder::add_cubes(glm::ivec3 shape, float scale, float gap)
{
    ERHE_PROFILE_FUNCTION();

    std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> scene_lock{m_scene_root->item_host_mutex};

    auto& material_library = m_content_library->materials;
    auto material = material_library->make<erhe::primitive::Material>(
        erhe::primitive::Material_create_info{
            .name = "cube",
            .data = {
                .base_color = glm::vec4{1.0, 1.0f, 1.0f, 1.0f},
                .roughness  = glm::vec2{0.3f, 0.4f},
                .metallic   = 0.0f
            }
        }
    );

    const int x_count = shape.x;
    const int y_count = shape.y;
    const int z_count = shape.z;
    float x_half_extent = static_cast<float>(x_count) * scale + static_cast<float>(x_count - 1) * gap;
    float y_half_extent = static_cast<float>(y_count) * scale + static_cast<float>(y_count - 1) * gap;
    float z_half_extent = static_cast<float>(z_count) * scale + static_cast<float>(z_count - 1) * gap;

    erhe::primitive::Element_mappings dummy; // TODO make Element_mappings optional

    GEO::Mesh cube_geo_mesh;
    erhe::geometry::shapes::make_cube(cube_geo_mesh, scale);

    erhe::scene_renderer::Mesh_memory& mesh_memory = *m_context.mesh_memory;

    erhe::primitive::Buffer_mesh buffer_mesh{};
    const bool buffer_mesh_ok = erhe::primitive::build_buffer_mesh(
        buffer_mesh,
        cube_geo_mesh,
        build_info(mesh_memory),
        dummy,
        Normal_style::polygon_normals
    );
    ERHE_VERIFY(buffer_mesh_ok); // TODO

    std::shared_ptr<erhe::primitive::Primitive> primitive = std::make_shared<erhe::primitive::Primitive>(std::move(buffer_mesh));
    ERHE_VERIFY(primitive->render_shape->make_raytrace(cube_geo_mesh));
    const vec3 root_pos{0.0, 1.0f + y_half_extent, 0.0f};
    std::shared_ptr<erhe::scene::Node> root = std::make_shared<erhe::scene::Node>("Cubes");
    root->enable_flag_bits(Item_flags::content | Item_flags::visible | Item_flags::show_in_ui);
    root->set_world_from_node(erhe::math::create_translation<float>(root_pos));
    for (int x = 0; x < x_count; ++x) {
        const float px = erhe::math::remap(static_cast<float>(x), 0.0f, static_cast<float>(x_count - 1), -x_half_extent, x_half_extent);
        for (int y = 0; y < y_count; ++y) {
            const float py = erhe::math::remap(static_cast<float>(y), 0.0f, static_cast<float>(y_count - 1), -y_half_extent, y_half_extent);
            for (int z = 0; z < z_count; ++z) {
                const float pz = erhe::math::remap(static_cast<float>(z), 0.0f, static_cast<float>(z_count - 1), -z_half_extent, z_half_extent);
                auto node = std::make_shared<erhe::scene::Node>("Cube");
                auto mesh = std::make_shared<erhe::scene::Mesh>("");
                mesh->add_primitive(primitive, material);
                mesh->layer_id = m_scene_root->layers().content()->id;
                mesh->enable_flag_bits(Item_flags::content | Item_flags::shadow_cast);
                node->attach(mesh);
                node->set_parent(root);
                node->set_parent_from_node(erhe::math::create_translation<float>(px, py, pz));
                node->enable_flag_bits(Item_flags::content | Item_flags::visible | Item_flags::show_in_ui);
            }
        }
    }
    root->set_parent(m_scene_root->get_scene().get_root_node());
}

auto Scene_builder::make_directional_light(
    const std::string_view name,
    const vec3             position,
    const vec3             color,
    const float            intensity,
    const bool             cast_shadow
) -> std::shared_ptr<erhe::scene::Node>
{
    std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> scene_lock{m_scene_root->item_host_mutex};

    auto node  = std::make_shared<erhe::scene::Node>(name);
    auto light = std::make_shared<erhe::scene::Light>(name);
    light->type        = Light::Type::directional;
    light->color       = color;
    light->intensity   = intensity;
    light->range       = 0.0f;
    light->cast_shadow = cast_shadow;
    light->layer_id    = m_scene_root->layers().light()->id;
    light->enable_flag_bits(Item_flags::content | Item_flags::visible | Item_flags::show_in_ui | Item_flags::show_debug_visualizations);
    node->attach          (light);
    node->enable_flag_bits(Item_flags::content | Item_flags::visible | Item_flags::show_in_ui);

    const mat4 m = erhe::math::create_look_at(
        position,                // eye
        vec3{0.0f, 0.0f, 0.0f},  // center
        vec3{0.0f, 1.0f, 0.0f}   // up
    );
    node->set_parent_from_node(m);

    return node;
}

auto Scene_builder::make_spot_light(
    const std::string_view name,
    const vec3             position,
    const vec3             target,
    const vec3             color,
    const float            intensity,
    const vec2             spot_cone_angle,
    const bool             cast_shadow
) -> std::shared_ptr<erhe::scene::Node>
{
    std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> scene_lock{m_scene_root->item_host_mutex};

    auto node  = std::make_shared<erhe::scene::Node>(name);
    auto light = std::make_shared<erhe::scene::Light>(name);
    light->type             = Light::Type::spot;
    light->color            = color;
    light->intensity        = intensity;
    light->range            = 25.0f;
    light->inner_spot_angle = spot_cone_angle[0];
    light->outer_spot_angle = spot_cone_angle[1];
    light->cast_shadow      = cast_shadow;
    light->layer_id         = m_scene_root->layers().light()->id;
    light->enable_flag_bits(Item_flags::content | Item_flags::visible | Item_flags::show_in_ui);
    node->attach          (light);
    node->enable_flag_bits(Item_flags::content | Item_flags::visible | Item_flags::show_in_ui);

    const mat4 m = erhe::math::create_look_at(position, target, vec3{0.0f, 0.0f, 1.0f});
    node->set_parent_from_node(m);

    return node;
}

auto Scene_builder::make_point_light(
    const std::string_view name,
    const vec3             position,
    const vec3             color,
    const float            intensity,
    const bool             cast_shadow
) -> std::shared_ptr<erhe::scene::Node>
{
    std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> scene_lock{m_scene_root->item_host_mutex};

    auto node  = std::make_shared<erhe::scene::Node>(name);
    auto light = std::make_shared<erhe::scene::Light>(name);
    light->type        = Light::Type::point;
    light->color       = color;
    light->intensity   = intensity;
    light->range       = 25.0f;
    light->cast_shadow = cast_shadow;
    light->layer_id    = m_scene_root->layers().light()->id;
    light->enable_flag_bits(Item_flags::content | Item_flags::visible | Item_flags::show_in_ui | Item_flags::show_debug_visualizations);
    node->attach          (light);
    node->enable_flag_bits(Item_flags::content | Item_flags::visible | Item_flags::show_in_ui);

    const mat4 m = erhe::math::create_translation<float>(position);
    node->set_parent_from_node(m);

    return node;
}

void Scene_builder::add_lights(const Add_lights_args& args)
{
    const glm::vec4           target_ambient   {0.04f, 0.04f, 0.04f, 0.0f};

    const float directional_light_intensity         = args.directional_light_intensity;
    const float directional_light_radius            = args.directional_light_radius;
    const float directional_light_height            = args.directional_light_height;
    int         directional_light_shadow_count      = std::max(0, args.directional_light_shadow_count);
    const int   directional_light_no_shadow_count   = std::max(0, args.directional_light_no_shadow_count);
    const float spot_light_intensity                = args.spot_light_intensity;
    const float spot_light_radius                   = args.spot_light_radius;
    const float spot_light_height                   = args.spot_light_height;
    int         spot_light_shadow_count             = std::max(0, args.spot_light_shadow_count);
    const int   spot_light_no_shadow_count          = std::max(0, args.spot_light_no_shadow_count);
    const float point_light_intensity               = args.point_light_intensity;
    const float point_light_radius                  = args.point_light_radius;
    const float point_light_height                  = args.point_light_height;
    int         point_light_shadow_count            = std::max(0, args.point_light_shadow_count);
    const int   point_light_no_shadow_count         = std::max(0, args.point_light_no_shadow_count);

    // Clamp shadow-light counts to the active graphics preset's capacities.
    // The shadow-map arrays have fixed capacities, and exceeding them would
    // silently drop lights or break shader bindings. add_lights() runs the
    // clamp here so callers via try_call() / key bindings / MCP / UI bypass
    // the duplicate clamp that used to live in Add_lights_command::apply_args.
    //
    // Directional and spot lights both render into the shared 2D shadow-map
    // texture array (capacity shadow_light_count). Directional lights take
    // priority; spot lights get whatever budget remains. Point lights render
    // into a separate cube-map shadow array (capacity point_shadow_light_count).
    if (m_context.app_settings != nullptr) {
        const Graphics_preset_entry& preset = m_context.app_settings->graphics.current_graphics_preset;
        if (preset.shadow_enable) {
            if (directional_light_shadow_count > preset.shadow_light_count) {
                log_startup->info(
                    "Clamping Scene_builder::add_lights directional_light_shadow_count from {} to {} (graphics preset '{}' shadow_light_count)",
                    directional_light_shadow_count, preset.shadow_light_count, preset.name
                );
                directional_light_shadow_count = preset.shadow_light_count;
            }
            const int remaining_2d_shadow = preset.shadow_light_count - directional_light_shadow_count;
            if (spot_light_shadow_count > remaining_2d_shadow) {
                log_startup->info(
                    "Clamping Scene_builder::add_lights spot_light_shadow_count from {} to {} (graphics preset '{}' shadow_light_count {} shared with {} directional shadow light(s))",
                    spot_light_shadow_count, remaining_2d_shadow, preset.name, preset.shadow_light_count, directional_light_shadow_count
                );
                spot_light_shadow_count = remaining_2d_shadow;
            }
            if (point_light_shadow_count > preset.point_shadow_light_count) {
                log_startup->info(
                    "Clamping Scene_builder::add_lights point_light_shadow_count from {} to {} (graphics preset '{}' point_shadow_light_count)",
                    point_light_shadow_count, preset.point_shadow_light_count, preset.name
                );
                point_light_shadow_count = preset.point_shadow_light_count;
            }
        }
    }

    const int   directional_light_count             = directional_light_shadow_count + directional_light_no_shadow_count;
    const int   spot_light_count                    = spot_light_shadow_count + spot_light_no_shadow_count;
    const int   point_light_count                   = point_light_shadow_count + point_light_no_shadow_count;

    std::vector<std::shared_ptr<erhe::scene::Node>> light_nodes;
    light_nodes.reserve(static_cast<std::size_t>(directional_light_count + spot_light_count + point_light_count));

    if (directional_light_count == 1) {
        const bool cast_shadow = (directional_light_shadow_count == 1);
        light_nodes.push_back(
            make_directional_light(
                "X",
                vec3{0.0f, 2.0f, 0.0f}, // pos
                vec3{1.0f, 1.0f, 1.0f}, // color
                2.0f,                   // intensity
                cast_shadow
            )
        );
    } else {
        for (int i = 0; i < directional_light_count; ++i) {
            const float rel = static_cast<float>(i) / static_cast<float>(directional_light_count);
            const float R   = directional_light_radius;
            const float h   = rel * 360.0f;
            const float s   = (directional_light_count > 1) ? 0.5f : 0.0f;
            const float v   = 1.0f;
            float r, g, b;

            erhe::math::hsv_to_rgb(h, s, v, r, g, b);

            const vec3        color       = vec3{r, g, b};
            const float       intensity   = directional_light_intensity / static_cast<float>(directional_light_count);
            const bool        cast_shadow = (i < directional_light_shadow_count);
            const std::string name        = fmt::format(
                "Directional light {}{}", i, cast_shadow ? "" : " (no shadow)"
            );
            const float       x_pos       = R * std::sin(rel * glm::two_pi<float>() + 1.0f / 7.0f);
            const float       z_pos       = R * std::cos(rel * glm::two_pi<float>() + 1.0f / 7.0f);
            const vec3        position    = vec3{x_pos, directional_light_height, z_pos};
            light_nodes.push_back(make_directional_light(name, position, color, intensity, cast_shadow));
        }
    }

    for (int i = 0; i < spot_light_count; ++i) {
        const float rel   = static_cast<float>(i) / static_cast<float>(spot_light_count);
        const float theta = rel * glm::two_pi<float>();
        const float R     = spot_light_radius;
        const float h     = rel * 360.0f;
        const float s     = (spot_light_count > 1) ? 0.9f : 0.0f;
        const float v     = 1.0f;
        float r, g, b;

        erhe::math::hsv_to_rgb(h, s, v, r, g, b);

        const vec3        color           = vec3{r, g, b};
        const float       intensity       = spot_light_intensity;
        const bool        cast_shadow     = (i < spot_light_shadow_count);
        const std::string name            = fmt::format("Spot {}{}", i, cast_shadow ? "" : " (no shadow)");
        const float       x_pos           = R * std::sin(theta);
        const float       z_pos           = R * std::cos(theta);
        const vec3        position        = vec3{x_pos, spot_light_height, z_pos};
        const vec3        target          = vec3{x_pos * 0.1f, 0.0f, z_pos * 0.1f};
        const vec2        spot_cone_angle = vec2{
            glm::pi<float>() / 5.0f,
            glm::pi<float>() / 4.0f
        };
        light_nodes.push_back(make_spot_light(name, position, target, color, intensity, spot_cone_angle, cast_shadow));
    }

    for (int i = 0; i < point_light_count; ++i) {
        const float rel   = static_cast<float>(i) / static_cast<float>(point_light_count);
        const float theta = rel * glm::two_pi<float>();
        const float R     = point_light_radius;
        const float h     = rel * 360.0f;
        const float s     = (point_light_count > 1) ? 0.9f : 0.0f;
        const float v     = 1.0f;
        float r, g, b;

        erhe::math::hsv_to_rgb(h, s, v, r, g, b);

        const vec3        color       = vec3{r, g, b};
        const float       intensity   = point_light_intensity;
        const bool        cast_shadow = (i < point_light_shadow_count);
        const std::string name        = fmt::format("Point {}{}", i, cast_shadow ? "" : " (no shadow)");
        const float       x_pos       = R * std::sin(theta);
        const float       z_pos       = R * std::cos(theta);
        const vec3        position    = vec3{x_pos, point_light_height, z_pos};
        light_nodes.push_back(make_point_light(name, position, color, intensity, cast_shadow));
    }

    erhe::scene::Scene& scene = m_scene_root->get_scene();
    if (light_nodes.empty() && (scene.ambient_light == target_ambient)) {
        return;
    }

    const std::shared_ptr<erhe::scene::Node>& root_node = scene.get_root_node();
    std::vector<std::shared_ptr<Operation>> operations;
    operations.reserve(light_nodes.size() + 1);
    operations.push_back(std::make_shared<Ambient_light_operation>(&scene, target_ambient));
    for (const std::shared_ptr<erhe::scene::Node>& light_node : light_nodes) {
        operations.push_back(
            std::make_shared<Item_insert_remove_operation>(
                Item_insert_remove_operation::Parameters{
                    .context = m_context,
                    .item    = light_node,
                    .parent  = root_node,
                    .mode    = Item_insert_remove_operation::Mode::insert
                }
            )
        );
    }

    m_context.operation_stack->queue(
        std::make_shared<Compound_operation>(
            Compound_operation::Parameters{.operations = std::move(operations)}
        )
    );
}

void Scene_builder::animate_lights(const double time_d)
{
    std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> scene_lock{m_scene_root->item_host_mutex};

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

        const auto eye    = vec3{R * std::sin(rel + t * 0.52f), 8.0f, R * std::cos(rel + t * 0.71f)};
        const auto center = vec3{r * std::sin(rel + t * 0.35f), 0.0f, r * std::cos(rel + t * 0.93f)};

        const glm::vec3 up{0.0f, 1.0f, 0.0f};
        const auto m = erhe::math::create_look_at(eye, center, up);

        l->get_node()->set_parent_from_node(m);

        light_index++;
    }
}

auto Scene_builder::get_scene_root() const -> std::shared_ptr<Scene_root>
{
    return m_scene_root;
}

void Scene_builder::set_scene_root(const std::shared_ptr<Scene_root>& scene_root)
{
    m_scene_root = scene_root;
}

void Scene_builder::register_floor_resources(
    const std::shared_ptr<Brush>&                           brush,
    const std::shared_ptr<erhe::physics::ICollision_shape>& collision_shape,
    const std::shared_ptr<erhe::primitive::Material>&       material
)
{
    {
        std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_brush_mutex};
        if (brush) {
            if (std::find(m_floor_brushes.begin(), m_floor_brushes.end(), brush) == m_floor_brushes.end()) {
                m_floor_brushes.push_back(brush);
            }
        }
        if (collision_shape) {
            if (std::find(m_collision_shapes.begin(), m_collision_shapes.end(), collision_shape) == m_collision_shapes.end()) {
                m_collision_shapes.push_back(collision_shape);
            }
        }
    }

    if (material && m_scene_root) {
        const std::shared_ptr<Content_library>& content_library = m_content_library;
        if (content_library && content_library->materials) {
            std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{content_library->mutex};
            content_library->materials->add(material);
        }
    }
}

void Scene_builder::unregister_floor_resources(
    const std::shared_ptr<Brush>&                           brush,
    const std::shared_ptr<erhe::physics::ICollision_shape>& collision_shape,
    const std::shared_ptr<erhe::primitive::Material>&       material
)
{
    {
        std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_brush_mutex};
        if (brush) {
            const auto it = std::find(m_floor_brushes.begin(), m_floor_brushes.end(), brush);
            if (it != m_floor_brushes.end()) {
                m_floor_brushes.erase(it);
            }
        }
        if (collision_shape) {
            const auto it = std::find(m_collision_shapes.begin(), m_collision_shapes.end(), collision_shape);
            if (it != m_collision_shapes.end()) {
                m_collision_shapes.erase(it);
            }
        }
    }

    if (material && m_scene_root) {
        const std::shared_ptr<Content_library>& content_library = m_content_library;
        if (content_library && content_library->materials) {
            std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{content_library->mutex};
            content_library->materials->remove(material);
        }
    }
}

auto Scene_builder::floor_brushes_size() const -> std::size_t
{
    return m_floor_brushes.size();
}

auto Scene_builder::collision_shapes_size() const -> std::size_t
{
    return m_collision_shapes.size();
}

}
