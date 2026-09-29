#include "shadow_gpu_test_fixture.hpp"
#include "scene_renderer_test_logging.hpp"

#include "erhe_scene_renderer/camera_buffer.hpp"
#include "erhe_scene_renderer/shader_key.hpp"

#include "erhe_dataformat/vertex_format.hpp"
#include "erhe_graphics/command_buffer.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/render_command_encoder.hpp"
#include "erhe_graphics/state/color_blend_state.hpp"
#include "erhe_graphics/state/depth_stencil_state.hpp"
#include "erhe_graphics/state/input_assembly_state.hpp"
#include "erhe_graphics/state/rasterization_state.hpp"
#include "erhe_math/math_util.hpp"
#include "erhe_math/viewport.hpp"
#include "erhe_primitive/primitive.hpp"
#include "erhe_primitive/triangle_soup.hpp"
#include "erhe_scene/projection.hpp"

#include <fmt/format.h>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <array>
#include <bit>
#include <cstring>
#include <filesystem>
#include <span>

namespace erhe::scene_renderer::test {

namespace {

[[nodiscard]] auto repo_path(const char* relative) -> std::filesystem::path
{
    return std::filesystem::path{ERHE_SCENE_RENDERER_TEST_REPO_ROOT} / std::filesystem::path{relative};
}

class Box_face
{
public:
    glm::vec3 normal;
    glm::vec3 corners[4]; // counter-clockwise seen from outside
};

} // anonymous namespace

auto c_str(const Shadow_light_kind light_kind) -> const char*
{
    switch (light_kind) {
        case Shadow_light_kind::directional: return "directional";
        case Shadow_light_kind::spot:        return "spot";
        default:                             return "?";
    }
}

auto get_shadow_filter_cases() -> const std::vector<Shadow_filter_case>&
{
    static const std::vector<Shadow_filter_case> cases{
        Shadow_filter_case{.name = "hard",                   .filter = 0, .bias = 1},
        Shadow_filter_case{.name = "pcf_2x2",                .filter = 2, .bias = 1},
        Shadow_filter_case{.name = "pcf_4x4/receiver_plane", .filter = 4, .bias = 1},
        Shadow_filter_case{.name = "pcf_4x4/slope_scaled",   .filter = 4, .bias = 0},
        Shadow_filter_case{.name = "pcf_6x6/receiver_plane", .filter = 6, .bias = 1},
        Shadow_filter_case{.name = "pcf_6x6/slope_scaled",   .filter = 6, .bias = 0}
    };
    return cases;
}

auto get_shadow_poses() -> const std::vector<Shadow_pose>&
{
    static const std::vector<Shadow_pose> poses{
        Shadow_pose{.spot_height = 2.0f, .shadow_range = 4.0f, .frame_axis = { 0.0f,  1.0f, 0.0f}, .frame_angle = 0.0f, .frame_translation = { 0.0f,  0.0f,   0.0f }},
        Shadow_pose{.spot_height = 2.3f, .shadow_range = 4.7f, .frame_axis = { 1.0f,  0.3f, 0.2f}, .frame_angle = 0.3f, .frame_translation = { 0.37f, 0.11f, -0.61f}},
        Shadow_pose{.spot_height = 2.9f, .shadow_range = 5.9f, .frame_axis = {-0.2f,  1.0f, 0.5f}, .frame_angle = 1.1f, .frame_translation = {-1.3f,  0.7f,   0.9f }},
        Shadow_pose{.spot_height = 3.4f, .shadow_range = 7.3f, .frame_axis = { 0.6f, -0.4f, 1.0f}, .frame_angle = 2.2f, .frame_translation = { 3.1f, -0.4f,   1.7f }}
    };
    return poses;
}

auto Shadow_pose::get_world_from_station() const -> glm::mat4
{
    const glm::mat4 rotation    = glm::rotate(glm::mat4{1.0f}, frame_angle, glm::normalize(frame_axis));
    const glm::mat4 translation = glm::translate(glm::mat4{1.0f}, frame_translation);
    return translation * rotation;
}

auto describe(const Shadow_light_kind light_kind, const Shadow_pose& pose) -> std::string
{
    const std::string frame = fmt::format("frame angle {}", pose.frame_angle);
    return (light_kind == Shadow_light_kind::spot)
        ? fmt::format("spot height {} {}", pose.spot_height, frame)
        : fmt::format("directional shadow range {} {}", pose.shadow_range, frame);
}

auto Shadow_tie_result::total_failing() const -> int
{
    int total = 0;
    for (const int count : failing_per_ulp) {
        total += count;
    }
    return total;
}

auto Shadow_tie_result::describe() const -> std::string
{
    std::string result = fmt::format("failing pixels per k (of {} each):", pixels_per_ulp);
    for (std::size_t i = 0, end = failing_per_ulp.size(); i < end; ++i) {
        const int k = static_cast<int>(i) - max_ulps;
        result += fmt::format(" k={:+d}:{}", k, failing_per_ulp[i]);
    }
    return result;
}

auto Visibility_image::at(const int x, const int y) const -> float
{
    return visibility[(static_cast<std::size_t>(y) * static_cast<std::size_t>(size)) + static_cast<std::size_t>(x)];
}

// The view is symmetric about the origin in both image axes, so which image
// axis shows station x and which station z (and in which direction) does not
// matter to the region tests built on these.
auto Visibility_image::station_x(const int x) const -> float
{
    return ((((static_cast<float>(x) + 0.5f) / static_cast<float>(size)) * 2.0f) - 1.0f) * extent;
}

auto Visibility_image::station_z(const int y) const -> float
{
    return ((((static_cast<float>(y) + 0.5f) / static_cast<float>(size)) * 2.0f) - 1.0f) * extent;
}

void Shadow_gpu_test::SetUp()
{
    Gpu_test::SetUp();
    initialize_scene_renderer_test_logging();

    erhe::graphics::Device& graphics_device = device();

    m_mesh_memory = std::make_unique<Mesh_memory>(m_mesh_memory_config, graphics_device);
    m_program_interface_config.shader_paths = {
        repo_path("res/shaders"),
        repo_path("src/erhe/scene_renderer/test/shaders")
    };
    m_program_interface    = std::make_unique<Program_interface>(graphics_device, *m_mesh_memory, m_program_interface_config);
    m_shader_variant_cache = std::make_unique<Shader_variant_cache>(graphics_device, *m_program_interface);
    m_fallback_sampler     = std::make_unique<erhe::graphics::Sampler>(
        graphics_device,
        erhe::graphics::Sampler_create_info{.debug_label = "shadow test fallback sampler"}
    );

    // The constructors below record init-time uploads / clears into a
    // recording command buffer.
    submit_and_wait(
        [&](erhe::graphics::Command_buffer& command_buffer) {
            m_fallback_texture = graphics_device.create_dummy_texture(command_buffer, erhe::dataformat::Format::format_8_vec4_srgb);
            m_material_set = std::make_unique<Material_set>(
                Material_set_create_info{
                    .graphics_device        = &graphics_device,
                    .material_interface     = &m_program_interface->material_interface,
                    .bind_group_layout      = m_program_interface->bind_group_layout.get(),
                    .fallback_texture       = m_fallback_texture.get(),
                    .fallback_sampler       = m_fallback_sampler.get(),
                    .max_textures           = 16,
                    .initial_material_count = 4,
                    .debug_label            = erhe::utility::Debug_label{"shadow test material set"}
                }
            );
            m_pass_resources   = std::make_unique<Scene_pass_resources>(graphics_device, command_buffer, *m_program_interface, nullptr, *m_material_set);
            m_forward_renderer = std::make_unique<Forward_renderer>(graphics_device, *m_mesh_memory, *m_program_interface, *m_shader_variant_cache, *m_pass_resources);
            m_shadow_renderer  = std::make_unique<Shadow_renderer>(graphics_device, command_buffer, *m_mesh_memory, *m_program_interface, *m_shader_variant_cache);
        }
    );

    const bool reverse_depth = graphics_device.get_reverse_depth();
    const bool y_flip        = graphics_device.get_info().coordinate_conventions.clip_space_y_flip == erhe::math::Clip_space_y_flip::enabled;
    m_forward_pipeline = std::make_unique<erhe::graphics::Base_render_pipeline>(
        graphics_device,
        erhe::graphics::Base_render_pipeline_create_info{
            .debug_label    = erhe::utility::Debug_label{"shadow test forward"},
            .input_assembly = erhe::graphics::Input_assembly_state::triangle,
            .rasterization  = erhe::graphics::Rasterization_state::cull_mode_back_ccw.with_winding_flip_if(y_flip),
            .depth_stencil  = erhe::graphics::Depth_stencil_state::depth_test_enabled_stencil_test_disabled(reverse_depth)
        }
    );
    m_tie_pipeline = std::make_unique<erhe::graphics::Base_render_pipeline>(
        graphics_device,
        erhe::graphics::Base_render_pipeline_create_info{
            .debug_label    = erhe::utility::Debug_label{"shadow test tie"},
            .input_assembly = erhe::graphics::Input_assembly_state::triangle,
            .rasterization  = erhe::graphics::Rasterization_state::cull_mode_none,
            .depth_stencil  = erhe::graphics::Depth_stencil_state::depth_test_disabled_stencil_test_disabled
        }
    );

    erhe::primitive::Material_create_info material_create_info{};
    material_create_info.name              = "shadow test white";
    material_create_info.values.base_color = glm::vec3{1.0f, 1.0f, 1.0f};
    m_material = std::make_shared<erhe::primitive::Material>(material_create_info);
    const std::shared_ptr<erhe::primitive::Material> materials[] = { m_material };
    m_material_set->sync_library(std::span<const std::shared_ptr<erhe::primitive::Material>>{materials});
    submit_and_wait(
        [&](erhe::graphics::Command_buffer& command_buffer) {
            m_material_set->update(command_buffer);
        }
    );

    // Floor: the top face is the plane y = 0.
    m_meshes.push_back(
        make_box_mesh(
            "floor",
            glm::vec3{-c_floor_extent, -c_floor_thickness, -c_floor_extent},
            glm::vec3{ c_floor_extent,  0.0f,               c_floor_extent}
        )
    );
    flush_meshes();

    // Top-down orthographic view covering x, z in [-c_view_extent, c_view_extent].
    // Its position and shadow range also place the directional light's
    // stable fit (set_light() sets the shadow range). The depth range
    // keeps y in [-0.5, 0.2] only, so the view shows the plane and never a
    // caster above it.
    m_camera = std::make_shared<erhe::scene::Camera>("shadow test camera");
    m_camera->set_projection(
        erhe::scene::Projection{
            .projection_type     = erhe::scene::Projection::Type::orthographic,
            .orthographic_z_near = 2.8f,
            .orthographic_z_far  = 3.5f,
            .ortho_width         = 2.0f * c_view_extent,
            .ortho_height        = 2.0f * c_view_extent
        }
    );
}

void Shadow_gpu_test::TearDown()
{
    device().wait_idle();
    m_light_projections.clear();
    m_light_projections.shadow_map_texture.reset();
    m_shadow_render_passes.clear();
    m_shadow_map.reset();
    m_light.reset();
    m_camera.reset();
    m_meshes.clear();
    m_material.reset();
    m_tie_stages.clear();
    m_tie_pipeline.reset();
    m_forward_pipeline.reset();
    m_shadow_renderer.reset();
    m_forward_renderer.reset();
    m_pass_resources.reset();
    m_material_set.reset();
    m_fallback_sampler.reset();
    m_fallback_texture.reset();
    m_shader_variant_cache.reset();
    m_program_interface.reset();
    m_mesh_memory.reset();
    Gpu_test::TearDown();
}

auto Shadow_gpu_test::get_shadow_depth_formats() -> std::vector<erhe::dataformat::Format>
{
    const std::vector<erhe::dataformat::Format> supported = device().get_supported_depth_stencil_formats();
    std::vector<erhe::dataformat::Format> result;
    for (const std::size_t bits : { std::size_t{16}, std::size_t{24}, std::size_t{32} }) {
        erhe::dataformat::Format chosen = erhe::dataformat::Format::format_undefined;
        for (const erhe::dataformat::Format format : supported) {
            if (erhe::dataformat::get_depth_size_bits(format) != bits) {
                continue;
            }
            const bool has_stencil = erhe::dataformat::get_stencil_size_bits(format) != 0;
            if ((chosen == erhe::dataformat::Format::format_undefined) || !has_stencil) {
                chosen = format;
            }
        }
        if (chosen != erhe::dataformat::Format::format_undefined) {
            result.push_back(chosen);
        }
    }
    return result;
}

auto Shadow_gpu_test::make_box_mesh(const char* name, const glm::vec3& min_corner, const glm::vec3& max_corner) -> std::shared_ptr<erhe::scene::Mesh>
{
    const glm::vec3 a = min_corner;
    const glm::vec3 b = max_corner;
    const std::array<Box_face, 6> faces{
        Box_face{.normal = { 0.0f,  1.0f,  0.0f}, .corners = {{a.x, b.y, a.z}, {a.x, b.y, b.z}, {b.x, b.y, b.z}, {b.x, b.y, a.z}}},
        Box_face{.normal = { 0.0f, -1.0f,  0.0f}, .corners = {{a.x, a.y, a.z}, {b.x, a.y, a.z}, {b.x, a.y, b.z}, {a.x, a.y, b.z}}},
        Box_face{.normal = { 1.0f,  0.0f,  0.0f}, .corners = {{b.x, a.y, a.z}, {b.x, b.y, a.z}, {b.x, b.y, b.z}, {b.x, a.y, b.z}}},
        Box_face{.normal = {-1.0f,  0.0f,  0.0f}, .corners = {{a.x, a.y, a.z}, {a.x, a.y, b.z}, {a.x, b.y, b.z}, {a.x, b.y, a.z}}},
        Box_face{.normal = { 0.0f,  0.0f,  1.0f}, .corners = {{a.x, a.y, b.z}, {b.x, a.y, b.z}, {b.x, b.y, b.z}, {a.x, b.y, b.z}}},
        Box_face{.normal = { 0.0f,  0.0f, -1.0f}, .corners = {{a.x, a.y, a.z}, {a.x, b.y, a.z}, {b.x, b.y, a.z}, {b.x, a.y, a.z}}}
    };

    erhe::primitive::Triangle_soup soup{};
    soup.vertex_format = erhe::dataformat::Vertex_format{
        erhe::dataformat::Vertex_stream{
            0,
            {
                {erhe::dataformat::Format::format_32_vec3_float, erhe::dataformat::Vertex_attribute_usage::position, 0},
                {erhe::dataformat::Format::format_32_vec3_float, erhe::dataformat::Vertex_attribute_usage::normal,   0}
            }
        }
    };
    for (const Box_face& face : faces) {
        const uint32_t base = static_cast<uint32_t>(soup.vertex_data.size() / (6 * sizeof(float)));
        for (const glm::vec3& corner : face.corners) {
            const float vertex[6] = { corner.x, corner.y, corner.z, face.normal.x, face.normal.y, face.normal.z };
            const std::size_t offset = soup.vertex_data.size();
            soup.vertex_data.resize(offset + sizeof(vertex));
            std::memcpy(soup.vertex_data.data() + offset, vertex, sizeof(vertex));
        }
        for (const uint32_t index : { 0u, 1u, 2u, 0u, 2u, 3u }) {
            soup.index_data.push_back(base + index);
        }
    }

    std::optional<erhe::primitive::Buffer_mesh> buffer_mesh = erhe::primitive::build_buffer_mesh_from_triangle_soup(
        soup,
        m_mesh_memory->make_primitive_buffer_info()
    );
    EXPECT_TRUE(buffer_mesh.has_value()) << "box mesh build failed";
    if (!buffer_mesh.has_value()) {
        return {};
    }

    std::shared_ptr<erhe::scene::Mesh> mesh = std::make_shared<erhe::scene::Mesh>(name);
    mesh->add_primitive(std::make_shared<erhe::primitive::Primitive>(std::move(buffer_mesh.value())), m_material);
    mesh->set_value(erhe::scene::Mesh::shadow_cast_property, true);
    return mesh;
}

void Shadow_gpu_test::flush_meshes()
{
    submit_and_wait(
        [&](erhe::graphics::Command_buffer& command_buffer) {
            m_mesh_memory->flush(command_buffer);
        }
    );
}

void Shadow_gpu_test::add_box(const glm::vec3& min_corner, const glm::vec3& max_corner)
{
    m_meshes.push_back(make_box_mesh("caster box", min_corner, max_corner));
    m_meshes.back()->set_world_from_node(m_world_from_station);
    flush_meshes();
}

void Shadow_gpu_test::set_light(const Shadow_light_kind light_kind, const Shadow_pose& pose)
{
    m_world_from_station = pose.get_world_from_station();
    for (const std::shared_ptr<erhe::scene::Mesh>& mesh : m_meshes) {
        mesh->set_world_from_node(m_world_from_station);
    }
    m_camera->set_world_from_node(
        m_world_from_station *
        erhe::math::create_look_at(glm::vec3{0.0f, 3.0f, 0.0f}, glm::vec3{0.0f, 0.0f, 0.0f}, glm::vec3{0.0f, 0.0f, -1.0f})
    );
    m_camera->set_shadow_range(pose.shadow_range);

    m_light = std::make_shared<erhe::scene::Light>(c_str(light_kind));
    m_light->set_light_type(
        (light_kind == Shadow_light_kind::spot) ? erhe::scene::Light_type::spot : erhe::scene::Light_type::directional
    );
    m_light->set_color      (glm::vec3{1.0f, 1.0f, 1.0f});
    m_light->set_intensity  (1.0f);
    m_light->set_range      (10.0f);
    // Full cone angle: the spot map covers x, z in [-spot_height, spot_height]
    // at the plane.
    m_light->set_inner_spot_angle(0.4f * glm::pi<float>());
    m_light->set_outer_spot_angle(0.5f * glm::pi<float>());
    m_light->set_cast_shadow(true);
    // Straight down onto the plane: node +Z (the direction toward the light)
    // is world +Y. Position matters for the spot light only.
    m_light->set_world_from_node(
        m_world_from_station *
        erhe::math::create_look_at(glm::vec3{0.0f, pose.spot_height, 0.0f}, glm::vec3{0.0f, 0.0f, 0.0f}, glm::vec3{0.0f, 0.0f, -1.0f})
    );

    const std::shared_ptr<erhe::scene::Light> lights[] = { m_light };
    m_light_set.invalidate();
    m_light_set.resolve(std::span<const std::shared_ptr<erhe::scene::Light>>{lights}, Light_count_limits::uniform(1, 0));
}

void Shadow_gpu_test::render_shadow_map(const Shadow_map_settings& settings)
{
    erhe::graphics::Device& graphics_device = device();
    const bool reverse_depth = graphics_device.get_reverse_depth();
    ASSERT_TRUE(m_light);

    m_shadow_render_passes.clear();
    m_shadow_map.reset();
    m_shadow_map_format = settings.depth_format;
    m_shadow_map = std::make_shared<erhe::graphics::Texture>(
        graphics_device,
        erhe::graphics::Texture_create_info{
            .device            = graphics_device,
            .usage_mask        =
                erhe::graphics::Image_usage_flag_bit_mask::depth_stencil_attachment |
                erhe::graphics::Image_usage_flag_bit_mask::sampled |
                erhe::graphics::Image_usage_flag_bit_mask::transfer_dst,
            .type              = erhe::graphics::Texture_type::texture_2d_array,
            .pixelformat       = settings.depth_format,
            .width             = settings.resolution,
            .height            = settings.resolution,
            .depth             = 1,
            .array_layer_count = 1,
            .debug_label       = erhe::utility::Debug_label{"shadow test map"}
        }
    );
    submit_and_wait(
        [&](erhe::graphics::Command_buffer& command_buffer) {
            command_buffer.transition_texture_layout(*m_shadow_map, erhe::graphics::Image_layout::depth_stencil_read_only_optimal);
        }
    );

    erhe::graphics::Render_pass_descriptor descriptor{};
    descriptor.depth_attachment.texture        = m_shadow_map.get();
    descriptor.depth_attachment.texture_level  = 0;
    descriptor.depth_attachment.texture_layer  = 0;
    descriptor.depth_attachment.load_action    = erhe::graphics::Load_action::Clear;
    descriptor.depth_attachment.store_action   = erhe::graphics::Store_action::Store;
    descriptor.depth_attachment.usage_before   = erhe::graphics::Image_usage_flag_bit_mask::sampled;
    descriptor.depth_attachment.layout_before  = erhe::graphics::Image_layout::depth_stencil_read_only_optimal;
    descriptor.depth_attachment.usage_after    = erhe::graphics::Image_usage_flag_bit_mask::sampled;
    descriptor.depth_attachment.layout_after   = erhe::graphics::Image_layout::depth_stencil_read_only_optimal;
    descriptor.depth_attachment.clear_value[0] = reverse_depth ? 0.0 : 1.0;
    descriptor.render_target_width             = settings.resolution;
    descriptor.render_target_height            = settings.resolution;
    descriptor.debug_label                     = erhe::utility::Debug_label{"shadow test map pass"};
    m_shadow_render_passes.push_back(std::make_unique<erhe::graphics::Render_pass>(graphics_device, descriptor));

    const erhe::math::Coordinate_conventions conventions = graphics_device.get_info().coordinate_conventions;
    const erhe::math::Viewport view_viewport  {0, 0, c_view_size, c_view_size};
    const erhe::math::Viewport shadow_viewport{0, 0, settings.resolution, settings.resolution};
    const std::span<const std::shared_ptr<erhe::scene::Mesh>> meshes{m_meshes};
    const std::span<const std::shared_ptr<erhe::scene::Skin>> no_skins{};

    submit_and_wait(
        [&](erhe::graphics::Command_buffer& command_buffer) {
            const bool rendered = m_shadow_renderer->render(
                Shadow_renderer::Render_parameters{
                    .command_buffer        = command_buffer,
                    .view_camera           = m_camera.get(),
                    .view_camera_viewport  = view_viewport,
                    .light_camera_viewport = shadow_viewport,
                    .texture               = m_shadow_map,
                    .render_passes         = m_shadow_render_passes,
                    .mesh_spans            = { meshes },
                    .light_set             = m_light_set,
                    .skins                 = no_skins,
                    .material_source       = m_material_set.get(),
                    .light_projections     = m_light_projections,
                    .reverse_depth         = reverse_depth,
                    .depth_range           = conventions.native_depth_range,
                    .conventions           = conventions,
                    .fit_settings          = nullptr,
                    .depth_bias_constant   = settings.depth_bias_constant,
                    .depth_bias_slope      = settings.depth_bias_slope,
                    .cull_mode             = settings.cull_mode
                }
            );
            static_cast<void>(rendered);
        }
    );
    ASSERT_EQ(m_light_projections.light_projection_transforms.size(), 1u);
    ASSERT_TRUE(m_light_projections.light_projection_transforms.front().is_shadow_mapped());
}

auto Shadow_gpu_test::render_visibility(const Shadow_filter_case& filter_case) -> Visibility_image
{
    erhe::graphics::Device& graphics_device = device();
    const bool reverse_depth = graphics_device.get_reverse_depth();
    const erhe::math::Coordinate_conventions conventions = graphics_device.get_info().coordinate_conventions;

    const std::shared_ptr<erhe::graphics::Texture> color_target = make_color_target(
        c_view_size, c_view_size, erhe::dataformat::Format::format_32_vec4_float
    );
    const std::shared_ptr<erhe::graphics::Texture> depth_target = std::make_shared<erhe::graphics::Texture>(
        graphics_device,
        erhe::graphics::Texture_create_info{
            .device      = graphics_device,
            .usage_mask  = erhe::graphics::Image_usage_flag_bit_mask::depth_stencil_attachment,
            .type        = erhe::graphics::Texture_type::texture_2d,
            .pixelformat = erhe::dataformat::Format::format_d32_sfloat,
            .width       = c_view_size,
            .height      = c_view_size,
            .debug_label = erhe::utility::Debug_label{"shadow test view depth"}
        }
    );

    erhe::graphics::Render_pass_descriptor descriptor{};
    descriptor.color_attachments[0].texture       = color_target.get();
    descriptor.color_attachments[0].clear_value   = std::array<double, 4>{ -1.0, -1.0, -1.0, 1.0 };
    descriptor.color_attachments[0].load_action   = erhe::graphics::Load_action::Clear;
    descriptor.color_attachments[0].store_action  = erhe::graphics::Store_action::Store;
    descriptor.color_attachments[0].usage_before  = erhe::graphics::Image_usage_flag_bit_mask::transfer_src;
    descriptor.color_attachments[0].layout_before = erhe::graphics::Image_layout::transfer_src_optimal;
    descriptor.color_attachments[0].usage_after   = erhe::graphics::Image_usage_flag_bit_mask::transfer_src;
    descriptor.color_attachments[0].layout_after  = erhe::graphics::Image_layout::transfer_src_optimal;
    descriptor.depth_attachment.texture           = depth_target.get();
    descriptor.depth_attachment.clear_value[0]    = reverse_depth ? 0.0 : 1.0;
    descriptor.depth_attachment.load_action       = erhe::graphics::Load_action::Clear;
    descriptor.depth_attachment.store_action      = erhe::graphics::Store_action::Dont_care;
    descriptor.depth_attachment.usage_before      = erhe::graphics::Image_usage_flag_bit_mask::depth_stencil_attachment;
    descriptor.depth_attachment.layout_before     = erhe::graphics::Image_layout::undefined;
    descriptor.depth_attachment.usage_after       = erhe::graphics::Image_usage_flag_bit_mask::depth_stencil_attachment;
    descriptor.depth_attachment.layout_after      = erhe::graphics::Image_layout::depth_stencil_attachment_optimal;
    descriptor.render_target_width                = c_view_size;
    descriptor.render_target_height               = c_view_size;
    descriptor.debug_label                        = erhe::utility::Debug_label{"shadow test view"};

    const erhe::math::Viewport viewport{0, 0, c_view_size, c_view_size};
    const Camera_view_input view_input{
        .projection = m_camera->projection(),
        .node       = m_camera.get(),
        .viewport   = viewport
    };
    const std::vector<std::span<const std::shared_ptr<erhe::scene::Mesh>>> mesh_spans{
        std::span<const std::shared_ptr<erhe::scene::Mesh>>{m_meshes}
    };
    erhe::graphics::Base_render_pipeline* pipelines[] = { m_forward_pipeline.get() };

    submit_and_wait(
        [&](erhe::graphics::Command_buffer& command_buffer) {
            erhe::graphics::Render_pass              render_pass{graphics_device, descriptor};
            erhe::graphics::Render_command_encoder   encoder = graphics_device.make_render_command_encoder(command_buffer);
            const erhe::graphics::Scoped_render_pass scoped{render_pass, command_buffer};
            encoder.set_viewport_rect(0, 0, c_view_size, c_view_size);
            encoder.set_scissor_rect (0, 0, c_view_size, c_view_size);
            m_forward_renderer->render(
                Forward_renderer::Render_parameters{
                    .base = Base_render_parameters{
                        .render_encoder           = encoder,
                        .render_pass              = &render_pass,
                        .viewport                 = viewport,
                        .views                    = std::span<const Camera_view_input>{&view_input, 1},
                        .light_projections        = &m_light_projections,
                        .shadow_debug_light_index = 0,
                        .material_source          = m_material_set.get(),
                        .reverse_depth            = reverse_depth,
                        .depth_range              = conventions.native_depth_range,
                        .conventions              = conventions,
                        .debug_label              = "shadow test view"
                    },
                    .mesh_spans            = mesh_spans,
                    .base_render_pipelines = std::span<erhe::graphics::Base_render_pipeline*>{pipelines},
                    .blending_mode_policy  = Blending_mode_policy::opaque_primitives_only,
                    .shader_debug          = Shader_debug::shadow_visibility,
                    .shadow_filter         = filter_case.filter,
                    .shadow_bias           = filter_case.bias,
                    .shadow_technique      = 0,
                    .shadow_depth_bits     = get_shadow_depth_bits_axis(m_shadow_map_format)
                }
            );
        }
    );

    const std::vector<float> rgba = read_texture_rgba32f(*color_target);
    Visibility_image image{};
    image.size   = c_view_size;
    image.extent = c_view_extent;
    image.visibility.resize(static_cast<std::size_t>(c_view_size) * static_cast<std::size_t>(c_view_size));
    for (std::size_t i = 0, end = image.visibility.size(); i < end; ++i) {
        image.visibility[i] = rgba[i * 4];
    }
    return image;
}

auto Shadow_gpu_test::get_tie_stages(const Shadow_filter_case& filter_case) -> erhe::graphics::Shader_stages*
{
    // The receiver points are view-relative, as standard.vert produces them:
    // the station frame with its translation taken relative to the view
    // camera's view origin, subtracted in double (Primitive_struct::
    // view_relative_translation). It goes in as exact float bits: GLSL has
    // no hexadecimal float literals, and a decimal round trip would not
    // reproduce the matrix the other passes use.
    const uint32_t  depth_bits  = get_shadow_depth_bits_axis(m_shadow_map_format);
    const glm::vec3 view_origin = get_view_origin(m_camera->world_from_node_transform().get_matrix());
    glm::mat4 view_relative_from_station = m_world_from_station;
    view_relative_from_station[3] = glm::vec4{
        glm::vec3{glm::dvec3{glm::vec3{m_world_from_station[3]}} - glm::dvec3{view_origin}},
        1.0f
    };
    std::string view_relative_from_station_string = "mat4(";
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            view_relative_from_station_string += fmt::format(
                "{}uintBitsToFloat(0x{:08x}u)",
                ((column == 0) && (row == 0)) ? "" : ", ",
                std::bit_cast<uint32_t>(view_relative_from_station[column][row])
            );
        }
    }
    view_relative_from_station_string += ")";
    const std::string key = fmt::format("{} {} {} {}", filter_case.filter, filter_case.bias, depth_bits, view_relative_from_station_string);
    const auto it = m_tie_stages.find(key);
    if (it != m_tie_stages.end()) {
        return it->second.get();
    }

    // The environment axes exactly as a forward pass with one shadow-mapped
    // light would set them; only the shadow axes matter to erhe_light.glsl.
    Shader_key environment_key{};
    environment_key.set(Shader_int::SHADOW_FILTER,     filter_case.filter);
    environment_key.set(Shader_int::SHADOW_BIAS,       filter_case.bias);
    environment_key.set(Shader_int::SHADOW_TECHNIQUE,  0u);
    environment_key.set(Shader_int::SHADOW_DEPTH_BITS, depth_bits);
    std::vector<std::pair<std::string, std::string>> defines = environment_key.get_defines();
    defines.emplace_back("SHADOW_TIE_BAND_WIDTH", fmt::format("{}", c_tie_band_width));
    defines.emplace_back("SHADOW_TIE_HEIGHT",     fmt::format("{}", c_tie_height));
    defines.emplace_back("SHADOW_TIE_MAX_ULPS",   fmt::format("{}", c_tie_max_ulps));
    defines.emplace_back("SHADOW_TIE_EXTENT",     fmt::format("{:.1f}", c_view_extent));
    defines.emplace_back("SHADOW_TIE_PLANE_Y",    "0.0");
    defines.emplace_back("SHADOW_TIE_VIEW_RELATIVE_FROM_STATION", view_relative_from_station_string);

    erhe::graphics::Shader_stages_create_info create_info{
        .name            = "shadow_tie",
        .defines         = defines,
        .no_vertex_input = true
    };
    erhe::graphics::Shader_stages_prototype prototype = erhe::graphics::build_shader_stages(
        m_program_interface->make_prototype(std::move(create_info))
    );
    EXPECT_TRUE(prototype.is_valid()) << "shadow_tie shader failed to compile/link";
    if (!prototype.is_valid()) {
        return nullptr;
    }
    std::unique_ptr<erhe::graphics::Shader_stages> stages = std::make_unique<erhe::graphics::Shader_stages>(device(), std::move(prototype));
    erhe::graphics::Shader_stages* result = stages.get();
    m_tie_stages.emplace(key, std::move(stages));
    return result;
}

auto Shadow_gpu_test::render_tie(const Shadow_filter_case& filter_case) -> Shadow_tie_result
{
    erhe::graphics::Device& graphics_device = device();
    const bool reverse_depth = graphics_device.get_reverse_depth();
    const erhe::math::Coordinate_conventions conventions = graphics_device.get_info().coordinate_conventions;

    Shadow_tie_result result{};
    result.max_ulps       = c_tie_max_ulps;
    result.pixels_per_ulp = c_tie_band_width * c_tie_height;
    result.failing_per_ulp.resize(static_cast<std::size_t>((2 * c_tie_max_ulps) + 1), 0);

    erhe::graphics::Shader_stages* shader_stages = get_tie_stages(filter_case);
    if (shader_stages == nullptr) {
        return result;
    }

    const int width  = c_tie_band_width * ((2 * c_tie_max_ulps) + 1);
    const int height = c_tie_height;
    const std::shared_ptr<erhe::graphics::Texture> color_target = make_color_target(
        width, height, erhe::dataformat::Format::format_32_vec4_float
    );

    erhe::graphics::Render_pass_descriptor descriptor{};
    descriptor.color_attachments[0].texture       = color_target.get();
    descriptor.color_attachments[0].clear_value   = std::array<double, 4>{ -1.0, -100.0, 0.0, 1.0 };
    descriptor.color_attachments[0].load_action   = erhe::graphics::Load_action::Clear;
    descriptor.color_attachments[0].store_action  = erhe::graphics::Store_action::Store;
    descriptor.color_attachments[0].usage_before  = erhe::graphics::Image_usage_flag_bit_mask::transfer_src;
    descriptor.color_attachments[0].layout_before = erhe::graphics::Image_layout::transfer_src_optimal;
    descriptor.color_attachments[0].usage_after   = erhe::graphics::Image_usage_flag_bit_mask::transfer_src;
    descriptor.color_attachments[0].layout_after  = erhe::graphics::Image_layout::transfer_src_optimal;
    descriptor.render_target_width                = width;
    descriptor.render_target_height               = height;
    descriptor.debug_label                        = erhe::utility::Debug_label{"shadow tie"};

    // The view camera is bound for camera.cameras[0].clip_depth_direction
    // and view_origin, which sample_light_visibility() reads; the receiver
    // points come from gl_FragCoord, relative to this camera's view origin.
    const erhe::math::Viewport viewport{0, 0, width, height};
    const Camera_view_input view_input{
        .projection = m_camera->projection(),
        .node       = m_camera.get(),
        .viewport   = viewport
    };

    submit_and_wait(
        [&](erhe::graphics::Command_buffer& command_buffer) {
            erhe::graphics::Render_pass              render_pass{graphics_device, descriptor};
            erhe::graphics::Render_command_encoder   encoder = graphics_device.make_render_command_encoder(command_buffer);
            const erhe::graphics::Scoped_render_pass scoped{render_pass, command_buffer};
            m_forward_renderer->draw_primitives(
                Forward_renderer::Primitive_render_parameters{
                    .base = Base_render_parameters{
                        .render_encoder    = encoder,
                        .render_pass       = &render_pass,
                        .viewport          = viewport,
                        .views             = std::span<const Camera_view_input>{&view_input, 1},
                        .light_projections = &m_light_projections,
                        .material_source   = m_material_set.get(),
                        .reverse_depth     = reverse_depth,
                        .depth_range       = conventions.native_depth_range,
                        .conventions       = conventions,
                        .debug_label       = "shadow tie"
                    },
                    .vertex_count         = 3,
                    .base_render_pipeline = *m_tie_pipeline,
                    .color_blend          = &erhe::graphics::Color_blend_state::color_blend_disabled,
                    .shader_stages        = shader_stages
                },
                nullptr
            );
        }
    );

    const std::vector<float> rgba = read_texture_rgba32f(*color_target);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const std::size_t index      = ((static_cast<std::size_t>(y) * static_cast<std::size_t>(width)) + static_cast<std::size_t>(x)) * 4u;
            const float       visibility = rgba[index + 0];
            const int         k          = static_cast<int>(rgba[index + 1]);
            const int         band       = (x / c_tie_band_width);
            EXPECT_EQ(k, band - c_tie_max_ulps) << "shadow_tie pixel (" << x << ", " << y << ") not written";
            if (k != (band - c_tie_max_ulps)) {
                return result;
            }
            if (visibility < 1.0f) {
                ++result.failing_per_ulp[static_cast<std::size_t>(band)];
            }
        }
    }
    return result;
}

} // namespace erhe::scene_renderer::test
