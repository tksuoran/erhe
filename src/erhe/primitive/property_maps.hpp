#pragma once

#include "erhe/primitive/format_info.hpp"
#include "erhe/gl/strong_gl_enums.hpp"
#include "erhe/geometry/geometry.hpp"
#include "erhe/geometry/property_map.hpp"
#include "erhe/geometry/property_map_collection.hpp"
#include "erhe/graphics/buffer.hpp"
#include "erhe/graphics/vertex_format.hpp"
#include "erhe/graphics/state/vertex_input_state.hpp"

#include <glm/glm.hpp>

#include <cstddef>
#include <memory>
#include <string>

namespace erhe::geometry
{
    class Geometry;
}

namespace erhe::primitive
{


class Property_maps
{
public:
    Property_maps(
        const erhe::geometry::Geometry& geometry,
        const Format_info&              format_info
    );

    template <typename Key_type, typename Value_type>
    [[nodiscard]] auto find_or_create(
        const erhe::geometry::Property_map_collection<Key_type>& geometry_attributes,
        erhe::geometry::Property_map_collection<Key_type>&       attributes,
        const std::string&                                       name
    ) -> erhe::geometry::Property_map<Key_type, Value_type> *
    {
        ERHE_PROFILE_FUNCTION

        auto map = geometry_attributes.template find<Value_type>(name);
        if (map == nullptr)
        {
            map = attributes.template create<Value_type>(name);
        }
        return map;
    }

    erhe::geometry::Property_map_collection<erhe::geometry::Point_id>   point_attributes;
    erhe::geometry::Property_map_collection<erhe::geometry::Corner_id>  corner_attributes;
    erhe::geometry::Property_map_collection<erhe::geometry::Polygon_id> polygon_attributes;
    erhe::geometry::Property_map_collection<erhe::geometry::Edge_id>    edge_attributes;

    erhe::geometry::Property_map<erhe::geometry::Polygon_id, glm::vec3>* polygon_ids_vector3 {nullptr};
    erhe::geometry::Property_map<erhe::geometry::Polygon_id, uint32_t>*  polygon_ids_uint32  {nullptr};
    erhe::geometry::Property_map<erhe::geometry::Polygon_id, glm::vec3>* polygon_normals     {nullptr};
    erhe::geometry::Property_map<erhe::geometry::Polygon_id, glm::vec3>* polygon_centroids   {nullptr};
    erhe::geometry::Property_map<erhe::geometry::Polygon_id, glm::vec4>* polygon_colors      {nullptr};
    erhe::geometry::Property_map<erhe::geometry::Corner_id, glm::vec3>*  corner_normals      {nullptr};
    erhe::geometry::Property_map<erhe::geometry::Corner_id, glm::vec4>*  corner_tangents     {nullptr};
    erhe::geometry::Property_map<erhe::geometry::Corner_id, glm::vec4>*  corner_bitangents   {nullptr};
    erhe::geometry::Property_map<erhe::geometry::Corner_id, glm::vec2>*  corner_texcoords    {nullptr};
    erhe::geometry::Property_map<erhe::geometry::Corner_id, glm::vec4>*  corner_colors       {nullptr};
    erhe::geometry::Property_map<erhe::geometry::Corner_id, uint32_t>*   corner_indices      {nullptr};
    erhe::geometry::Property_map<erhe::geometry::Point_id, glm::vec3>*   point_locations     {nullptr};
    erhe::geometry::Property_map<erhe::geometry::Point_id, glm::vec3>*   point_normals       {nullptr};
    erhe::geometry::Property_map<erhe::geometry::Point_id, glm::vec3>*   point_normals_smooth{nullptr};
    erhe::geometry::Property_map<erhe::geometry::Point_id, glm::vec4>*   point_tangents      {nullptr};
    erhe::geometry::Property_map<erhe::geometry::Point_id, glm::vec4>*   point_bitangents    {nullptr};
    erhe::geometry::Property_map<erhe::geometry::Point_id, glm::vec2>*   point_texcoords     {nullptr};
    erhe::geometry::Property_map<erhe::geometry::Point_id, glm::vec4>*   point_colors        {nullptr};
};

} // namespace erhe::primitive
