#pragma once

#include "erhe/application/imgui/imgui_window.hpp"
#include "erhe/components/components.hpp"

#include <memory>

namespace ImNodes::Ez
{
    struct Context;
}

namespace erhe::application
{
    class Rendergraph;
}

namespace editor
{

class Editor_scenes;

class Rendergraph_window
    : public erhe::components::Component
    , public erhe::application::Imgui_window
{
public:
    static constexpr std::string_view c_type_name{"Rendergraph"};
    static constexpr std::string_view c_title{"Render Graph"};
    static constexpr uint32_t c_type_hash = compiletime_xxhash::xxh32(c_type_name.data(), c_type_name.size(), {});

    Rendergraph_window ();
    ~Rendergraph_window() noexcept override;

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return c_type_hash; }
    void declare_required_components() override;
    void initialize_component       () override;
    void post_initialize            () override;

    // Implements Imgui_window
    void imgui() override;
    auto flags() -> ImGuiWindowFlags override;

private:
    void imnodes_demo();

    // Component dependencies
    std::shared_ptr<Editor_scenes>                     m_editor_scenes;
    std::shared_ptr<erhe::application::Rendergraph>    m_render_graph;
    std::shared_ptr<erhe::application::Imgui_renderer> m_imgui_renderer;

    float                 m_image_size{100.0f};
    float                 m_curve_strength{10.0f};
    ImNodes::Ez::Context* m_imnodes_context{nullptr};

};

} // namespace editor
