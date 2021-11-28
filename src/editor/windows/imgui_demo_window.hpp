#pragma once

#include "windows/imgui_window.hpp"

#include <memory>

namespace editor
{

class Imgui_demo_window
    : public erhe::components::Component
    , public Imgui_window
{
public:
    static constexpr std::string_view c_name {"ImGui_demo_window"};
    static constexpr std::string_view c_title{"ImGui Demo"};
    static constexpr uint32_t hash = compiletime_xxhash::xxh32(c_name.data(), c_name.size(), {});

    Imgui_demo_window ();
    ~Imgui_demo_window() override;

    // Implements Component
    auto get_type_hash       () const -> uint32_t override { return hash; }
    void initialize_component() override;

    // Implements Imgui_window
    void imgui(Pointer_context& pointer_context) override;

private:
};

} // namespace editor
