#include "parsers/json_polyhedron.hpp"

#include "erhe_geometry/geometry.hpp"
#include "erhe_file/file.hpp"
#include "erhe_profile/profile.hpp"

#include <geogram/mesh/mesh_repair.h>

namespace editor {

Json_library::Json_library()
{
}

Json_library::Json_library(const std::filesystem::path& path)
{
    const auto opt_text = erhe::file::read("Json_library", path);
    if (!opt_text.has_value()) {
        return;
    }

    const std::string& text = opt_text.value();

    m_json.Parse(text.c_str(), text.length());

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

auto Json_library::make_geometry(erhe::geometry::Geometry& geometry, const std::string& key_name) const -> bool
{
    ERHE_PROFILE_FUNCTION();

    const auto json_mesh = m_json.FindMember(key_name.c_str());
    if (json_mesh == m_json.MemberEnd()) {
        return false;
    }

    GEO::Mesh& mesh = geometry.get_mesh();

    try {
        geometry.set_name((*json_mesh).value["name"].GetString());

        const auto& json_vertices = (*json_mesh).value["vertex"];
        const size_t vertex_count = json_vertices.GetArray().Size();
        mesh.vertices.create_vertices(static_cast<GEO::index_t>(vertex_count));
        {
            GEO::index_t vertex = 0;
            for (auto& json_vertex : json_vertices.GetArray()) {
                assert(json_vertex.IsArray());
                const float x = json_vertex[0].GetFloat();
                const float y = json_vertex[1].GetFloat();
                const float z = json_vertex[2].GetFloat();
                set_pointf(mesh.vertices, vertex++, GEO::vec3f{x, y, z});
            }
        }

        const auto& json_faces = (*json_mesh).value["face"];
        for (auto& json_face : json_faces.GetArray()) {
            assert(json_face.IsArray());
            const size_t corner_count = json_face.GetArray().Size();
            const GEO::index_t mesh_facet = mesh.facets.create_polygon(static_cast<GEO::index_t>(corner_count));
            GEO::index_t local_facet_corner = 0;
            for (auto& corner : json_face.GetArray()) {
                const int vertex = corner.GetInt();
                assert(vertex < (int)vertex_count);
                mesh.facets.set_vertex(mesh_facet, local_facet_corner, vertex);
                ++local_facet_corner;
            }
        }

        GEO::mesh_reorient(mesh);
        return true;
    } catch (...) {
        return false;
    }
}

}
