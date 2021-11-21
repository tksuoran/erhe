#pragma once

#include "erhe/geometry/geometry.hpp"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <string>
#include <vector>

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

    explicit Json_library(const std::filesystem::path& path);

    auto make_geometry(const std::string& key_name) const -> erhe::geometry::Geometry;

    std::vector<std::string> names;       // all meshes
    std::vector<Category>    categories;  // categories

private:
    nlohmann::json m_json;
};
erhe::geometry::Geometry make_json_polyhedron(const std::filesystem::path& path, const std::string& entry);

}
