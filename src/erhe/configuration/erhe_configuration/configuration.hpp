#pragma once

#include "erhe_profile/profile.hpp"

#include <glm/glm.hpp>

#include "mini/ini.h"

namespace erhe::configuration {

auto str     (bool value      ) -> const char*;
auto split   (const std::string& s, char delimeter) -> std::vector<std::string>;
auto trim    (const std::string& str, const std::string& whitespace = " \t") -> std::string;

auto to_lower(std::string data) -> std::string;
void ini_get (const mINI::INIMap<std::string>& ini, std::string key, std::size_t& destination);
void ini_get (const mINI::INIMap<std::string>& ini, std::string key, int& destination);
void ini_get (const mINI::INIMap<std::string>& ini, std::string key, float& destination);
void ini_get (const mINI::INIMap<std::string>& ini, std::string key, glm::vec4& destination);
void ini_get (const mINI::INIMap<std::string>& ini, std::string key, std::string& destination);
void ini_get (const mINI::INIMap<std::string>& ini, std::string key, bool& destination);

class Ini_section
{
public:
    virtual ~Ini_section(){}

    virtual void get(const std::string& key, std::size_t& destination) const = 0;
    virtual void get(const std::string& key, bool&        destination) const = 0;
    virtual void get(const std::string& key, int&         destination) const = 0;
    virtual void get(const std::string& key, float&       destination) const = 0;
    virtual void get(const std::string& key, glm::vec2&   destination) const = 0;
    virtual void get(const std::string& key, glm::vec3&   destination) const = 0;
    virtual void get(const std::string& key, glm::vec4&   destination) const = 0;
    virtual void get(const std::string& key, std::string& destination) const = 0;
};

class Ini_file
{
public:
    virtual ~Ini_file(){}

    virtual auto get_section(const std::string& name) -> const Ini_section& = 0;
};

class Ini_cache
{
public:
    virtual ~Ini_cache() {}

    static auto get_instance() -> Ini_cache&;

    virtual auto get_ini_file(const std::string& name) -> Ini_file& = 0;
};

auto get_ini_file(std::string_view file_name) -> Ini_file&;
auto get_ini_file_section(std::string_view file_name, std::string_view section_name) -> const Ini_section&;

//auto get_ini(const char* path) -> std::unique_ptr<Ini>;

} // namespace erhe::configuration
