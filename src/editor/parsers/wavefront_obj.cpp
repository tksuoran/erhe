#include "parsers/wavefront_obj.hpp"
#include "log.hpp"

#include "erhe/geometry/geometry.hpp"
#include "erhe/log/log_fmt.hpp"
#include "erhe/toolkit/file.hpp"
#include "erhe/toolkit/profile.hpp"

#include <gsl/gsl>

#include <algorithm>
#include <cctype>
#include <glm/glm.hpp>
#include <limits>
#include <string>

namespace editor {

using erhe::geometry::Corner_id;
using erhe::geometry::Point_id;
using erhe::geometry::Polygon_id;
using erhe::geometry::c_point_locations;
using erhe::geometry::c_point_colors;
using erhe::geometry::c_corner_normals;
using erhe::geometry::c_corner_texcoords;

// http://paulbourke.net/dataformats/obj/
// http://www.martinreddy.net/gfx/3d/OBJ.spec
// https://www.marxentlabs.com/obj-files/

enum class Command : unsigned int
{
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

auto parse_obj_geometry(
    const fs::path& path
) -> std::vector<std::shared_ptr<erhe::geometry::Geometry>>
{
    ERHE_PROFILE_FUNCTION

    log_parsers->trace("path = {}", path.generic_string());

    std::vector<std::shared_ptr<erhe::geometry::Geometry>> result;
    const auto opt_text = erhe::toolkit::read(path);

    // I dislike this big scope, I'd prefer just to
    // return {} but unfortunately having more than
    // one return kills named return value optimization.
    if (opt_text.has_value())
    {
        const std::string& text = opt_text.value();

        std::shared_ptr<erhe::geometry::Geometry> geometry{};
        erhe::geometry::Property_map<erhe::geometry::Point_id,  glm::vec3>* point_positions {nullptr};
        erhe::geometry::Property_map<erhe::geometry::Point_id,  glm::vec3>* point_colors    {nullptr};
        erhe::geometry::Property_map<erhe::geometry::Corner_id, glm::vec3>* corner_normals  {nullptr};
        erhe::geometry::Property_map<erhe::geometry::Corner_id, glm::vec2>* corner_texcoords{nullptr};

        const std::string delimiters  = " \t\v";
        const std::string end_of_line = "\n";
        const std::string slash       = "/\r";
        const std::string comment     = "#";

        std::string::size_type line_last_pos = text.find_first_not_of(end_of_line, 0);
        std::string::size_type line_pos      = text.find_first_of(end_of_line, line_last_pos);
        std::vector<glm::vec3> positions;
        std::vector<glm::vec3> colors;
        std::vector<glm::vec3> normals;
        std::vector<glm::vec2> texcoords;

        // Mapping from OBJ point id to erhe::geometry::Geometry::Point_Id
        std::vector<Point_id>  obj_point_to_geometry_point;

        bool has_vertex_colors = false;

        while (
            (line_pos != std::string::npos) ||
            (line_last_pos != std::string::npos)
        )
        {
            auto line = text.substr(line_last_pos, line_pos - line_last_pos);
            line.erase(
                std::remove(
                    line.begin(),
                    line.end(),
                    '\r'
                ),
                line.end()
            );

            // Drop comments
            const auto coment_pos = line.find_first_of(comment);
            if (coment_pos != std::string::npos)
            {
                line.erase(line.begin() + coment_pos, line.end());
            }

            //log_parsers->trace("line: {}", line);
            if (line.length() == 0)
            {
                line_last_pos = text.find_first_not_of(end_of_line, line_pos);
                line_pos      = text.find_first_of(end_of_line, line_last_pos);
                continue;
            }

            //const erhe::log::Indenter scope_indent;

            // process line
            std::string::size_type token_last_pos = line.find_first_not_of(delimiters, 0);
            std::string::size_type token_pos      = line.find_first_of    (delimiters, token_last_pos);
            if (token_last_pos == std::string::npos || token_pos == std::string::npos)
            {
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
            while ((token_pos != std::string::npos) || (token_last_pos != std::string::npos))
            {
                switch (command)
                {
                    //using enum Command;
                    case Command::Object_name:
                    {
                        // TODO Choose Geometry splitting based on o / g / s / mg
                        //      Currently fixed to use g
                        token_last_pos = line.find_first_not_of(end_of_line, token_pos);
                        token_pos      = line.find_first_of    (end_of_line, token_last_pos);
                        if (token_last_pos != std::string::npos || token_pos != std::string::npos)
                        {
                            const auto arg_text = line.substr(token_last_pos, token_pos - token_last_pos);
                            geometry         = std::make_shared<erhe::geometry::Geometry>(arg_text);
                            point_positions  = geometry->point_attributes().create<glm::vec3>(c_point_locations);
                            point_colors     = geometry->point_attributes().create<glm::vec3>(c_point_colors);
                            corner_normals   = geometry->corner_attributes().create<glm::vec3>(c_corner_normals);
                            corner_texcoords = geometry->corner_attributes().create<glm::vec2>(c_corner_texcoords);
                            result.push_back(geometry);
                            obj_point_to_geometry_point.clear();
                            log_parsers->trace("arg: {}", arg_text);
                        }
                        break;
                    }

                    case Command::Group_name:
                    {
                        token_last_pos = line.find_first_not_of(delimiters, token_pos);
                        token_pos      = line.find_first_of(end_of_line, token_last_pos);
                        if (token_last_pos != std::string::npos || token_pos != std::string::npos)
                        {
                            const auto arg_text = line.substr(token_last_pos, token_pos - token_last_pos);
                            geometry         = std::make_shared<erhe::geometry::Geometry>(arg_text);
                            point_positions  = geometry->point_attributes().create<glm::vec3>(c_point_locations);
                            point_colors     = geometry->point_attributes().create<glm::vec3>(c_point_colors);
                            corner_normals   = geometry->corner_attributes().create<glm::vec3>(c_corner_normals);
                            corner_texcoords = geometry->corner_attributes().create<glm::vec2>(c_corner_texcoords);
                            result.push_back(geometry);
                            obj_point_to_geometry_point.clear();
                            //log_parsers->trace("arg: {}", arg_text);
                        }
                        //token_pos = text.find_first_of(end_of_line, token_last_pos);
                        break;
                    }

                    case Command::Use_material:
                    case Command::Unknown:
                    case Command::Material_library:
                    {
                        // consume rest of the line for now
                        token_last_pos = line.find_first_not_of(end_of_line, token_pos);
                        token_pos      = line.find_first_of    (end_of_line, token_last_pos);
                        if (token_last_pos != std::string::npos || token_pos != std::string::npos)
                        {
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
                        if (token_last_pos != std::string::npos || token_pos != std::string::npos)
                        {
                            const auto  arg_text = line.substr(token_last_pos, token_pos - token_last_pos);
                            const float value    = std::stof(arg_text);
                            //log_parsers->trace("arg: {}", arg_text);
                            float_args.push_back(value);
                        }
                        //token_last_pos = line.find_first_not_of(delimiters, token_pos);
                        break;
                    }

                    case Command::Face:
                    {
                        token_last_pos = line.find_first_not_of(delimiters, token_pos);
                        token_pos      = line.find_first_of    (delimiters, token_last_pos);
                        if (token_last_pos != std::string::npos || token_pos != std::string::npos)
                        {
                            const auto arg_text = line.substr(token_last_pos, token_pos - token_last_pos);
                            //log_parsers->trace("arg: {}", arg_text);
                            std::string::size_type subtoken_last_pos = arg_text.find_first_not_of(slash, 0);
                            std::string::size_type subtoken_pos      = arg_text.find_first_of(slash, subtoken_last_pos);
                            int subtoken_slot = 0;
                            while ((subtoken_pos != std::string::npos) || (subtoken_last_pos != std::string::npos))
                            {
                                const auto subtoken_text = arg_text.substr(subtoken_last_pos, subtoken_pos - subtoken_last_pos);
                                //log_parsers->trace("subtoken: {}", subtoken_text);
                                if (arg_text.length() > 0)
                                {
                                    const int value = std::stoi(subtoken_text);
                                    switch (subtoken_slot)
                                    {
                                        case 0: face_vertex_position_indices.push_back(value); break;
                                        case 1: face_vertex_texcoord_indices.push_back(value); break;
                                        case 2: face_vertex_normal_indices  .push_back(value); break;
                                        default:
                                        {
                                            //ERHE_FATAL("bad subtoken slot for wavefront obj parser face command\n");
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

            switch (command)
            {
                //using enum Command;
                case Command::Group_name:
                case Command::Use_material:
                case Command::Unknown:
                case Command::Material_library:
                default:
                    break;
                case Command::Vertex_position:
                {
                    //ZoneScopedN("position");
                    if (float_args.size() >= 3)
                    {
                        Expects(geometry);
                        if (float_args.size() >= 6)
                        {
                            while (colors.size() < positions.size())
                            {
                                colors.emplace_back(1.0f, 1.0f, 1.0f);
                            }
                            colors.emplace_back(float_args[3], float_args[4], float_args[5]);
                            has_vertex_colors = true;
                            //point_colors->put(point_id, glm::vec3{float_args[3], float_args[4], float_args[5]});
                        }

                        positions.emplace_back(float_args[0], float_args[1], float_args[2]);
                    }
                    //else
                    //{
                    //    ERHE_FATAL("unsupported vertex dimension\n");
                    //}
                    break;
                }

                case Command::Vertex_normal:
                {
                    //ZoneScopedN("normal");
                    if (float_args.size() == 3)
                    {
                        normals.emplace_back(float_args[0], float_args[1], float_args[2]);
                    }
                    //else
                    //{
                    //    ERHE_FATAL("unsupported normal dimension\n");
                    //}
                    break;
                }
                case Command::Vertex_texture_coordinate:
                {
                    //ZoneScopedN("texcoord");
                    // TODO support 1 / 3
                    if (float_args.size() == 2)
                    {
                        texcoords.emplace_back(float_args[0], float_args[1]);
                    }
                    //else
                    //{
                    //    ERHE_FATAL("unsupported texcoord dimension\n");
                    //}
                    break;
                }
                case Command::Face:
                {
                    //ZoneScopedN("face");
                    Expects(geometry);

                    const Polygon_id polygon_id   = geometry->make_polygon();
                    const int        corner_count = static_cast<int>(face_vertex_position_indices.size());
                    for (int i = 0; i < corner_count; ++i)
                    {
                        const int obj_vertex_index = face_vertex_position_indices[i];
                        const int position_index =
                            (obj_vertex_index > 0)
                                ? obj_vertex_index - 1
                                : obj_vertex_index + static_cast<int>(positions.size());

                        // Vertex indices in OBJ file are global.
                        // Each erhe::geometry Geometry has it's own namespace for Point_id.
                        // This maps OBJ vertex indices to geometry Point_id.
                        constexpr auto null_point = std::numeric_limits<Point_id>::max();
                        while (static_cast<int>(obj_point_to_geometry_point.size()) <= position_index)
                        {
                            obj_point_to_geometry_point.push_back(null_point);
                        }
                        if (obj_point_to_geometry_point[position_index] == null_point)
                        {
                            obj_point_to_geometry_point[position_index] = geometry->make_point();
                        }

                        const Point_id  point_id  = obj_point_to_geometry_point[position_index];
                        const Corner_id corner_id = geometry->make_polygon_corner(polygon_id, point_id);
                        ERHE_VERIFY(position_index >= 0);
                        ERHE_VERIFY(position_index < static_cast<int>(positions.size()));

                        point_positions->put(point_id, positions[position_index]);

                        if (has_vertex_colors)
                        {
                            point_colors->put(point_id, colors[position_index]);
                        }

                        if (i < static_cast<int>(face_vertex_texcoord_indices.size()))
                        {
                            const int obj_texcoord_index = face_vertex_texcoord_indices[i];
                            const int texcoord_index =
                                (obj_texcoord_index > 0)
                                    ? obj_texcoord_index - 1
                                    : obj_texcoord_index + static_cast<int>(texcoords.size());
                            ERHE_VERIFY(texcoord_index < static_cast<int>(texcoords.size()));
                            corner_texcoords->put(corner_id, texcoords[texcoord_index]);
                        }

                        if (i < static_cast<int>(face_vertex_normal_indices.size()))
                        {
                            const int obj_normal_index = face_vertex_normal_indices[i];
                            const int normal_index =
                                (obj_normal_index > 0)
                                    ? obj_normal_index - 1
                                    : obj_normal_index + static_cast<int>(normals.size());
                            ERHE_VERIFY(normal_index < static_cast<int>(normals.size()));
                            corner_normals->put(corner_id, normals[normal_index]);
                        }
                    }
                }
            }

            line_last_pos = text.find_first_not_of(end_of_line, line_pos);
            line_pos = text.find_first_of(end_of_line, line_last_pos);
        }

        for (auto g : result)
        {
            ERHE_PROFILE_SCOPE("post processing");

            g->make_point_corners();

            g->build_edges();
            g->generate_polygon_texture_coordinates();
            g->compute_tangents();
        }
    }

    return result;
}

} // namespace editor


// mtllib teapot.mtl
//
// g Mesh1 Teapot Model
//
// usemtl FrontColor
// v 0 2.4 -1.4
// vt -0.109561 1.71761
// vn 1.39147e-17 -0.369129 0.929378
// v 0.229712 2.4 -1.38197
// vt 0.120858 1.71761
// vn -0.145716 -0.369332 0.917802
// v 0.227403 2.43544 -1.36807
// vt 0.119643 1.75572
// vn -0.150341 -0.284166 0.946915
// v 0 2.43544 -1.38593
// vt -0.108459 1.75572
// vn -1.64188e-16 -0.284002 0.958824
// f 1/1/1 2/2/2 3/3/3 4/4/4
