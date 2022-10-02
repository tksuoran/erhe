#pragma once

#include "tools/tool.hpp"

#include "erhe/application/imgui/imgui_window.hpp"
#include "erhe/components/components.hpp"

#include <glm/glm.hpp>

namespace erhe::application
{
    class Imgui_renderer;
}

namespace erhe::scene
{
    class Camera;
}

namespace editor
{

class Icon_set;
class Rendertarget_imgui_viewport;
class Rendertarget_node;
class Viewport_windows;

class Hotbar
    : public erhe::application::Imgui_window
    , public erhe::components::Component
    , public erhe::components::IUpdate_once_per_frame
    , public Tool
{
public:
    static constexpr std::string_view c_type_name{"Hotbar"};
    static constexpr std::string_view c_title{"Hotbar"};
    static constexpr uint32_t c_type_hash = compiletime_xxhash::xxh32(c_type_name.data(), c_type_name.size(), {});

    Hotbar ();
    ~Hotbar() noexcept override;

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

    // Implements Imgui_window
    [[nodiscard]] auto flags() -> ImGuiWindowFlags override;
    void on_begin() override;
    void imgui   () override;

    // Public API
    void update_node_transform(const glm::mat4& world_from_camera);

private:
    [[nodiscard]] auto get_camera() const -> std::shared_ptr<erhe::scene::Camera>;

    // Component dependencies
    std::shared_ptr<erhe::application::Imgui_renderer> m_imgui_renderer;
    std::shared_ptr<Icon_set>                          m_icon_set;
    std::shared_ptr<Viewport_windows>                  m_viewport_windows;


    std::shared_ptr<Rendertarget_node>           m_rendertarget_node;
    std::shared_ptr<Rendertarget_imgui_viewport> m_rendertarget_imgui_viewport;
    float m_x{ 0.0f};
    float m_y{-0.1f};
    float m_z{-0.4f};
};

} // namespace editor
