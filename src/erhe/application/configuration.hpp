#pragma once

#include "erhe/components/components.hpp"
#include "erhe/gl/wrapper_enums.hpp"

#include <glm/glm.hpp>

#include "mini/ini.h"

namespace erhe::application {

auto str     (bool value      ) -> const char*;
auto split   (const std::string& s, char delimeter) -> std::vector<std::string>;
auto trim    (const std::string& str, const std::string& whitespace = " \t") -> std::string;

auto to_lower(std::string data) -> std::string;
void ini_get (const mINI::INIMap<std::string>& ini, std::string key, int& destination);
void ini_get (const mINI::INIMap<std::string>& ini, std::string key, float& destination);
void ini_get (const mINI::INIMap<std::string>& ini, std::string key, glm::vec4& destination);
void ini_get (const mINI::INIMap<std::string>& ini, std::string key, std::string& destination);
void ini_get (const mINI::INIMap<std::string>& ini, std::string key, bool& destination);

class Ini
{
public:
    virtual ~Ini() noexcept;
    virtual void get(const char* key, int&         destination) const = 0;
    virtual void get(const char* key, float&       destination) const = 0;
    virtual void get(const char* key, glm::vec3&   destination) const = 0;
    virtual void get(const char* key, glm::vec2&   destination) const = 0;
    virtual void get(const char* key, glm::vec4&   destination) const = 0;
    virtual void get(const char* key, std::string& destination) const = 0;
    virtual void get(const char* key, bool&        destination) const = 0;
};

auto get_ini(const char* path, const char* section) -> std::unique_ptr<Ini>;

class Configuration
    : public erhe::components::Component
{
public:
    static constexpr std::string_view c_type_name{"Configuration"};
    static constexpr uint32_t c_type_hash{
        compiletime_xxhash::xxh32(
            c_type_name.data(),
            c_type_name.size(),
            {}
        )
    };

    Configuration();
    ~Configuration();

    void parse_args(int argc, char** argv);

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return c_type_hash; }
    void initialize_component() override;

    // Public API
    [[nodiscard]] auto depth_clear_value_pointer() const -> const float *; // reverse_depth ? 0.0f : 1.0f;
    [[nodiscard]] auto depth_function           (const gl::Depth_function depth_function) const -> gl::Depth_function;

    class Imgui
    {
    public:
        bool        window_viewport {true};
        std::string primary_font    {"res/fonts/SourceSansPro-Regular.otf"};
        std::string mono_font       {"res/fonts/SourceCodePro-Semibold.otf"};
        float       font_size       {17.0f};
        float       vr_font_size    {22.0f};
        float       padding         {2.0f};
        float       rounding        {3.0f};
    };
    Imgui imgui;

    class Threading
    {
    public:
        bool parallel_initialization{true};
    };
    Threading threading;

    class Graphics
    {
    public:
        bool reverse_depth              {true};  // TODO move to editor
        bool post_processing            {true};  // TODO move to editor
        bool use_time_query             {true};
        bool force_no_bindless          {false}; // TODO move to erhe::graphics
        bool force_no_persistent_buffers{false}; // TODO move to erhe::graphics
        bool initial_clear              {true};
        int  msaa_sample_count          {4};     // TODO move to editor
    };
    Graphics graphics;

    class Window
    {
    public:
        bool  show            {true};
        bool  fullscreen      {false};
        bool  use_finish      {false};
        bool  use_transparency{false};
        int   gl_major        {4};
        int   gl_minor        {6};
        float sleep_time      {0.004f};
        float wait_time       {0.000f};
        int   swap_interval   {1};
        int   width           {1920};
        int   height          {1080};
    };
    Window window;
};

extern Configuration* g_configuration;

} // namespace erhe::application
