#pragma once

#include "erhe/application/imgui/imgui_window.hpp"
#include "erhe/components/components.hpp"

namespace hextiles
{

class Game_window
    : public erhe::application::Imgui_window
    , public erhe::components::Component
{
public:
    static constexpr const char* c_type_name{"Game_window"};
    static constexpr const char* c_title{"Game"};
    static constexpr uint32_t c_type_hash = compiletime_xxhash::xxh32(c_type_name, compiletime_strlen(c_type_name), {});

    Game_window ();
    ~Game_window() noexcept override;

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return c_type_hash; }
    void initialize_component();

    // Implements Imgui_window
    void imgui() override;
    auto flags() -> ImGuiWindowFlags override;
};

extern Game_window* g_game_window;

} // namespace hextiles
