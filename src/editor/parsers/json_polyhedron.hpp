#pragma once

#include "erhe_geometry/geometry.hpp"

#include "rapidjson/document.h"

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

    Json_library();
    explicit Json_library(const std::filesystem::path& path);

    [[nodiscard]] auto make_geometry(const std::string& key_name) const -> erhe::geometry::Geometry;

    std::vector<std::string> names;       // all meshes
    std::vector<Category>    categories;  // categories

private:
    rapidjson::Document m_json;
};

}
