#include "rendering_test_app.hpp"
#include "rendering_test_log.hpp"

#include "erhe_dataformat/vertex_format.hpp"
#include "erhe_geometry/geometry.hpp"
#include "erhe_geometry/shapes/box.hpp"
#include "erhe_geometry/shapes/sphere.hpp"
#include "erhe_graphics/blit_command_encoder.hpp"
#include "erhe_graphics/buffer_transfer_queue.hpp"
#include "erhe_graphics/ring_buffer.hpp"
#include "erhe_graphics/swapchain.hpp"
#include "erhe_math/math_util.hpp"
#include "erhe_primitive/build_info.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_primitive/primitive.hpp"
#include "erhe_scene/camera.hpp"
#include "erhe_scene/light.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_verify/verify.hpp"

#include <cstring>

namespace rendering_test {

void Rendering_test::print_conventions()
{
    const auto& c = m_graphics_device.get_info().coordinate_conventions;
    const char* depth_str = (c.native_depth_range == erhe::math::Depth_range::zero_to_one)        ? "[0, 1]"      : "[-1, 1]";
    const char* fb_str    = (c.framebuffer_origin == erhe::math::Framebuffer_origin::bottom_left) ? "bottom-left" : "top-left";
    const char* flip_str  = (c.clip_space_y_flip  == erhe::math::Clip_space_y_flip::enabled)      ? "enabled"     : "disabled";
    const char* tex_str   = (c.texture_origin     == erhe::math::Texture_origin::bottom_left)     ? "bottom-left" : "top-left";
    const bool  y_flip    = (c.clip_space_y_flip  == erhe::math::Clip_space_y_flip::enabled);

    log_test->info("=== Coordinate Conventions ===");
    log_test->info("  Depth range:       {}", depth_str);
    log_test->info("  Framebuffer origin: {}", fb_str);
    log_test->info("  Clip space Y-flip: {}", flip_str);
    log_test->info("  Texture origin:    {}", tex_str);
    log_test->info("  Projection Y-flip: {}", y_flip ? "yes" : "no");
    log_test->info("==============================");
}

void Rendering_test::create_test_scene(erhe::graphics::Command_buffer& init_command_buffer)
{
    // Camera at (1.5, 1.2, 2.4) looking at origin - close enough for cube to fill cell
    {
        auto node   = std::make_shared<erhe::scene::Node>("Camera");
        auto camera = std::make_shared<erhe::scene::Camera>("Camera");
        camera->projection()->fov_y           = glm::radians(50.0f);
        camera->projection()->projection_type = erhe::scene::Projection::Type::perspective_vertical;
        camera->projection()->z_near          = 0.1f;
        camera->projection()->z_far           = 100.0f;
        camera->enable_flag_bits(erhe::Item_flags::content | erhe::Item_flags::show_in_ui);
        node->attach(camera);
        node->set_parent(m_scene.get_root_node());
        node->set_parent_from_node(erhe::math::create_look_at(
            glm::vec3{1.5f, 1.2f, 2.4f},
            glm::vec3{0.0f, 0.0f, 0.0f},
            glm::vec3{0.0f, 1.0f, 0.0f}
        ));
        node->enable_flag_bits(erhe::Item_flags::content | erhe::Item_flags::show_in_ui);
        m_camera = camera;
    }

    // Point light
    {
        auto node  = std::make_shared<erhe::scene::Node>("Light");
        auto light = std::make_shared<erhe::scene::Light>("Light");
        light->type      = erhe::scene::Light::Type::point;
        light->color     = glm::vec3{1.0f, 1.0f, 1.0f};
        light->intensity = 30.0f;
        light->range     = 30.0f;
        light->enable_flag_bits(erhe::Item_flags::content | erhe::Item_flags::show_in_ui);
        node->attach(light);
        node->set_parent(m_scene.get_root_node());
        node->set_parent_from_node(erhe::math::create_look_at(
            glm::vec3{3.0f, 4.0f, 5.0f},
            glm::vec3{0.0f, 0.0f, 0.0f},
            glm::vec3{0.0f, 1.0f, 0.0f}
        ));
        node->enable_flag_bits(erhe::Item_flags::content | erhe::Item_flags::show_in_ui);
        m_light = light;
    }

    // Colored cube
    {
        auto material = std::make_shared<erhe::primitive::Material>(
            erhe::primitive::Material_create_info{.name = "Cube Material"}
        );
        m_materials.push_back(material);

        auto geometry = std::make_shared<erhe::geometry::Geometry>("Orientation Cube");
        erhe::geometry::shapes::make_box(geometry->get_mesh(), 1.0f, 1.0f, 1.0f);
        geometry->process(
            erhe::geometry::Geometry::process_flag_connect |
            erhe::geometry::Geometry::process_flag_build_edges |
            erhe::geometry::Geometry::process_flag_compute_smooth_vertex_normals
        );

        erhe::primitive::Build_info build_info{
            .primitive_types = {.fill_triangles = true, .edge_lines = true},
            .buffer_info = {
                .index_type    = erhe::dataformat::Format::format_32_scalar_uint,
                .vertex_format = m_mesh_memory.vertex_format,
                .buffer_sink   = m_mesh_memory.graphics_buffer_sink
            }
        };

        auto primitive = std::make_shared<erhe::primitive::Primitive>(
            geometry, build_info, erhe::primitive::Normal_style::corner_normals
        );
        m_mesh_memory.buffer_transfer_queue.flush(init_command_buffer);

        const erhe::primitive::Buffer_mesh* buffer_mesh = primitive->get_renderable_mesh();
        if (buffer_mesh != nullptr) {
            const erhe::primitive::Index_range edge_range = buffer_mesh->index_range(erhe::primitive::Primitive_mode::edge_lines);
            log_test->info("Cube edge line index count: {}", edge_range.index_count);
            log_test->info(
                "Cube edge line vertex buffer range: byte_offset={}, count={}, element_size={}",
                buffer_mesh->edge_line_vertex_buffer_range.byte_offset,
                buffer_mesh->edge_line_vertex_buffer_range.count,
                buffer_mesh->edge_line_vertex_buffer_range.element_size
            );
        }

        auto node = std::make_shared<erhe::scene::Node>("Cube");
        auto mesh = std::make_shared<erhe::scene::Mesh>("Cube");
        mesh->add_primitive(primitive, material);
        mesh->enable_flag_bits(erhe::Item_flags::content | erhe::Item_flags::visible | erhe::Item_flags::show_in_ui);
        node->attach(mesh);
        node->set_parent(m_scene.get_root_node());
        node->enable_flag_bits(erhe::Item_flags::content | erhe::Item_flags::visible | erhe::Item_flags::show_in_ui);
        m_test_cube = mesh;
    }

    // Stencil cell (1,3): co-located cube and sphere. The sphere radius
    // is slightly larger than the cube's half-extent so it pokes out
    // beyond the cube on every side. The cube renders first with
    // "stencil always replace 1" so its silhouette stamps stencil = 1;
    // the sphere then renders with "stencil test != 1" so only the
    // parts outside the cube's silhouette become visible.
    //
    // The dedicated stencil_color shaders (compiled with different
    // STENCIL_COLOR defines per pipeline) ignore the material entirely
    // and output a constant color, so a single shared default material
    // is sufficient here.
    auto stencil_material = std::make_shared<erhe::primitive::Material>(
        erhe::primitive::Material_create_info{.name = "Stencil Material"}
    );
    m_materials.push_back(stencil_material);

    // Note: edge_lines are required by cell (1,4) which feeds the
    // sphere into content_wide_line_renderer; the cube doesn't need
    // edges itself, but a single shared build_info is fine.
    const erhe::primitive::Build_info stencil_build_info{
        .primitive_types = {.fill_triangles = true, .edge_lines = true},
        .buffer_info = {
            .index_type    = erhe::dataformat::Format::format_32_scalar_uint,
            .vertex_format = m_mesh_memory.vertex_format,
            .buffer_sink   = m_mesh_memory.graphics_buffer_sink
        }
    };
    {
        auto geometry = std::make_shared<erhe::geometry::Geometry>("Stencil Cube");
        erhe::geometry::shapes::make_box(geometry->get_mesh(), 1.6f, 1.6f, 1.6f);
        geometry->process(
            erhe::geometry::Geometry::process_flag_connect |
            erhe::geometry::Geometry::process_flag_build_edges |
            erhe::geometry::Geometry::process_flag_compute_smooth_vertex_normals
        );

        auto primitive = std::make_shared<erhe::primitive::Primitive>(
            geometry, stencil_build_info, erhe::primitive::Normal_style::corner_normals
        );

        auto node = std::make_shared<erhe::scene::Node>("Stencil Cube");
        auto mesh = std::make_shared<erhe::scene::Mesh>("Stencil Cube");
        mesh->add_primitive(primitive, stencil_material);
        mesh->enable_flag_bits(erhe::Item_flags::content | erhe::Item_flags::visible | erhe::Item_flags::show_in_ui);
        node->attach(mesh);
        node->set_parent(m_scene.get_root_node());
        node->enable_flag_bits(erhe::Item_flags::content | erhe::Item_flags::visible | erhe::Item_flags::show_in_ui);
        m_stencil_cube = mesh;
    }
    {
        auto geometry = std::make_shared<erhe::geometry::Geometry>("Stencil Sphere");
        erhe::geometry::shapes::make_sphere(geometry->get_mesh(), 1.2f, 32, 16);
        geometry->process(
            erhe::geometry::Geometry::process_flag_connect |
            erhe::geometry::Geometry::process_flag_build_edges |
            erhe::geometry::Geometry::process_flag_compute_smooth_vertex_normals
        );

        auto primitive = std::make_shared<erhe::primitive::Primitive>(
            geometry, stencil_build_info, erhe::primitive::Normal_style::point_normals
        );
        m_mesh_memory.buffer_transfer_queue.flush(init_command_buffer);

        auto node = std::make_shared<erhe::scene::Node>("Stencil Sphere");
        auto mesh = std::make_shared<erhe::scene::Mesh>("Stencil Sphere");
        mesh->add_primitive(primitive, stencil_material);
        mesh->enable_flag_bits(erhe::Item_flags::content | erhe::Item_flags::visible | erhe::Item_flags::show_in_ui);
        node->attach(mesh);
        node->set_parent(m_scene.get_root_node());
        node->enable_flag_bits(erhe::Item_flags::content | erhe::Item_flags::visible | erhe::Item_flags::show_in_ui);
        m_stencil_sphere = mesh;
    }
}

void Rendering_test::update_swapchain_render_pass(const int width, const int height)
{
    if (
        m_swapchain_render_pass &&
        (m_swapchain_render_pass->get_render_target_width () == width) &&
        (m_swapchain_render_pass->get_render_target_height() == height)
    ) {
        return;
    }
    m_swapchain_render_pass.reset();
    m_swapchain_depth_texture.reset();

    // Use a depth + stencil format so the diagnostic stencil cells (row 3)
    // can write and test the stencil aspect.
    const erhe::dataformat::Format depth_format = m_graphics_device.choose_depth_stencil_format(
        erhe::graphics::format_flag_require_depth | erhe::graphics::format_flag_require_stencil, 0
    );
    ERHE_VERIFY(erhe::dataformat::get_stencil_size_bits(depth_format) > 0);
    m_swapchain_depth_texture = std::make_unique<erhe::graphics::Texture>(
        m_graphics_device,
        erhe::graphics::Texture_create_info{
            .device      = m_graphics_device,
            .usage_mask  = erhe::graphics::Image_usage_flag_bit_mask::depth_stencil_attachment,
            .type        = erhe::graphics::Texture_type::texture_2d,
            .pixelformat = depth_format,
            .width       = width,
            .height      = height,
            .debug_label = erhe::utility::Debug_label{"Swapchain depth+stencil"}
        }
    );

    erhe::graphics::Render_pass_descriptor desc;
    desc.swapchain = m_graphics_device.get_surface()->get_swapchain();
    desc.color_attachments[0].load_action    = erhe::graphics::Load_action::Clear;
    desc.color_attachments[0].clear_value[0] = 0.02;
    desc.color_attachments[0].clear_value[1] = 0.02;
    desc.color_attachments[0].clear_value[2] = 0.15;
    desc.color_attachments[0].clear_value[3] = 1.0;
    desc.color_attachments[0].usage_before   = erhe::graphics::Image_usage_flag_bit_mask::present;
    desc.color_attachments[0].layout_before  = erhe::graphics::Image_layout::present_src;
    desc.color_attachments[0].usage_after    = erhe::graphics::Image_usage_flag_bit_mask::present;
    desc.color_attachments[0].layout_after   = erhe::graphics::Image_layout::present_src;
    desc.depth_attachment.texture            = m_swapchain_depth_texture.get();
    desc.depth_attachment.load_action        = erhe::graphics::Load_action::Clear;
    desc.depth_attachment.clear_value[0]     = 0.0; // reverse depth: near=1, far=0, clear to 0 (far)
    desc.depth_attachment.usage_before       = erhe::graphics::Image_usage_flag_bit_mask::depth_stencil_attachment;
    desc.depth_attachment.layout_before      = erhe::graphics::Image_layout::depth_stencil_attachment_optimal;
    desc.depth_attachment.usage_after        = erhe::graphics::Image_usage_flag_bit_mask::depth_stencil_attachment;
    desc.depth_attachment.layout_after       = erhe::graphics::Image_layout::depth_stencil_attachment_optimal;
    // The stencil attachment must reference the same texture so the
    // off-screen render-pass build path picks up the stencil load/store
    // ops via Render_pass_attachment_descriptor::is_defined() == true.
    desc.stencil_attachment.texture          = m_swapchain_depth_texture.get();
    desc.stencil_attachment.load_action      = erhe::graphics::Load_action::Clear;
    desc.stencil_attachment.usage_before     = erhe::graphics::Image_usage_flag_bit_mask::depth_stencil_attachment;
    desc.stencil_attachment.layout_before    = erhe::graphics::Image_layout::depth_stencil_attachment_optimal;
    desc.stencil_attachment.usage_after      = erhe::graphics::Image_usage_flag_bit_mask::depth_stencil_attachment;
    desc.stencil_attachment.layout_after     = erhe::graphics::Image_layout::depth_stencil_attachment_optimal;
    desc.render_target_width                 = width;
    desc.render_target_height                = height;
    desc.debug_label                         = erhe::utility::Debug_label{"Swapchain Render_pass"};
    m_swapchain_render_pass = std::make_unique<erhe::graphics::Render_pass>(m_graphics_device, desc);
}

// Allocate / refresh the MSAA depth resolve cell's textures and render
// pass. Renders the cube into a 4x MSAA color+depth pair and resolves the
// depth aspect into a single-sample texture, which the swapchain pass then
// samples to visualize the resolved depth.
void Rendering_test::update_msaa_depth_render_pass(const int width, const int height)
{
    if (
        m_msaa_color_texture &&
        (m_msaa_color_texture->get_width () == width) &&
        (m_msaa_color_texture->get_height() == height)
    ) {
        return;
    }

    m_msaa_depth_render_pass.reset();
    m_msaa_color_texture.reset();
    m_msaa_depth_texture.reset();
    m_msaa_depth_resolve_texture.reset();

    constexpr int msaa_sample_count = 4;

    m_msaa_color_texture = std::make_shared<erhe::graphics::Texture>(
        m_graphics_device,
        erhe::graphics::Texture_create_info{
            .device       = m_graphics_device,
            .usage_mask   = erhe::graphics::Image_usage_flag_bit_mask::color_attachment,
            .type         = erhe::graphics::Texture_type::texture_2d,
            .pixelformat  = erhe::dataformat::Format::format_8_vec4_srgb,
            .sample_count = msaa_sample_count,
            .width        = width,
            .height       = height,
            .debug_label  = erhe::utility::Debug_label{"MSAA depth-resolve color"}
        }
    );

    const erhe::dataformat::Format depth_format = m_graphics_device.choose_depth_stencil_format(
        erhe::graphics::format_flag_require_depth, msaa_sample_count
    );
    m_msaa_depth_texture = std::make_unique<erhe::graphics::Texture>(
        m_graphics_device,
        erhe::graphics::Texture_create_info{
            .device       = m_graphics_device,
            .usage_mask   = erhe::graphics::Image_usage_flag_bit_mask::depth_stencil_attachment,
            .type         = erhe::graphics::Texture_type::texture_2d,
            .pixelformat  = depth_format,
            .sample_count = msaa_sample_count,
            .width        = width,
            .height       = height,
            .debug_label  = erhe::utility::Debug_label{"MSAA depth-resolve depth"}
        }
    );
    m_msaa_depth_resolve_texture = std::make_shared<erhe::graphics::Texture>(
        m_graphics_device,
        erhe::graphics::Texture_create_info{
            .device      = m_graphics_device,
            .usage_mask  =
                erhe::graphics::Image_usage_flag_bit_mask::depth_stencil_attachment |
                erhe::graphics::Image_usage_flag_bit_mask::sampled,
            .type        = erhe::graphics::Texture_type::texture_2d,
            .pixelformat = depth_format,
            .width       = width,
            .height      = height,
            .debug_label = erhe::utility::Debug_label{"MSAA depth-resolve target"}
        }
    );

    erhe::graphics::Render_pass_descriptor desc;
    desc.color_attachments[0].texture        = m_msaa_color_texture.get();
    desc.color_attachments[0].load_action    = erhe::graphics::Load_action::Clear;
    desc.color_attachments[0].store_action   = erhe::graphics::Store_action::Dont_care;
    desc.color_attachments[0].clear_value[0] = 0.10;
    desc.color_attachments[0].clear_value[1] = 0.10;
    desc.color_attachments[0].clear_value[2] = 0.15;
    desc.color_attachments[0].clear_value[3] = 1.00;
    desc.color_attachments[0].usage_before   = erhe::graphics::Image_usage_flag_bit_mask::color_attachment;
    desc.color_attachments[0].layout_before  = erhe::graphics::Image_layout::color_attachment_optimal;
    desc.color_attachments[0].usage_after    = erhe::graphics::Image_usage_flag_bit_mask::color_attachment;
    desc.color_attachments[0].layout_after   = erhe::graphics::Image_layout::color_attachment_optimal;

    // Depth attachment with MSAA resolve. resolve_mode defaults to
    // sample_zero, which is universally supported across backends.
    desc.depth_attachment.texture        = m_msaa_depth_texture.get();
    desc.depth_attachment.resolve_texture= m_msaa_depth_resolve_texture.get();
    desc.depth_attachment.load_action    = erhe::graphics::Load_action::Clear;
    desc.depth_attachment.store_action   = erhe::graphics::Store_action::Multisample_resolve;
    desc.depth_attachment.clear_value[0] = 0.0; // reverse depth
    desc.depth_attachment.usage_before   = erhe::graphics::Image_usage_flag_bit_mask::sampled;
    desc.depth_attachment.layout_before  = erhe::graphics::Image_layout::depth_stencil_read_only_optimal;
    desc.depth_attachment.usage_after    = erhe::graphics::Image_usage_flag_bit_mask::sampled;
    desc.depth_attachment.layout_after   = erhe::graphics::Image_layout::depth_stencil_read_only_optimal;

    desc.render_target_width  = width;
    desc.render_target_height = height;
    desc.debug_label          = erhe::utility::Debug_label{"MSAA depth-resolve Render_pass"};
    m_msaa_depth_render_pass = std::make_unique<erhe::graphics::Render_pass>(m_graphics_device, desc);
}

void Rendering_test::update_texture_render_pass(const int width, const int height)
{
    if (
        m_color_texture &&
        (m_color_texture->get_width () == width) &&
        (m_color_texture->get_height() == height)
    ) {
        return;
    }

    m_texture_render_pass.reset();
    m_color_texture.reset();
    m_depth_texture.reset();

    m_color_texture = std::make_shared<erhe::graphics::Texture>(
        m_graphics_device,
        erhe::graphics::Texture_create_info{
            .device      = m_graphics_device,
            .usage_mask  =
                erhe::graphics::Image_usage_flag_bit_mask::color_attachment |
                erhe::graphics::Image_usage_flag_bit_mask::sampled,
            .type        = erhe::graphics::Texture_type::texture_2d,
            .pixelformat = erhe::dataformat::Format::format_8_vec4_srgb,
            .width       = width,
            .height      = height,
            .debug_label = erhe::utility::Debug_label{"RTT color"}
        }
    );

    const erhe::dataformat::Format depth_format = m_graphics_device.choose_depth_stencil_format(
        erhe::graphics::format_flag_require_depth, 0
    );
    m_depth_texture = std::make_unique<erhe::graphics::Texture>(
        m_graphics_device,
        erhe::graphics::Texture_create_info{
            .device      = m_graphics_device,
            .usage_mask  = erhe::graphics::Image_usage_flag_bit_mask::depth_stencil_attachment,
            .type        = erhe::graphics::Texture_type::texture_2d,
            .pixelformat = depth_format,
            .width       = width,
            .height      = height,
            .debug_label = erhe::utility::Debug_label{"RTT depth"}
        }
    );

    erhe::graphics::Render_pass_descriptor desc;
    desc.color_attachments[0].texture      = m_color_texture.get();
    desc.color_attachments[0].load_action  = erhe::graphics::Load_action::Clear;
    desc.color_attachments[0].clear_value[0] = 0.15;
    desc.color_attachments[0].clear_value[1] = 0.1;
    desc.color_attachments[0].clear_value[2] = 0.1;
    desc.color_attachments[0].clear_value[3] = 1.0;
    desc.color_attachments[0].store_action   = erhe::graphics::Store_action::Store;
    desc.color_attachments[0].usage_before   = erhe::graphics::Image_usage_flag_bit_mask::sampled;
    desc.color_attachments[0].layout_before  = erhe::graphics::Image_layout::shader_read_only_optimal;
    desc.color_attachments[0].usage_after    = erhe::graphics::Image_usage_flag_bit_mask::sampled;
    desc.color_attachments[0].layout_after   = erhe::graphics::Image_layout::shader_read_only_optimal;
    desc.depth_attachment.texture        = m_depth_texture.get();
    desc.depth_attachment.load_action    = erhe::graphics::Load_action::Clear;
    desc.depth_attachment.store_action   = erhe::graphics::Store_action::Dont_care;
    desc.depth_attachment.clear_value[0] = 0.0; // reverse depth
    desc.depth_attachment.usage_before   = erhe::graphics::Image_usage_flag_bit_mask::depth_stencil_attachment;
    desc.depth_attachment.layout_before  = erhe::graphics::Image_layout::depth_stencil_attachment_optimal;
    desc.depth_attachment.usage_after    = erhe::graphics::Image_usage_flag_bit_mask::depth_stencil_attachment;
    desc.depth_attachment.layout_after   = erhe::graphics::Image_layout::depth_stencil_attachment_optimal;
    desc.render_target_width             = width;
    desc.render_target_height            = height;
    desc.debug_label                     = erhe::utility::Debug_label{"RTT Render_pass"};
    m_texture_render_pass = std::make_unique<erhe::graphics::Render_pass>(m_graphics_device, desc);
}

auto Rendering_test::get_grid_tile_viewport(int col, int row) const -> erhe::math::Viewport
{
    const int cols = (m_settings.cols > 0) ? m_settings.cols : 1;
    const int rows = (m_settings.rows > 0) ? m_settings.rows : 1;
    const int full_width  = m_window.get_width();
    const int full_height = m_window.get_height();
    const int cell_width  = full_width  / cols;
    const int cell_height = full_height / rows;
    // On OpenGL, viewport Y=0 is at the bottom of the screen.
    // Flip row so that row 0 appears at the top.
    const auto& conventions = m_graphics_device.get_info().coordinate_conventions;
    const int y_row = (conventions.framebuffer_origin == erhe::math::Framebuffer_origin::bottom_left)
        ? (rows - 1 - row)
        : row;
    return erhe::math::Viewport{
        .x      = col * cell_width,
        .y      = y_row * cell_height,
        .width  = cell_width,
        .height = cell_height
    };
}

auto Rendering_test::is_fullscreen_mode() const -> bool
{
    return (m_settings.fullscreen_cell_col >= 0) && (m_settings.fullscreen_cell_row >= 0);
}

auto Rendering_test::is_replicate_mode() const -> bool
{
    return (m_settings.replicate_cell_col >= 0) && (m_settings.replicate_cell_row >= 0);
}

auto Rendering_test::get_subtest_at(int col, int row) const -> std::string_view
{
    const int cols = m_settings.cols;
    const int rows = m_settings.rows;
    if ((col < 0) || (row < 0) || (col >= cols) || (row >= rows)) {
        return {};
    }
    const std::size_t index = static_cast<std::size_t>(row) * static_cast<std::size_t>(cols) + static_cast<std::size_t>(col);
    if (index >= m_settings.cells.size()) {
        return {};
    }
    const std::string& name = m_settings.cells[index];
    if (name == "empty") {
        return {};
    }
    return name;
}

// Reports whether a given subtest name is present somewhere in the
// currently-effective cell layout. Fullscreen and replicate modes
// collapse the effective layout to a single subtest (the one at the
// chosen grid slot). Used for pre-pass gating (RTT, MSAA) and for
// deciding which compute-wide-line groups to dispatch.
auto Rendering_test::has_subtest(std::string_view name) const -> bool
{
    if (is_fullscreen_mode()) {
        return get_subtest_at(m_settings.fullscreen_cell_col, m_settings.fullscreen_cell_row) == name;
    }
    if (is_replicate_mode()) {
        return get_subtest_at(m_settings.replicate_cell_col, m_settings.replicate_cell_row) == name;
    }
    for (const std::string& entry : m_settings.cells) {
        if (entry == name) {
            return true;
        }
    }
    return false;
}

void Rendering_test::create_test_textures(erhe::graphics::Command_buffer& init_command_buffer)
{
    // Create small solid-color textures for texture heap testing
    constexpr int tex_size = 4;

    auto make_solid_texture = [&](const char* name, uint8_t r, uint8_t g, uint8_t b) {
        auto texture = std::make_shared<erhe::graphics::Texture>(
            m_graphics_device,
            erhe::graphics::Texture_create_info{
                .device      = m_graphics_device,
                .usage_mask  =
                    erhe::graphics::Image_usage_flag_bit_mask::sampled |
                    erhe::graphics::Image_usage_flag_bit_mask::transfer_dst,
                .type        = erhe::graphics::Texture_type::texture_2d,
                .pixelformat = erhe::dataformat::Format::format_8_vec4_srgb,
                .width       = tex_size,
                .height      = tex_size,
                .debug_label = erhe::utility::Debug_label{std::string_view{name}}
            }
        );
        uint8_t pixels[tex_size * tex_size * 4];
        for (int i = 0; i < tex_size * tex_size; ++i) {
            pixels[i * 4 + 0] = r;
            pixels[i * 4 + 1] = g;
            pixels[i * 4 + 2] = b;
            pixels[i * 4 + 3] = 255;
        }
        const int row_stride = tex_size * 4;
        const int byte_count = tex_size * row_stride;
        erhe::graphics::Ring_buffer_client upload_buf{
            m_graphics_device, erhe::graphics::Buffer_target::transfer_src, "test texture upload"
        };
        erhe::graphics::Ring_buffer_range range = upload_buf.acquire(erhe::graphics::Ring_buffer_usage::CPU_write, byte_count);
        std::memcpy(range.get_span().data(), pixels, byte_count);
        range.bytes_written(byte_count);
        range.close();
        erhe::graphics::Blit_command_encoder blit{m_graphics_device, init_command_buffer};
        blit.copy_from_buffer(
            range.get_buffer()->get_buffer(), range.get_byte_start_offset_in_buffer(),
            row_stride, byte_count,
            glm::ivec3{tex_size, tex_size, 1},
            texture.get(), 0, 0,
            glm::ivec3{0, 0, 0}
        );
        range.release();
        return texture;
    };
    m_red_texture   = make_solid_texture("test red",   255, 0, 0);
    m_green_texture = make_solid_texture("test green", 0, 255, 0);
    m_blue_texture  = make_solid_texture("test blue",  0, 0, 255);
}

} // namespace rendering_test
