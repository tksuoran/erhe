#include "erhe/configuration/configuration.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>

namespace erhe::configuration {

auto str(const bool value) -> const char*
{
    return value ? "true" : "false";
}

auto to_lower(std::string data) -> std::string
{
    const std::string::size_type space_pos = data.find_first_of(std::string{" \t"});
    if (space_pos != std::string::npos) {
        data.resize(space_pos);
    }
    std::transform(
        data.begin(),
        data.end(),
        data.begin(),
        [](unsigned char c) {
            return std::tolower(c);
        }
    );
    return data;
}

auto split(const std::string& s, const char delimeter) -> std::vector<std::string>
{
    std::vector<std::string> result;
    std::stringstream        ss{s};
    std::string              item;

    while (std::getline(ss, item, delimeter)) {
        result.push_back(item);
    }

    return result;
}

auto trim(const std::string& str, const std::string& whitespace) -> std::string
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
    std::size_t&                     destination
)
{
    if (ini.has(key)) {
        destination = std::stoi(ini.get(key));
    }
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
        if (file.read(ini)) {
            section = ini.get(section_name);
            //const auto current_path = std::filesystem::current_path();
            //log_configuration->warn("Unable to read ini file '{}' in '{}'", path, current_path.string());
        }
    }
    ~Ini_impl() noexcept override = default;
    Ini_impl& operator=(const Ini_impl&) = default;
    void get(const char* key, std::size_t& destination) const override
    {
        ini_get(section, key, destination);
    }
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

} // namespace erhe::configuration

