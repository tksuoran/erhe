#include "parsers/json_polyhedron.hpp"
#include "log.hpp"

#include "erhe/toolkit/file.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <glm/glm.hpp>
#include <sstream>
#include <string>

namespace editor {

using namespace glm;
using namespace erhe::geometry;

using json = nlohmann::json;

Geometry make_json_polyhedron(const std::filesystem::path& path)
{
    log_parsers.trace("path = {}\n", path.generic_string());

    auto opt_text = erhe::toolkit::read(path);
    if (!opt_text.has_value())
    {
        return {};
    }

    const std::string& text = opt_text.value();
    log_parsers.trace("text = {}\n", text.c_str());

    Geometry geometry;

    auto json = json::parse(text);
    auto& mesh = json["mesh"];
    auto& points = mesh["vertices"];
    for (auto& point : points)
    {
        auto xo = point["x"];
        auto yo = point["y"];
        auto zo = point["z"];
        std::string xs = xo.get<std::string>();
        std::string ys = yo.get<std::string>();
        std::string zs = zo.get<std::string>();
        log_parsers.trace("{}, {}, {}\n", xs, ys, zs);
        float x = std::stof(xs);
        float y = std::stof(ys);
        float z = std::stof(zs);
        geometry.make_point(-x, -y, -z);
    }
    auto& polygons = mesh["polygons"];
    for (auto& polygon : polygons)
    {
        auto &corners = polygon["corners"];
        auto g_polygon = geometry.make_polygon();
        for (auto& corner : corners)
        {
            std::string s_value = corner.get<std::string>();
            int index = std::stoi(s_value);
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
