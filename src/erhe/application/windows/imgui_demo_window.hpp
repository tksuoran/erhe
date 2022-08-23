#pragma once

#include "erhe/application/imgui/imgui_window.hpp"

#include "erhe/components/components.hpp"

#include <memory>

namespace erhe::application
{

class Imgui_demo_window
    : public erhe::components::Component
    , public Imgui_window
{
public:
    static constexpr std::string_view c_type_name{"ImGui_demo_window"};
    static constexpr std::string_view c_title{"ImGui Demo"};
    static constexpr uint32_t c_type_hash = compiletime_xxhash::xxh32(c_type_name.data(), c_type_name.size(), {});

    Imgui_demo_window ();
    ~Imgui_demo_window() noexcept override;

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return c_type_hash; }
    void declare_required_components() override;
    void initialize_component       () override;

    // Implements Imgui_window
    void imgui() override;

private:
};

} // namespace erhe::application
