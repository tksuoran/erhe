#include "preview/brush_preview.hpp"

#include "app_context.hpp"
#include "editor_log.hpp"
#include "brushes/brush.hpp"
#include "content_library/content_library.hpp"
#include "renderers/mesh_memory.hpp"
#include "renderers/programs.hpp"
#include "renderers/render_context.hpp"
#include "renderers/composition_pass.hpp"
#include "renderers/viewport_config.hpp"
#include "scene/scene_root.hpp"

#include "erhe_graphics/device.hpp"
#include "erhe_imgui/imgui_renderer.hpp"
#include "erhe_geometry/shapes/sphere.hpp"
#include "erhe_graphics/blit_command_encoder.hpp"
#include "erhe_graphics/render_command_encoder.hpp"
#include "erhe_graphics/render_pass.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_math/math_util.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_primitive/primitive_builder.hpp"
#include "erhe_scene/light.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene.hpp"

#include <fmt/format.h>

namespace editor {

Brush_preview::Brush_preview(
    erhe::graphics::Device&         graphics_device,
    erhe::scene::Scene_message_bus& scene_message_bus,
    App_context&                    app_context,
    Mesh_memory&                    mesh_memory,
    Programs&                       programs
)
    : Scene_preview{graphics_device, scene_message_bus, app_context, mesh_memory, programs}
{
    make_preview_scene();
}

Brush_preview::~Brush_preview()
{
}

void Brush_preview::make_preview_scene()
{
    ERHE_PROFILE_FUNCTION();

    m_node = std::make_shared<erhe::scene::Node>("Brush Preview Node");
    m_node->enable_flag_bits(
        erhe::Item_flags::brush   |
        erhe::Item_flags::visible |
        erhe::Item_flags::no_message
    );
    const auto paremt = m_scene_root->get_hosted_scene()->get_root_node();
    m_node->set_parent(paremt);

    m_material = std::make_shared<erhe::primitive::Material>(
        erhe::primitive::Material_create_info{
            .name       = "Brush preview material",
            .base_color = glm::vec3{1.0f, 0.2f, 0.1f},
            .roughness  = glm::vec2{0.5f, 0.4f},
            .metallic   = 0.5f
        }
    );

    m_camera_node = std::make_shared<erhe::scene::Node>("Camera node");
    m_camera = std::make_shared<erhe::scene::Camera>("Camera");
    //m_camera_node->enable_flag_bits(erhe::Item_flags::content);
    //m_camera->enable_flag_bits(erhe::Item_flags::content);
    m_camera->projection()->fov_y  =  0.3f;
    m_camera->projection()->z_near =  4.0f;
    m_camera->projection()->z_far  = 12.0f;
    m_camera_node->attach(m_camera);
    m_camera_node->set_parent(paremt);

    m_scene_root->layers().light()->ambient_light = glm::vec4{0.1f, 0.1f, 0.1f, 0.0f};
    
    m_key_light      = std::make_shared<erhe::scene::Light>("Key Light");
    m_key_light->enable_flag_bits(erhe::Item_flags::content);
    m_key_light->layer_id = m_scene_root->layers().light()->id;
    m_key_light->intensity = 2.0f;
    m_key_light_node = std::make_shared<erhe::scene::Node>("Key Light Node");
    m_key_light_node->enable_flag_bits(erhe::Item_flags::content);
    m_key_light_node->attach(m_key_light);
    m_key_light_node->set_parent(paremt);
    m_key_light_node->set_parent_from_node(
        erhe::math::create_look_at(
            glm::vec3{8.0f, 8.0f, 8.0f},  // eye
            glm::vec3{0.0f, 0.0f, 0.0f},  // center
            glm::vec3{0.0f, 1.0f, 0.0f}   // up
        )
    );

    m_fill_light = std::make_shared<erhe::scene::Light>("Fill Light");
    m_fill_light->enable_flag_bits(erhe::Item_flags::content);
    m_fill_light->layer_id = m_scene_root->layers().light()->id;
    m_fill_light->intensity = 0.5f;
    m_fill_light_node = std::make_shared<erhe::scene::Node>("Fill Light Node");
    m_fill_light_node->enable_flag_bits(erhe::Item_flags::content);
    m_fill_light_node->attach(m_key_light);
    m_fill_light_node->set_parent(paremt);
    m_fill_light_node->set_parent_from_node(
        erhe::math::create_look_at(
            glm::vec3{-8.0f, 8.0f, 8.0f},  // eye
            glm::vec3{ 0.0f, 0.0f, 0.0f},  // center
            glm::vec3{ 0.0f, 1.0f, 0.0f}   // up
        )
    );

    auto composition_pass = std::make_shared<Composition_pass>("Material Preview Composition_pass");
    composition_pass->mesh_layers    = {Mesh_layer_id::brush};
    composition_pass->primitive_mode = erhe::primitive::Primitive_mode::polygon_fill;
    composition_pass->filter         = erhe::Item_filter{};
    composition_pass->passes         = {&m_pipeline_pass};
    {
        std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_composer.mutex};
        m_composer.composition_passes.push_back(composition_pass);
    }
}

void Brush_preview::render_preview(
    const std::shared_ptr<erhe::graphics::Texture>& texture,
    const std::shared_ptr<Brush>&                   brush,
    int64_t                                         time
)
{
    log_tree->trace("Brush_preview::render_preview()");
    set_color_texture(texture);
    resize(texture->get_width(), texture->get_height());
    set_clear_color(glm::vec4{0.0f, 0.0f, 0.0f, 0.0f});
    update_rendertarget(m_graphics_device);

    if (m_mesh) {
        m_node->detach(m_mesh.get());
        m_mesh.reset();
    }

    const Brush::Scaled& brush_scaled = brush->get_scaled(1.0);
    m_context.mesh_memory->buffer_transfer_queue.flush();

    if (m_mesh) {
        m_mesh->clear_primitives();
    } else {
        m_mesh = std::make_shared<erhe::scene::Mesh>("Brush Preview Mesh");
        m_mesh->enable_flag_bits(
            erhe::Item_flags::brush       |
            erhe::Item_flags::visible     |
            erhe::Item_flags::translucent | // redundant
            erhe::Item_flags::no_message  |
            (m_context.developer_mode ? erhe::Item_flags::show_in_ui : 0)
        );
        m_mesh->layer_id = { Mesh_layer_id::brush };
    }

    m_mesh->add_primitive(brush_scaled.primitive, m_material);
    m_node->attach(m_mesh);

    // Get brush primitive aabb in world space
    //const erhe::math::Aabb primitive_local_aabb = brush_scaled.primitive.get_bounding_box();
    //ERHE_VERIFY(primitive_local_aabb.is_valid());
    //erhe::math::Aabb world_aabb = primitive_local_aabb.transformed_by(m_node->world_from_node());
    //ERHE_VERIFY(world_aabb.is_valid());
    //const glm::vec3 target_position = world_aabb.center();
    //const float     size            = glm::length(world_aabb.diagonal());

    const erhe::primitive::Buffer_mesh* renderable_mesh = brush_scaled.primitive.get_renderable_mesh();
    ERHE_VERIFY(renderable_mesh != nullptr);
    const erhe::math::Sphere primitive_local_bounding_sphere = renderable_mesh->bounding_sphere;
    const erhe::math::Sphere world_sphere    = primitive_local_bounding_sphere.transformed_by(m_node->world_from_node());
    const glm::vec3          target_position = world_sphere.center;
    const float              size            = world_sphere.radius;

    const erhe::math::Viewport viewport{
        .x      = 0,
        .y      = 0,
        .width  = m_width,
        .height = m_height
    };

    const float                      time_s   = static_cast<float>(static_cast<double>(time) / 1'000'000'000.0);
    const glm::quat                  rotation = glm::angleAxis(2.0f * time_s, glm::vec3{0.0f, 1.0f, 0.0f});
    const erhe::scene::Trs_transform node_transform{rotation};
    m_node->set_parent_from_node(node_transform);

    m_camera_node->set_parent_from_node(
        erhe::math::create_look_at(
            glm::vec3{0.0f, 4.0f, 8.0f},  // eye
            glm::vec3{0.0f, 0.0f, 0.0f},  // center
            glm::vec3{0.0f, 1.0f, 0.0f}   // up
        )
    );

    // Frame bounding volume into camera view
    glm::vec3   camera_position      = m_camera_node->position_in_world();
    glm::vec3   direction            = target_position - camera_position;
    glm::vec3   direction_normalized = glm::normalize(target_position - camera_position);
    const       erhe::scene::Projection::Fov_sides fov_sides = m_camera->projection()->get_fov_sides(viewport);
    float min_fov_side = std::numeric_limits<float>::max();
    for (float fov_side : { fov_sides.left, fov_sides.right, fov_sides.up, fov_sides.down }) {
        min_fov_side = std::min(std::abs(fov_side), min_fov_side);
    }
    ////float tan_fov_side = std::tanf(min_fov_side);
    const float     tan_fov_side        = tanf(min_fov_side);
    const float     fit_distance        = size / tan_fov_side;
    const glm::vec3 new_position        = target_position - fit_distance * direction_normalized;
    const glm::mat4 new_world_from_node = erhe::math::create_look_at(new_position, target_position, glm::vec3{0.0f, 1.0f, 0.0});
    m_camera_node->set_world_from_node(new_world_from_node);

    //const glm::mat4 clip_from_node  = m_camera->projection()->get_projection_matrix(1.0f);
    //const glm::mat4 clip_from_world = clip_from_node * m_camera_node->node_from_world();
    //const glm::mat4 node_from_clip  = inverse(clip_from_node);
    //const glm::mat4 world_from_clip = m_camera_node->world_from_node() * node_from_clip;
    //
    // TODO Compute good near and far planes
    m_camera->projection()->z_near =  0.1f;
    m_camera->projection()->z_far  = 80.0f;

    m_scene_root->get_hosted_scene()->update_node_transforms();

    const auto& layers = m_scene_root->layers();
    m_light_projections = erhe::scene_renderer::Light_projections{
        layers.light()->lights,
        m_camera.get(),
        viewport,
        erhe::math::Viewport{},
        m_shadow_texture
    };

    {
        erhe::graphics::Render_command_encoder render_encoder = m_graphics_device.make_render_command_encoder(*m_render_pass.get());
        const Render_context context{
            .encoder                = &render_encoder,
            .app_context            = m_context,
            .scene_view             = *this,
            .viewport_config        = m_viewport_config,
            .camera                 = m_camera.get(),
            .viewport_scene_view    = nullptr,
            .viewport               = viewport,
            .override_shader_stages = nullptr
        };
        m_composer.render(context);
    }

    {
        erhe::graphics::Blit_command_encoder blit_encoder = m_graphics_device.make_blit_command_encoder();
        blit_encoder.generate_mipmaps(texture.get());
    }
}

}  // namespace editor
