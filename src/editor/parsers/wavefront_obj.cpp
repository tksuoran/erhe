#include "parsers/wavefront_obj.hpp"
#include "editor_log.hpp"

#include "erhe_geometry/geometry.hpp"
#include "erhe_file/file.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_verify/verify.hpp"

#include <limits>
#include <string>

namespace editor {

// http://paulbourke.net/dataformats/obj/
// http://www.martinreddy.net/gfx/3d/OBJ.spec
// https://www.marxentlabs.com/obj-files/

enum class Command : unsigned int {
    Unknown = 0,
    //Basis_matrix,
    //Bevel_interpolation,
    //Color_interpolation,
    //Connectivity,
    //Curve,
    //Curve_2D,
    //Curve_approximation_technique,
    //Curve_type,
    //Degree,
    //Dissolve_interpolation,
    //End,
    Face,
    Group_name,
    //Inner_trimming_loop,
    //Level_of_detail,
    //Line,
    Material_library,
    //Merging_group,
    Object_name,
    //Outer_trimming,
    //Parameter_values,
    //Point,
    //Ray_tracing,
    //Shadow_casting,
    //Smoothing_group,
    //Special_curve,
    //Special_point,
    //Step_size,
    //Surface,
    //Surface_approximation_technique,
    Use_material,
    Vertex_parameter_space,
    Vertex_position,
    Vertex_texture_coordinate,
    Vertex_normal,
};

Command tokenize(const std::string& text)
{
    // Vertex data
    if (text == "v")          return Command::Vertex_position;
    if (text == "vt")         return Command::Vertex_texture_coordinate;
    if (text == "vn")         return Command::Vertex_normal;
    if (text == "vp")         return Command::Vertex_parameter_space;

    // Element data
    if (text == "f")          return Command::Face;
    if (text == "fo")         return Command::Face; // Face outline
    //if (text == "l")          return Command::Line;
    //if (text == "p")          return Command::Point;
    //if (text == "curv")       return Command::Curve;
    //if (text == "curv2D")     return Command::Curve_2D;
    //if (text == "s")          return Command::Surface;

    // Surface data
    //if (text == "deg")        return Command::Degree;
    //if (text == "bmat")       return Command::Basis_matrix;
    //if (text == "step")       return Command::Step_size;
    //if (text == "cstype")     return Command::Curve_type;
    //if (text == "con")        return Command::Connectivity;

    //if (text == "parm")       return Command::Parameter_values;
    //if (text == "trim")       return Command::Outer_trimming;
    //if (text == "hole")       return Command::Inner_trimming_loop;
    //if (text == "scrv")       return Command::Special_curve;
    //if (text == "sp")         return Command::Special_point;
    //if (text == "end")        return Command::End;

    // Grouping data
    if (text == "g")          return Command::Group_name;
    //if (text == "s")          return Command::Smoothing_group;
    //if (text == "mg")         return Command::Merging_group;
    if (text == "o")          return Command::Object_name;

    // Display and rendering data
    //if (text == "bevel")      return Command::Bevel_interpolation;
    //if (text == "c_interp")   return Command::Color_interpolation;
    //if (text == "d_interp")   return Command::Dissolve_interpolation;
    //if (text == "lod")        return Command::Level_of_detail;
    if (text == "usemtl")     return Command::Use_material;
    if (text == "mtllib")     return Command::Material_library;
    //if (text == "shadow_obj") return Command::Shadow_casting;
    //if (text == "trace_obj")  return Command::Ray_tracing;
    //if (text == "ctech")      return Command::Curve_approximation_technique;
    //if (text == "stech")      return Command::Surface_approximation_technique;

    return Command::Unknown;
}

// v 0 2.43544 -1.38593
// vt -0.108459 1.75572
// vn -1.64188e-16 -0.284002 0.958824
// f 1/1/1 2/2/2 3/3/3 4/4/4

auto parse_obj_geometry(const std::filesystem::path& path) -> std::vector<std::shared_ptr<erhe::geometry::Geometry>>
{
    ERHE_PROFILE_FUNCTION();

    log_parsers->trace("path = {}", path.generic_string());

    const auto opt_text = erhe::file::read("parse_obj_geometry", path);

    std::vector<std::shared_ptr<erhe::geometry::Geometry>> result;

    std::shared_ptr<erhe::geometry::Geometry> geometry{};
    GEO::Mesh* geo_mesh{nullptr};
    std::unique_ptr<Mesh_attributes> attributes{};

    // I dislike this big scope, I'd prefer just to
    // return {} but unfortunately having more than
    // one return kills named return value optimization.
    if (opt_text.has_value()) {
        const std::string& text = opt_text.value();

        const std::string delimiters  = " \t\v";
        const std::string end_of_line = "\n";
        const std::string slash       = "/\r";
        const std::string comment     = "#";

        std::string::size_type line_last_pos = text.find_first_not_of(end_of_line, 0);
        std::string::size_type line_pos      = text.find_first_of(end_of_line, line_last_pos);
        std::vector<GEO::vec3>  positions;
        std::vector<GEO::vec4f> colors;
        std::vector<GEO::vec3f> normals;
        std::vector<GEO::vec2f> texcoords;

        // Mapping from OBJ point id to geogram vertex
        std::vector<GEO::index_t> obj_point_to_mesh_vertex;

        bool has_vertex_colors = false;

        while (
            (line_pos != std::string::npos) ||
            (line_last_pos != std::string::npos)
        ) {
            auto line = text.substr(line_last_pos, line_pos - line_last_pos);
            line.erase(
                std::remove(line.begin(), line.end(), '\r'),
                line.end()
            );

            // Drop comments
            const auto coment_pos = line.find_first_of(comment);
            if (coment_pos != std::string::npos) {
                line.erase(line.begin() + coment_pos, line.end());
            }

            //log_parsers->trace("line: {}", line);
            if (line.length() == 0) {
                line_last_pos = text.find_first_not_of(end_of_line, line_pos);
                line_pos      = text.find_first_of(end_of_line, line_last_pos);
                continue;
            }

            //const erhe::log::Indenter scope_indent;

            // process line
            std::string::size_type token_last_pos = line.find_first_not_of(delimiters, 0);
            std::string::size_type token_pos      = line.find_first_of    (delimiters, token_last_pos);
            if (token_last_pos == std::string::npos || token_pos == std::string::npos) {
                line_last_pos = text.find_first_not_of(end_of_line, line_pos);
                line_pos      = text.find_first_of(end_of_line, line_last_pos);
                continue;
            }

            const auto command_text = line.substr(token_last_pos, token_pos - token_last_pos);
            const auto command      = tokenize(command_text);

            //log_parsers->trace("command: {}", command_text);
            std::vector<float> float_args;
            std::vector<int>   int_args;
            std::vector<int>   face_vertex_position_indices;
            std::vector<int>   face_vertex_texcoord_indices;
            std::vector<int>   face_vertex_normal_indices;
            while ((token_pos != std::string::npos) || (token_last_pos != std::string::npos)) {
                switch (command) {
                    //using enum Command;
                    case Command::Object_name: {
                        // TODO Choose Geometry splitting based on o / g / s / mg
                        //      Currently fixed to use g
                        token_last_pos = line.find_first_not_of(end_of_line, token_pos);
                        token_pos      = line.find_first_of    (end_of_line, token_last_pos);
                        if (token_last_pos != std::string::npos || token_pos != std::string::npos) {
                            const auto arg_text = line.substr(token_last_pos, token_pos - token_last_pos);
                            geometry = std::make_shared<erhe::geometry::Geometry>(arg_text);
                            geo_mesh = &geometry->get_mesh();
                            attributes = std::make_unique<Mesh_attributes>(*geo_mesh);
                            result.push_back(geometry);
                            obj_point_to_mesh_vertex.clear();
                            log_parsers->trace("arg: {}", arg_text);
                        }
                        break;
                    }

                    case Command::Group_name: {
                        token_last_pos = line.find_first_not_of(delimiters, token_pos);
                        token_pos      = line.find_first_of(end_of_line, token_last_pos);
                        if (token_last_pos != std::string::npos || token_pos != std::string::npos) {
                            const auto arg_text = line.substr(token_last_pos, token_pos - token_last_pos);
                            geometry = std::make_shared<erhe::geometry::Geometry>(arg_text);
                            geo_mesh = &geometry->get_mesh();
                            attributes = std::make_unique<Mesh_attributes>(*geo_mesh);
                            result.push_back(geometry);
                            obj_point_to_mesh_vertex.clear();
                            //log_parsers->trace("arg: {}", arg_text);
                        }
                        //token_pos = text.find_first_of(end_of_line, token_last_pos);
                        break;
                    }

                    case Command::Use_material:
                    case Command::Unknown:
                    case Command::Material_library: {
                        // consume rest of the line for now
                        token_last_pos = line.find_first_not_of(end_of_line, token_pos);
                        token_pos      = line.find_first_of    (end_of_line, token_last_pos);
                        if (token_last_pos != std::string::npos || token_pos != std::string::npos) {
                            const auto arg_text = line.substr(token_last_pos, token_pos - token_last_pos);
                            //log_parsers->trace("arg: {}", arg_text);
                        }
                        //token_last_pos = line.find_first_not_of(end_of_line, token_pos);
                        //token_pos = text.find_first_of(end_of_line, token_last_pos);
                        break;
                    }

                    case Command::Vertex_position:
                    // Three required variables: x, y, and z
                    // One optional variable: w
                    // Some applications support colors; if they are available, add RBG values after the variables.
                    // The default is 1.

                    case Command::Vertex_normal:
                    // If a UV (vt) or vertex normal (vn) are defined for one vertex in a shape, they must be defined for all.
                    // Three required variables: x, y, and z

                    case Command::Vertex_texture_coordinate:
                    // If a UV (vt) or vertex normal (vn) are defined for one vertex in a shape, they must be defined for all.
                    // One required variable: u
                    // Two optional variables: v and w
                    // The default is 0.

                    case Command::Vertex_parameter_space:
                    // Use u for curve points
                    // Use u and v for surface points and non-rational trimming curve control points
                    // Use u, v, and w for rational trimming curve control points

                    {
                        token_last_pos = line.find_first_not_of(delimiters, token_pos);
                        token_pos      = line.find_first_of    (delimiters, token_last_pos);
                        if (token_last_pos != std::string::npos || token_pos != std::string::npos) {
                            const auto  arg_text = line.substr(token_last_pos, token_pos - token_last_pos);
                            const float value    = std::stof(arg_text);
                            //log_parsers->trace("arg: {}", arg_text);
                            float_args.push_back(value);
                        }
                        //token_last_pos = line.find_first_not_of(delimiters, token_pos);
                        break;
                    }

                    case Command::Face: {
                        token_last_pos = line.find_first_not_of(delimiters, token_pos);
                        token_pos      = line.find_first_of    (delimiters, token_last_pos);
                        if (token_last_pos != std::string::npos || token_pos != std::string::npos) {
                            const auto arg_text = line.substr(token_last_pos, token_pos - token_last_pos);
                            //log_parsers->trace("arg: {}", arg_text);
                            std::string::size_type subtoken_last_pos = arg_text.find_first_not_of(slash, 0);
                            std::string::size_type subtoken_pos      = arg_text.find_first_of(slash, subtoken_last_pos);
                            int subtoken_slot = 0;
                            while ((subtoken_pos != std::string::npos) || (subtoken_last_pos != std::string::npos)) {
                                const auto subtoken_text = arg_text.substr(subtoken_last_pos, subtoken_pos - subtoken_last_pos);
                                //log_parsers->trace("subtoken: {}", subtoken_text);
                                if (arg_text.length() > 0) {
                                    const int value = std::stoi(subtoken_text);
                                    switch (subtoken_slot) {
                                        case 0: face_vertex_position_indices.push_back(value); break;
                                        case 1: face_vertex_texcoord_indices.push_back(value); break;
                                        case 2: face_vertex_normal_indices  .push_back(value); break;
                                        default: {
                                            //ERHE_FATAL("bad subtoken slot for wavefront obj parser face command");
                                            break;
                                        }
                                    }
                                }
                                subtoken_last_pos = arg_text.find_first_not_of(slash, subtoken_pos);
                                subtoken_pos      = arg_text.find_first_of    (slash, subtoken_last_pos);
                                subtoken_slot++;
                            }
                        }
                        //token_last_pos = line.find_first_not_of(delimiters, token_pos);
                        break;
                    }
                }
            }

            switch (command) {
                //using enum Command;
                case Command::Group_name:
                case Command::Use_material:
                case Command::Unknown:
                case Command::Material_library:
                default:
                    break;
                case Command::Vertex_position: {
                    //ZoneScopedN("position");
                    if (float_args.size() >= 3) {
                        ERHE_VERIFY(geo_mesh != nullptr);
                        if (float_args.size() >= 6) {
                            while (colors.size() < positions.size()) {
                                colors.emplace_back(1.0f, 1.0f, 1.0f, 1.0f);
                            }
                            colors.emplace_back(float_args[3], float_args[4], float_args[5], 1.0f);
                            has_vertex_colors = true;
                            //point_colors->put(point_id, glm::vec3{float_args[3], float_args[4], float_args[5]});
                        }

                        positions.emplace_back(float_args[0], float_args[1], float_args[2]);
                    }
                    //else
                    //{
                    //    ERHE_FATAL("unsupported vertex dimension");
                    //}
                    break;
                }

                case Command::Vertex_normal: {
                    //ZoneScopedN("normal");
                    if (float_args.size() == 3) {
                        normals.emplace_back(float_args[0], float_args[1], float_args[2]);
                    }
                    //else
                    //{
                    //    ERHE_FATAL("unsupported normal dimension");
                    //}
                    break;
                }
                case Command::Vertex_texture_coordinate: {
                    //ZoneScopedN("texcoord");
                    // TODO support 1 / 3
                    if (float_args.size() == 2) {
                        texcoords.emplace_back(float_args[0], float_args[1]);
                    }
                    //else
                    //{
                    //    ERHE_FATAL("unsupported texcoord dimension");
                    //}
                    break;
                }
                case Command::Face: {
                    //ZoneScopedN("face");
                    ERHE_VERIFY(geo_mesh != nullptr);

                    const int          corner_count = static_cast<int>(face_vertex_position_indices.size());
                    const GEO::index_t mesh_facet   = geo_mesh->facets.create_polygon(corner_count);
                    for (int local_facet_corner = 0; local_facet_corner < corner_count; ++local_facet_corner) {
                        const int obj_vertex_index = face_vertex_position_indices[local_facet_corner];
                        const int position_index =
                            (obj_vertex_index > 0)
                                ? obj_vertex_index - 1
                                : obj_vertex_index + static_cast<int>(positions.size());

                        // Vertex indices in OBJ file are global.
                        // Each erhe::geometry Geometry has it's own namespace for Point_id.
                        // This maps OBJ vertex indices to geometry Point_id.
                        while (static_cast<int>(obj_point_to_mesh_vertex.size()) <= position_index) {
                            obj_point_to_mesh_vertex.push_back(GEO::NO_INDEX);
                        }
                        if (obj_point_to_mesh_vertex[position_index] == GEO::NO_INDEX) {
                            obj_point_to_mesh_vertex[position_index] = geo_mesh->vertices.create_vertices(1);
                        }

                        const GEO::index_t mesh_vertex = obj_point_to_mesh_vertex[position_index];
                        const GEO::index_t mesh_corner = geo_mesh->facets.corner(mesh_facet, local_facet_corner);
                        geo_mesh->facets.set_vertex(mesh_facet, local_facet_corner, mesh_vertex);
                        ERHE_VERIFY(position_index >= 0);
                        ERHE_VERIFY(position_index < static_cast<int>(positions.size()));

                        geo_mesh->vertices.point(mesh_vertex) = positions[position_index];

                        if (has_vertex_colors) {
                            attributes->vertex_color_0.set(mesh_vertex, colors[position_index]);
                        }

                        if (local_facet_corner < static_cast<int>(face_vertex_texcoord_indices.size())) {
                            const int obj_texcoord_index = face_vertex_texcoord_indices[local_facet_corner];
                            const int texcoord_index =
                                (obj_texcoord_index > 0)
                                    ? obj_texcoord_index - 1
                                    : obj_texcoord_index + static_cast<int>(texcoords.size());
                            ERHE_VERIFY(texcoord_index < static_cast<int>(texcoords.size()));
                            attributes->corner_texcoord_0.set(mesh_corner, texcoords[texcoord_index]);
                        }

                        if (local_facet_corner < static_cast<int>(face_vertex_normal_indices.size())) {
                            const int obj_normal_index = face_vertex_normal_indices[local_facet_corner];
                            const int normal_index =
                                (obj_normal_index > 0)
                                    ? obj_normal_index - 1
                                    : obj_normal_index + static_cast<int>(normals.size());
                            ERHE_VERIFY(normal_index < static_cast<int>(normals.size()));
                            attributes->corner_normal.set(mesh_corner, normals[normal_index]);
                        }
                    }
                }
            }

            line_last_pos = text.find_first_not_of(end_of_line, line_pos);
            line_pos = text.find_first_of(end_of_line, line_last_pos);
        }
    }

    return result;
}

} // namespace editor
