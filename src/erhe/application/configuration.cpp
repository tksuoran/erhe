#include "erhe/application/configuration.hpp"
#include "erhe/application/application_log.hpp"

#include "erhe/graphics/state/depth_stencil_state.hpp"
#include "erhe/toolkit/verify.hpp"

#include <cxxopts.hpp>

#include <algorithm>
#include <cctype>
#include <string>

namespace erhe::application {

constexpr float float_zero_value{0.0f};
constexpr float float_one_value {1.0f};

auto str(const bool value) -> const char*
{
    return value ? "true" : "false";
}

auto to_lower(std::string data) -> std::string
{
    data = data.substr(0, data.find(' '));
    data = data.substr(0, data.find('\t'));
    std::transform(
        data.begin(),
        data.end(),
        data.begin(),
        [](unsigned char c)
        {
            return std::tolower(c);
        }
    );
    return data;
}

auto split(
    const std::string& s,
    const char         delimeter
) -> std::vector<std::string>
{
    std::vector<std::string> result;
    std::stringstream        ss{s};
    std::string              item;

    while (std::getline(ss, item, delimeter)) {
        result.push_back(item);
    }

    return result;
}

auto trim(
    const std::string& str,
    const std::string& whitespace
) -> std::string
{
    const auto strBegin = str.find_first_not_of(whitespace);
    if (strBegin == std::string::npos) {
        return ""; // no content
    }

    const auto strEnd = str.find_last_not_of(whitespace);
    const auto strRange = strEnd - strBegin + 1;

    return str.substr(strBegin, strRange);
}

void ini_get(
    const mINI::INIMap<std::string>& ini,
    std::string                      key,
    int&                             destination
)
{
    if (ini.has(key)) {
        destination = std::stoi(ini.get(key));
    }
}

void ini_get(
    const mINI::INIMap<std::string>& ini,
    std::string                      key,
    float&                           destination
)
{
    if (ini.has(key)) {
        destination = std::stof(ini.get(key));
    }
}

void ini_get(
    const mINI::INIMap<std::string>& ini,
    std::string                      key,
    glm::vec2&                       destination
)
{
    if (ini.has(key)) {
        std::string value = ini.get(key);
        const auto values = split(value, ',');
        if (values.size() > 0) {
            destination.x = std::stof(trim(values.at(0)));
        }
        if (values.size() > 1) {
            destination.y = std::stof(trim(values.at(1)));
        }
    }
}

void ini_get(
    const mINI::INIMap<std::string>& ini,
    std::string                      key,
    glm::vec3&                       destination
)
{
    if (ini.has(key)) {
        std::string value = ini.get(key);
        const auto values = split(value, ',');
        if (values.size() > 0) {
            destination.x = std::stof(trim(values.at(0)));
        }
        if (values.size() > 1) {
            destination.y = std::stof(trim(values.at(1)));
        }
        if (values.size() > 2) {
            destination.z = std::stof(trim(values.at(2)));
        }
    }
}

void ini_get(
    const mINI::INIMap<std::string>& ini,
    std::string                      key,
    glm::vec4&                       destination
)
{
    if (ini.has(key)) {
        std::string value = ini.get(key);
        const auto values = split(value, ',');
        destination.w = 1.0f;
        if (values.size() >= 3) {
            destination.x = std::stof(trim(values.at(0)));
            destination.y = std::stof(trim(values.at(1)));
            destination.z = std::stof(trim(values.at(2)));
        }
        if (values.size() >= 4) {
            destination.w = std::stof(trim(values.at(3)));
        }
    }
}

void ini_get(
    const mINI::INIMap<std::string>& ini,
    std::string                      key,
    std::string&                     destination
)
{
    if (ini.has(key)) {
        destination = ini.get(key);
    }
}

void ini_get(
    const mINI::INIMap<std::string>& ini,
    std::string                      key,
    bool&                            destination
)
{
    if (ini.has(key)) {
        const std::string value = to_lower(ini.get(key));
        if (value == "1" || value == "yes" || value == "true" || value == "on") {
            destination = true;
        }
        else if (value == "0" || value == "no" || value == "false" || value == "off") {
            destination = false;
        }
    }
}

Ini::~Ini() noexcept = default;

class Ini_impl
    : public Ini
{
public:
    Ini_impl(const char* path, const char* section_name)
    {
        mINI::INIFile file{path};
        if (!file.read(ini)) {
            log_startup->warn("Unable to read ini file '{}'", path);
        }
        section = ini.get(section_name);
    }
    ~Ini_impl() noexcept override = default;
    void get(const char* key, int& destination) const override
    {
        ini_get(section, key, destination);
    }
    void get(const char* key, float& destination) const override
    {
        ini_get(section, key, destination);
    }
    void get(const char* key, glm::vec2& destination) const override
    {
        ini_get(section, key, destination);
    }
    void get(const char* key, glm::vec3& destination) const override
    {
        ini_get(section, key, destination);
    }
    void get(const char* key, glm::vec4& destination) const override
    {
        ini_get(section, key, destination);
    }
    void get(const char* key, std::string& destination) const override
    {
        ini_get(section, key, destination);
    }
    void get(const char* key, bool& destination) const override
    {
        ini_get(section, key, destination);
    }

private:
    mINI::INIStructure ini;
    mINI::INIMap<std::string> section;
};

auto get_ini(const char* path, const char* section) -> std::unique_ptr<Ini>
{
    return std::make_unique<Ini_impl>(path, section);
}

Configuration* g_configuration{nullptr};

Configuration::Configuration()
    : erhe::components::Component{c_type_name}
{
}

Configuration::~Configuration()
{
    ERHE_VERIFY(g_configuration == this);
    g_configuration = nullptr;
}

void Configuration::initialize_component()
{
    ERHE_VERIFY((g_configuration == nullptr) || (g_configuration == this));
    g_configuration = this;
}

void Configuration::parse_args(int argc, char** argv)
{
    mINI::INIFile file{"erhe.ini"};
    mINI::INIStructure ini;
    if (file.read(ini)) {
        if (ini.has("imgui")) {
            const auto& section = ini["imgui"];
            ini_get(section, "window_viewport",  imgui.window_viewport);
            ini_get(section, "primary_font",     imgui.primary_font);
            ini_get(section, "mono_font",        imgui.mono_font);
            ini_get(section, "font_size",        imgui.font_size);
            ini_get(section, "vr_font_size",     imgui.vr_font_size);
            ini_get(section, "padding",          imgui.padding);
            ini_get(section, "rounding",         imgui.rounding);
        }
        if (ini.has("threading")) {
            const auto& section = ini["threading"];
            ini_get(section, "parallel_init", threading.parallel_initialization);
        }
        if (ini.has("graphics")) {
            const auto& section = ini["graphics"];
            ini_get(section, "reverse_depth",     graphics.reverse_depth);
            ini_get(section, "simpler_shaders",   graphics.simpler_shaders);
            ini_get(section, "post_processing",   graphics.post_processing);
            ini_get(section, "use_time_query",    graphics.use_time_query);
            ini_get(section, "force_no_bindless", graphics.force_no_bindless);
            ini_get(section, "msaa_sample_count", graphics.msaa_sample_count);
        }
        if (ini.has("window")) {
            const auto& section = ini["window"];
            ini_get(section, "show",          window.show);
            ini_get(section, "fullscreen",    window.fullscreen);
            ini_get(section, "use_finish",    window.use_finish);
            ini_get(section, "sleep_time",    window.sleep_time);
            ini_get(section, "wait_time",     window.wait_time);
            ini_get(section, "swap_interval", window.swap_interval);
            ini_get(section, "width",         window.width);
            ini_get(section, "height",        window.height);
        }
    }

    cxxopts::Options options("Editor", "Erhe Editor (C) 2023 Timo Suoranta");

    options.add_options()
        ("window-imgui-viewport",      "Enable hosting ImGui windows in window viewport",  cxxopts::value<bool>()->default_value(str( imgui.window_viewport)))
        ("no-window-imgui-viewport",   "Disable hosting ImGui windows in window viewport", cxxopts::value<bool>()->default_value(str(!imgui.window_viewport)))
        //("openxr",                     "Enable OpenXR HMD support",             cxxopts::value<bool>()->default_value(str( headset.openxr)))
        //("no-openxr",                  "Disable OpenXR HMD support",            cxxopts::value<bool>()->default_value(str(!headset.openxr)))
        ("parallel-initialization",    "Use parallel component initialization", cxxopts::value<bool>()->default_value(str( threading.parallel_initialization)))
        ("no-parallel-initialization", "Use serial component initialization",   cxxopts::value<bool>()->default_value(str(!threading.parallel_initialization)))
        ("reverse-depth",              "Enable reverse depth",                  cxxopts::value<bool>()->default_value(str( graphics.reverse_depth)))
        ("no-reverse-depth",           "Disable reverse depth",                 cxxopts::value<bool>()->default_value(str(!graphics.reverse_depth)));

    try {
        auto arguments = options.parse(argc, argv);

        imgui.window_viewport             = arguments["window-imgui-viewport"  ].as<bool>() && !arguments["no-window-imgui-viewport"  ].as<bool>();
        //headset.openxr                    = arguments["openxr"                 ].as<bool>() && !arguments["no-openxr"                 ].as<bool>();
        threading.parallel_initialization = arguments["parallel-initialization"].as<bool>() && !arguments["no-parallel-initialization"].as<bool>();
        graphics.reverse_depth            = arguments["reverse-depth"          ].as<bool>() && !arguments["no-reverse-depth"          ].as<bool>();
    } catch (const std::exception& e) {
        log_startup->error(
            "Error parsing command line argumenst: {}",
            e.what()
        );
    }
}

auto Configuration::depth_clear_value_pointer() const -> const float*
{
    return graphics.reverse_depth ? &float_zero_value : &float_one_value;
}

auto Configuration::depth_function(
    const gl::Depth_function depth_function
) const -> gl::Depth_function
{
    return graphics.reverse_depth
        ? erhe::graphics::reverse(depth_function)
        : depth_function;
}

} // namespace erhe::application

