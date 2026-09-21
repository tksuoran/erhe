#pragma once

#include "rapidjson/document.h"

#include <filesystem>
#include <string>
#include <vector>

namespace erhe::geometry { class Geometry; }

namespace editor {

class Json_library
{
public:
    class Category
    {
    public:
        explicit Category(std::string&& category_name)
            : category_name{std::move(category_name)}
        {
        }
        std::string              category_name;
        std::vector<std::string> key_names;
    };

    Json_library();
    explicit Json_library(const std::filesystem::path& path);

    // Display name of a key, read from the JSON "name" member without
    // building a mesh. Returns an empty string when the key is not in the
    // library or carries no "name" member.
    [[nodiscard]] auto get_display_name(const std::string& key_name) const -> std::string;

    auto make_geometry(erhe::geometry::Geometry& geometry, const std::string& key_name) const -> bool;

    std::vector<std::string> names;       // all meshes
    std::vector<Category>    categories;  // categories

private:
    rapidjson::Document m_json;
};

}
