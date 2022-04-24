#include "erhe/application/configuration.hpp"
#include "erhe/application/log.hpp"

#include "erhe/graphics/state/depth_stencil_state.hpp"

#include <cxxopts.hpp>

namespace erhe::application {

constexpr float float_zero_value{0.0f};
constexpr float float_one_value {1.0f};

auto str(const bool value) -> const char*
{
    return value ? "true" : "false";
}

Configuration::Configuration(int argc, char** argv)
    : erhe::components::Component      {c_label}
    , viewports_hosted_in_imgui_windows{true}
    , openxr                           {false}
    , show_window                      {true}
    , parallel_initialization          {true}
    , reverse_depth                    {true}
    , fullscreen                       {false}
#if 1
    , window_width                     {1920}//{1280}
    , window_height                    {1080}//{720}
#else
    , window_width                     {1280}
    , window_height                    {720}
#endif
    , window_msaa_sample_count         {0}
{
    cxxopts::Options options("Editor", "Erhe Editor (C) 2022 Timo Suoranta");

    options.add_options()
        ("gui",                        "Enable ImGui hosted viewports",         cxxopts::value<bool>()->default_value(str( viewports_hosted_in_imgui_windows)))
        ("no-gui",                     "Disable ImGui hosted viewports",        cxxopts::value<bool>()->default_value(str(!viewports_hosted_in_imgui_windows)))
        ("openxr",                     "Enable OpenXR HMD support",             cxxopts::value<bool>()->default_value(str( openxr)))
        ("no-openxr",                  "Disable OpenXR HMD support",            cxxopts::value<bool>()->default_value(str(!openxr)))
        ("parallel-initialization",    "Use parallel component initialization", cxxopts::value<bool>()->default_value(str( parallel_initialization)))
        ("no-parallel-initialization", "Use serial component initialization",   cxxopts::value<bool>()->default_value(str(!parallel_initialization)))
        ("reverse-depth",              "Enable reverse depth",                  cxxopts::value<bool>()->default_value(str( reverse_depth)))
        ("no-reverse-depth",           "Disable reverse depth",                 cxxopts::value<bool>()->default_value(str(!reverse_depth)));

    try
    {
        auto arguments = options.parse(argc, argv);

        viewports_hosted_in_imgui_windows = arguments["gui"                    ].as<bool>() && !arguments["no-gui"                    ].as<bool>();
        openxr                            = arguments["openxr"                 ].as<bool>() && !arguments["no-openxr"                 ].as<bool>();
        parallel_initialization           = arguments["parallel-initialization"].as<bool>() && !arguments["no-parallel-initialization"].as<bool>();
        reverse_depth                     = arguments["reverse-depth"          ].as<bool>() && !arguments["no-reverse-depth"          ].as<bool>();
    }
    catch (const std::exception& e)
    {
        log_startup.error("Error parsing command line argumenst: {}\n", e.what());
    }
}

auto Configuration::depth_clear_value_pointer() const -> const float*
{
    return reverse_depth ? &float_zero_value : &float_one_value;
}

auto Configuration::depth_function(
    const gl::Depth_function depth_function
) const -> gl::Depth_function
{
    return reverse_depth
        ? erhe::graphics::reverse(depth_function)
        : depth_function;
}

} // namespace erhe::application

