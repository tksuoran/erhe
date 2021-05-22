#include "parsers/wavefront_obj.hpp"
#include "log.hpp"

#include "erhe/toolkit/file.hpp"
#include "erhe/toolkit/tracy_client.hpp"

#include <algorithm>
#include <cctype>
#include <glm/glm.hpp>
#include <string>

namespace editor {

using namespace glm;
using namespace erhe::geometry;

enum class Command : unsigned int
{
    Unknown = 0,
    Material_library,
    Begin_geometry,
    Use_material,
    Vertex_position,
    Vertex_texture_coordinate,
    Vertex_normal,
    Face,
};

Command tokenize(const std::string& text)
{
    if (text == "v")      return Command::Vertex_position;
    if (text == "vt")     return Command::Vertex_texture_coordinate;
    if (text == "vn")     return Command::Vertex_normal;
    if (text == "f")      return Command::Face;
    if (text == "g")      return Command::Begin_geometry;
    if (text == "usemtl") return Command::Use_material;
    if (text == "mtllib") return Command::Material_library;
    return Command::Unknown;
}

// v 0 2.43544 -1.38593
// vt -0.108459 1.75572
// vn -1.64188e-16 -0.284002 0.958824
// f 1/1/1 2/2/2 3/3/3 4/4/4 

Geometry parse_obj_geometry(const std::filesystem::path& path)
{
    ZoneScoped;

    log_parsers.trace("path = {}\n", path.generic_string());

    Geometry geometry;

    auto opt_text = erhe::toolkit::read(path);

    // I dislike this big scope, I'd prefer just to
    // return {} but unfortunately having more than
    // one return kills named return value optimization.
    if (opt_text.has_value())
    {
        const std::string& text = opt_text.value();

        auto* point_positions  = geometry.point_attributes().create<glm::vec3>(c_point_locations);
        auto* corner_normals   = geometry.corner_attributes().create<glm::vec3>(c_corner_normals);
        auto* corner_texcoords = geometry.corner_attributes().create<glm::vec2>(c_corner_texcoords);

        const std::string& delimiters  = " \t\v";
        const std::string& end_of_line = "\n";
        const std::string& slash       = "/\r";
        std::string::size_type line_last_pos = text.find_first_not_of(end_of_line, 0);
        std::string::size_type line_pos      = text.find_first_of(end_of_line, line_last_pos);

        Point_id next_vertex_id = 0;
        std::vector<glm::vec3> normals;
        std::vector<glm::vec2> texcoords;
        while ((line_pos != std::string::npos) || (line_last_pos != std::string::npos))
        {
            auto line = text.substr(line_last_pos, line_pos - line_last_pos);
            line.erase(std::remove(line.begin(), line.end(), '\r'), line.end());
            //log_parsers.trace("line: {}\n", line);
            if (line.length() == 0)
            {
                line_last_pos = text.find_first_not_of(end_of_line, line_pos);
                line_pos = text.find_first_of(end_of_line, line_last_pos);
                continue;
            }
            erhe::log::Indenter scope_indent;

            // process line
            std::string::size_type token_last_pos = line.find_first_not_of(delimiters, 0);
            std::string::size_type token_pos      = line.find_first_of(delimiters, token_last_pos);
            if (token_last_pos == std::string::npos || token_pos == std::string::npos)
            {
                line_last_pos = text.find_first_not_of(end_of_line, line_pos);
                line_pos = text.find_first_of(end_of_line, line_last_pos);
                continue;
            }
            auto command_text = line.substr(token_last_pos, token_pos - token_last_pos);
            auto command = tokenize(command_text);
            //log_parsers.trace("command: {}\n", command_text);
            std::vector<float> float_args;
            std::vector<int> int_args;
            std::vector<int> face_vertex_position_indices;
            std::vector<int> face_vertex_texcoord_indices;
            std::vector<int> face_vertex_normal_indices;
            while ((token_pos != std::string::npos) || (token_last_pos != std::string::npos))
            {
                switch (command)
                {
                    case Command::Begin_geometry:
                    {
                        token_last_pos = line.find_first_not_of(end_of_line, token_pos);
                        token_pos = line.find_first_of(end_of_line, token_last_pos);
                        if (token_last_pos != std::string::npos || token_pos != std::string::npos)
                        {
                            auto arg_text = line.substr(token_last_pos, token_pos - token_last_pos);
                            //log_parsers.trace("arg: {}\n", arg_text);
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
                        token_pos = line.find_first_of(end_of_line, token_last_pos);
                        if (token_last_pos != std::string::npos || token_pos != std::string::npos)
                        {
                            auto arg_text = line.substr(token_last_pos, token_pos - token_last_pos);
                            //log_parsers.trace("arg: {}\n", arg_text);
                        }
                        //token_last_pos = line.find_first_not_of(end_of_line, token_pos);
                        //token_pos = text.find_first_of(end_of_line, token_last_pos);
                        break;
                    }
                    case Command::Vertex_position:
                    case Command::Vertex_normal:
                    case Command::Vertex_texture_coordinate:
                    {
                        token_last_pos = line.find_first_not_of(delimiters, token_pos);
                        token_pos = line.find_first_of(delimiters, token_last_pos);
                        if (token_last_pos != std::string::npos || token_pos != std::string::npos)
                        {
                            auto arg_text = line.substr(token_last_pos, token_pos - token_last_pos);
                            log_parsers.trace("arg: {}\n", arg_text);
                            float value = std::stof(arg_text);
                            float_args.push_back(value);
                        }
                        //token_last_pos = line.find_first_not_of(delimiters, token_pos);
                        break;
                    }

                    case Command::Face:
                    {
                        token_last_pos = line.find_first_not_of(delimiters, token_pos);
                        token_pos = line.find_first_of(delimiters, token_last_pos);
                        if (token_last_pos != std::string::npos || token_pos != std::string::npos)
                        {
                            auto arg_text = line.substr(token_last_pos, token_pos - token_last_pos);
                            //log_parsers.trace("arg: {}\n", arg_text);
                            std::string::size_type subtoken_last_pos = arg_text.find_first_not_of(slash, 0);
                            std::string::size_type subtoken_pos      = arg_text.find_first_of(slash, subtoken_last_pos);
                            int subtoken_slot = 0;
                            while ((subtoken_pos != std::string::npos) || (subtoken_last_pos != std::string::npos))
                            {
                                auto subtoken_text = arg_text.substr(subtoken_last_pos, subtoken_pos - subtoken_last_pos);
                                //log_parsers.trace("subtoken: {}\n", subtoken_text);
                                if (arg_text.length() > 0)
                                {
                                    int value = std::stoi(subtoken_text);
                                    switch (subtoken_slot)
                                    {
                                        case 0: face_vertex_position_indices.push_back(value); break;
                                        case 1: face_vertex_texcoord_indices.push_back(value); break;
                                        case 2: face_vertex_normal_indices.push_back(value); break;
                                        default:
                                        {
                                            //FATAL("bad subtoken slot for wavefront obj parser face command\n");
                                            break;
                                        }
                                    }
                                }
                                subtoken_last_pos = arg_text.find_first_not_of(slash, subtoken_pos);
                                subtoken_pos = arg_text.find_first_of(slash, subtoken_last_pos);
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
                case Command::Begin_geometry:
                case Command::Use_material:
                case Command::Unknown:
                case Command::Material_library:
                    break;
                case Command::Vertex_position:
                {
                    //ZoneScopedN("position");
                    if (float_args.size() == 3)
                    {
                        Point_id point_id = geometry.make_point();
                        //Expects(point_id == next_vertex_id);
                        point_positions->put(point_id, glm::vec3(float_args[0], float_args[1], float_args[2]));
                        next_vertex_id++;
                    }
                    //else
                    //{
                    //    FATAL("unsupported vertex dimension\n");
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
                    //    FATAL("unsupported normal dimension\n");
                    //}
                    break;
                }
                case Command::Vertex_texture_coordinate:
                {
                    //ZoneScopedN("texcoord");
                    if (float_args.size() == 2)
                    {
                        texcoords.emplace_back(float_args[0], float_args[1]);
                    }
                    //else
                    //{
                    //    FATAL("unsupported texcoord dimension\n");
                    //}
                    break;
                }
                case Command::Face:
                {
                    //ZoneScopedN("face");
                    Polygon_id polygon_id = geometry.make_polygon();
                    size_t corner_count = face_vertex_position_indices.size();
                    for (size_t i = 0; i < corner_count; ++i)
                    {
                        auto pos_index = face_vertex_position_indices[i] - 1;
                        Point_id point_id = static_cast<Point_id>(pos_index);
                        Corner_id corner_id = geometry.make_polygon_corner(polygon_id, point_id);
                        if (i < face_vertex_texcoord_indices.size())
                        {
                            auto texcoord_index = static_cast<size_t>(face_vertex_texcoord_indices[i] - 1);
                            //VERIFY(texcoord_index < texcoords.size());
                            corner_texcoords->put(corner_id, texcoords[texcoord_index]);
                        }
                        if (i < face_vertex_normal_indices.size())
                        {
                            auto normal_index = static_cast<size_t>(face_vertex_normal_indices[i] - 1);
                            //VERIFY(normal_index < normals.size());
                            corner_normals->put(corner_id, normals[normal_index]);
                        }
                    }
                }
            }

            line_last_pos = text.find_first_not_of(end_of_line, line_pos);
            line_pos = text.find_first_of(end_of_line, line_last_pos);
        }

        {
            ZoneScopedN("post processing");

            geometry.make_point_corners();

            for (Polygon_id polygon_id = 0; polygon_id < geometry.polygon_count(); ++polygon_id)
            {
                erhe::geometry::Polygon& polygon = geometry.polygons[polygon_id];
                polygon.reverse(geometry);
            }

            geometry.build_edges();
            geometry.generate_polygon_texture_coordinates();
            geometry.compute_tangents();
        }
    }

    return geometry;
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
