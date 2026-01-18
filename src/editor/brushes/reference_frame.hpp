#pragma once

#include "erhe_geometry/geometry.hpp"

namespace editor {

enum class Frame_orientation : unsigned int
{
    invalid,
    in,
    out
};

enum class Frame_source : unsigned int
{
    invalid,
    derived_from_surface_points,
    direct_tangent_frame
};

class Grid;

class Reference_frame
{
public:
    Reference_frame();

    Reference_frame(const GEO::Mesh& mesh, GEO::index_t facet, GEO::index_t face_offset, GEO::index_t corner_offset,
        Frame_orientation frame_orientation);

    Reference_frame(const Grid& grid, const GEO::vec3f& position);

    void transform_by(const GEO::mat4f& m);

    [[nodiscard]] auto transform(float hover_distance) const -> GEO::mat4f;
    [[nodiscard]] auto scale    () const -> float;
    [[nodiscard]] auto is_valid () const -> bool;

    Frame_orientation m_frame_orientation{Frame_orientation::invalid};
    Frame_source      m_frame_source     {Frame_source::invalid};
    GEO::index_t      m_corner_count     {0};
    GEO::index_t      m_face_offset      {0};
    GEO::index_t      m_corner_offset0   {0};
    GEO::index_t      m_corner_offset1   {0};
    GEO::index_t      m_facet            {0};
    GEO::index_t      m_corner0          {0};
    GEO::index_t      m_corner1          {0};
    float             m_scale            {0.0f};
    GEO::vec3f        m_centroid         {0.0f, 0.0f, 0.0f}; // facet centroid
    GEO::vec3f        m_position0        {1.0f, 0.0f, 0.0f}; // 1st facet corner vertex
    GEO::vec3f        m_position1        {1.0f, 0.0f, 0.0f}; // 2nf facet corner vertex
    GEO::vec3f        m_B                {0.0f, 0.0f, 1.0f};
    GEO::vec3f        m_T                {1.0f, 0.0f, 0.0f};
    GEO::vec3f        m_N                {0.0f, 1.0f, 0.0f};
};

}
