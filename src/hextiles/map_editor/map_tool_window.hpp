#pragma once

#include "coordinate.hpp"

#include "erhe/application/imgui/imgui_window.hpp"
#include "erhe/components/components.hpp"

#include <string>

namespace hextiles
{

class Map_tool_window
    : public erhe::components::Component
    , public erhe::application::Imgui_window
{
public:
    static constexpr const char* c_title{"Map Tool"};
    static constexpr const char* c_type_name{"Map_tool"};
    static constexpr uint32_t c_type_hash = compiletime_xxhash::xxh32(c_type_name, compiletime_strlen(c_type_name), {});

    Map_tool_window ();
    ~Map_tool_window() noexcept override;

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return c_type_hash; }
    void declare_required_components() override;
    void initialize_component       () override;

    // Implements Imgui_window
    void imgui() override;

private:
    void tile_info(const Tile_coordinate tile_position);
};

extern Map_tool_window* g_map_tool_window;

} // namespace hextiles
