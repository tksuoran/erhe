#pragma once

#include "erhe/application/windows/imgui_window.hpp"
#include "erhe/components/components.hpp"

#include <memory>

namespace erhe::application
{
    class Render_graph;
}

namespace editor
{

class Editor_scenes;

class Render_graph_window
    : public erhe::components::Component
    , public erhe::application::Imgui_window
{
public:
    static constexpr std::string_view c_label{"Render_graph"};
    static constexpr std::string_view c_title{"Render Graph"};
    static constexpr uint32_t hash = compiletime_xxhash::xxh32(c_label.data(), c_label.size(), {});

    Render_graph_window ();
    ~Render_graph_window() noexcept override;

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return hash; }
    void declare_required_components() override;
    void initialize_component       () override;
    void post_initialize            () override;

    // Implements Imgui_window
    void imgui() override;

private:
    // Component dependencies
    std::shared_ptr<Editor_scenes>                   m_editor_scenes;
    std::shared_ptr<erhe::application::Render_graph> m_render_graph;
};

} // namespace editor
