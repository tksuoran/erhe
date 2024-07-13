#pragma once

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

class Ini
{
public:
    virtual ~Ini() noexcept;
    virtual void get(const char* key, std::size_t& destination) const = 0;
    virtual void get(const char* key, int&         destination) const = 0;
    virtual void get(const char* key, float&       destination) const = 0;
    virtual void get(const char* key, glm::vec3&   destination) const = 0;
    virtual void get(const char* key, glm::vec2&   destination) const = 0;
    virtual void get(const char* key, glm::vec4&   destination) const = 0;
    virtual void get(const char* key, std::string& destination) const = 0;
    virtual void get(const char* key, bool&        destination) const = 0;
};

auto get_ini(const char* path, const char* section) -> std::unique_ptr<Ini>;

} // namespace erhe::configuration
