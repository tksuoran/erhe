#pragma once

#include "app_message.hpp"
#include "erhe_graphics/sampler.hpp"
#include "erhe_message_bus/message_bus.hpp"
#include "erhe_item/item.hpp"
#include "erhe_property/dependency_object.hpp"

#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace erhe::graphics {
    class Command_buffer;
    class Device;
    class Texture;
}

struct Thumbnails_config;

namespace editor {

class App_context;
class App_message_bus;
class Programs;


class Thumbnail
{
public:
    Thumbnail();
    ~Thumbnail() noexcept;
    Thumbnail(const Thumbnail&) = delete;
    Thumbnail(Thumbnail&&) noexcept;
    auto operator=(const Thumbnail&) -> Thumbnail& = delete;
    auto operator=(Thumbnail&&) noexcept -> Thumbnail&;

    std::size_t                              item_id{};
    // The item this slot shows, for the removal / close checks only (never
    // locked into a strong reference the slot would then pin).
    std::weak_ptr<erhe::Item_base>           item{};
    uint64_t                                 last_use_frame_number{0};
    int64_t                                  time{0};
    std::shared_ptr<erhe::graphics::Texture> texture_view{};
    unsigned int                             texture_layer{0};
    std::optional<
        std::function<void(const std::shared_ptr<erhe::graphics::Texture>&, unsigned int, int64_t)>
    >                                        callback{};

    // Property observer on the item this slot shows (D21): a change marks
    // the slot stale, and the next draw() of it re-queues the render.
    erhe::property::Observer_token           observer{};
    bool                                     stale{false};
};

class Thumbnails
{
public:
    Thumbnails(
        const Thumbnails_config&        thumbnails_config,
        erhe::graphics::Device&         graphics_device,
        erhe::graphics::Command_buffer& init_command_buffer,
        App_context&                    context,
        App_message_bus&                app_message_bus
    );
    ~Thumbnails() noexcept;

    // This should be called once per frame, outside command encoder
    void update();

    // Frees every slot: pending render callbacks are dropped unrendered
    // (they own the item they would render), property observers are
    // released and the slots forget their items, so the next draw() of any
    // item renders it afresh. The editor state reset (MCP
    // reset_editor_state) calls this before closing the scenes.
    void flush();

    // Scene close / items removed: a slot showing content of the closing
    // scene, or an item the message names, is freed the same way (AGENTS.md
    // "Scene-hosted references in editor parts"). Slot count is the
    // configured capacity, so the walk is bounded and independent of the
    // message size.
    void on_close_scene  (erhe::Item_host* closing_host);
    void on_items_removed(const Removed_items& removed);

    // The callback is NOT invoked from inside draw(): it is stored in a
    // thumbnail slot and invoked later from update() -- typically on the
    // next frame, after the message bus pump has run. It is stored when the
    // slot is claimed, while the thumbnail is hovered, and when a property
    // of the item changed since the slot was last rendered. Anything destroyed
    // by then (an ImGui window torn down by scene close, any per-scene
    // part) must not be captured. Capture only whole-app-lifetime state
    // (App_context&) and shared ownership of the item being rendered.
    auto draw(
        const std::shared_ptr<erhe::Item_base>& item,
        std::function<void(
            const std::shared_ptr<erhe::graphics::Texture>&,
            unsigned int,
            int64_t
        )> callback,
        float display_size = 0.0f // 0 = use text line height
    ) -> bool;

private:
    void release_slot(Thumbnail& thumbnail);

    App_context&                             m_context;
    erhe::message_bus::Subscription<Close_scene_message>   m_close_scene_subscription;
    erhe::message_bus::Subscription<Items_removed_message> m_items_removed_subscription;
    erhe::graphics::Device&                  m_graphics_device;
    std::shared_ptr<erhe::graphics::Texture> m_color_texture;
    erhe::graphics::Sampler                  m_color_sampler;
    std::vector<Thumbnail>                   m_thumbnails;
    int                                      m_size_pixels{0};
    std::vector<uint64_t>                    m_color_texture_handles;
};

}
