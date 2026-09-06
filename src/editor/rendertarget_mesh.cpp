#include "rendertarget_mesh.hpp"

#include "app_context.hpp"
#include "content_library/content_library.hpp"
#include "editor_log.hpp"
#include "erhe_scene_renderer/mesh_memory.hpp"
#include "scene/scene_view.hpp"

#include "erhe_geometry/geometry.hpp"
#include "erhe_geometry/shapes/regular_polygon.hpp"
#include "erhe_graphics/blit_command_encoder.hpp"
#include "erhe_graphics/buffer_transfer_queue.hpp"
#include "erhe_graphics/command_buffer.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/render_pass.hpp"
#include "erhe_graphics/sampler.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_math/math_util.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_utility/bit_helpers.hpp"
#include "erhe_verify/verify.hpp"

namespace editor {

auto Rendertarget_mesh::property_owner_type() -> erhe::property::Owner_type
{
    static const erhe::property::Owner_type id = erhe::property::allocate_owner_type(erhe::scene::Mesh::property_owner_type(), "Rendertarget_mesh");
    return id;
}

namespace {

constexpr std::string_view c_rendertarget_group = "Rendertarget";

} // anonymous namespace

const erhe::property::Property<float> Rendertarget_mesh::width_property = erhe::property::Property<float>::register_computed(
    "width", Rendertarget_mesh::property_owner_type(),
    [](const erhe::property::Dependency_object& object) -> erhe::property::Property_value { return static_cast<const Rendertarget_mesh&>(object).get_width(); },
    erhe::property::Property_metadata{.flags = erhe::property::Property_flags::none, .ui = erhe::property::Property_ui{.group = c_rendertarget_group, .tooltip = "Texture width in pixels; the mesh is this over the pixels per meter wide, in meters", .label = "Width"}}
);
const erhe::property::Property<float> Rendertarget_mesh::height_property = erhe::property::Property<float>::register_computed(
    "height", Rendertarget_mesh::property_owner_type(),
    [](const erhe::property::Dependency_object& object) -> erhe::property::Property_value { return static_cast<const Rendertarget_mesh&>(object).get_height(); },
    erhe::property::Property_metadata{.flags = erhe::property::Property_flags::none, .ui = erhe::property::Property_ui{.group = c_rendertarget_group, .tooltip = "Texture height in pixels; the mesh is this over the pixels per meter high, in meters", .label = "Height"}}
);
const erhe::property::Property<float> Rendertarget_mesh::pixels_per_meter_property = erhe::property::Property<float>::register_computed(
    "pixels_per_meter", Rendertarget_mesh::property_owner_type(),
    [](const erhe::property::Dependency_object& object) -> erhe::property::Property_value { return static_cast<const Rendertarget_mesh&>(object).get_pixels_per_meter(); },
    erhe::property::Property_metadata{.flags = erhe::property::Property_flags::none, .ui = erhe::property::Property_ui{.group = c_rendertarget_group, .tooltip = "Texture pixels per world meter, set at creation", .label = "Pixels per Meter"}}
);

Rendertarget_mesh::Rendertarget_mesh(
    erhe::graphics::Device&                 graphics_device,
    erhe::graphics::Command_buffer&         command_buffer,
    erhe::scene_renderer::Mesh_memory&      mesh_memory,
    const std::shared_ptr<Content_library>& material_home,
    const int                               width,
    const int                               height,
    const float                             pixels_per_meter
)
    : erhe::scene::Mesh {"Rendertarget Node"}
    , m_pixels_per_meter{pixels_per_meter}
    , m_material_home   {material_home}
{
    ERHE_VERIFY(material_home);

    enable_flag_bits(erhe::Item_flags::rendertarget);

    resize_rendertarget(command_buffer, graphics_device, mesh_memory, width, height);
}

Rendertarget_mesh::~Rendertarget_mesh() noexcept
{
    // Symmetric to the registration in resize_rendertarget(): remove the
    // owning entry so dead materials do not accumulate in the home library
    // when the mesh is rebuilt (e.g. the Hotbar / Hud quad on re-home).
    // Locking fails during the home scene's own teardown, where the library
    // dies with the scene and no removal is needed.
    const std::shared_ptr<Content_library> material_home = m_material_home.lock();
    if (material_home && m_material) {
        std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{material_home->mutex};
        material_home->materials->remove(m_material);
    }
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
    erhe::graphics::Command_buffer&    command_buffer,
    erhe::graphics::Device&            graphics_device,
    erhe::scene_renderer::Mesh_memory& mesh_memory,
    const int                          width,
    const int                          height
)
{
    if (m_texture && m_texture->get_width() == width && m_texture->get_height() == height) {
        return;
    }

    using Texture     = erhe::graphics::Texture;
    using Render_pass = erhe::graphics::Render_pass;

    m_texture = std::make_shared<Texture>(
        graphics_device,
        erhe::graphics::Texture_create_info{
            .device       = graphics_device,
            .usage_mask   =
                erhe::graphics::Image_usage_flag_bit_mask::color_attachment |
                erhe::graphics::Image_usage_flag_bit_mask::sampled          |
                erhe::graphics::Image_usage_flag_bit_mask::transfer_src     |
                erhe::graphics::Image_usage_flag_bit_mask::transfer_dst,
            .type         = erhe::graphics::Texture_type::texture_2d,
            .pixelformat  = erhe::dataformat::Format::format_8_vec4_srgb,
            .use_mipmaps  = true,
            .sample_count = 0,
            .width        = width,
            .height       = height,
            .debug_label  = "Rendertarget_mesh::m_texture"
        }
    );
    // Render pass below uses layout_before=shader_read_only_optimal. Texture
    // starts in UNDEFINED layout, so transition it up front to match what
    // start_render_pass expects on first use; subsequent frames already end
    // in shader_read_only_optimal via layout_after, so this is a one-shot.
    // Also, the texture may be sampled by ImGui before it has been rendered
    // into (rendertarget_mesh is referenced as a sampled texture by
    // rendertarget_imgui_host's draw data).
    command_buffer.transition_texture_layout(*m_texture.get(), erhe::graphics::Image_layout::shader_read_only_optimal);

    // TODO Use multisample resolve
    erhe::graphics::Render_pass_descriptor render_pass_descriptor{};
    render_pass_descriptor.color_attachments[0].texture        = m_texture.get();
    render_pass_descriptor.color_attachments[0].load_action    = erhe::graphics::Load_action::Clear;
    render_pass_descriptor.color_attachments[0].clear_value[0] = 0.0f;
    render_pass_descriptor.color_attachments[0].clear_value[1] = 0.0f;
    render_pass_descriptor.color_attachments[0].clear_value[2] = 0.0f;
    render_pass_descriptor.color_attachments[0].clear_value[3] = 0.66f;
    render_pass_descriptor.color_attachments[0].store_action   = erhe::graphics::Store_action::Store;
    render_pass_descriptor.color_attachments[0].usage_before   = erhe::graphics::Image_usage_flag_bit_mask::sampled;
    render_pass_descriptor.color_attachments[0].layout_before  = erhe::graphics::Image_layout::shader_read_only_optimal;
    render_pass_descriptor.color_attachments[0].usage_after    = erhe::graphics::Image_usage_flag_bit_mask::sampled;
    render_pass_descriptor.color_attachments[0].layout_after   = erhe::graphics::Image_layout::shader_read_only_optimal;
    render_pass_descriptor.render_target_width                 = width;
    render_pass_descriptor.render_target_height                = height;
    render_pass_descriptor.debug_label                         = "Rendertarget Node";
    m_render_pass = std::make_shared<Render_pass>(graphics_device, render_pass_descriptor);

    if (!m_material) {
        m_material = std::make_shared<erhe::primitive::Material>(
            erhe::primitive::Material_create_info{
                .name = "Rendertarget Node",
                .values = {
                    .base_color = glm::vec3{1.0f, 1.0f, 1.0f},
                    .bxdf_model = erhe::primitive::Bxdf_model::unlit,
                    .blending_mode = erhe::primitive::Material_blending_mode::alpha_blend
                }
            }
        );
        m_material->disable_flag_bits(erhe::Item_flags::show_in_ui);
        // R5.2b explicit definition registration: the home library stated at
        // creation owns the material. Without this, the mesh registering
        // into a scene would find an unhosted, unlisted material (loud
        // register_mesh warning) instead of a definition.
        const std::shared_ptr<Content_library> material_home = m_material_home.lock();
        ERHE_VERIFY(material_home);
        std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{material_home->mutex};
        material_home->materials->add(m_material);
    }
    // Resize recreates the texture; the material identity is stable across
    // resizes, only its texture binding follows.
    m_material->set_base_color_texture(m_texture);
    m_material->set_slot_sampler(m_material->data.texture_samplers.base_color, sampler_state());

    m_local_width  = static_cast<float>(m_texture->get_width ()) / m_pixels_per_meter;
    m_local_height = static_cast<float>(m_texture->get_height()) / m_pixels_per_meter;
    invalidate_dependents(width_property.get());  // D26: expressions reading the size re-evaluate
    invalidate_dependents(height_property.get());

    std::shared_ptr<erhe::geometry::Geometry> geometry = std::make_shared<erhe::geometry::Geometry>();
    erhe::geometry::shapes::make_rectangle(geometry->get_mesh(), m_local_width, m_local_height, true, false);

    // make_rectangle bakes UVs for a bottom-left texture origin (OpenGL).
    // On backends whose texture origin is top-left (Vulkan, Metal), content
    // rendered into the rendertarget is stored top-down, so sampling with
    // the default UVs produces an upside-down image. Flip V on the quad
    // UVs to match. Mirrors Imgui_renderer::get_rtt_uv0/uv1.
    if (graphics_device.get_info().coordinate_conventions.texture_origin == erhe::math::Texture_origin::top_left) {
        GEO::Mesh& geo_mesh = geometry->get_mesh();
        erhe::geometry::Mesh_attributes attributes{geo_mesh};
        const GEO::index_t vertex_count = geo_mesh.vertices.nb();
        for (GEO::index_t v = 0; v < vertex_count; ++v) {
            if (attributes.vertex_texcoord_0.has(v)) {
                const GEO::vec2f uv = attributes.vertex_texcoord_0.get(v);
                attributes.vertex_texcoord_0.set(v, GEO::vec2f{uv.x, 1.0f - uv.y});
            }
        }
    }

    std::shared_ptr<erhe::primitive::Primitive> primitive = std::make_shared<erhe::primitive::Primitive>(
        geometry,
        erhe::primitive::Build_info{
            .primitive_types{ .fill_triangles = true },
            .buffer_info = mesh_memory.make_primitive_buffer_info()
        },
        erhe::primitive::Normal_style::polygon_normals
    );

    ERHE_VERIFY(primitive->make_raytrace());

    clear_primitives();
    add_primitive(primitive, m_material);

    mesh_memory.flush(command_buffer);

    enable_flag_bits(erhe::Item_flags::id |erhe::Item_flags::rendertarget);
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

    const erhe::scene::Node* const node = this;

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

    const erhe::scene::Node* const node = this;

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
    const erhe::scene::Node* const node = this;

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

// The slot sampler of the rendertarget texture: clamped, mipmapped,
// nearest magnification (the imgui host draws at pixel scale), with the
// tunable LOD bias.
auto Rendertarget_mesh::sampler_state() -> erhe::primitive::Material_sampler_state
{
    return erhe::primitive::Material_sampler_state{
        .wrap_u      = erhe::graphics::Sampler_address_mode::clamp_to_edge,
        .wrap_v      = erhe::graphics::Sampler_address_mode::clamp_to_edge,
        .min_filter  = erhe::graphics::Filter::linear,
        .mag_filter  = erhe::graphics::Filter::nearest,
        .mipmap_mode = erhe::graphics::Sampler_mipmap_mode::linear,
        .lod_bias    = s_rendertarget_mesh_lod_bias
    };
}

void Rendertarget_mesh::render_done(erhe::graphics::Command_buffer& command_buffer, App_context& context)
{
    {
        erhe::graphics::Blit_command_encoder encoder = context.graphics_device->make_blit_command_encoder(command_buffer);
        encoder.generate_mipmaps(m_texture.get());
    }

    if (s_rendertarget_mesh_lod_bias != m_material->data.texture_samplers.base_color.sampler.lod_bias) {
        m_material->set_slot_sampler(m_material->data.texture_samplers.base_color, sampler_state());
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
