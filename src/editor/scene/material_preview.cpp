#include "scene/material_preview.hpp"

#include "app_context.hpp"
#include "renderers/mesh_memory.hpp"
#include "renderers/programs.hpp"
#include "renderers/render_context.hpp"
#include "renderers/renderpass.hpp"
#include "renderers/viewport_config.hpp"
#include "scene/scene_root.hpp"
#include "scene/content_library.hpp"

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

Material_preview::Material_preview(
    erhe::graphics::Device&         graphics_device,
    erhe::scene::Scene_message_bus& scene_message_bus,
    App_context&                    context,
    Mesh_memory&                    mesh_memory,
    Programs&                       programs
)
    : Scene_view{context, Viewport_config{}}
    , m_graphics_device    {graphics_device}
    , m_pipeline_renderpass{erhe::graphics::Render_pipeline_state{{
        .name           = "Polygon Fill Opaque",
        .shader_stages  = &programs.standard.shader_stages,
        .vertex_input   = &mesh_memory.vertex_input,
        .input_assembly = Input_assembly_state::triangles,
        .rasterization  = Rasterization_state::cull_mode_back_ccw,
        .depth_stencil  = Depth_stencil_state::depth_test_enabled_stencil_test_disabled(),
        .color_blend    = Color_blend_state::color_blend_disabled
    }}}
    , m_composer{"Material Preview Composer"}
{
    ERHE_PROFILE_FUNCTION();

    m_content_library = std::make_shared<Content_library>();

    m_scene_root = std::make_shared<Scene_root>(
        nullptr, // No content library
        nullptr, // No content library
        scene_message_bus,
        nullptr, // No content library
        nullptr, // Don't process editor messages
        nullptr, // Don't register to App_scenes
        m_content_library,
        "Material preview scene"
    );

    m_scene_root->get_scene().disable_flag_bits(erhe::Item_flags::show_in_ui);

    make_preview_scene(mesh_memory);

    set_area_size(256);
    update_rendertarget(graphics_device);
}

void Material_preview::set_area_size(int size)
{
    size = std::max(1, size);
    m_width  = size;
    m_height = size;
}

void Material_preview::update_rendertarget(erhe::graphics::Device& graphics_device)
{
    ERHE_PROFILE_FUNCTION();

    if (
        m_color_texture &&
        (m_color_texture->get_width () == m_width) &&
        (m_color_texture->get_height() == m_height)
    )
    {
        return;
    }
    m_color_format = erhe::dataformat::Format::format_16_vec4_float;
    m_depth_format = erhe::dataformat::Format::format_d32_sfloat;

    using Render_pass  = erhe::graphics::Render_pass;
    using Renderbuffer = erhe::graphics::Renderbuffer;
    using Texture      = erhe::graphics::Texture;
    m_color_texture = std::make_shared<Texture>(
        graphics_device,
        Texture::Create_info{
            .device      = graphics_device,
            .target      = gl::Texture_target::texture_2d,
            .pixelformat = m_color_format,
            .width       = m_width,
            .height      = m_height,
            .debug_label = "Material preview Color Texture"
        }
    );
    const float clear_value[4] = { 1.0f, 0.0f, 0.5f, 0.0f };
    gl::clear_tex_image(
        m_color_texture->gl_name(),
        0,
        gl::Pixel_format::rgba,
        gl::Pixel_type::float_,
        &clear_value[0]
    );

    m_depth_renderbuffer = std::make_unique<erhe::graphics::Renderbuffer>(
        graphics_device,
        m_depth_format,
        m_width,
        m_height
    );

    m_depth_renderbuffer->set_debug_label("Material Preview Depth Renderbuffer");

    erhe::graphics::Render_pass_descriptor render_pass_descriptor; 
    render_pass_descriptor.color_attachments[0].texture      = m_color_texture.get();
    render_pass_descriptor.color_attachments[0].load_action  = erhe::graphics::Load_action::Clear;
    render_pass_descriptor.color_attachments[0].store_action = erhe::graphics::Store_action::Store;
    render_pass_descriptor.color_attachments[0].clear_value  = {0.0, 0.0, 0.0, 0.333};
    render_pass_descriptor.depth_attachment.renderbuffer     = m_depth_renderbuffer.get();
    render_pass_descriptor.depth_attachment.load_action      = erhe::graphics::Load_action::Clear;
    render_pass_descriptor.depth_attachment.clear_value[0]   = 0.0; // Reverse Z
    render_pass_descriptor.depth_attachment.store_action     = erhe::graphics::Store_action::Dont_care;
    render_pass_descriptor.render_target_width               = m_width;
    render_pass_descriptor.render_target_height              = m_height;
    render_pass_descriptor.debug_label                       = "Material Preview Render_pass";
    m_render_pass = std::make_unique<erhe::graphics::Render_pass>(graphics_device, render_pass_descriptor);

    m_shadow_texture = std::make_shared<Texture>(
        graphics_device,
        Texture::Create_info {
            .device            = graphics_device,
            .target            = gl::Texture_target::texture_2d_array,
            .pixelformat       = erhe::dataformat::Format::format_d32_sfloat,
            .width             = 1,
            .height            = 1,
            .depth             = 1,
            .array_layer_count = 1,
            .debug_label       = "Material_preview::m_shadow_texture (dummy shadowmap)"
        }
    );

    const float depth_clear_value = 0.0f; // reverse Z
    gl::clear_tex_image(m_shadow_texture->gl_name(), 0, gl::Pixel_format::depth_component, gl::Pixel_type::float_, &depth_clear_value);
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
            .primitive_types = { .fill_triangles = true },
            .buffer_info     = mesh_memory.buffer_info
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
    m_node->attach          (m_mesh);
    m_node->enable_flag_bits(Item_flags::content | Item_flags::visible);

    const auto paremt = m_scene_root->get_hosted_scene()->get_root_node();
    m_node->set_parent(paremt);

    m_key_light_node  = std::make_shared<erhe::scene::Node>("Key Light Node");
    m_key_light       = std::make_shared<erhe::scene::Light>("Key Light");
    m_key_light_node ->enable_flag_bits(erhe::Item_flags::content);
    m_key_light      ->enable_flag_bits(erhe::Item_flags::content);
    m_key_light      ->layer_id = m_scene_root->layers().light()->id;
    m_key_light_node ->attach(m_key_light);
    m_key_light_node ->set_parent(paremt);
    m_key_light_node->set_parent_from_node(
        erhe::math::create_look_at(
            glm::vec3{-8.0f, 8.0f, 8.0f},  // eye
            glm::vec3{ 0.0f, 0.0f, 0.0f},  // center
            glm::vec3{ 0.0f, 1.0f, 0.0f}   // up
        )
    );

    //// m_fill_light_node = std::make_shared<erhe::scene::Node>("Fill Light Node");
    //// m_fill_light      = std::make_shared<erhe::scene::Light>("Fill Light");
    //// m_fill_light_node->enable_flag_bits(erhe::Item_flags::content);
    //// m_fill_light     ->enable_flag_bits(erhe::Item_flags::content);
    //// m_fill_light     ->layer_id = m_scene_root->layers().light()->id;

    m_camera_node = std::make_shared<erhe::scene::Node>("Camera node");
    m_camera      = std::make_shared<erhe::scene::Camera>("Camera");
    m_camera_node->enable_flag_bits(Item_flags::content | Item_flags::show_in_ui);
    m_camera     ->enable_flag_bits(erhe::Item_flags::content | Item_flags::show_in_ui);
    m_camera     ->projection()->fov_y  =  0.3f;
    m_camera     ->projection()->z_near =  4.0f;
    m_camera     ->projection()->z_far  = 12.0f;
    m_camera_node->attach(m_camera);
    m_camera_node->set_parent(paremt);
    m_camera_node->set_parent_from_node(
        erhe::math::create_look_at(
            glm::vec3{0.0f, 0.0f, 8.0f},  // eye
            glm::vec3{0.0f, 0.0f, 0.0f},  // center
            glm::vec3{0.0f, 1.0f, 0.0f}   // up
        )
    );

    using erhe::graphics::Vertex_input_state;
    using erhe::graphics::Input_assembly_state;
    using erhe::graphics::Rasterization_state;
    using erhe::graphics::Depth_stencil_state;
    using erhe::graphics::Color_blend_state;

    auto renderpass = std::make_shared<Renderpass>("Material Preview Renderpass");
    renderpass->mesh_layers      = { Mesh_layer_id::content };
    renderpass->primitive_mode   = erhe::primitive::Primitive_mode::polygon_fill;
    renderpass->filter           = erhe::Item_filter{};
    renderpass->passes           = { &m_pipeline_renderpass };
    std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_composer.mutex};
    m_composer.renderpasses.push_back(renderpass);
}

void Material_preview::render_preview(const std::shared_ptr<erhe::primitive::Material>& material)
{
    ERHE_PROFILE_FUNCTION();

    erhe::graphics::Scoped_debug_group outer_debug_scope{"Material_preview::render_preview()"};

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

    const Render_context context{
        .app_context         = m_context,
        .scene_view          = *this,
        .viewport_config     = m_viewport_config,
        .camera              = m_camera.get(),
        .viewport_scene_view = nullptr,
        .viewport            = erhe::math::Viewport{
            .x      = 0,
            .y      = 0,
            .width  = m_width,
            .height = m_height
        },
        .override_shader_stages = nullptr
    };

    std::unique_ptr<erhe::graphics::Render_command_encoder> render_encoder = m_graphics_device.make_render_command_encoder(*m_render_pass.get());
    m_composer.render(context);
}

auto Material_preview::get_scene_root() const -> std::shared_ptr<Scene_root>
{
    return m_scene_root;
}

auto Material_preview::get_camera() const -> std::shared_ptr<erhe::scene::Camera>
{
    return m_camera;
}

auto Material_preview::get_rendergraph_node() -> erhe::rendergraph::Rendergraph_node*
{
    return {};
}

auto Material_preview::get_light_projections() const -> const erhe::scene_renderer::Light_projections*
{
    return &m_light_projections;
}

auto Material_preview::get_shadow_texture() const -> erhe::graphics::Texture*
{
    return m_shadow_texture.get();
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

auto Material_preview::get_content_library() -> std::shared_ptr<Content_library>
{
    return m_content_library;
}

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
        false
    );
}

}  // namespace editor
