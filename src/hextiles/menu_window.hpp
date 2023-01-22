#pragma once

#include "coordinate.hpp"

#include "erhe/application/commands/command.hpp"
#include "erhe/application/imgui/imgui_window.hpp"

#include "erhe/components/components.hpp"

namespace hextiles
{

class Menu_window
    : public erhe::components::Component
    , public erhe::application::Imgui_window
{
public:
    static constexpr std::string_view c_type_name{"Menu_window"};
    static constexpr std::string_view c_title{"Menu"};
    static constexpr uint32_t c_type_hash = compiletime_xxhash::xxh32(c_type_name.data(), c_type_name.size(), {});

    Menu_window ();
    ~Menu_window() noexcept override;

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return c_type_hash; }
    void declare_required_components() override;
    void initialize_component       () override;

    // Implements Imgui_window
    void imgui() override;

    // Public API
    void show_menu();
};

extern Menu_window* g_menu_window;

} // namespace hextiles
