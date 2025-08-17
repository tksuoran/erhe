// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "rendertarget_mesh.hpp"

#include "app_context.hpp"
#include "editor_log.hpp"
#include "renderers/mesh_memory.hpp"
#include "scene/scene_view.hpp"

#include "erhe_geometry/geometry.hpp"
#include "erhe_geometry/shapes/regular_polygon.hpp"
#include "erhe_graphics/blit_command_encoder.hpp"
#include "erhe_graphics/buffer_transfer_queue.hpp"
#include "erhe_graphics/render_pass.hpp"
#include "erhe_graphics/sampler.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_utility/bit_helpers.hpp"
#include "erhe_math/math_util.hpp"

namespace editor {

Rendertarget_mesh::Rendertarget_mesh(
    erhe::graphics::Device& graphics_device,
    Mesh_memory&            mesh_memory,
    const int               width,
    const int               height,
    const float             pixels_per_meter
)
    : erhe::scene::Mesh {"Rendertarget Node"}
    , m_pixels_per_meter{pixels_per_meter}
{
    enable_flag_bits(erhe::Item_flags::rendertarget | erhe::Item_flags::translucent);

    resize_rendertarget(graphics_device, mesh_memory, width, height);
}

auto Rendertarget_mesh::get_static_type() -> uint64_t
{
    return erhe::Item_type::node_attachment | erhe::Item_type::mesh | erhe::Item_type::rendertarget;
}

auto Rendertarget_mesh::get_type() const -> uint64_t
{
    return get_static_type();
}

auto Rendertarget_mesh::get_type_name() const -> std::string_view
{
    return static_type_name;
}

void Rendertarget_mesh::resize_rendertarget(erhe::graphics::Device& graphics_device, Mesh_memory& mesh_memory, const int width, const int height)
{
    if (m_texture && m_texture->get_width() == width && m_texture->get_height() == height) {
        return;
    }

    using Texture     = erhe::graphics::Texture;
    using Render_pass = erhe::graphics::Render_pass;

    m_texture = std::make_shared<Texture>(
        graphics_device,
        Texture::Create_info{
            .device       = graphics_device,
            .type         = erhe::graphics::Texture_type::texture_2d,
            .pixelformat  = erhe::dataformat::Format::format_8_vec4_srgb,
            .use_mipmaps  = true,
            .sample_count = 0,
            .width        = width,
            .height       = height,
            .debug_label  = "Rendertarget_mesh::m_texture"
        }
    );
    graphics_device.clear_texture(*m_texture.get(), { 0.0, 0.0, 0.0, 0.85 });

    m_sampler = std::make_shared<erhe::graphics::Sampler>(
        graphics_device,
        erhe::graphics::Sampler_create_info{
            .min_filter  = erhe::graphics::Filter::linear,
            .mag_filter  = erhe::graphics::Filter::nearest,
            .mipmap_mode = erhe::graphics::Sampler_mipmap_mode::linear,
            .lod_bias    = -0.666f,
            .debug_label = "Rendertarget_mesh"
        }
    );

    // TODO Use multisample resolve
    erhe::graphics::Render_pass_descriptor render_pass_descriptor{};
    render_pass_descriptor.color_attachments[0].texture        = m_texture.get();
    render_pass_descriptor.color_attachments[0].load_action    = erhe::graphics::Load_action::Clear;
    render_pass_descriptor.color_attachments[0].clear_value[0] = 0.0f;
    render_pass_descriptor.color_attachments[0].clear_value[1] = 0.0f;
    render_pass_descriptor.color_attachments[0].clear_value[2] = 0.0f;
    render_pass_descriptor.color_attachments[0].clear_value[3] = 0.66f;
    render_pass_descriptor.color_attachments[0].store_action   = erhe::graphics::Store_action::Store;
    render_pass_descriptor.render_target_width                 = width;
    render_pass_descriptor.render_target_height                = height;
    render_pass_descriptor.debug_label                         = "Rendertarget Node";
    m_render_pass = std::make_shared<Render_pass>(graphics_device, render_pass_descriptor);

    m_material = std::make_shared<erhe::primitive::Material>(
        erhe::primitive::Material_create_info{
            .name       = "Rendertarget Node",
            .base_color = glm::vec3{0.1f, 0.1f, 0.2f}
        }
    );
    m_material->texture_samplers.base_color.texture = m_texture;
    m_material->texture_samplers.base_color.sampler = m_sampler;
    m_material->disable_flag_bits(erhe::Item_flags::show_in_ui);

    m_local_width  = static_cast<float>(m_texture->get_width ()) / m_pixels_per_meter;
    m_local_height = static_cast<float>(m_texture->get_height()) / m_pixels_per_meter;

    std::shared_ptr<erhe::geometry::Geometry> geometry = std::make_shared<erhe::geometry::Geometry>();
    erhe::geometry::shapes::make_rectangle(geometry->get_mesh(), m_local_width, m_local_height, true, false);

    std::shared_ptr<erhe::primitive::Primitive> primitive = std::make_shared<erhe::primitive::Primitive>(
        geometry,
        erhe::primitive::Build_info{
            .primitive_types{ .fill_triangles = true },
            .buffer_info = mesh_memory.buffer_info
        },
        erhe::primitive::Normal_style::polygon_normals
    );

    ERHE_VERIFY(primitive->make_raytrace());

    clear_primitives();
    add_primitive(primitive, m_material);

    mesh_memory.buffer_transfer_queue.flush();

    enable_flag_bits(
        erhe::Item_flags::visible      |
        erhe::Item_flags::translucent  |
        erhe::Item_flags::id           |
        erhe::Item_flags::rendertarget
    );
}

auto Rendertarget_mesh::get_texture() const -> std::shared_ptr<erhe::graphics::Texture>
{
    return m_texture;
}

auto Rendertarget_mesh::get_render_pass() const -> erhe::graphics::Render_pass*
{
    return m_render_pass.get();
}

#if defined(ERHE_XR_LIBRARY_OPENXR)
void Rendertarget_mesh::update_headset_hand_tracking()
{
#if 0
    auto* hand_tracker = headset_view.get_hand_tracker();
    if (hand_tracker == nullptr) {
        return;
    }
    const auto thumb_opt  = hand_tracker->get_hand(Hand_name::Right).get_joint(XR_HAND_JOINT_THUMB_TIP_EXT);
    const auto index_opt  = hand_tracker->get_hand(Hand_name::Right).get_joint(XR_HAND_JOINT_INDEX_TIP_EXT);
    const auto middle_opt = hand_tracker->get_hand(Hand_name::Right).get_joint(XR_HAND_JOINT_MIDDLE_TIP_EXT);
    m_pointer_finger.reset();
    if (!index_opt.has_value()) {
        return;
    }
    if (thumb_opt.has_value() && middle_opt.has_value()) {
        const auto thumb    = thumb_opt .value();
        const auto middle   = middle_opt.value();
        const auto distance = glm::distance(thumb.position, middle.position);
        m_finger_trigger = distance < 0.014f;
    }

    auto const* node = get_node();
    if (node == nullptr) {
        return;
    }

    const auto pointer      = index_opt.value();
    const auto direction    = glm::vec3{pointer.orientation * glm::vec4{0.0f, 0.0f, 1.0f, 0.0f}};
    const auto intersection = erhe::math::intersect_plane<float>(
        glm::vec3{node->direction_in_world()},
        glm::vec3{node->position_in_world()},
        pointer.position,
        direction
    );
    if (!intersection.has_value()) {
        return;
    }
    const auto world_position      = pointer.position + intersection.value() * direction;
    const auto window_position_opt = world_to_window(world_position);
    if (window_position_opt.has_value()) {
        m_pointer = window_position_opt;
        m_pointer_finger = Finger_point{
            .finger       = static_cast<std::size_t>(XR_HAND_JOINT_INDEX_TIP_EXT),
            .finger_point = pointer.position,
            .point        = world_position
        };
    }
#endif
}
#endif

auto Rendertarget_mesh::update_pointer(Scene_view* scene_view) -> bool
{
    // TODO Duplication with Rendertarget_imgui_host::begin_imgui_frame()
    m_pointer.reset();

    if (scene_view == nullptr) {
        return false;
    }
    const auto& rendertarget_hover = scene_view->get_hover(Hover_entry::rendertarget_slot);
    if (!rendertarget_hover.valid) {
        return false;
    }
    const auto opt_origin_in_world    = scene_view->get_control_ray_origin_in_world();
    const auto opt_direction_in_world = scene_view->get_control_ray_direction_in_world();
    if (!opt_origin_in_world.has_value() || !opt_direction_in_world.has_value()) {
        return false;
    }

    auto const* node = get_node();
    if (node == nullptr) {
        return false;
    }

    const glm::vec3 origin_position_in_world = opt_origin_in_world.value();
    const glm::vec3 direction_in_world       = opt_direction_in_world.value();
    const glm::vec3 origin_in_mesh           = node->transform_point_from_world_to_local(origin_position_in_world);
    const glm::vec3 direction_in_mesh        = node->transform_direction_from_world_to_local(direction_in_world);

    const glm::vec3 origo      {0.0f, 0.0f, 0.0f};
    const glm::vec3 unit_axis_z{0.0f, 0.0f, 1.0f};
    const auto hit = erhe::math::intersect_plane<float>(unit_axis_z, origo, origin_in_mesh, direction_in_mesh);

    if (!hit.has_value() || hit.value() < 0.0f) {
        return false;
    }

    const glm::vec3 hit_position_in_mesh = origin_in_mesh + hit.value() * direction_in_mesh;
    const glm::vec2 a{
        hit_position_in_mesh.x / m_local_width,
        hit_position_in_mesh.y / m_local_height
    };
    const glm::vec2 b{
         a.x + 0.5f,
        -a.y + 0.5f
    };
    if (
        (b.x < 0.0f) ||
        (b.y < 0.0f) ||
        (b.x > 1.0f) ||
        (b.y > 1.0f)
    ) {
        return false;
    }
    const glm::vec2 hit_position_in_viewport{
        m_texture->get_width()  * b.x,
        m_texture->get_height() * b.y
    };

    SPDLOG_LOGGER_TRACE(log_pointer, "rt ray hit position {}", hit_position_in_viewport);

    m_pointer = hit_position_in_viewport;

    return true;
}

auto Rendertarget_mesh::get_world_to_window(const glm::vec3 position_in_world) const -> std::optional<glm::vec2>
{
    auto const* node = get_node();
    if (node == nullptr) {
        return {};
    }

    const glm::vec3 position_in_mesh = node->transform_point_from_world_to_local(position_in_world);
    const glm::vec2 a{
        position_in_mesh.x / m_local_width,
        position_in_mesh.y / m_local_height
    };
    const glm::vec2 b{
         a.x + 0.5f,
        -a.y + 0.5f
    };
    if (
        (b.x < 0.0f) ||
        (b.y < 0.0f) ||
        (b.x > 1.0f) ||
        (b.y > 1.0f)
    ) {
        return {};
    }
    return glm::vec2{
        m_texture->get_width() * b.x,
        m_texture->get_height() * b.y
    };
}

void Rendertarget_mesh::render_done(App_context& context)
{
    {
        erhe::graphics::Blit_command_encoder encoder = context.graphics_device->make_blit_command_encoder();
        encoder.generate_mipmaps(m_texture.get());
    }

    if (s_rendertarget_mesh_lod_bias != m_sampler->get_lod_bias()) {
        m_sampler = std::make_shared<erhe::graphics::Sampler>(
            *context.graphics_device,
            erhe::graphics::Sampler_create_info{
                .min_filter  = erhe::graphics::Filter::linear,
                .mag_filter  = erhe::graphics::Filter::nearest,
                .mipmap_mode = erhe::graphics::Sampler_mipmap_mode::linear,
                .lod_bias    = s_rendertarget_mesh_lod_bias,
                .debug_label = "Rendertarget_mesh"
            }
        );
        m_material->texture_samplers.base_color.sampler = m_sampler;
    }
}

auto Rendertarget_mesh::get_pointer() const -> std::optional<glm::vec2>
{
    return m_pointer;
}

auto Rendertarget_mesh::get_width() const -> float
{
    return static_cast<float>(m_texture->get_width());
}

auto Rendertarget_mesh::get_height() const -> float
{
    return static_cast<float>(m_texture->get_height());
}

auto Rendertarget_mesh::get_pixels_per_meter() const -> float
{
    return m_pixels_per_meter;
}

auto is_rendertarget(const erhe::Item_base* const item) -> bool
{
    if (item == nullptr) {
        return false;
    }
    return erhe::utility::test_bit_set(item->get_type(), erhe::Item_type::rendertarget);
}

auto is_rendertarget(const std::shared_ptr<erhe::Item_base>& item) -> bool
{
    return is_rendertarget(item.get());
}

auto get_rendertarget(const erhe::scene::Node* const node) -> std::shared_ptr<Rendertarget_mesh>
{
    for (const auto& attachment : node->get_attachments()) {
        auto rendertarget = std::dynamic_pointer_cast<Rendertarget_mesh>(attachment);
        if (rendertarget) {
            return rendertarget;
        }
    }
    return {};
}

float Rendertarget_mesh::s_rendertarget_mesh_lod_bias{-0.666f};

void Rendertarget_mesh::set_mesh_lod_bias(float lod_bias)
{
    s_rendertarget_mesh_lod_bias = lod_bias;
}

auto Rendertarget_mesh::get_mesh_lod_bias() -> float
{
    return s_rendertarget_mesh_lod_bias;
}
    
}  // namespace editor
