#include "graphics/thumbnails.hpp"
#include "app_context.hpp"
#include "app_message_bus.hpp"
#include "assets/asset_manager.hpp"
#include "scene/scene_root.hpp"
#include "app_rendering.hpp"
#include "editor_log.hpp"
#include "app_settings.hpp"
#include "time.hpp"

#include "config/generated/thumbnails_config.hpp"
#include "erhe_graphics/command_buffer.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/scoped_debug_group.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_verify/verify.hpp"

#include <fmt/format.h>

namespace editor {

Thumbnail::Thumbnail() = default;
Thumbnail::Thumbnail(Thumbnail&&) noexcept = default;
auto Thumbnail::operator=(Thumbnail&&) noexcept -> Thumbnail& = default;
Thumbnail::~Thumbnail() noexcept = default;

Thumbnails::Thumbnails(
    const Thumbnails_config&        thumbnails_config,
    erhe::graphics::Device&         graphics_device,
    erhe::graphics::Command_buffer& init_command_buffer,
    App_context&                    context,
    App_message_bus&                app_message_bus
)
    : m_context{context}
    , m_graphics_device{graphics_device}
    , m_color_sampler{
        graphics_device,
        erhe::graphics::Sampler_create_info{
            .min_filter   = erhe::graphics::Filter::linear,
            .mag_filter   = erhe::graphics::Filter::nearest,
            .mipmap_mode  = erhe::graphics::Sampler_mipmap_mode::not_mipmapped, // TODO
            .address_mode = {
                erhe::graphics::Sampler_address_mode::clamp_to_edge,
                erhe::graphics::Sampler_address_mode::clamp_to_edge,
                erhe::graphics::Sampler_address_mode::clamp_to_edge
            },
            .debug_label  = "Thumbnail sampler"
        }
    }
{
    int capacity    = thumbnails_config.capacity;
    int size_pixels = thumbnails_config.size_pixels;

    m_thumbnails.resize(capacity);
    m_size_pixels = static_cast<unsigned int>(size_pixels);
    m_color_texture = std::make_shared<erhe::graphics::Texture>(
        graphics_device,
        erhe::graphics::Texture_create_info{
            .device            = graphics_device,
            .usage_mask        =
                erhe::graphics::Image_usage_flag_bit_mask::color_attachment |
                erhe::graphics::Image_usage_flag_bit_mask::sampled |
                erhe::graphics::Image_usage_flag_bit_mask::transfer_src |
                erhe::graphics::Image_usage_flag_bit_mask::transfer_dst,
            .type              = erhe::graphics::Texture_type::texture_2d_array,
            .pixelformat       = erhe::dataformat::Format::format_8_vec4_unorm, // TODO sRGB?
            .use_mipmaps       = true,
            .width             = m_size_pixels,
            .height            = m_size_pixels,
            .array_layer_count = capacity,
            .debug_label       = "Thumbnails color texture"
        }
    );
    // Defensive: Thumbnails::draw never records an image() referencing an
    // unrendered slot (fresh allocations return false without calling
    // image(); see Thumbnails::draw), and on Vulkan each used slot has its
    // own per-layer view so unused layers never bind to a descriptor. So
    // strictly speaking this transition is not required to keep validation
    // happy. Kept as a precaution: if a future change ever samples the
    // whole array as a single descriptor (e.g. dropping use_texture_view)
    // the unused layers would otherwise stay in UNDEFINED and trip
    // VUID-vkCmdDraw-None-09600.
    init_command_buffer.transition_texture_layout(*m_color_texture.get(), erhe::graphics::Image_layout::shader_read_only_optimal);

    if (graphics_device.get_info().use_texture_view) {
        for (int i = 0; i < capacity; ++i) {
            Thumbnail& t = m_thumbnails[i];

            erhe::graphics::Texture_create_info texture_create_info = erhe::graphics::Texture_create_info::make_view(m_graphics_device, m_color_texture);
            texture_create_info.type                  = erhe::graphics::Texture_type::texture_2d; // Single-layer view sampled as sampler2D
            texture_create_info.usage_mask            =
                erhe::graphics::Image_usage_flag_bit_mask::color_attachment |
                erhe::graphics::Image_usage_flag_bit_mask::sampled,
            texture_create_info.view_base_level       = 0;
            texture_create_info.array_layer_count     = 0;
            texture_create_info.use_mipmaps           = true;
            texture_create_info.level_count           = m_color_texture->get_level_count();
            texture_create_info.view_base_array_layer = i;
            texture_create_info.debug_label           = erhe::utility::Debug_label{fmt::format("Thumbnail layer {}", i)};
            t.texture_view = std::make_shared<erhe::graphics::Texture>(m_graphics_device, texture_create_info);
            t.texture_layer = 0; // view already targets specific layer
        }
    } else {
        // Without texture views, use the array texture directly with layer index
        for (int i = 0; i < capacity; ++i) {
            Thumbnail& t = m_thumbnails[i];
            t.texture_view  = m_color_texture;
            t.texture_layer = static_cast<unsigned int>(i);
        }
    }

    m_close_scene_subscription = app_message_bus.close_scene.subscribe(
        [this](Close_scene_message& message) {
            on_close_scene(static_cast<erhe::Item_host*>(message.scene_root.get()));
        }
    );
    m_items_removed_subscription = app_message_bus.items_removed.subscribe(
        [this](Items_removed_message& message) {
            on_items_removed(*message.removed.get());
        }
    );
}

Thumbnails::~Thumbnails() noexcept
{
}

void Thumbnails::release_slot(Thumbnail& thumbnail)
{
    thumbnail.callback.reset();
    thumbnail.observer = {};
    thumbnail.item.reset();
    thumbnail.item_id               = 0;
    thumbnail.last_use_frame_number = 0;
    thumbnail.time                  = 0;
    thumbnail.stale                 = false;
}

void Thumbnails::flush()
{
    for (Thumbnail& thumbnail : m_thumbnails) {
        release_slot(thumbnail);
    }
}

void Thumbnails::on_close_scene(erhe::Item_host* const closing_host)
{
    // R5.6: library assets (materials, brushes) are not hosted; the manager
    // knows whether the closing scene's container record defines them.
    Asset_manager* const asset_manager = m_context.asset_manager;
    for (Thumbnail& thumbnail : m_thumbnails) {
        if (thumbnail.item_id == 0) {
            continue;
        }
        const std::shared_ptr<erhe::Item_base> item = thumbnail.item.lock();
        const bool gone =
            !item ||
            ((asset_manager != nullptr) && asset_manager->is_hosted_or_defined_by(*item, closing_host)) ||
            ((asset_manager == nullptr) && (item->get_item_host() == closing_host));
        if (gone) {
            release_slot(thumbnail);
        }
    }
}

void Thumbnails::on_items_removed(const Removed_items& removed)
{
    for (Thumbnail& thumbnail : m_thumbnails) {
        if (thumbnail.item_id == 0) {
            continue;
        }
        const std::shared_ptr<erhe::Item_base> item = thumbnail.item.lock();
        if (!item || removed.lookup.contains(item.get())) {
            release_slot(thumbnail);
        }
    }
}

auto Thumbnails::draw(
    const std::shared_ptr<erhe::Item_base>& item,
    std::function<
        void(const std::shared_ptr<erhe::graphics::Texture>&, unsigned int, int64_t)
    >                                       callback,
    float                                   display_size
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
            thumbnail.last_use_frame_number = m_context.graphics_device->get_frame_index();
            if (thumbnail.stale) {
                // A property of the item changed since the last render:
                // re-render through the same deferred path as the hover
                // refresh below, keeping the old image up until then.
                thumbnail.callback = callback;
                thumbnail.stale    = false;
            }
            const float height = (display_size > 0.0f) ? display_size : ImGui::GetTextLineHeightWithSpacing();
            const int array_layer = m_graphics_device.get_info().use_texture_view
                ? -1
                : static_cast<int>(thumbnail.texture_layer);
            m_context.imgui_renderer->image(
                erhe::imgui::Draw_texture_parameters{
                    .texture_reference = thumbnail.texture_view,
                    .width             = static_cast<int>(height),
                    .height            = static_cast<int>(height),
                    .uv0               = m_context.imgui_renderer->get_rtt_uv0(),
                    .uv1               = m_context.imgui_renderer->get_rtt_uv1(),
                    .filter            = erhe::graphics::Filter::linear,
                    .mipmap_mode       = erhe::graphics::Sampler_mipmap_mode::linear,
                    .array_layer       = array_layer,
                    .debug_label       = "Thumbnails::draw()",
                }
            );
            if (ImGui::IsItemHovered()) {
                thumbnail.callback = callback;
                thumbnail.time += m_context.time->get_host_system_last_frame_duration_ns();
                ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4{0.0f, 0.0f, 0.0f, 0.8f});
                ImGui::BeginTooltip();
                m_context.imgui_renderer->image(
                    erhe::imgui::Draw_texture_parameters{
                        .texture_reference = thumbnail.texture_view,
                        .width             = static_cast<int>(m_size_pixels),
                        .height            = static_cast<int>(m_size_pixels),
                        .uv0               = m_context.imgui_renderer->get_rtt_uv0(),
                        .uv1               = m_context.imgui_renderer->get_rtt_uv1(),
                        .array_layer       = array_layer,
                        .debug_label       = "Thumbnails::draw()"
                    }
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

    oldest_thumbnail->last_use_frame_number = m_context.graphics_device->get_frame_index();
    oldest_thumbnail->item_id  = item_id;
    oldest_thumbnail->item     = item;
    oldest_thumbnail->callback = callback;
    oldest_thumbnail->stale    = false;
    // Follow the item's properties while the slot shows it; the token
    // unsubscribes when the slot is reclaimed, and a destroyed item
    // deactivates it. The slot vector never resizes after construction,
    // so the index stays valid.
    const std::size_t slot_index = static_cast<std::size_t>(oldest_thumbnail - m_thumbnails.data());
    oldest_thumbnail->observer = item->add_observer(
        [this, slot_index](erhe::property::Dependency_object&, const erhe::property::Property_changed_args&) {
            m_thumbnails[slot_index].stale = true;
        }
    );
    return false;
}

void Thumbnails::update()
{
    // log_frame->trace("Thumbnails::update()");

    ERHE_VERIFY(m_context.current_command_buffer != nullptr);
    erhe::graphics::Scoped_debug_group render_graph_scope{
        *m_context.current_command_buffer,
        "Thumbnails::update()"
    };

    for (size_t i = 0, end = m_thumbnails.size(); i < end; ++i) {
        Thumbnail& thumbnail = m_thumbnails[i];
        if (thumbnail.callback) {
            //log_render->trace("Updating thumbnail slot {}", i);
            thumbnail.callback.value()(thumbnail.texture_view, thumbnail.texture_layer, thumbnail.time);
            thumbnail.callback.reset();
        }
    }
}

}
