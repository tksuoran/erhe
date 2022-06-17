#pragma once

#include "coordinate.hpp"

#include "erhe/application/commands/command.hpp"
#include "erhe/application/windows/imgui_window.hpp"

#include "erhe/components/components.hpp"

namespace hextiles
{

class Menu_window
    : public erhe::components::Component
    , public erhe::application::Imgui_window
{
public:
    static constexpr std::string_view c_label{"Menu_window"};
    static constexpr std::string_view c_title{"Menu"};
    static constexpr uint32_t hash = compiletime_xxhash::xxh32(c_label.data(), c_label.size(), {});

    Menu_window ();
    ~Menu_window() noexcept override;

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return hash; }
    void declare_required_components() override;
    void initialize_component       () override;

    // Implements Imgui_window
    void imgui() override;

    // Public API
    void show_menu();
};

} // namespace hextiles
