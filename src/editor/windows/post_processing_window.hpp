#pragma once

#include "erhe/application/imgui/imgui_window.hpp"
#include "erhe/components/components.hpp"

#include <memory>

namespace editor
{

/// <summary>
/// ImGui window for showing downsample steps for a Post_processing node
/// </summary>
class Post_processing_window
    : public erhe::components::Component
    , public erhe::application::Imgui_window
{
public:
    static constexpr std::string_view c_type_name{"Post_processing_window"};
    static constexpr std::string_view c_title{"Post Processing"};
    static constexpr uint32_t c_type_hash = compiletime_xxhash::xxh32(c_type_name.data(), c_type_name.size(), {});

    Post_processing_window();
    ~Post_processing_window();

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return c_type_hash; }
    void declare_required_components() override;
    void initialize_component       () override;

    // Implements Imgui_window
    void imgui() override;
};

extern Post_processing_window* g_post_processing_window;

} // namespace editor
