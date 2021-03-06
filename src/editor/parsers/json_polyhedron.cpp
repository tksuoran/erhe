#include "parsers/json_polyhedron.hpp"
#include "log.hpp"

#include "erhe/toolkit/file.hpp"

#include <algorithm>
#include <cctype>
#include <glm/glm.hpp>
#include <sstream>
#include <string>

namespace editor {

using namespace glm;
using namespace erhe::geometry;

using json = nlohmann::json;

Json_library::Json_library(const std::filesystem::path& path)
{
    auto opt_text = erhe::toolkit::read(path);
    if (!opt_text.has_value())
    {
        return;
    }

    const std::string& text = opt_text.value();

    m_json = json::parse(text);

    // Collect dcategories
    for (auto& entry : m_json.items())
    {
        std::string key_name       = entry.key();
        std::string name           = entry.value()["name"];
        auto        category_names = entry.value()["category"];
        for (auto& category_name_object : category_names)
        {
            std::string category_name = category_name_object.get<std::string>();
            auto i = std::find_if(categories.begin(), categories.end(), [category_name](const Category& category){
                return category.category_name == category_name;
            });
            if (i != categories.end())
            {
                i->key_names.emplace_back(key_name);
            }
            else
            {
                auto& category = categories.emplace_back(std::move(category_name));
                category.key_names.emplace_back(key_name);
            }
        }
        names.emplace_back(std::move(key_name));
    }
}

auto Json_library::make_geometry(const std::string& key_name) const
-> Geometry
{
    Geometry geometry;
    auto& mesh = m_json[key_name];
    geometry.name = mesh["name"];
    auto& points = mesh["vertex"];
    for (auto& point : points)
    {
        float x = point[0].get<float>();
        float y = point[1].get<float>();
        float z = point[2].get<float>();
        geometry.make_point(-x, -y, -z);
    }
    auto& polygons = mesh["face"];
    for (auto& polygon : polygons)
    {
        auto g_polygon = geometry.make_polygon();
        for (auto& corner : polygon)
        {
            int index = corner.get<int>();
            if (index < (int)geometry.point_count())
            {
                geometry.make_polygon_corner(g_polygon, index);
            }
        }
    }
    geometry.make_point_corners();
    geometry.build_edges();
    geometry.generate_polygon_texture_coordinates();
    return geometry;
}

} // namespace editor
