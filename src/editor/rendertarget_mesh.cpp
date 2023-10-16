// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "rendertarget_mesh.hpp"

#include "editor_context.hpp"
#include "editor_log.hpp"
#include "renderers/mesh_memory.hpp"
#include "scene/scene_view.hpp"
#include "windows/viewport_config_window.hpp"

#include "erhe_geometry/shapes/regular_polygon.hpp"
#include "erhe_graphics/buffer_transfer_queue.hpp"
#include "erhe_graphics/framebuffer.hpp"
#include "erhe_graphics/sampler.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_primitive/primitive_builder.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_bit/bit_helpers.hpp"
#include "erhe_math/math_util.hpp"

namespace editor
{

Rendertarget_mesh::Rendertarget_mesh(
    erhe::graphics::Instance& graphics_instance,
    Mesh_memory&              mesh_memory,
    const int                 width,
    const int                 height,
    const float               pixels_per_meter
)
    : erhe::scene::Mesh {"Rendertarget Node"}
    , m_pixels_per_meter{pixels_per_meter}
{
    enable_flag_bits(erhe::Item_flags::rendertarget | erhe::Item_flags::translucent);

    resize_rendertarget(graphics_instance, mesh_memory, width, height);
}

auto Rendertarget_mesh::get_static_type() -> uint64_t
{
    return
        erhe::Item_type::node_attachment |
        erhe::Item_type::mesh            |
        erhe::Item_type::rendertarget;
}

auto Rendertarget_mesh::get_type() const -> uint64_t
{
    return get_static_type();
}

auto Rendertarget_mesh::get_type_name() const -> std::string_view
{
    return static_type_name;
}

void Rendertarget_mesh::resize_rendertarget(
    erhe::graphics::Instance& graphics_instance,
    Mesh_memory&              mesh_memory,
    const int                 width,
    const int                 height
)
{
    if (m_texture && m_texture->width() == width && m_texture->height() == height) {
        return;
    }

    using Texture     = erhe::graphics::Texture;
    using Framebuffer = erhe::graphics::Framebuffer;

    m_texture = std::make_shared<Texture>(
        Texture::Create_info{
            .instance        = graphics_instance,
            .target          = gl::Texture_target::texture_2d,
            .internal_format = gl::Internal_format::srgb8_alpha8,
            .use_mipmaps     = true,
            .sample_count    = 0,
            .width           = width,
            .height          = height
        }
    );
    m_texture->set_debug_label("Rendertarget Node");
    const float clear_value[4] = { 0.0f, 0.0f, 0.0f, 0.85f };
    if (gl::is_command_supported(gl::Command::Command_glClearTexImage)) {
        gl::clear_tex_image(
            m_texture->gl_name(),
            0,
            gl::Pixel_format::rgba,
            gl::Pixel_type::float_,
            &clear_value[0]
        );
    } else {
        // TODO
    }

    m_sampler = std::make_shared<erhe::graphics::Sampler>(
        erhe::graphics::Sampler_create_info{
            .min_filter  = gl::Texture_min_filter::linear_mipmap_linear,
            .mag_filter  = gl::Texture_mag_filter::nearest,
            .lod_bias    = -0.666f,
            .debug_label = "Rendertarget_mesh"
        }
    );

    Framebuffer::Create_info create_info;
    create_info.attach(gl::Framebuffer_attachment::color_attachment0, m_texture.get());
    m_framebuffer = std::make_shared<Framebuffer>(create_info);
    m_framebuffer->set_debug_label("Rendertarget Node");

    m_material = std::make_shared<erhe::primitive::Material>(
        "Rendertarget Node",
        glm::vec4{0.1f, 0.1f, 0.2f, 1.0f}
    );
    m_material->base_color_texture = m_texture;
    m_material->base_color_sampler = m_sampler;

    m_local_width  = static_cast<float>(m_texture->width ()) / m_pixels_per_meter;
    m_local_height = static_cast<float>(m_texture->height()) / m_pixels_per_meter;

    auto geometry = erhe::geometry::shapes::make_rectangle(
        m_local_width,
        m_local_height,
        false,
        true
    );

    const auto shared_geometry = std::make_shared<erhe::geometry::Geometry>(
        std::move(geometry)
    );

    clear_primitives();
    add_primitive(
        erhe::primitive::Primitive{
            .material           = m_material,
            .geometry_primitive = std::make_shared<erhe::primitive::Geometry_primitive>(
                shared_geometry,
                erhe::primitive::Build_info{
                    .primitive_types{ .fill_triangles = true },
                    .buffer_info = mesh_memory.buffer_info
                }
            )
        }
    );

    mesh_memory.gl_buffer_transfer_queue.flush();

    enable_flag_bits(
        erhe::Item_flags::visible      |
        erhe::Item_flags::translucent  |
        erhe::Item_flags::id           |
        erhe::Item_flags::rendertarget
    );
}

auto Rendertarget_mesh::texture() const -> std::shared_ptr<erhe::graphics::Texture>
{
    return m_texture;
}

auto Rendertarget_mesh::framebuffer() const -> std::shared_ptr<erhe::graphics::Framebuffer>
{
    return m_framebuffer;
}

#if defined(ERHE_XR_LIBRARY_OPENXR)
void Rendertarget_mesh::update_headset()
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
    if (
        !opt_origin_in_world.has_value() ||
        !opt_direction_in_world.has_value()
    ) {
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
    const auto hit = erhe::math::intersect_plane<float>(
        unit_axis_z,
        origo,
        origin_in_mesh,
        direction_in_mesh
    );

    if (!hit.has_value()) {
        return false;
    }

    {
        const glm::vec3 hit_position_in_mesh = origin_in_mesh + hit.value() * direction_in_mesh;
        const glm::vec2 a{
            hit_position_in_mesh.x / m_local_width,
            hit_position_in_mesh.y / m_local_height
        };
        const glm::vec2 b{
            a.x + 0.5f,
            0.5f - a.y
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
            m_texture->width() * (1.0f - b.x),
            m_texture->height() * b.y
        };

        SPDLOG_LOGGER_TRACE(
            log_pointer,
            "rt ray hit position {}",
            hit_position_in_viewport
        );

        m_pointer = hit_position_in_viewport;

        return true;
    }
}

[[nodiscard]] auto Rendertarget_mesh::world_to_window(
    const glm::vec3 position_in_world
) const -> std::optional<glm::vec2>
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
        0.5f - a.y
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
        m_texture->width() * (1.0f - b.x),
        m_texture->height() * b.y
    };
}

void Rendertarget_mesh::bind()
{
    gl::bind_framebuffer(gl::Framebuffer_target::draw_framebuffer, m_framebuffer->gl_name());
    gl::viewport        (0, 0, m_texture->width(), m_texture->height());
}

void Rendertarget_mesh::clear(const glm::vec4 clear_color)
{
    //m_pipeline_state_tracker->shader_stages.reset();
    //m_pipeline_state_tracker->color_blend.execute(
    //    erhe::graphics::Color_blend_state::color_blend_disabled
    //);

    // TODO Should probably restore m_pipeline_state_tracker code above instead
    gl::color_mask (GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

    gl::clear_color(clear_color.r, clear_color.g, clear_color.b, clear_color.a);
    gl::clear      (gl::Clear_buffer_mask::color_buffer_bit);
}

void Rendertarget_mesh::render_done(Editor_context& context)
{
    gl::generate_texture_mipmap(m_texture->gl_name());

    if (context.viewport_config_window->rendertarget_mesh_lod_bias != m_sampler->lod_bias) {
        m_sampler = std::make_shared<erhe::graphics::Sampler>(
            erhe::graphics::Sampler_create_info{
                .min_filter  = gl::Texture_min_filter::linear_mipmap_linear,
                .mag_filter  = gl::Texture_mag_filter::nearest,
                .lod_bias    = context.viewport_config_window->rendertarget_mesh_lod_bias,
                .debug_label = "Rendertarget_mesh"
            }
        );
        m_material->base_color_sampler = m_sampler;
    }
}

[[nodiscard]] auto Rendertarget_mesh::get_pointer() const -> std::optional<glm::vec2>
{
    return m_pointer;
}

[[nodiscard]] auto Rendertarget_mesh::width() const -> float
{
    return static_cast<float>(m_texture->width());
}

[[nodiscard]] auto Rendertarget_mesh::height() const -> float
{
    return static_cast<float>(m_texture->height());
}

[[nodiscard]] auto Rendertarget_mesh::pixels_per_meter() const -> float
{
    return m_pixels_per_meter;
}

auto is_rendertarget(const erhe::Item_base* const item) -> bool
{
    if (item == nullptr) {
        return false;
    }
    return erhe::bit::test_all_rhs_bits_set(item->get_type(), erhe::Item_type::rendertarget);
}

auto is_rendertarget(const std::shared_ptr<erhe::Item_base>& item) -> bool
{
    return is_rendertarget(item.get());
}

auto get_rendertarget(
    const erhe::scene::Node* const node
) -> std::shared_ptr<Rendertarget_mesh>
{
    for (const auto& attachment : node->get_attachments()) {
        auto rendertarget = std::dynamic_pointer_cast<Rendertarget_mesh>(attachment);
        if (rendertarget) {
            return rendertarget;
        }
    }
    return {};
}

}  // namespace editor
