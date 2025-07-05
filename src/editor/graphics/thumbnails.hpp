#pragma once

#include "erhe_graphics/sampler.hpp"
#include "erhe_item/item.hpp"

#include <memory>
#include <vector>

namespace erhe::graphics {
    class Device;
    class Render_command_encoder;
    class Render_pass;
    class Renderbuffer;
    class Texture;
}

namespace editor {

class App_context;
class Icon_settings;
class Programs;

class Icon_settings;

class Thumbnail
{
public:
    Thumbnail();
    ~Thumbnail();
    Thumbnail(const Thumbnail&) = delete;
    Thumbnail(Thumbnail&&);
    auto operator=(const Thumbnail&) -> Thumbnail& = delete;
    auto operator=(Thumbnail&&) -> Thumbnail&;

    std::weak_ptr<erhe::Item_base>               item{};
    uint64_t                                     last_use_frame_number{0};
    std::shared_ptr<erhe::graphics::Texture>     texture_view{};
    std::unique_ptr<erhe::graphics::Render_pass> render_pass{};
    uint64_t                                     color_texture_handle{};
};

class Thumbnails
{
public:
    Thumbnails(erhe::graphics::Device& graphics_device, App_context& context);
    ~Thumbnails();

    void update(uint64_t frame_number);

    void draw(
        std::shared_ptr<erhe::Item_base>            item,
        std::function<bool(Thumbnails& thumbnails)> callback
    );

private:
    App_context&                                  m_context;
    erhe::graphics::Device&                       m_graphics_device;
    std::shared_ptr<erhe::graphics::Texture>      m_color_texture;
    std::shared_ptr<erhe::graphics::Renderbuffer> m_depth_renderbuffer;
    std::unique_ptr<erhe::graphics::Render_pass>  m_render_pass;
    erhe::graphics::Sampler                       m_color_sampler;
    std::vector<Thumbnail>                        m_thumbnails;
    int                                           m_size_pixels{0};
    std::vector<uint64_t>                         m_color_texture_handles;
};

}
