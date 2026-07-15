#include "preview/brush_preview.hpp"

#include "app_context.hpp"
#include "editor_log.hpp"
#include "brushes/brush.hpp"
#include "config/generated/editor_settings_config.hpp"
#include "content_library/content_library.hpp"
#include "erhe_scene_renderer/mesh_memory.hpp"
#include "renderers/programs.hpp"
#include "renderers/render_context.hpp"
#include "renderers/composition_pass.hpp"
#include "renderers/viewport_config.hpp"
#include "scene/scene_root.hpp"

#include "erhe_graphics/command_buffer.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_imgui/imgui_renderer.hpp"
#include "erhe_geometry/shapes/sphere.hpp"
#include "erhe_graphics/blit_command_encoder.hpp"
#include "erhe_graphics/render_command_encoder.hpp"
#include "erhe_graphics/render_pass.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_log/log.hpp"
#include "erhe_math/math_util.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_primitive/primitive_builder.hpp"
#include "erhe_scene/light.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_scene_renderer/shader_key.hpp"

#include <fmt/format.h>

namespace editor {

Brush_preview::Brush_preview(
    erhe::graphics::Device&         graphics_device,
    erhe::graphics::Command_buffer& init_command_buffer,
    App_context&                    app_context
)
    : Scene_preview{graphics_device, init_command_buffer, app_context}
    , m_solid_wireframe_supported{graphics_device.get_info().use_solid_wireframe}
    , m_wireframe_pipeline{
        graphics_device,
        erhe::graphics::Base_render_pipeline_create_info{
            .debug_label    = erhe::utility::Debug_label{"Brush Preview Solid Wireframe"},
            .input_assembly = erhe::graphics::Input_assembly_state::triangle,
            .rasterization  = erhe::graphics::Rasterization_state::cull_mode_back_ccw.with_winding_flip_if(m_y_flip),
            // Depth less-or-equal overlay over the fill (same expanded
            // positions -> equal depth -> passes), no depth write - mirrors
            // Pipeline_renderpasses::solid_wireframe.
            .depth_stencil  = {
                .depth_test_enable   = true,
                .depth_write_enable  = false,
                .depth_compare_op    = erhe::graphics::get_depth_function(erhe::graphics::Compare_operation::less_or_equal, graphics_device.get_reverse_depth()),
                .stencil_test_enable = false
            }
        }
    }
{
    make_preview_scene();
}

Brush_preview::~Brush_preview() noexcept
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
    const auto paremt = m_scene_root_shared->get_hosted_scene()->get_root_node();
    m_node->set_parent(paremt);

    m_material = std::make_shared<erhe::primitive::Material>(
        erhe::primitive::Material_create_info{
            .name = "Brush preview material",
            .data = {
                .base_color = glm::vec3{1.0f, 0.2f, 0.1f},
                .roughness  = glm::vec2{0.5f, 0.4f},
                .metallic   = 0.5f
            }
        }
    );

    // Neutral diffuse white for headlight_shading (geometry graph node
    // previews): with the key light at the camera, Lambert diffuse dims
    // with dot(N, V), reading surface curvature without material color.
    m_headlight_material = std::make_shared<erhe::primitive::Material>(
        erhe::primitive::Material_create_info{
            .name = "Headlight preview material",
            .data = {
                .base_color = glm::vec3{0.85f, 0.85f, 0.85f},
                .roughness  = glm::vec2{0.9f, 0.9f},
                .metallic   = 0.0f
            }
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

    m_scene_root_shared->get_scene().ambient_light = glm::vec4{0.1f, 0.1f, 0.1f, 0.0f};
    
    m_key_light      = std::make_shared<erhe::scene::Light>("Key Light");
    m_key_light->enable_flag_bits(erhe::Item_flags::content);
    m_key_light->layer_id  = m_scene_root_shared->layers().light()->id;
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
    m_fill_light->layer_id  = m_scene_root_shared->layers().light()->id;
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


    auto composition_pass = std::make_shared<Composition_pass>("Brush preview Composition_pass");
    composition_pass->data.mesh_layers           = {Mesh_layer_id::brush};
    composition_pass->data.primitive_mode        = erhe::primitive::Primitive_mode::polygon_fill;
    composition_pass->data.filter                = erhe::Item_filter{};
    composition_pass->data.base_render_pipelines = m_render_pipelines;
    composition_pass->data.blending_mode_policy  = erhe::scene_renderer::Blending_mode_policy::allow_all;

    // Edge-line overlay: the expanded fill soup redrawn depth less-or-equal
    // with the SOLID_WIREFRAME variant, painting real polygon edges inside
    // the fill shader (no z-fight). Enabled / colored per render_preview call
    // from Preview_edge_lines_config; meshes without an expanded fill range
    // are skipped by the bucket walk (fill-only look). Not created where the
    // device cannot link the variant (macOS GL 4.1); no wide-line fallback
    // for previews.
    if (m_solid_wireframe_supported) {
        auto wireframe_pass = std::make_shared<Composition_pass>("Brush preview solid wireframe");
        wireframe_pass->data.enabled                      = false; // per-call state (render_preview)
        wireframe_pass->data.mesh_layers                  = {Mesh_layer_id::brush};
        wireframe_pass->data.primitive_mode               = erhe::primitive::Primitive_mode::solid_wireframe;
        wireframe_pass->data.filter                       = erhe::Item_filter{};
        wireframe_pass->data.base_render_pipelines        = {&m_wireframe_pipeline};
        wireframe_pass->data.blending_mode_policy         = erhe::scene_renderer::Blending_mode_policy::allow_all;
        wireframe_pass->data.shader_key_force_enable_mask = erhe::scene_renderer::make_shader_bool_mask(erhe::scene_renderer::Shader_bool::SOLID_WIREFRAME);
        m_wireframe_pass = wireframe_pass;
    }

    {
        std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_composer.mutex};
        m_composer.composition_passes.push_back(composition_pass);
        if (m_wireframe_pass) {
            m_composer.composition_passes.push_back(m_wireframe_pass);
        }
    }
}

void Brush_preview::render_preview(
    const std::shared_ptr<erhe::graphics::Texture>& texture,
    unsigned int                                    texture_layer,
    const std::shared_ptr<Brush>&                   brush,
    int64_t                                         time
)
{
    // Breadcrumb names the brush whose preview primitive is being (lazily)
    // built, so the watchdog can identify the culprit if build_polygon_fill
    // spins on a corrupt mesh. See doc/intermittent_main_loop_hang.md.
    erhe::log::set_breadcrumb(fmt::format("thumbnail: brush '{}'", brush->get_name()));
    const Brush::Scaled& brush_scaled = brush->get_scaled(1.0);
    const float time_s = static_cast<float>(static_cast<double>(time) / 1'000'000'000.0);
    const Preview_edge_lines_config* edge_lines = (m_context.editor_settings != nullptr)
        ? &m_context.editor_settings->brush_preview_edge_lines
        : nullptr;
    const glm::quat orientation = glm::angleAxis(2.0f * time_s, glm::vec3{0.0f, 1.0f, 0.0f});
    render_preview(texture, texture_layer, brush_scaled.primitive, brush->get_material(), orientation, false, edge_lines);
}

void Brush_preview::render_preview(
    const std::shared_ptr<erhe::graphics::Texture>&    texture,
    unsigned int                                       texture_layer,
    const std::shared_ptr<erhe::primitive::Primitive>& primitive,
    const std::shared_ptr<erhe::primitive::Material>&  material,
    const glm::quat&                                   orientation,
    const bool                                         headlight_shading,
    const Preview_edge_lines_config*                   edge_lines
)
{
    // Edge-line overlay is per-call state (the preview scene is shared by
    // brush thumbnails and graph node previews, each with its own settings
    // group), so enable / color the pass here rather than at construction.
    if (m_wireframe_pass) {
        const bool edge_lines_enabled = (edge_lines != nullptr) && edge_lines->enabled;
        m_wireframe_pass->data.enabled = edge_lines_enabled;
        if (edge_lines_enabled) {
            m_wireframe_pass->data.primitive_settings = erhe::scene_renderer::Primitive_interface_settings{
                .color_source    = erhe::scene_renderer::Primitive_color_source::constant_color,
                .constant_color0 = edge_lines->color,
                .constant_color1 = edge_lines->color,
                .size_source     = erhe::scene_renderer::Primitive_size_source::constant_size,
                .constant_size   = edge_lines->width
            };
        }
    }

    set_color_texture(texture);
    set_color_texture_layer(texture_layer);
    resize(texture->get_width(), texture->get_height());
    set_clear_color(glm::vec4{0.0f, 0.0f, 0.0f, 0.0f});
    update_rendertarget(*m_context.graphics_device);

    if (m_mesh) {
        m_node->detach(m_mesh.get());
        m_mesh.reset();
    }

    ERHE_VERIFY(m_context.current_command_buffer != nullptr);
    erhe::graphics::Command_buffer& command_buffer = *m_context.current_command_buffer;
    m_context.mesh_memory->flush(command_buffer);

    if (m_mesh) {
        m_mesh->clear_primitives();
    } else {
        m_mesh = std::make_shared<erhe::scene::Mesh>("Brush Preview Mesh");
        m_mesh->enable_flag_bits(
            erhe::Item_flags::brush       |
            erhe::Item_flags::visible     |
            erhe::Item_flags::no_message  |
            erhe::Item_flags::show_in_developer_ui
        );
        m_mesh->layer_id = { Mesh_layer_id::brush };
    }

    const std::shared_ptr<erhe::primitive::Material>& render_material =
        material ? material : (headlight_shading ? m_headlight_material : m_material);
    m_mesh->add_primitive(primitive, render_material);
    m_node->attach(m_mesh);

    // Get brush primitive aabb in world space
    //const erhe::math::Aabb primitive_local_aabb = brush_scaled.primitive.get_bounding_box();
    //ERHE_VERIFY(primitive_local_aabb.is_valid());
    //erhe::math::Aabb world_aabb = primitive_local_aabb.transformed_by(m_node->world_from_node());
    //ERHE_VERIFY(world_aabb.is_valid());
    //const glm::vec3 target_position = world_aabb.center();
    //const float     size            = glm::length(world_aabb.diagonal());

    const erhe::primitive::Buffer_mesh* renderable_mesh = primitive->get_renderable_mesh();
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

    const erhe::scene::Trs_transform node_transform{orientation};
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

    // Light setup is per-call state (the preview scene is shared by brush
    // thumbnails and headlight-shaded graph node previews), so both modes
    // set it fully instead of relying on the constructor pose.
    if (headlight_shading) {
        // Key light co-located with the fitted camera: Lambert diffuse
        // then falls off with dot(N, V) (the N.V-dimmed look).
        m_key_light->intensity  = 2.5f;
        m_fill_light->intensity = 0.0f;
        m_key_light_node->set_world_from_node(new_world_from_node);
    } else {
        m_key_light->intensity  = 2.0f;
        m_fill_light->intensity = 0.5f;
        m_key_light_node->set_world_from_node(
            erhe::math::create_look_at(
                glm::vec3{8.0f, 8.0f, 8.0f},  // eye
                glm::vec3{0.0f, 0.0f, 0.0f},  // center
                glm::vec3{0.0f, 1.0f, 0.0f}   // up
            )
        );
    }

    //const glm::mat4 clip_from_node  = m_camera->projection()->get_projection_matrix(1.0f);
    //const glm::mat4 clip_from_world = clip_from_node * m_camera_node->node_from_world();
    //const glm::mat4 node_from_clip  = inverse(clip_from_node);
    //const glm::mat4 world_from_clip = m_camera_node->world_from_node() * node_from_clip;
    //
    // TODO Compute good near and far planes
    m_camera->projection()->z_near =  0.1f;
    m_camera->projection()->z_far  = 80.0f;

    m_scene_root_shared->get_hosted_scene()->update_node_transforms();

    const auto& layers = m_scene_root_shared->layers();
    m_light_projections.apply(
        layers.light()->lights,
        m_camera.get(),
        viewport,
        erhe::math::Viewport{},
        m_shadow_texture,
        get_reverse_depth(),
        get_depth_range()
    );

    {
        erhe::graphics::Render_command_encoder render_encoder = m_context.graphics_device->make_render_command_encoder(command_buffer);
        erhe::graphics::Scoped_render_pass scoped_render_pass{*m_render_pass.get(), command_buffer};
        const erhe::scene_renderer::Camera_view_input single_view_input{
            .projection = m_camera->projection(),
            .node       = m_camera->get_node(),
            .viewport   = viewport
        };
        const Render_context context{
            .command_buffer      = &command_buffer,
            .encoder             = &render_encoder,
            .render_pass         = m_render_pass.get(),
            .app_context         = m_context,
            .scene_view          = *this,
            .viewport_config     = m_viewport_config,
            .camera              = m_camera.get(),
            .viewport_scene_view = nullptr,
            .viewport            = viewport,
            .views               = std::span<const erhe::scene_renderer::Camera_view_input>(&single_view_input, 1)
        };
        // No post-processing for the preview: render content + overlay phases in
        // one pass (issue #230). The preview composer has no overlay passes.
        m_composer.render(context, true, true);
    }

    {
        erhe::graphics::Blit_command_encoder blit_encoder = m_context.graphics_device->make_blit_command_encoder(command_buffer);
        blit_encoder.generate_mipmaps(texture.get());
    }
}

}  // namespace editor
