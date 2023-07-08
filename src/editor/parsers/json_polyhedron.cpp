#include "parsers/json_polyhedron.hpp"
#include "editor_log.hpp"

#include "erhe/geometry/geometry.hpp"
#include "erhe/toolkit/file.hpp"
#include "erhe/toolkit/profile.hpp"

#include <algorithm>
#include <cctype>
#include <glm/glm.hpp>
#include <sstream>
#include <string>

namespace editor {

Json_library::Json_library()
{
}

Json_library::Json_library(const std::filesystem::path& path)
{
    ERHE_PROFILE_FUNCTION();

    const auto opt_text = erhe::toolkit::read("Json_library", path);
    if (!opt_text.has_value()) {
        return;
    }

    const std::string& text = opt_text.value();

    {
        ERHE_PROFILE_SCOPE("parse");
        m_json.Parse(text.c_str(), text.length());
    }

    {
        ERHE_PROFILE_SCOPE("collect categories");
        for (auto i = m_json.MemberBegin(), end = m_json.MemberEnd(); i != end; ++i) {
            const std::string key_name       = (*i).name.GetString();
            const auto&       category_names = (*i).value["category"].GetArray();

            for (auto& category_name_object : category_names) {
                std::string category_name = category_name_object.GetString();
                const auto j = std::find_if(
                    categories.begin(),
                    categories.end(),
                    [category_name](const Category& category) {
                        return category.category_name == category_name;
                    }
                );
                if (j != categories.end()) {
                    j->key_names.emplace_back(key_name);
                } else {
                    auto& category = categories.emplace_back(std::move(category_name));
                    category.key_names.emplace_back(key_name);
                }
            }
            names.emplace_back(key_name);
        }
    }
}

auto Json_library::make_geometry(
    const std::string& key_name
) const -> erhe::geometry::Geometry
{
    ERHE_PROFILE_FUNCTION();

    const auto mesh = m_json.FindMember(key_name.c_str());
    if (mesh == m_json.MemberEnd()) {
        return {};
    }

    erhe::geometry::Geometry geometry;
    geometry.name = (*mesh).value["name"].GetString();

    const auto& points = (*mesh).value["vertex"];
    {
        ERHE_PROFILE_SCOPE("points");
        for (auto& i : points.GetArray()) {
            assert(i.IsArray());
            const float x = i[0].GetFloat();
            const float y = i[1].GetFloat();
            const float z = i[2].GetFloat();
            geometry.make_point(-x, -y, -z);
        }
    }

    const auto& polygons = (*mesh).value["face"];
    {
        ERHE_PROFILE_SCOPE("faces");
        for (auto& polygon : polygons.GetArray()) {
            assert(polygon.IsArray());
            auto g_polygon = geometry.make_polygon();
            for (auto& corner : polygon.GetArray()) {
                const int index = corner.GetInt();
                if (index < (int)geometry.get_point_count()) {
                    geometry.make_polygon_corner(g_polygon, index);
                }
            }
        }
    }

    geometry.flip_reversed_polygons(); // This only works for 'round' shapes
    geometry.make_point_corners();
    geometry.build_edges();
    geometry.generate_polygon_texture_coordinates();
    return geometry;
}

} // namespace editor
