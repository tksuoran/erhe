#pragma once

#include "renderers/primitive_buffer.hpp"
#include "renderers/viewport_config.hpp"

#include "erhe/application/imgui/imgui_window.hpp"
#include "erhe/components/components.hpp"
#include "erhe/graphics/instance.hpp"

#include <glm/glm.hpp>

#include <string_view>

namespace editor
{

class Viewport_config_window
    : public erhe::components::Component
    , public erhe::application::Imgui_window
{
public:
    static constexpr const char* c_visualization_mode_strings[] =
    {
        "Off",
        "Selected",
        "All"
    };

    static constexpr std::string_view c_type_name{"Viewport_config"};
    static constexpr std::string_view c_title{"Viewport config"};
    static constexpr uint32_t c_type_hash = compiletime_xxhash::xxh32(c_type_name.data(), c_type_name.size(), {});

    Viewport_config_window();
    ~Viewport_config_window() override;

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return c_type_hash; }
    void declare_required_components() override;
    void initialize_component       () override;
    void deinitialize_component     () override;

    // Implements Imgui_window
    void imgui          () override;

    // Public API
    void render_style_ui(Render_style& render_style);

    Viewport_config  data;
    float            rendertarget_mesh_lod_bias{-0.666f};
    Viewport_config* edit_data{nullptr};
};

extern Viewport_config_window* g_viewport_config_window;

} // namespace editor
