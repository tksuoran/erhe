#include "level/kit_thumbnails.hpp"

#include "level/kit_shape_library.hpp"
#include "level/level_kits.hpp"

#include "app_context.hpp"
#include "app_message_bus.hpp"
#include "brushes/brush.hpp"
#include "config/generated/editor_settings_config.hpp"
#include "editor_log.hpp"
#include "graphics/thumbnails.hpp"
#include "preview/brush_preview.hpp"
#include "scene/scene_root.hpp"

#include "erhe_graphics/command_buffer.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_imgui/imgui_renderer.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_verify/verify.hpp"

#include <fmt/format.h>
#include <glm/gtc/quaternion.hpp>
#include <imgui/imgui.h>

#include <algorithm>
#include <cmath>

namespace editor {

auto c_str(const Kit_thumbnail_state state) -> const char*
{
    switch (state) {
        case Kit_thumbnail_state::none:    return "none";
        case Kit_thumbnail_state::queued:  return "queued";
        case Kit_thumbnail_state::current: return "current";
        case Kit_thumbnail_state::failed:  return "failed";
    }
    return "?";
}

Kit_thumbnails::Kit_thumbnails(Level_kits& level_kits, App_context& context, App_message_bus& app_message_bus)
    : m_level_kits{level_kits}
    , m_context   {context}
{
    const std::size_t shape_count = level_kits.get_library().get_shapes().size();
    m_thumbnails.resize(shape_count);
    m_queue.reserve(shape_count);
    // As Thumbnails: the preset selects the shader variants the thumbnails draw with.
    m_graphics_settings_subscription = app_message_bus.graphics_settings.subscribe(
        [this](Graphics_settings_message& message) {
            on_graphics_settings_changed(message.graphics_preset);
        }
    );
}

void Kit_thumbnails::on_graphics_settings_changed(const Graphics_preset_entry* const graphics_preset)
{
    if (graphics_preset != nullptr) {
        std::string key = make_thumbnail_preset_key(*graphics_preset);
        if (key == m_rendered_preset_key) {
            return;
        }
        m_rendered_preset_key = std::move(key);
    }
    for (std::size_t i = 0, end = m_thumbnails.size(); i < end; ++i) {
        const Kit_thumbnail_state state = m_thumbnails[i].state;
        if ((state == Kit_thumbnail_state::current) || (state == Kit_thumbnail_state::failed)) {
            queue(i);
        }
    }
}

auto Kit_thumbnails::get_size_pixels() const -> int
{
    if (m_texture) {
        return m_texture->get_width();
    }
    return (m_context.editor_settings != nullptr) ? m_context.editor_settings->thumbnails.size_pixels : 0;
}

Kit_thumbnails::~Kit_thumbnails() noexcept = default;

auto Kit_thumbnails::get_lengths() const -> const std::optional<Kit_lengths>&
{
    return m_lengths;
}

auto Kit_thumbnails::get_thumbnails() const -> const std::vector<Kit_thumbnail>&
{
    return m_thumbnails;
}

auto Kit_thumbnails::get_queued_count() const -> std::size_t
{
    return m_queue.size();
}

auto Kit_thumbnails::get_render_count() const -> std::uint64_t
{
    return m_render_count;
}

void Kit_thumbnails::on_level_lengths_changed()
{
    m_lengths_changed = true;
}

auto Kit_thumbnails::get_shape_index(const Kit_shape_definition& shape) const -> std::size_t
{
    const std::vector<Kit_shape_definition>& shapes = m_level_kits.get_library().get_shapes();
    ERHE_VERIFY((&shape >= shapes.data()) && (&shape < (shapes.data() + shapes.size())));
    return static_cast<std::size_t>(&shape - shapes.data());
}

auto Kit_thumbnails::get_mesh_key(const Kit_shape_definition& shape) const -> std::string
{
    ERHE_VERIFY(m_lengths.has_value());
    return Level_kits::get_mesh_key(make_default_values(shape, m_lengths.value().module, m_lengths.value().wall_height));
}

auto Kit_thumbnails::get_material(const std::string& palette_name) -> std::shared_ptr<erhe::primitive::Material>
{
    const std::map<std::string, std::shared_ptr<erhe::primitive::Material>>::iterator i = m_materials.find(palette_name);
    if (i != m_materials.end()) {
        return i->second;
    }
    const Kit_palette_material* const palette = m_level_kits.get_library().find_palette_material(palette_name);
    if (palette == nullptr) {
        return {};
    }
    std::shared_ptr<erhe::primitive::Material> material = make_palette_material(*palette);
    m_context.brush_preview->add_material(material);
    m_materials.emplace(palette_name, material);
    return material;
}

void Kit_thumbnails::follow_scene(const std::shared_ptr<Scene_root>& scene_root)
{
    ERHE_VERIFY(scene_root);
    // A per-draw identity check: the last hovered viewport's scene changes without a message; lengths edits notify.
    const bool same_scene = !m_scene.owner_before(scene_root) && !scene_root.owner_before(m_scene) && !m_scene.expired();
    if (same_scene && !m_lengths_changed && m_lengths.has_value()) {
        return;
    }
    m_scene           = scene_root;
    m_lengths_changed = false;
    apply_lengths(m_level_kits.get_lengths(*scene_root));
}

void Kit_thumbnails::apply_lengths(const Kit_lengths& lengths)
{
    if (
        m_lengths.has_value() &&
        (m_lengths.value().module == lengths.module) &&
        (m_lengths.value().wall_height == lengths.wall_height)
    ) {
        return;
    }
    m_lengths = lengths;
    // Only the thumbnails whose defaults these lengths change render again; they show their image until then.
    const std::vector<Kit_shape_definition>& shapes = m_level_kits.get_library().get_shapes();
    for (std::size_t i = 0, end = m_thumbnails.size(); i < end; ++i) {
        const Kit_thumbnail& thumbnail = m_thumbnails[i];
        if ((thumbnail.state == Kit_thumbnail_state::none) || (thumbnail.state == Kit_thumbnail_state::queued)) {
            continue;
        }
        if (get_mesh_key(shapes[i]) != thumbnail.mesh_key) {
            queue(i);
        }
    }
    log_scene->trace("Kit thumbnails: lengths {} / {}, {} queued", lengths.module, lengths.wall_height, m_queue.size());
}

void Kit_thumbnails::queue(const std::size_t shape_index)
{
    m_thumbnails[shape_index].state = Kit_thumbnail_state::queued;
    m_queue.push_back(shape_index);
}

auto Kit_thumbnails::draw(const Kit_shape_definition& shape, const float size) -> bool
{
    const std::size_t shape_index = get_shape_index(shape);
    Kit_thumbnail&    thumbnail   = m_thumbnails[shape_index];
    if (thumbnail.state == Kit_thumbnail_state::none) {
        queue(shape_index);
    }
    // An image button: hover and press feedback, and a press on it never drags the window. Until its image exists an
    // invisible button of the same size and id stands in (a framed Button would shift the row's text baseline).
    const float side = std::floor(size);
    if (!thumbnail.has_image || !m_texture) {
        return ImGui::InvisibleButton(shape.id.c_str(), ImVec2{side, side});
    }
    const bool                   layer_views    = !m_layer_views.empty();
    erhe::imgui::Imgui_renderer& imgui_renderer = *m_context.imgui_renderer;
    // No frame padding, so both stand-ins are the same size; the hover and press colors show through the transparent
    // background of the image.
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{0.0f, 0.0f});
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{0.0f, 0.0f, 0.0f, 0.0f});
    const bool clicked = imgui_renderer.image_button(
        erhe::imgui::Draw_texture_parameters{
            .texture_reference = layer_views ? m_layer_views[shape_index] : m_texture,
            .id                = ImGui::GetID(shape.id.c_str()),
            .width             = static_cast<int>(side),
            .height            = static_cast<int>(side),
            .uv0               = imgui_renderer.get_rtt_uv0(),
            .uv1               = imgui_renderer.get_rtt_uv1(),
            .filter            = erhe::graphics::Filter::linear,
            .mipmap_mode       = erhe::graphics::Sampler_mipmap_mode::linear,
            .array_layer       = layer_views ? -1 : static_cast<int>(shape_index),
            .debug_label       = "Kit_thumbnails::draw()"
        }
    );
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
    return clicked;
}

void Kit_thumbnails::make_texture(erhe::graphics::Command_buffer& command_buffer)
{
    erhe::graphics::Device& device      = *m_context.graphics_device;
    const int               layer_count = static_cast<int>(m_thumbnails.size());
    const int               size_pixels = get_size_pixels();
    m_texture = std::make_shared<erhe::graphics::Texture>(
        device,
        erhe::graphics::Texture_create_info{
            .device            = device,
            .usage_mask        =
                erhe::graphics::Image_usage_flag_bit_mask::color_attachment |
                erhe::graphics::Image_usage_flag_bit_mask::sampled |
                erhe::graphics::Image_usage_flag_bit_mask::transfer_src |
                erhe::graphics::Image_usage_flag_bit_mask::transfer_dst,
            .type              = erhe::graphics::Texture_type::texture_2d_array,
            .pixelformat       = erhe::dataformat::Format::format_8_vec4_unorm,
            .use_mipmaps       = true,
            .width             = size_pixels,
            .height            = size_pixels,
            .array_layer_count = layer_count,
            .debug_label       = "Kit thumbnails"
        }
    );
    // A layer is sampled only after its render; the transition keeps the unrendered ones valid to bind.
    command_buffer.transition_texture_layout(*m_texture, erhe::graphics::Image_layout::shader_read_only_optimal);
    if (!device.get_info().use_texture_view) {
        return;
    }
    // As Thumbnails: a single-layer view per shape, sampled as sampler2D.
    m_layer_views.reserve(m_thumbnails.size());
    for (int layer = 0; layer < layer_count; ++layer) {
        erhe::graphics::Texture_create_info view_info = erhe::graphics::Texture_create_info::make_view(device, m_texture);
        view_info.type                  = erhe::graphics::Texture_type::texture_2d;
        view_info.usage_mask            =
            erhe::graphics::Image_usage_flag_bit_mask::color_attachment |
            erhe::graphics::Image_usage_flag_bit_mask::sampled;
        view_info.view_base_level       = 0;
        view_info.array_layer_count     = 0;
        view_info.use_mipmaps           = true;
        view_info.level_count           = m_texture->get_level_count();
        view_info.view_base_array_layer = layer;
        view_info.debug_label           = erhe::utility::Debug_label{fmt::format("Kit thumbnail layer {}", layer)};
        m_layer_views.push_back(std::make_shared<erhe::graphics::Texture>(device, view_info));
    }
}

void Kit_thumbnails::update(erhe::graphics::Command_buffer& command_buffer)
{
    if (m_queue.empty() || !m_lengths.has_value() || (m_context.brush_preview == nullptr)) {
        return;
    }
    ERHE_PROFILE_FUNCTION();
    if (!m_texture) {
        make_texture(command_buffer);
    }
    const std::size_t count = std::min(m_queue.size(), c_renders_per_frame);
    for (std::size_t i = 0; i < count; ++i) {
        render(command_buffer, m_queue[i]);
    }
    m_queue.erase(m_queue.begin(), m_queue.begin() + static_cast<std::ptrdiff_t>(count));
}

void Kit_thumbnails::render(erhe::graphics::Command_buffer& command_buffer, const std::size_t shape_index)
{
    const Kit_shape_definition& shape     = m_level_kits.get_library().get_shapes()[shape_index];
    Kit_thumbnail&              thumbnail = m_thumbnails[shape_index];
    const Kit_lengths&          lengths   = m_lengths.value();
    const Kit_parameter_values  values    = make_default_values(shape, lengths.module, lengths.wall_height);
    thumbnail.mesh_key = Level_kits::get_mesh_key(values);

    const std::string            problem = values.validate();
    const std::shared_ptr<Brush> brush   = problem.empty() ? m_level_kits.make_brush(values, Kit_brush_use::thumbnail) : std::shared_ptr<Brush>{};
    const Brush::Scaled* const   scaled  = brush ? brush->get_scaled(1.0) : nullptr;
    if (scaled == nullptr) {
        thumbnail.state     = Kit_thumbnail_state::failed;
        thumbnail.has_image = false;
        log_scene->warn("Kit thumbnail '{}': the defaults make no geometry{}", shape.id, problem.empty() ? std::string{} : (": " + problem));
        return;
    }

    m_level_kits.get_default_slot_materials(shape, m_slot_names);
    m_part_materials.clear();
    for (std::size_t part = 0, end = scaled->part_primitives.size(); part < end; ++part) {
        const bool has_slot = (part < m_slot_names.size()) && !m_slot_names[part].empty();
        m_part_materials.push_back(has_slot ? get_material(m_slot_names[part]) : std::shared_ptr<erhe::primitive::Material>{});
    }
    // A three-quarter view: the piece turned so its +x and +z faces face the preview camera.
    const glm::quat orientation = glm::angleAxis(glm::radians(-35.0f), glm::vec3{0.0f, 1.0f, 0.0f});
    const bool      layer_views = !m_layer_views.empty();
    m_context.brush_preview->render_parts_preview(
        command_buffer,
        layer_views ? m_layer_views[shape_index] : m_texture,
        layer_views ? 0u : static_cast<unsigned int>(shape_index),
        scaled->part_primitives,
        m_part_materials,
        orientation,
        nullptr
    );
    thumbnail.state     = Kit_thumbnail_state::current;
    thumbnail.has_image = true;
    ++thumbnail.render_count;
    ++m_render_count;
    log_scene->trace("Kit thumbnail '{}' rendered at {} / {}", shape.id, lengths.module, lengths.wall_height);
}

} // namespace editor
