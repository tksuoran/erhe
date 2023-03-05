#include "scene/material_preview.hpp"

#include "editor_rendering.hpp"
#include "editor_scenes.hpp"
#include "editor_log.hpp"
#include "renderers/mesh_memory.hpp"
#include "renderers/render_context.hpp"
#include "renderers/viewport_config.hpp"
#include "scene/scene_root.hpp"
#include "scene/content_library.hpp"

#include "erhe/application/commands/commands.hpp"
#include "erhe/application/configuration.hpp"
#include "erhe/application/graphics/gl_context_provider.hpp"
#include "erhe/application/imgui/imgui_renderer.hpp"
#include "erhe/geometry/geometry.hpp"
#include "erhe/geometry/shapes/sphere.hpp"
#include "erhe/geometry/shapes/torus.hpp"
#include "erhe/gl/wrapper_functions.hpp"
#include "erhe/gl/enum_bit_mask_operators.hpp"
#include "erhe/graphics/framebuffer.hpp"
#include "erhe/graphics/renderbuffer.hpp"
#include "erhe/graphics/texture.hpp"
#include "erhe/physics/iworld.hpp"
#include "erhe/primitive/primitive_builder.hpp"
#include "erhe/raytrace/iscene.hpp"
#include "erhe/scene/light.hpp"
#include "erhe/scene/mesh.hpp"
#include "erhe/scene/node.hpp"
#include "erhe/scene/scene.hpp"
#include "erhe/toolkit/bit_helpers.hpp"
#include "erhe/toolkit/verify.hpp"

#include <fmt/format.h>

namespace editor {

Material_preview* g_material_preview{nullptr};

Material_preview::Material_preview()
    : erhe::components::Component{c_type_name}
{
}

Material_preview::~Material_preview() noexcept
{
    ERHE_VERIFY(g_material_preview == nullptr);
}

void Material_preview::deinitialize_component()
{
    ERHE_VERIFY(g_material_preview == this);
    m_scene_root->sanity_check();

    m_color_texture.reset();
    m_depth_renderbuffer.reset();
    m_framebuffer.reset();

    m_mesh.reset();
    m_node.reset();

    m_key_light.reset();
    m_key_light_node.reset();

    m_camera.reset();
    m_camera_node.reset();

    m_shadow_texture.reset();
    m_last_material.reset();
    m_content_library.reset();
    m_scene_root->sanity_check();

    const auto use_count = m_scene_root.use_count();
    ERHE_VERIFY(use_count == 1);
    m_scene_root.reset();
    reset_hover_slots();
    g_material_preview = nullptr;
}

void Material_preview::declare_required_components()
{
    require<Editor_scenes>();
    require<Mesh_memory>();
}

void Material_preview::initialize_component()
{
    ERHE_VERIFY(g_material_preview == nullptr);
    m_content_library = std::make_shared<Content_library>();
    m_content_library->is_shown_in_ui = false;

    m_scene_root = std::make_shared<Scene_root>(
        m_content_library,
        "Material preview scene"
    );

    m_scene_root->get_shared_scene()->disable_flag_bits(erhe::scene::Item_flags::show_in_ui);

    make_preview_scene();
    make_rendertarget();

    g_material_preview = this;
}

void Material_preview::make_rendertarget()
{
    const erhe::application::Scoped_gl_context gl_context;

    m_width        = 256;
    m_height       = 256;
    m_color_format = gl::Internal_format::rgba16f;
    m_depth_format = gl::Internal_format::depth_component32f;

    using Framebuffer  = erhe::graphics::Framebuffer;
    using Renderbuffer = erhe::graphics::Renderbuffer;
    using Texture      = erhe::graphics::Texture;
    m_color_texture = std::make_shared<Texture>(
        Texture::Create_info{
            .target          = gl::Texture_target::texture_2d,
            .internal_format = m_color_format,
            .width           = m_width,
            .height          = m_height
        }
    );
    m_color_texture->set_debug_label("Material Preview Color Texture");
    const float clear_value[4] = { 1.0f, 0.0f, 0.5f, 0.0f };
    gl::clear_tex_image(
        m_color_texture->gl_name(),
        0,
        gl::Pixel_format::rgba,
        gl::Pixel_type::float_,
        &clear_value[0]
    );

    m_depth_renderbuffer = std::make_unique<erhe::graphics::Renderbuffer>(
        m_depth_format,
        m_width,
        m_height
    );

    m_depth_renderbuffer->set_debug_label("Material Preview Depth Renderbuffer");

    Framebuffer::Create_info create_info;
    create_info.attach(
        gl::Framebuffer_attachment::color_attachment0,
        m_color_texture.get()
    );

    create_info.attach(
        gl::Framebuffer_attachment::depth_attachment,
        m_depth_renderbuffer.get()
    );
    m_framebuffer = std::make_unique<Framebuffer>(create_info);
    m_framebuffer->set_debug_label("Material Preview Framebuffer");

    gl::Color_buffer draw_buffers[] = { gl::Color_buffer::color_attachment0 };
    gl::named_framebuffer_draw_buffers(m_framebuffer->gl_name(), 1, &draw_buffers[0]);
    gl::named_framebuffer_read_buffer(m_framebuffer->gl_name(), gl::Color_buffer::color_attachment0);

    if (!m_framebuffer->check_status()) {
        m_framebuffer.reset();
    }

    m_shadow_texture = std::make_shared<Texture>(
        Texture::Create_info
        {
            .target          = gl::Texture_target::texture_2d_array,
            .internal_format = gl::Internal_format::depth_component32f,
            .width           = 1,
            .height          = 1,
            .depth           = 1
        }
    );

    const bool reverse_depth = erhe::application::g_configuration->graphics.reverse_depth;
    m_shadow_texture->set_debug_label("Material Preview Shadowmap");
    float depth_clear_value = reverse_depth ? 0.0f : 1.0f;
    gl::clear_tex_image(
        m_shadow_texture->gl_name(),
        0,
        gl::Pixel_format::depth_component,
        gl::Pixel_type::float_,
        &depth_clear_value
    );
}

void Material_preview::make_preview_scene()
{
    const erhe::application::Scoped_gl_context gl_context;

    erhe::geometry::Geometry sphere_geometry = erhe::geometry::shapes::make_sphere(
        m_radius,
        std::max(1, m_slice_count),
        std::max(1, m_stack_count)
    );
    erhe::primitive::Primitive_geometry gl_primitive_geometry = erhe::primitive::make_primitive(
        sphere_geometry,
        g_mesh_memory->build_info,
        erhe::primitive::Normal_style::corner_normals
    );

    m_node = std::make_shared<erhe::scene::Node>("Material Preview Node");
    m_mesh = std::make_shared<erhe::scene::Mesh>("Material Preview Mesh");
    m_mesh->mesh_data.primitives.push_back(
        erhe::primitive::Primitive{
            .gl_primitive_geometry = gl_primitive_geometry,
            .normal_style          = erhe::primitive::Normal_style::corner_normals
        }
    );

    using Item_flags = erhe::scene::Item_flags;
    m_mesh->mesh_data.layer_id = m_scene_root->layers().content()->id;
    m_mesh->enable_flag_bits(Item_flags::content | Item_flags::visible | Item_flags::opaque);
    m_node->attach          (m_mesh);
    m_node->enable_flag_bits(Item_flags::content | Item_flags::visible);

    const auto paremt = m_scene_root->get_hosted_scene()->get_root_node();
    m_node->set_parent(paremt);

    m_key_light_node  = std::make_shared<erhe::scene::Node>("Key Light Node");
    m_key_light       = std::make_shared<erhe::scene::Light>("Key Light");
    m_key_light_node ->enable_flag_bits(erhe::scene::Item_flags::content);
    m_key_light      ->enable_flag_bits(erhe::scene::Item_flags::content);
    m_key_light      ->layer_id = m_scene_root->layers().light()->id;
    m_key_light_node ->attach(m_key_light);
    m_key_light_node ->set_parent(paremt);
    m_key_light_node->set_parent_from_node(
        erhe::toolkit::create_look_at(
            glm::vec3{-8.0f, 8.0f, 8.0f},  // eye
            glm::vec3{ 0.0f, 0.0f, 0.0f},  // center
            glm::vec3{ 0.0f, 1.0f, 0.0f}   // up
        )
    );

    //// m_fill_light_node = std::make_shared<erhe::scene::Node>("Fill Light Node");
    //// m_fill_light      = std::make_shared<erhe::scene::Light>("Fill Light");
    //// m_fill_light_node->enable_flag_bits(erhe::scene::Item_flags::content);
    //// m_fill_light     ->enable_flag_bits(erhe::scene::Item_flags::content);
    //// m_fill_light     ->layer_id = m_scene_root->layers().light()->id;

    m_camera_node = std::make_shared<erhe::scene::Node>("Camera node");
    m_camera      = std::make_shared<erhe::scene::Camera>("Camera");
    m_camera_node->enable_flag_bits(Item_flags::content | Item_flags::show_in_ui);
    m_camera     ->enable_flag_bits(erhe::scene::Item_flags::content | Item_flags::show_in_ui);
    m_camera     ->projection()->fov_y  =  0.3f;
    m_camera     ->projection()->z_near =  4.0f;
    m_camera     ->projection()->z_far  = 12.0f;
    m_camera_node->attach(m_camera);
    m_camera_node->set_parent(paremt);
    m_camera_node->set_parent_from_node(
        erhe::toolkit::create_look_at(
            glm::vec3{0.0f, 0.0f, 8.0f},  // eye
            glm::vec3{0.0f, 0.0f, 0.0f},  // center
            glm::vec3{0.0f, 1.0f, 0.0f}   // up
        )
    );

}

void Material_preview::render_preview(
    const std::shared_ptr<erhe::primitive::Material>& material
    //const erhe::scene::Viewport&                      viewport
)
{
    erhe::graphics::Scoped_debug_group outer_debug_scope{"Material preview"};

    m_content_library->materials.entries().clear();
    m_content_library->materials.add(material);
    m_last_material = material;

    m_mesh->mesh_data.primitives.front().material = material;

    erhe::scene::Viewport viewport{
        0, 0, m_width, m_height
    };

    gl::enable(gl::Enable_cap::scissor_test);
    gl::scissor(viewport.x, viewport.y, viewport.width, viewport.height);
    gl::clear_color(
        m_clear_color[0],
        m_clear_color[1],
        m_clear_color[2],
        m_clear_color[3]
    );
    gl::clear_stencil(0);
    gl::clear_depth_f(*erhe::application::g_configuration->depth_clear_value_pointer());

    Viewport_config viewport_config;
    viewport_config.post_processing_enable = false;

    const auto& layers = m_scene_root->layers();

    m_light_projections = Light_projections{
        layers.light()->lights,
        m_camera.get(),
        erhe::scene::Viewport{},
        0
    };

    const Render_context context
    {
        .scene_view      = this,
        .viewport_window = nullptr,
        .viewport_config = &viewport_config,
        .camera          = m_camera.get(),
        .viewport        = erhe::scene::Viewport{
            .x      = 0,
            .y      = 0,
            .width  = m_width,
            .height = m_height
        },
        .override_shader_stages = nullptr
    };

    gl::bind_framebuffer(gl::Framebuffer_target::draw_framebuffer, m_framebuffer->gl_name());
    gl::clear(
        gl::Clear_buffer_mask::color_buffer_bit |
        gl::Clear_buffer_mask::depth_buffer_bit
    );
    using Fill_mode      = IEditor_rendering::Fill_mode;
    using Blend_mode     = IEditor_rendering::Blend_mode;
    using Selection_mode = IEditor_rendering::Selection_mode;
    g_editor_rendering->render_content(context, Fill_mode::fill, Blend_mode::opaque, Selection_mode::not_selected);
    gl::disable(gl::Enable_cap::scissor_test);
}

auto Material_preview::get_scene_root() const -> std::shared_ptr<Scene_root>
{
    return m_scene_root;
}

auto Material_preview::get_camera() const -> std::shared_ptr<erhe::scene::Camera>
{
    return m_camera;
}

auto Material_preview::get_rendergraph_node() -> std::shared_ptr<erhe::application::Rendergraph_node>
{
    return {};
}

auto Material_preview::get_light_projections() const -> const Light_projections*
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

[[nodiscard]] auto Material_preview::get_content_library() -> std::shared_ptr<Content_library>
{
    return m_content_library;
}

void Material_preview::show_preview()
{
    erhe::application::g_imgui_renderer->image(
        m_color_texture,
        m_width,
        m_height,
        glm::vec2{0.0f, 1.0f},
        glm::vec2{1.0f, 0.0f},
        glm::vec4{1.0f, 1.0f, 1.0f, 1.0f},
        false
    );
}

}  // namespace erhe::application
