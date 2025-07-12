#include "graphics/thumbnails.hpp"
#include "app_context.hpp"
#include "app_rendering.hpp"
#include "editor_log.hpp"
#include "app_settings.hpp"
#include "time.hpp"

#include "erhe_configuration/configuration.hpp"
#include "erhe_graphics/debug.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/renderbuffer.hpp"
#include "erhe_graphics/texture.hpp"

#include <fmt/format.h>

namespace editor {

Thumbnail::Thumbnail() = default;
Thumbnail::Thumbnail(Thumbnail&&) = default;
auto Thumbnail::operator=(Thumbnail&&) -> Thumbnail& = default;
Thumbnail::~Thumbnail() = default;

Thumbnails::Thumbnails(erhe::graphics::Device& graphics_device, App_context& context)
    : m_context{context}
    , m_graphics_device{graphics_device}
    , m_color_sampler{
        graphics_device,
        erhe::graphics::Sampler_create_info{
            .min_filter  = erhe::graphics::Filter::linear,
            .mag_filter  = erhe::graphics::Filter::nearest,
            .mipmap_mode = erhe::graphics::Sampler_mipmap_mode::not_mipmapped, // TODO
            .debug_label = "Thumbnail sampler"
        }
    }
{
    int capacity = 200;
    int size_pixels = 256;
    const auto& section = erhe::configuration::get_ini_file_section("erhe.ini", "thumbnails");
    section.get("capacity", capacity);
    section.get("capacity", size_pixels);

    m_thumbnails.resize(capacity);
    m_size_pixels = static_cast<unsigned int>(size_pixels);
    m_color_texture = std::make_shared<erhe::graphics::Texture>(
        graphics_device,
        erhe::graphics::Texture_create_info{
            .device            = graphics_device,
            .type              = erhe::graphics::Texture_type::texture_2d,
            .pixelformat       = erhe::dataformat::Format::format_8_vec4_unorm, // TODO sRGB?
            .use_mipmaps       = true,
            .width             = m_size_pixels,
            .height            = m_size_pixels,
            .array_layer_count = capacity,
            .debug_label       = "Thumbnails"
        }
    );

    m_depth_renderbuffer = std::make_unique<erhe::graphics::Renderbuffer>(
        graphics_device,
        erhe::dataformat::Format::format_d32_sfloat,
        m_size_pixels,
        m_size_pixels
    );
    m_depth_renderbuffer->set_debug_label("Thumbnail depth renderbuffer");

    for (int i = 0; i < capacity; ++i) {
        Thumbnail& t = m_thumbnails[i];

        erhe::graphics::Texture_create_info texture_create_info = erhe::graphics::Texture_create_info::make_view(m_graphics_device, m_color_texture);
        texture_create_info.view_base_level       = 0;
        texture_create_info.array_layer_count     = 0;
        texture_create_info.use_mipmaps           = true;
        texture_create_info.level_count           = m_color_texture->get_level_count();
        texture_create_info.view_base_array_layer = i;
        texture_create_info.debug_label           = fmt::format("Thumbnail layer {}", i);
        t.texture_view = std::make_shared<erhe::graphics::Texture>(m_graphics_device, texture_create_info);
    }
}

Thumbnails::~Thumbnails()
{
}

auto Thumbnails::draw(
    const std::shared_ptr<erhe::Item_base>&                                       item,
    std::function<void(const std::shared_ptr<erhe::graphics::Texture>&, int64_t)> callback
) -> bool
{
    const std::size_t item_id = item->get_id();
    uint64_t oldest_frame_number = m_thumbnails[0].last_use_frame_number;
    Thumbnail* oldest_thumbnail = &m_thumbnails[0];
    for (size_t i = 0, end = m_thumbnails.size(); i < end; ++i) {
        Thumbnail& thumbnail = m_thumbnails[i];
        if (
            (thumbnail.item_id == item_id) &&
            !thumbnail.callback.has_value()
        ) {
            thumbnail.last_use_frame_number = m_context.graphics_device->get_frame_number();
            const float height = ImGui::GetTextLineHeightWithSpacing();
            m_context.imgui_renderer->image(
                thumbnail.texture_view,
                static_cast<int>(height), //m_size_pixels,
                static_cast<int>(height), //m_size_pixels,
                glm::vec2{0.0f, 1.0f},
                glm::vec2{1.0f, 0.0f},
                glm::vec4{0.0f, 0.0f, 0.0f, 0.0f},
                glm::vec4{1.0f, 1.0f, 1.0f, 1.0f},
                erhe::graphics::Filter::linear,
                erhe::graphics::Sampler_mipmap_mode::linear
            );
            if (ImGui::IsItemHovered()) {
                thumbnail.callback = callback;
                thumbnail.time += m_context.time->get_host_system_last_frame_duration_ns();
                ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4{0.0f, 0.0f, 0.0f, 0.8f});
                ImGui::BeginTooltip();
                m_context.imgui_renderer->image(
                    thumbnail.texture_view,
                    m_size_pixels,
                    m_size_pixels,
                    glm::vec2{0.0f, 1.0f},
                    glm::vec2{1.0f, 0.0f},
                    glm::vec4{0.0f, 0.0f, 0.0f, 0.0f},
                    glm::vec4{1.0f, 1.0f, 1.0f, 1.0f},
                    erhe::graphics::Filter::nearest,
                    erhe::graphics::Sampler_mipmap_mode::not_mipmapped
                );
                ImGui::EndTooltip();
                ImGui::PopStyleColor();
            }
            ImGui::SameLine();
            return true;
        }
        if (thumbnail.last_use_frame_number < oldest_frame_number) {
            oldest_frame_number = thumbnail.last_use_frame_number;
            oldest_thumbnail = &thumbnail;
        }
    }

    oldest_thumbnail->last_use_frame_number = m_context.graphics_device->get_frame_number();
    oldest_thumbnail->item_id = item_id;
    oldest_thumbnail->callback = callback;
    return false;
}

void Thumbnails::update()
{
    erhe::graphics::Scoped_debug_group render_graph_scope{"Thumbnails::update()"};

    for (size_t i = 0, end = m_thumbnails.size(); i < end; ++i) {
        Thumbnail& thumbnail = m_thumbnails[i];
        if (thumbnail.callback) {
            log_render->trace("Updating thumbnail slot {}", i);
            thumbnail.callback.value()(thumbnail.texture_view, thumbnail.time);
            thumbnail.callback.reset();
        }
    }
}

}
