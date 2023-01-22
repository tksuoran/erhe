#pragma once

#include "erhe/application/imgui/imgui_window.hpp"
#include "erhe/components/components.hpp"

#include <glm/glm.hpp>

#include <memory>

namespace editor
{

class Layers_window
    : public erhe::components::Component
    , public erhe::application::Imgui_window
{
public:
    static constexpr std::string_view c_type_name{"Layers_window"};
    static constexpr std::string_view c_title{"Layers"};
    static constexpr uint32_t c_type_hash = compiletime_xxhash::xxh32(c_type_name.data(), c_type_name.size(), {});

    Layers_window ();
    ~Layers_window() noexcept override;

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return c_type_hash; }
    void declare_required_components() override;
    void initialize_component       () override;

    // Implements Imgui_window
    void imgui() override;
};

extern Layers_window* g_layers_window;

} // namespace editor
