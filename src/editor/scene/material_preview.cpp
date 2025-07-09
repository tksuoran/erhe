#include "scene/material_preview.hpp"

#include "app_context.hpp"
#include "editor_log.hpp"
#include "renderers/mesh_memory.hpp"
#include "renderers/programs.hpp"
#include "renderers/render_context.hpp"
#include "renderers/composition_pass.hpp"
#include "renderers/viewport_config.hpp"
#include "scene/scene_root.hpp"
#include "scene/content_library.hpp"
#include "tools/brushes/brush.hpp"

#include "erhe_graphics/debug.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_imgui/imgui_renderer.hpp"
#include "erhe_geometry/shapes/sphere.hpp"
#include "erhe_gl/wrapper_functions.hpp"
#include "erhe_graphics/render_command_encoder.hpp"
#include "erhe_graphics/render_pass.hpp"
#include "erhe_graphics/renderbuffer.hpp"
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

using Vertex_input_state   = erhe::graphics::Vertex_input_state;
using Input_assembly_state = erhe::graphics::Input_assembly_state;
using Rasterization_state  = erhe::graphics::Rasterization_state;
using Depth_stencil_state  = erhe::graphics::Depth_stencil_state;
using Color_blend_state    = erhe::graphics::Color_blend_state;

Scene_preview::Scene_preview(
    erhe::graphics::Device&         graphics_device,
    erhe::scene::Scene_message_bus& scene_message_bus,
    App_context&                    context,
    Mesh_memory&                    mesh_memory,
    Programs&                       programs
)
    : Scene_view       {context, Viewport_config{}}
    , m_graphics_device{graphics_device}
    , m_pipeline_pass  {erhe::graphics::Render_pipeline_state{{
        .name           = "Polygon Fill Opaque",
        .shader_stages  = &programs.standard.shader_stages,
        .vertex_input   = &mesh_memory.vertex_input,
        .input_assembly = Input_assembly_state::triangle,
        .rasterization  = Rasterization_state::cull_mode_back_ccw,
        .depth_stencil  = Depth_stencil_state::depth_test_enabled_stencil_test_disabled(),
        .color_blend    = Color_blend_state::color_blend_disabled
    }}}
    , m_composer{"Material Preview Composer"}
{
    ERHE_PROFILE_FUNCTION();

    m_content_library = std::make_shared<Content_library>();

    m_scene_root = std::make_shared<Scene_root>(
        nullptr, // No Imgui_renderer
        nullptr, // No Imgui_windows
        scene_message_bus,
        nullptr, // No App_context
        nullptr, // Don't process editor messages
        nullptr, // Don't register to App_scenes
        m_content_library,
        "Material preview scene"
    );

    m_scene_root->get_scene().disable_flag_bits(erhe::Item_flags::show_in_ui);

    // For now, shadow texture is dummy 1 by 1 pixel only cleared
    m_shadow_texture = std::make_shared<erhe::graphics::Texture>(
        graphics_device,
        erhe::graphics::Texture_create_info {
            .device            = graphics_device,
            .type              = erhe::graphics::Texture_type::texture_2d,
            .pixelformat       = erhe::dataformat::Format::format_d32_sfloat,
            .width             = 1,
            .height            = 1,
            .depth             = 1,
            .array_layer_count = 1,
            .debug_label       = "Scene_preview::m_shadow_texture (dummy shadowmap)"
        }
    );

    const double depth_clear_value = 0.0; // reverse Z
    graphics_device.clear_texture(*m_shadow_texture.get(), { depth_clear_value, 0.0, 0.0, 0.0 });
}

void Scene_preview::resize(int width, int height)
{
    m_width  = std::max(1, width);
    m_height = std::max(1, height);
}

void Scene_preview::set_color_texture(const std::shared_ptr<erhe::graphics::Texture>& color_texture)
{
    m_use_external_color_texture = static_cast<bool>(color_texture);
    if (m_color_texture != color_texture) {
        m_render_pass.reset();
    }
    m_color_texture = color_texture;
}

void Scene_preview::set_clear_color(glm::vec4 clear_color)
{
    m_clear_color = clear_color;
}

void Scene_preview::update_rendertarget(erhe::graphics::Device& graphics_device)
{
    ERHE_PROFILE_FUNCTION();

    bool attachment_changed = false;
    if (
        !m_use_external_color_texture &&
        (
            !m_color_texture ||
            (m_color_texture->get_width () != m_width) ||
            (m_color_texture->get_height() != m_height)
        )
    ) {
        m_color_format = erhe::dataformat::Format::format_16_vec4_float;
        m_color_texture.reset();
        m_color_texture = std::make_shared<erhe::graphics::Texture>(
            graphics_device,
            erhe::graphics::Texture_create_info{
                .device      = graphics_device,
                .type        = erhe::graphics::Texture_type::texture_2d,
                .pixelformat = m_color_format,
                .width       = m_width,
                .height      = m_height,
                .debug_label = "Preview Color Texture"
            }
        );
        graphics_device.clear_texture(*m_color_texture.get(), { 1.0, 0.0, 0.5, 0.0 });
        attachment_changed = true;
    }

    if (
        !m_depth_renderbuffer ||
        (m_depth_renderbuffer->get_width() != m_width) ||
        (m_depth_renderbuffer->get_height() != m_height)
    ) {
        m_depth_format = erhe::dataformat::Format::format_d32_sfloat;
        using Render_pass  = erhe::graphics::Render_pass;
        using Renderbuffer = erhe::graphics::Renderbuffer;
        using Texture      = erhe::graphics::Texture;
        m_depth_renderbuffer = std::make_unique<erhe::graphics::Renderbuffer>(
            graphics_device,
            m_depth_format,
            m_width,
            m_height
        );
        m_depth_renderbuffer->set_debug_label("Material Preview Depth Renderbuffer");
        attachment_changed = true;
    }

    if (!attachment_changed && m_render_pass) {
        return;
    }

    m_render_pass.reset();
    erhe::graphics::Render_pass_descriptor render_pass_descriptor; 
    render_pass_descriptor.color_attachments[0].texture      = m_color_texture.get();
    render_pass_descriptor.color_attachments[0].load_action  = erhe::graphics::Load_action::Clear;
    render_pass_descriptor.color_attachments[0].store_action = erhe::graphics::Store_action::Store;
    render_pass_descriptor.color_attachments[0].clear_value  = { m_clear_color.x, m_clear_color.y, m_clear_color.z, m_clear_color.w };
    render_pass_descriptor.depth_attachment.renderbuffer     = m_depth_renderbuffer.get();
    render_pass_descriptor.depth_attachment.load_action      = erhe::graphics::Load_action::Clear;
    render_pass_descriptor.depth_attachment.clear_value[0]   = 0.0; // Reverse Z
    render_pass_descriptor.depth_attachment.store_action     = erhe::graphics::Store_action::Dont_care;
    render_pass_descriptor.render_target_width               = m_width;
    render_pass_descriptor.render_target_height              = m_height;
    render_pass_descriptor.debug_label                       = "Preview Render_pass";
    m_render_pass = std::make_unique<erhe::graphics::Render_pass>(graphics_device, render_pass_descriptor);
}

auto Scene_preview::get_scene_root() const -> std::shared_ptr<Scene_root>
{
    return m_scene_root;
}

auto Scene_preview::get_camera() const -> std::shared_ptr<erhe::scene::Camera>
{
    return m_camera;
}

auto Scene_preview::get_rendergraph_node() -> erhe::rendergraph::Rendergraph_node*
{
    return {};
}

auto Scene_preview::get_light_projections() const -> const erhe::scene_renderer::Light_projections*
{
    return &m_light_projections;
}

auto Scene_preview::get_shadow_texture() const -> erhe::graphics::Texture*
{
    return m_shadow_texture.get();
}

auto Scene_preview::get_content_library() -> std::shared_ptr<Content_library>
{
    return m_content_library;
}

//
//
//

Material_preview::Material_preview(
    erhe::graphics::Device&         graphics_device,
    erhe::scene::Scene_message_bus& scene_message_bus,
    App_context&                    app_context,
    Mesh_memory&                    mesh_memory,
    Programs&                       programs
)
    : Scene_preview{graphics_device, scene_message_bus, app_context, mesh_memory, programs}
{
    make_preview_scene(mesh_memory);

    resize(256, 256);
    update_rendertarget(graphics_device);
}

void Material_preview::make_preview_scene(Mesh_memory& mesh_memory)
{
    ERHE_PROFILE_FUNCTION();

    m_node = std::make_shared<erhe::scene::Node>("Material Preview Node");
    m_mesh = std::make_shared<erhe::scene::Mesh>("Material Preview Mesh");
    erhe::primitive::Element_mappings dummy; // TODO make Element_mappings optional
    GEO::Mesh sphere_mesh{3, true};
    erhe::geometry::shapes::make_sphere(
        sphere_mesh,
        m_radius,
        std::max(1, m_slice_count),
        std::max(1, m_stack_count)
    );
    erhe::primitive::Buffer_mesh buffer_mesh{};
    const bool buffer_mesh_ok = erhe::primitive::build_buffer_mesh(
        buffer_mesh,
        sphere_mesh,
        erhe::primitive::Build_info{
            .primitive_types = {.fill_triangles = true },
            .buffer_info = mesh_memory.buffer_info
        },
        dummy,
        erhe::primitive::Normal_style::corner_normals
    );
    if (buffer_mesh_ok) {
        m_mesh->add_primitive(
            erhe::primitive::Primitive{buffer_mesh}
        );
    } else {
        // TODO handle error
        log_render->error("Unable to create material preview mesh - out of memory?");
    }

    using Item_flags = erhe::Item_flags;
    m_mesh->layer_id = m_scene_root->layers().content()->id;
    m_mesh->enable_flag_bits(Item_flags::content | Item_flags::visible | Item_flags::opaque);
    m_node->attach(m_mesh);
    m_node->enable_flag_bits(Item_flags::content | Item_flags::visible);

    const auto paremt = m_scene_root->get_hosted_scene()->get_root_node();
    m_node->set_parent(paremt);

    m_key_light_node = std::make_shared<erhe::scene::Node>("Key Light Node");
    m_key_light      = std::make_shared<erhe::scene::Light>("Key Light");
    m_key_light_node->enable_flag_bits(erhe::Item_flags::content);
    m_key_light->enable_flag_bits(erhe::Item_flags::content);
    m_key_light->layer_id = m_scene_root->layers().light()->id;
    m_key_light_node->attach(m_key_light);
    m_key_light_node->set_parent(paremt);
    m_key_light_node->set_parent_from_node(
        erhe::math::create_look_at(
            glm::vec3{-8.0f, 8.0f, 8.0f},  // eye
            glm::vec3{0.0f, 0.0f, 0.0f},  // center
            glm::vec3{0.0f, 1.0f, 0.0f}   // up
        )
    );

    //// m_fill_light_node = std::make_shared<erhe::scene::Node>("Fill Light Node");
    //// m_fill_light      = std::make_shared<erhe::scene::Light>("Fill Light");
    //// m_fill_light_node->enable_flag_bits(erhe::Item_flags::content);
    //// m_fill_light     ->enable_flag_bits(erhe::Item_flags::content);
    //// m_fill_light     ->layer_id = m_scene_root->layers().light()->id;

    m_camera_node = std::make_shared<erhe::scene::Node>("Camera node");
    m_camera = std::make_shared<erhe::scene::Camera>("Camera");
    m_camera_node->enable_flag_bits(Item_flags::content | Item_flags::show_in_ui);
    m_camera->enable_flag_bits(erhe::Item_flags::content | Item_flags::show_in_ui);
    m_camera->projection()->fov_y = 0.3f;
    m_camera->projection()->z_near = 4.0f;
    m_camera->projection()->z_far = 12.0f;
    m_camera_node->attach(m_camera);
    m_camera_node->set_parent(paremt);
    m_camera_node->set_parent_from_node(
        erhe::math::create_look_at(
            glm::vec3{0.0f, 0.0f, 8.0f},  // eye
            glm::vec3{0.0f, 0.0f, 0.0f},  // center
            glm::vec3{0.0f, 1.0f, 0.0f}   // up
        )
    );

    auto composition_pass = std::make_shared<Composition_pass>("Material Preview Composition_pass");
    composition_pass->mesh_layers = {Mesh_layer_id::content};
    composition_pass->primitive_mode = erhe::primitive::Primitive_mode::polygon_fill;
    composition_pass->filter = erhe::Item_filter{};
    composition_pass->passes = {&m_pipeline_pass};
    {
        std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_composer.mutex};
        m_composer.composition_passes.push_back(composition_pass);
    }
}

void Material_preview::render_preview(const std::shared_ptr<erhe::primitive::Material>& material)
{
    ERHE_PROFILE_FUNCTION();

    erhe::graphics::Scoped_debug_group outer_debug_scope{"Scene_preview::render_preview()"};

    m_content_library->materials->remove_all_children_recursively();
    m_content_library->materials->add(material);
    m_last_material = material;

    m_mesh->get_mutable_primitives().front().material = material;

    const erhe::math::Viewport viewport{0, 0, m_width, m_height};

    const auto& layers = m_scene_root->layers();

    m_light_projections = erhe::scene_renderer::Light_projections{
        layers.light()->lights,
        m_camera.get(),
        viewport,
        erhe::math::Viewport{},
        std::shared_ptr<erhe::graphics::Texture>{},
        erhe::graphics::invalid_texture_handle,
        erhe::graphics::invalid_texture_handle
    };

    erhe::graphics::Render_command_encoder render_encoder = m_graphics_device.make_render_command_encoder(*m_render_pass.get());
    const Render_context context{
        .encoder = &render_encoder,
        .app_context = m_context,
        .scene_view = *this,
        .viewport_config = m_viewport_config,
        .camera = m_camera.get(),
        .viewport_scene_view = nullptr,
        .viewport = erhe::math::Viewport{
            .x = 0,
            .y = 0,
            .width = m_width,
            .height = m_height
        },
        .override_shader_stages = nullptr
    };
    m_composer.render(context);
}

////void Material_preview::generate_torus_geometry()
////{
////    m_torus_geometry = std::make_shared<erhe::geometry::Geometry>(
////        erhe::geometry::shapes::make_torus(
////            m_major_radius,
////            m_minor_radius,
////            m_major_steps,
////            m_minor_steps
////        )
////    );
////}

void Material_preview::show_preview()
{
    m_context.imgui_renderer->image(
        m_color_texture,
        m_width,
        m_height,
        glm::vec2{0.0f, 1.0f},
        glm::vec2{1.0f, 0.0f},
        glm::vec4{0.0f, 0.0f, 0.0f, 0.0f},
        glm::vec4{1.0f, 1.0f, 1.0f, 1.0f},
        erhe::graphics::Filter::nearest,
        erhe::graphics::Sampler_mipmap_mode::not_mipmapped
    );
}

//
//
//

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
    m_context.mesh_memory->gl_buffer_transfer_queue.flush();

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
    const erhe::math::Aabb primitive_local_aabb = brush_scaled.primitive.get_bounding_box();
    ERHE_VERIFY(primitive_local_aabb.is_valid());
    erhe::math::Aabb world_aabb = primitive_local_aabb.transformed_by(m_node->world_from_node());
    ERHE_VERIFY(world_aabb.is_valid());

    const erhe::math::Viewport viewport{
        .x      = 0,
        .y      = 0,
        .width  = m_width,
        .height = m_height
    };

    const float                      time_s   = static_cast<float>(static_cast<double>(time) / 1'000'000'000.0);
    const glm::quat                  rotation = glm::angleAxis(time_s, glm::vec3{0.0f, 1.0f, 0.0f});
    const erhe::scene::Trs_transform node_transform{rotation};
    m_node->set_parent_from_node(node_transform);

    m_camera_node->set_parent_from_node(
        erhe::math::create_look_at(
            glm::vec3{0.0f, 4.0f, 8.0f},  // eye
            glm::vec3{0.0f, 0.0f, 0.0f},  // center
            glm::vec3{0.0f, 1.0f, 0.0f}   // up
        )
    );

    // Frame aabb into camera view
    glm::vec3   camera_position      = m_camera_node->position_in_world();
    glm::vec3   target_position      = world_aabb.center();
    glm::vec3   direction            = target_position - camera_position;
    glm::vec3   direction_normalized = glm::normalize(target_position - camera_position);
    const float size                 = glm::length(world_aabb.diagonal());
    const       erhe::scene::Projection::Fov_sides fov_sides = m_camera->projection()->get_fov_sides(viewport);
    float min_fov_side = std::numeric_limits<float>::max();
    for (float fov_side : { fov_sides.left, fov_sides.right, fov_sides.up, fov_sides.down }) {
        min_fov_side = std::min(std::abs(fov_side), min_fov_side);
    }
    ////float tan_fov_side = std::tanf(min_fov_side);
    const float     tan_fov_side        = tanf(min_fov_side);
    const float     fit_distance        = size / (2.0f * tan_fov_side);
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
        std::shared_ptr<erhe::graphics::Texture>{},
        erhe::graphics::invalid_texture_handle,
        erhe::graphics::invalid_texture_handle
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

    gl::generate_texture_mipmap(texture->gl_name());
}

}  // namespace editor
