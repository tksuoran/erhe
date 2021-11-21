#include "configuration.hpp"
#include "log.hpp"

#include "erhe/graphics/state/depth_stencil_state.hpp"

#include <cxxopts.hpp>

namespace editor {

constexpr float float_zero_value{0.0f};
constexpr float float_one_value {1.0f};

auto str(const bool value) -> const char*
{
    return value ? "true" : "false";
}

Configuration::Configuration(int argc, char** argv)
    : erhe::components::Component{c_name}
{
    cxxopts::Options options("Editor", "Erhe Editor (C) 2021 Timo Suoranta");

    options.add_options()
        ("gui",                        "Enable GUI",                            cxxopts::value<bool>()->default_value(str( gui)))
        ("no-gui",                     "Disable GUI",                           cxxopts::value<bool>()->default_value(str(!gui)))
        ("openxr",                     "Enable OpenXR HMD support",             cxxopts::value<bool>()->default_value(str( openxr)))
        ("no-openxr",                  "Disable OpenXR HMD support",            cxxopts::value<bool>()->default_value(str(!openxr)))
        ("parallel-initialization",    "Use parallel component initialization", cxxopts::value<bool>()->default_value(str( parallel_initialization)))
        ("no-parallel-initialization", "Use serial component initialization",   cxxopts::value<bool>()->default_value(str(!parallel_initialization)))
        ("reverse-depth",              "Enable reverse depth",                  cxxopts::value<bool>()->default_value(str( reverse_depth)))
        ("no-reverse-depth",           "Disable reverse depth",                 cxxopts::value<bool>()->default_value(str(!reverse_depth)));

    try
    {
        auto arguments = options.parse(argc, argv);

        gui                     = arguments["gui"                    ].as<bool>() && !arguments["no-gui"                    ].as<bool>();
        openxr                  = arguments["openxr"                 ].as<bool>() && !arguments["no-openxr"                 ].as<bool>();
        parallel_initialization = arguments["parallel-initialization"].as<bool>() && !arguments["no-parallel-initialization"].as<bool>();
        reverse_depth           = arguments["reverse-depth"          ].as<bool>() && !arguments["no-reverse-depth"          ].as<bool>();
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

auto Configuration::depth_function(const gl::Depth_function depth_function) const -> gl::Depth_function
{
    return reverse_depth ? erhe::graphics::reverse(depth_function) : depth_function;
}

} // namespace editor

