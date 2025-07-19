#include "erhe_configuration/configuration.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_file/file.hpp"

#include <etl/vector.h>
#include <toml++/toml.hpp>

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

class Ini_section_impl : public Ini_section
{
public:
    Ini_section_impl(const std::string& name, toml::table* table)
        : m_name{name}
        , m_table{table}
    {
    }
    Ini_section_impl(const Ini_section_impl&) = delete;
    Ini_section_impl(Ini_section_impl&&) = default;
    Ini_section_impl& operator=(const Ini_section_impl&) = delete;
    Ini_section_impl& operator=(Ini_section_impl&&) = default;
    ~Ini_section_impl() override {}

    auto get_name() const -> const std::string& { return m_name; }

    void get(const std::string& key, std::size_t& destination) const override 
    {
        if (m_table == nullptr) {
            return;
        }
        if (const toml::impl::wrap_node<int64_t>* const ptr = m_table->get_as<int64_t>(key)) {
            destination = static_cast<std::size_t>(ptr->get());
        }
    }
    void get(const std::string& key, bool& destination) const override
    {
        if (m_table == nullptr) {
            return;
        }
        if (const toml::impl::wrap_node<bool>* const ptr = m_table->get_as<bool>(key)) {
            destination = ptr->get();
        }
    }
    void get(const std::string& key, int& destination) const override
    {
        if (m_table == nullptr) {
            return;
        }
        if (const toml::impl::wrap_node<int64_t>* const ptr = m_table->get_as<int64_t>(key)) {
            destination = static_cast<int>(ptr->get());
        }
    }
    void get(const std::string& key, float& destination) const override
    {
        if (m_table == nullptr) {
            return;
        }
        if (const toml::impl::wrap_node<double>* const d_ptr = m_table->get_as<double>(key)) {
            destination = static_cast<float>(d_ptr->get());
        } else if (const toml::impl::wrap_node<int64_t>* const i_ptr = m_table->get_as<int64_t>(key)) {
            destination = static_cast<float>(i_ptr->get());
        }
    }
    void get(const std::string& key, glm::ivec2& destination) const override
    {
        if (m_table == nullptr) {
            return;
        }
        toml::node* node = m_table->get(key);
        if (node == nullptr) {
            return;
        }
        if (toml::array* array = node->as_array()) {
            toml::impl::wrap_node<int64_t>* x_node = array->get_as<int64_t>(0);
            toml::impl::wrap_node<int64_t>* y_node = array->get_as<int64_t>(1);
            if (x_node != nullptr) { destination.x = static_cast<int>(x_node->get()); }
            if (y_node != nullptr) { destination.y = static_cast<int>(y_node->get()); }
        } else if (toml::table* table = node->as_table()) {
            toml::impl::wrap_node<int64_t>* x_node = table->get_as<int64_t>("x");
            toml::impl::wrap_node<int64_t>* y_node = table->get_as<int64_t>("y");
            destination.x = (x_node != nullptr) ? static_cast<int>(x_node->get()) : 0;
            destination.y = (y_node != nullptr) ? static_cast<int>(x_node->get()) : 0;
        }
    }
    void get(const std::string& key, glm::vec2& destination) const override
    {
        if (m_table == nullptr) {
            return;
        }
        toml::node* node = m_table->get(key);
        if (toml::array* array = node->as_array()) {
            toml::impl::wrap_node<double>* x_node = array->get_as<double>(0);
            toml::impl::wrap_node<double>* y_node = array->get_as<double>(1);
            if (x_node != nullptr) { destination.x = static_cast<float>(x_node->get()); }
            if (y_node != nullptr) { destination.y = static_cast<float>(y_node->get()); }
        } else if (toml::table* table = node->as_table()) {
            toml::impl::wrap_node<double>* x_node = table->get_as<double>("x");
            toml::impl::wrap_node<double>* y_node = table->get_as<double>("y");
            if (x_node != nullptr) { destination.x = static_cast<float>(x_node->get()); }
            if (y_node != nullptr) { destination.y = static_cast<float>(y_node->get()); }
        }
    }
    void get(const std::string& key, glm::vec3& destination) const override
    {
        if (m_table == nullptr) {
            return;
        }
        toml::node* node = m_table->get(key);
        if (toml::array* array = node->as_array()) {
            toml::impl::wrap_node<double>* x_node = array->get_as<double>(0);
            toml::impl::wrap_node<double>* y_node = array->get_as<double>(1);
            toml::impl::wrap_node<double>* z_node = array->get_as<double>(2);
            if (x_node != nullptr) { destination.x = static_cast<float>(x_node->get()); }
            if (y_node != nullptr) { destination.y = static_cast<float>(y_node->get()); }
            if (z_node != nullptr) { destination.z = static_cast<float>(z_node->get()); }
        } else if (toml::table* table = node->as_table()) {
            toml::impl::wrap_node<double>* x_node = table->get_as<double>("x");
            toml::impl::wrap_node<double>* y_node = table->get_as<double>("y");
            toml::impl::wrap_node<double>* z_node = table->get_as<double>("z");
            destination.x = (x_node != nullptr) ? static_cast<float>(x_node->get()) : 0.0f;
            destination.y = (y_node != nullptr) ? static_cast<float>(x_node->get()) : 0.0f;
            destination.z = (z_node != nullptr) ? static_cast<float>(z_node->get()) : 0.0f;
        }
    }
    void get(const std::string& key, glm::vec4& destination) const override
    {
        if (m_table == nullptr) {
            return;
        }
        toml::node* node = m_table->get(key);
        if (toml::array* array = node->as_array()) {
            toml::impl::wrap_node<double>* x_node = array->get_as<double>(0);
            toml::impl::wrap_node<double>* y_node = array->get_as<double>(1);
            toml::impl::wrap_node<double>* z_node = array->get_as<double>(2);
            toml::impl::wrap_node<double>* w_node = array->get_as<double>(3);
            if (x_node != nullptr) { destination.x = static_cast<float>(x_node->get()); }
            if (y_node != nullptr) { destination.y = static_cast<float>(y_node->get()); }
            if (z_node != nullptr) { destination.z = static_cast<float>(z_node->get()); }
            if (w_node != nullptr) { destination.w = static_cast<float>(w_node->get()); }
        } else if (toml::table* table = node->as_table()) {
            toml::impl::wrap_node<double>* x_node = table->get_as<double>("x");
            toml::impl::wrap_node<double>* y_node = table->get_as<double>("y");
            toml::impl::wrap_node<double>* z_node = table->get_as<double>("z");
            toml::impl::wrap_node<double>* w_node = table->get_as<double>("w");
            destination.x = (x_node != nullptr) ? static_cast<float>(x_node->get()) : 0.0f;
            destination.y = (y_node != nullptr) ? static_cast<float>(x_node->get()) : 0.0f;
            destination.z = (z_node != nullptr) ? static_cast<float>(z_node->get()) : 0.0f;
            destination.w = (w_node != nullptr) ? static_cast<float>(w_node->get()) : 0.0f;
        }
    }
    void get(const std::string& key, std::string& destination) const override
    {
        if (m_table == nullptr) {
            return;
        }
        if (const toml::impl::wrap_node<std::string>* const ptr = m_table->get_as<std::string>(key)) {
            destination = ptr->get();
        }
    }

  private:
    std::string  m_name{};
    toml::table* m_table{nullptr};
};

class Ini_file_impl : public Ini_file
{
public:
    Ini_file_impl(const std::string& name)
        : m_name{name}
    {
        parse_toml(m_root_table, name);
        m_root_table.for_each(
            [this](auto& key, toml::table& table)
            {
                m_sections.emplace_back(std::string{key}, &table);
            }
        );
    }
    Ini_file_impl(const Ini_file_impl&) = delete;
    Ini_file_impl& operator=(const Ini_file_impl&) = delete;
    Ini_file_impl(Ini_file_impl&&) = delete;
    Ini_file_impl& operator=(Ini_file_impl&&) = delete;
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
        Ini_section_impl& section = m_sections.emplace_back(name, nullptr);
        return section;
    }

private:
    std::string                    m_name;
    ERHE_PROFILE_MUTEX(std::mutex, m_mutex);
    toml::table                    m_root_table;
    std::vector<Ini_section_impl>  m_sections;
};

class Ini_cache_impl : public Ini_cache
{
public:
    ~Ini_cache_impl() override {}

    void flush() override
    {
        std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_mutex};
        m_files.clear();
    }

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

auto parse_toml(toml::table& table, std::string_view file_name) -> bool
{
    try {
        std::optional<std::string> contents_opt = erhe::file::read("parse_toml()", file_name);
        if (!contents_opt.has_value()) {
            return false;
        }
        table = toml::parse(contents_opt.value(), file_name);
        return true;
    } catch (toml::parse_error e) {
        char const* what        = e.what();
        char const* description = e.description().data();
        printf("what = %s\n", what);
        printf("description = %s\n", description);
        printf("file = %s\n", e.source().path ? e.source().path->c_str() : "");
        printf("line = %u\n", e.source().begin.line);
        printf("column = %u\n", e.source().begin.column);
    } catch (std::runtime_error& e) {
        char const* what = e.what();
        printf("what = %s\n", what);
    } catch (std::exception e) {
        char const* what = e.what();
        printf("what = %s\n", what);
    } catch (...) {
        printf("?!\n");
    }
    return false;
}

auto write_toml(toml::table& table, std::string_view file_name) -> bool
{
    std::ostringstream ss;
    ss << table;
    return erhe::file::write_file(file_name, ss.str());
}

} // namespace erhe::configuration


