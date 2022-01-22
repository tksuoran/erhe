#pragma once

#include "erhe/components/component.hpp"
#include "erhe/components/components.hpp"
#include "erhe/gl/wrapper_enums.hpp"

namespace editor {

class Configuration
    : public erhe::components::Component
{
public:
    static constexpr std::string_view c_name{"Configuration"};
    static constexpr uint32_t hash{
        compiletime_xxhash::xxh32(
            c_name.data(),
            c_name.size(),
            {}
        )
    };

    Configuration(int argc, char** argv);

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return hash; }

    // Public API
    [[nodiscard]] auto depth_clear_value_pointer() const -> const float *; // reverse_depth ? 0.0f : 1.0f;
    [[nodiscard]] auto depth_function           (const gl::Depth_function depth_function) const -> gl::Depth_function;

    bool viewports_hosted_in_imgui_windows{true};
    //bool viewports_hosted_in_imgui_windows{false};
    bool openxr                           {false};
    bool show_window                      {true};
    bool parallel_initialization          {true};
    bool reverse_depth                    {true};
    //bool fullscreen                       {true};
    bool fullscreen                       {false};
    //int  window_width                     {1280};
    //int  window_height                    {720};
    int  window_width                     {1920};
    int  window_height                    {1080};
    //int  window_width                     {2560};
    //int  window_height                    {1440};
    int  window_msaa_sample_count         {0};
    //int  window_width            {256};
    //int  window_height           {256};
    //int  window_msaa_sample_count{0};
};

} // namespace editor
