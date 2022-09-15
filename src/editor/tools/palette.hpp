#pragma once

#include "tools/tool.hpp"

#include "erhe/application/commands/command.hpp"
#include "erhe/application/imgui/imgui_window.hpp"
#include "erhe/components/components.hpp"

#include <glm/glm.hpp>

namespace editor
{

class Rendertarget_imgui_viewport;
class Rendertarget_node;
class Viewport_windows;

class Palette;

class Toggle_palette_visibility_command
    : public erhe::application::Command
{
public:
    explicit Toggle_palette_visibility_command(Palette& palette)
        : Command  {"Palette.toggle_visibility"}
        , m_palette{palette}
    {
    }

    auto try_call(erhe::application::Command_context& context) -> bool override;

private:
    Palette& m_palette;
};

class Palette
    : public erhe::application::Imgui_window
    , public erhe::components::Component
    , public erhe::components::IUpdate_once_per_frame
    , public Tool
{
public:
    static constexpr std::string_view c_type_name{"Palette"};
    static constexpr std::string_view c_title{"Palette"};
    static constexpr uint32_t c_type_hash = compiletime_xxhash::xxh32(c_type_name.data(), c_type_name.size(), {});

    Palette ();
    ~Palette() noexcept override;

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return c_type_hash; }
    void declare_required_components() override;
    void initialize_component       () override;
    void post_initialize            () override;

    // Implements Tool
    [[nodiscard]] auto description() -> const char* override;
    void tool_render(const Render_context& context)  override;

    // Implements IUpdate_once_per_frame
    void update_once_per_frame(const erhe::components::Time_context& time_context) override;

    // Public APi
    void toggle_visibility();

#if defined(ERHE_GUI_LIBRARY_IMGUI)
    // Implements Imgui_window
    auto flags   () -> ImGuiWindowFlags override;
    void on_begin() override;
    void imgui   () override;
#endif

private:
    Toggle_palette_visibility_command m_toggle_visibility_command;

    std::shared_ptr<Viewport_windows> m_viewport_windows;

    std::shared_ptr<Rendertarget_node>           m_rendertarget_node;
    std::shared_ptr<Rendertarget_imgui_viewport> m_rendertarget_imgui_viewport;
    float m_x{ 0.0f};
    float m_y{ 0.0f};
    float m_z{-1.5f};
    int   m_width_items{10};
    int   m_height_items{10};
    bool  m_is_visible{false};
};

} // namespace editor
