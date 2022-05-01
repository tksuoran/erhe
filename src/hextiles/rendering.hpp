#pragma once

#include "types.hpp"

#include "erhe/components/components.hpp"
#include "erhe/scene/viewport.hpp"

namespace erhe::application
{
    class Imgui_renderer;
}

namespace hextiles
{

class Tile_renderer;
class Tiles;

class Rendering
    : public erhe::components::Component
{
public:
    static constexpr std::string_view c_label{"Rendering"};
    static constexpr uint32_t hash {
        compiletime_xxhash::xxh32(
            c_label.data(),
            c_label.size(),
            {}
        )
    };

    Rendering ();
    ~Rendering() noexcept override;

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return hash; }
    void connect() override;

    // Public API
    auto terrain_image          (terrain_tile_t terrain_tile, const int scale) -> bool;
    auto unit_image             (unit_tile_t unit_tile, const int scale) -> bool;
    void make_terrain_type_combo(const char* label, terrain_t& value);
    void make_unit_type_combo   (const char* label, unit_t& value);

private:
    // Component dependencies
    std::shared_ptr<Tile_renderer>                      m_tile_renderer;
    std::shared_ptr<Tiles>                             m_tiles;
    std::shared_ptr<erhe::application::Imgui_renderer> m_imgui_renderer;
};

}
