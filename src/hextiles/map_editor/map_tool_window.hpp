#pragma once

#include "coordinate.hpp"

#include "erhe/application/imgui/imgui_window.hpp"
#include "erhe/components/components.hpp"

#include <string>

namespace hextiles
{

class Map_editor;
class Map_window;
class Tiles;
class Tile_renderer;

class Map_tool_window
    : public erhe::components::Component
    , public erhe::application::Imgui_window
{
public:
    static constexpr std::string_view c_type_name{"Map_tool"};
    static constexpr std::string_view c_title{"Map Tool"};
    static constexpr uint32_t c_type_hash = compiletime_xxhash::xxh32(c_type_name.data(), c_type_name.size(), {});

    Map_tool_window ();
    ~Map_tool_window() noexcept override;

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return c_type_hash; }
    void declare_required_components() override;
    void initialize_component       () override;
    void post_initialize            () override;

    // Implements Imgui_window
    void imgui() override;

private:
    void tile_info(const Tile_coordinate tile_position);

    // Component dependencies
    std::shared_ptr<Map_editor>                        m_map_editor;
    std::shared_ptr<Map_window>                        m_map_window;
    std::shared_ptr<Tiles>                             m_tiles;
    std::shared_ptr<Tile_renderer>                     m_tile_renderer;
    std::shared_ptr<erhe::application::Imgui_renderer> m_imgui_renderer;
};

} // namespace hextiles