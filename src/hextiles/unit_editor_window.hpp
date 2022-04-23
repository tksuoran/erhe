#pragma once

#include "erhe/application/windows/imgui_window.hpp"
#include "erhe/components/components.hpp"

namespace hextiles
{

class Type_editor;

class Unit_editor_window
    : public erhe::components::Component
    , public erhe::application::Imgui_window
{
public:
    static constexpr std::string_view c_name {"Unit_editor_window"};
    static constexpr std::string_view c_title{"Unit Editor"};
    static constexpr uint32_t hash = compiletime_xxhash::xxh32(c_name.data(), c_name.size(), {});

    Unit_editor_window ();
    ~Unit_editor_window() noexcept override;

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return hash; }
    void initialize_component() override;

    // Implements Imgui_window
    void imgui() override;
};

} // namespace hextiles
