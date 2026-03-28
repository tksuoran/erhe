#pragma once

#include "erhe_graphics/sampler.hpp"
#include "erhe_item/item.hpp"

#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace erhe::graphics {
    class Device;
    class Texture;
}

struct Thumbnails_config;

namespace editor {

class App_context;
class Icon_settings;
class Programs;

class Icon_settings;

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
    uint64_t                                 last_use_frame_number{0};
    int64_t                                  time{0};
    std::shared_ptr<erhe::graphics::Texture> texture_view{};
    unsigned int                             texture_layer{0};
    std::optional<
        std::function<void(const std::shared_ptr<erhe::graphics::Texture>&, unsigned int, int64_t)>
    >                                        callback{};
};

class Thumbnails
{
public:
    Thumbnails(const Thumbnails_config& thumbnails_config, erhe::graphics::Device& graphics_device, App_context& context);
    ~Thumbnails() noexcept;

    // This should be called once per frame, outside command encoder
    void update();

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
    App_context&                             m_context;
    erhe::graphics::Device&                  m_graphics_device;
    std::shared_ptr<erhe::graphics::Texture> m_color_texture;
    erhe::graphics::Sampler                  m_color_sampler;
    std::vector<Thumbnail>                   m_thumbnails;
    int                                      m_size_pixels{0};
    std::vector<uint64_t>                    m_color_texture_handles;
};

}
