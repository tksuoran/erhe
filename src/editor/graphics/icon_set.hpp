#pragma once

#include "graphics/icon_rasterization.hpp"

#include <glm/glm.hpp>

#include <memory>
#include <optional>
#include <vector>

namespace erhe {
    class Item_base;
}
namespace erhe::graphics {
    class Device;
    class Texture;
}
namespace erhe::scene {
    enum class Light_type : unsigned int;
}

namespace lunasvg {
    class Document;
}

namespace editor {

class App_context;
class Icon_loader;
class Icon_rasterization;
class Icon_settings;

class Icons
{
public:
    void queue_load_icons(Icon_loader& icon_loader);

    glm::vec2 anim             {};
    glm::vec2 bone             {};
    glm::vec2 brush_small      {}; // for vertex paint
    glm::vec2 brush_big        {}; // for brush tool
    glm::vec2 camera           {};
    glm::vec2 directional_light{};
    glm::vec2 drag             {};
    glm::vec2 file             {};
    glm::vec2 folder           {};
    glm::vec2 grid             {};
    glm::vec2 hud              {};
    glm::vec2 material         {};
    glm::vec2 mesh             {};
    glm::vec2 mouse_lmb        {};
    glm::vec2 mouse_lmb_drag   {};
    glm::vec2 mouse_mmb        {};
    glm::vec2 mouse_mmb_drag   {};
    glm::vec2 mouse_move       {};
    glm::vec2 mouse_rmb        {};
    glm::vec2 mouse_rmb_drag   {};
    glm::vec2 move             {};
    glm::vec2 node             {};
    glm::vec2 physics          {};
    glm::vec2 point_light      {};
    glm::vec2 pull             {};
    glm::vec2 push             {};
    glm::vec2 raytrace         {};
    glm::vec2 rotate           {};
    glm::vec2 scale            {};
    glm::vec2 scene            {};
    glm::vec2 select           {};
    glm::vec2 skin             {};
    glm::vec2 space_mouse      {};
    glm::vec2 space_mouse_lmb  {};
    glm::vec2 space_mouse_rmb  {};
    glm::vec2 spot_light       {};
    glm::vec2 texture          {};
    glm::vec2 three_dots       {};
    glm::vec2 vive             {};
    glm::vec2 vive_menu        {};
    glm::vec2 vive_trackpad    {};
    glm::vec2 vive_trigger     {};
};

class Icon_set
{
public:
    Icon_set(
        App_context&            context,
        erhe::graphics::Device& graphics_device,
        Icon_settings&          icon_settings,
        Icons&                  icons,
        Icon_loader&            icon_loader
    );

    void load_icons(
        erhe::graphics::Device& graphics_device,
        Icon_settings&          icon_settings,
        Icons&                  icons_in,
        Icon_loader&            loader
    );

    void item_icon(const std::shared_ptr<erhe::Item_base>& item, float scale);

    [[nodiscard]] auto get_icon(const erhe::scene::Light_type type) const -> const glm::vec2;

    [[nodiscard]] auto get_small_rasterization () const -> const Icon_rasterization&;
    [[nodiscard]] auto get_large_rasterization () const -> const Icon_rasterization&;
    [[nodiscard]] auto get_hotbar_rasterization() const -> const Icon_rasterization&;

    void add_icons(uint64_t item_type, float scale);

private:
    App_context&                        m_context;
    std::unique_ptr<Icon_rasterization> m_small;
    std::unique_ptr<Icon_rasterization> m_large;
    std::unique_ptr<Icon_rasterization> m_hotbar;

public:
    Icons icons;
    static constexpr int s_row_count = 16;
    static constexpr int s_column_count = 16;

    class Type_icon
    {
    public:
        glm::vec2 icon;
        std::optional<glm::vec4> color{};
    };
    std::vector<std::optional<Type_icon>> type_icons;
};

} // namespace editor
