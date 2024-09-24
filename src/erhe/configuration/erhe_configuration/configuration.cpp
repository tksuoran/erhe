#include "erhe_configuration/configuration.hpp"
#include "erhe_profile/profile.hpp"

#include <etl/vector.h>

#include <algorithm>
#include <cctype>
#include <mutex>
#include <string>
#include <string_view>

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

void ini_get(const mINI::INIMap<std::string>& ini, std::string key, std::size_t& destination)
{
    if (ini.has(key)) {
        destination = std::stoi(ini.get(key));
    }
}

void ini_get(const mINI::INIMap<std::string>& ini, std::string key, int& destination)
{
    if (ini.has(key)) {
        destination = std::stoi(ini.get(key));
    }
}

void ini_get(const mINI::INIMap<std::string>& ini, std::string key, float& destination)
{
    if (ini.has(key)) {
        destination = std::stof(ini.get(key));
    }
}

void ini_get(const mINI::INIMap<std::string>& ini, std::string key, glm::vec2& destination)
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

void ini_get(const mINI::INIMap<std::string>& ini, std::string key, glm::vec3& destination)
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

void ini_get(const mINI::INIMap<std::string>& ini, std::string key, glm::vec4& destination)
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

void ini_get(const mINI::INIMap<std::string>& ini, std::string key, std::string& destination)
{
    if (ini.has(key)) {
        destination = ini.get(key);
    }
}

void ini_get(const mINI::INIMap<std::string>& ini, std::string key, bool& destination)
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

//Ini::~Ini() noexcept = default;

class Ini_section_impl : public Ini_section
{
public:
    Ini_section_impl(const std::string& name, mINI::INIStructure& ini)
        : m_name{name}
        , m_ini_map{ini.get(m_name)}
    {
    }
    Ini_section_impl(const Ini_section_impl&) = delete;
    Ini_section_impl(Ini_section_impl&&) = default;
    Ini_section_impl& operator=(const Ini_section_impl&) = delete;
    Ini_section_impl& operator=(Ini_section_impl&&) = default;
    ~Ini_section_impl() override {}

    auto get_name() const -> const std::string& { return m_name; }

    void get(const std::string& key, std::size_t& destination) const override { ini_get(m_ini_map, key, destination); }
    void get(const std::string& key, bool&        destination) const override { ini_get(m_ini_map, key, destination); }
    void get(const std::string& key, int&         destination) const override { ini_get(m_ini_map, key, destination); }
    void get(const std::string& key, float&       destination) const override { ini_get(m_ini_map, key, destination); }
    void get(const std::string& key, glm::vec2&   destination) const override { ini_get(m_ini_map, key, destination); }
    void get(const std::string& key, glm::vec3&   destination) const override { ini_get(m_ini_map, key, destination); }
    void get(const std::string& key, glm::vec4&   destination) const override { ini_get(m_ini_map, key, destination); }
    void get(const std::string& key, std::string& destination) const override { ini_get(m_ini_map, key, destination); }

private:
    std::string m_name;
    mINI::INIMap<std::string> m_ini_map;
};

class Ini_file_impl : public Ini_file
{
public:
    Ini_file_impl(const std::string& name)
        : m_name{name}
    {
        mINI::INIFile file{name};
        file.read(m_ini);
    }
    Ini_file_impl(const Ini_file_impl&) = delete;
    Ini_file_impl& operator=(const Ini_file_impl&) = delete;
    Ini_file_impl(Ini_file_impl&&) = default;
    Ini_file_impl& operator=(Ini_file_impl&&) = default;
    ~Ini_file_impl() override {}

    auto get_name() const -> const std::string& { return m_name; }

    auto get_section(const std::string& name) -> const Ini_section& override
    {
        std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_mutex};
        for (const auto& section : m_sections) {
            if (section.get_name() == name) {
                return section;
            }
        }
        Ini_section_impl& section = m_sections.emplace_back(name, m_ini);
        return section;
    }

private:
    std::string                    m_name;
    ERHE_PROFILE_MUTEX(std::mutex, m_mutex);
    mINI::INIStructure             m_ini;
    std::vector<Ini_section_impl>  m_sections;
};

class Ini_cache_impl : public Ini_cache
{
public:
    ~Ini_cache_impl() override {}

    auto get_ini_file(const std::string& name) -> Ini_file& override
    {
        std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_mutex};
        for (auto& file : m_files) {
            if (file.get_name() == name) {
                return file;
            }
        }
        Ini_file_impl& file = m_files.emplace_back(name);
        return file;
    }

private:
    ERHE_PROFILE_MUTEX(std::mutex, m_mutex);
    etl::vector<Ini_file_impl, 40> m_files;
};

auto Ini_cache::get_instance() -> Ini_cache&
{
    static Ini_cache_impl static_instance;
    return static_instance;
}

auto get_ini_file(std::string_view file_name) -> Ini_file&
{
    Ini_cache& cache = Ini_cache::get_instance();
    Ini_file& file = cache.get_ini_file(std::string{file_name});
    return file;
}

auto get_ini_file_section(std::string_view file_name, std::string_view section_name) -> const Ini_section&
{
    Ini_file& file = get_ini_file(file_name);
    const Ini_section& section = file.get_section(std::string{section_name});
    return section;
}

} // namespace erhe::configuration


