#pragma once

#include "tools/tool.hpp"
#include "scene/scene_view.hpp"

#include "erhe/application/imgui/imgui_window.hpp"
#include "erhe/components/components.hpp"

#include <glm/glm.hpp>

#include <memory>
#include <optional>

namespace editor
{

class Hover_tool
    : public erhe::application::Imgui_window
    , public erhe::components::Component
    , public Tool
{
public:
    static constexpr std::string_view c_type_name{"Hover_tool"};
    static constexpr std::string_view c_title{"Hover tool"};
    static constexpr uint32_t c_type_hash{
        compiletime_xxhash::xxh32(
            c_type_name.data(),
            c_type_name.size(),
            {}
        )
    };

    Hover_tool ();
    ~Hover_tool() noexcept override;

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return c_type_hash; }
    void declare_required_components() override;
    void initialize_component       () override;

    // Implements Tool
    void tool_render(const Render_context& context) override;

    // Implements Imgui_window
    void imgui() override;

private:
    bool m_show_snapped_grid_position{false};
};

extern Hover_tool* g_hover_tool;

} // namespace editor
