#pragma once

#include "erhe/application/imgui/imgui_window.hpp"

#include "erhe/components/components.hpp"

namespace hextiles
{

class Game;
class Rendering;
class Tiles;
class Tile_renderer;

class Game_window
    : public erhe::components::Component
    , public erhe::application::Imgui_window
{
public:
    static constexpr std::string_view c_type_name{"Game_window"};
    static constexpr std::string_view c_title{"Game"};
    static constexpr uint32_t c_type_hash = compiletime_xxhash::xxh32(c_type_name.data(), c_type_name.size(), {});

    Game_window ();
    ~Game_window() noexcept override;

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return c_type_hash; }
    void declare_required_components() override;
    void initialize_component       () override;
    void post_initialize            () override;

    // Implements Imgui_window
    void imgui() override;
    auto flags() -> ImGuiWindowFlags override;

private:
    // Component dependencies
    std::shared_ptr<Game>          m_game;
    std::shared_ptr<Rendering>     m_rendering;
    std::shared_ptr<Tiles>         m_tiles;
    std::shared_ptr<Tile_renderer> m_tile_renderer;
};

} // namespace hextiles
