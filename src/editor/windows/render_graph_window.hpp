#pragma once

#include "erhe/application/imgui/imgui_window.hpp"
#include "erhe/components/components.hpp"

#include <memory>

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
    static constexpr std::string_view c_label{"Rendergraph"};
    static constexpr std::string_view c_title{"Render Graph"};
    static constexpr uint32_t hash = compiletime_xxhash::xxh32(c_label.data(), c_label.size(), {});

    Rendergraph_window ();
    ~Rendergraph_window() noexcept override;

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
    std::shared_ptr<erhe::application::Rendergraph> m_render_graph;
};

} // namespace editor
