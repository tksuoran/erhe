#pragma once

#include "erhe/geometry/property_map.hpp"
#include "erhe/geometry/property_map_collection.hpp"
#include "erhe/geometry/types.hpp"

#include <glm/glm.hpp>
#include <gsl/assert>

#include "Tracy.hpp"

#include <functional>
#include <optional>
#include <set>
#include <string_view>
#include <vector>

namespace erhe::log
{
    struct Category;
}

namespace erhe::geometry
{

inline constexpr const Property_map_descriptor c_point_locations     { "point_locations"     , Transform_mode::matrix                                       , Interpolation_mode::linear };
inline constexpr const Property_map_descriptor c_point_normals       { "point_normals"       , Transform_mode::normalize_inverse_transpose_matrix           , Interpolation_mode::normalized };
inline constexpr const Property_map_descriptor c_point_normals_smooth{ "point_normals_smooth", Transform_mode::normalize_inverse_transpose_matrix           , Interpolation_mode::normalized  };
inline constexpr const Property_map_descriptor c_point_texcoords     { "point_texcoords"     , Transform_mode::none                                         , Interpolation_mode::linear };
inline constexpr const Property_map_descriptor c_point_tangents      { "point_tangents"      , Transform_mode::normalize_inverse_transpose_matrix_vec3_float, Interpolation_mode::normalized_vec3_float };
inline constexpr const Property_map_descriptor c_point_bitangents    { "point_bitangents"    , Transform_mode::normalize_inverse_transpose_matrix_vec3_float, Interpolation_mode::normalized_vec3_float };
inline constexpr const Property_map_descriptor c_point_colors        { "point_colors"        , Transform_mode::none                                         , Interpolation_mode::linear };
inline constexpr const Property_map_descriptor c_corner_normals      { "corner_normals"      , Transform_mode::normalize_inverse_transpose_matrix           , Interpolation_mode::none };
inline constexpr const Property_map_descriptor c_corner_texcoords    { "corner_texcoords"    , Transform_mode::none                                         , Interpolation_mode::none };
inline constexpr const Property_map_descriptor c_corner_tangents     { "corner_tangents"     , Transform_mode::normalize_inverse_transpose_matrix_vec3_float, Interpolation_mode::none };
inline constexpr const Property_map_descriptor c_corner_bitangents   { "corner_bitangents"   , Transform_mode::normalize_inverse_transpose_matrix_vec3_float, Interpolation_mode::none };
inline constexpr const Property_map_descriptor c_corner_colors       { "corner_colors"       , Transform_mode::none                                         , Interpolation_mode::none };
inline constexpr const Property_map_descriptor c_corner_indices      { "corner_indices"      , Transform_mode::none                                         , Interpolation_mode::none };
inline constexpr const Property_map_descriptor c_polygon_centroids   { "polygon_centroids"   , Transform_mode::matrix                                       , Interpolation_mode::none };
inline constexpr const Property_map_descriptor c_polygon_normals     { "polygon_normals"     , Transform_mode::normalize_inverse_transpose_matrix           , Interpolation_mode::none };
inline constexpr const Property_map_descriptor c_polygon_tangents    { "polygon_tangents"    , Transform_mode::normalize_inverse_transpose_matrix_vec3_float, Interpolation_mode::none };
inline constexpr const Property_map_descriptor c_polygon_bitangents  { "polygon_bitangents"  , Transform_mode::normalize_inverse_transpose_matrix_vec3_float, Interpolation_mode::none };
inline constexpr const Property_map_descriptor c_polygon_colors      { "polygon_colors"      , Transform_mode::none                                         , Interpolation_mode::none };
inline constexpr const Property_map_descriptor c_polygon_ids_vec3    { "polygon_ids_vec"     , Transform_mode::none                                         , Interpolation_mode::none };
inline constexpr const Property_map_descriptor c_polygon_ids_uint    { "polygon_ids_uint"    , Transform_mode::none                                         , Interpolation_mode::none };

struct Point;
struct Polygon;
class Geometry;
struct Edge;

struct Corner
{
    Point_id   point_id{0};
    Polygon_id polygon_id{0};

    template<typename T>
    void smooth_normalize(const Corner_id                            this_corner_id,
                          const Geometry&                            geometry,
                          Property_map<Corner_id, T>&                corner_attribute,
                          const Property_map<Polygon_id, T>&         polygon_attribute,
                          const Property_map<Polygon_id, glm::vec3>& polygon_normals,
                          const float                                cos_max_smoothing_angle) const;

    template<typename T>
    void smooth_average(const Corner_id                           this_corner_id,
                        const Geometry&                           geometry,
                        Property_map<Corner_id, T>&               new_corner_attribute,
                        const Property_map<Corner_id, T>&         old_corner_attribute,
                        const Property_map<Corner_id, glm::vec3>& corner_normals,
                        const Property_map<Point_id,  glm::vec3>& point_normals) const;
};

struct Point
{
    struct Point_corner_context
    {
        Geometry&       geometry;
        Point_corner_id point_corner_id;
        Corner_id       corner_id;
        Corner&         corner;
        bool            break_{false};

        void break_iteration()
        {
            break_ = true;
        }
    };

    void for_each_corner(Geometry&                                          geometry,
                         std::function<void(Point_corner_context& context)> callback);

    struct Point_corner_context_const
    {
        const Geometry& geometry;
        Point_corner_id point_corner_id;
        Corner_id       corner_id;
        const Corner&   corner;
        bool            break_{false};

        void break_iteration()
        {
            break_ = true;
        }
    };

    void for_each_corner_const(const Geometry&                                geometry,
                               std::function<void(Point_corner_context_const& context)> callback) const;

    struct Point_corner_neighborhood_context
    {
        Geometry&       geometry;
        Point_corner_id prev_point_corner_id;
        Point_corner_id point_corner_id;
        Point_corner_id next_point_corner_id;
        Corner_id       prev_corner_id;
        Corner_id       corner_id;
        Corner_id       next_corner_id;
        Corner&         prev_corner;
        Corner&         corner;
        Corner&         next_corner;
        bool            break_{false};

        void break_iteration()
        {
            break_ = true;
        }
    };

    struct Point_corner_neighborhood_context_const
    {
        const Geometry& geometry;
        Point_corner_id prev_point_corner_id;
        Point_corner_id point_corner_id;
        Point_corner_id next_point_corner_id;
        Corner_id       prev_corner_id;
        Corner_id       corner_id;
        Corner_id       next_corner_id;
        const Corner&   prev_corner;
        const Corner&   corner;
        const Corner&   next_corner;
        bool            break_{false};

        void break_iteration()
        {
            break_ = true;
        }
    };

    void for_each_corner_neighborhood(Geometry&                                                       geometry,
                                      std::function<void(Point_corner_neighborhood_context& context)> callback);

    void for_each_corner_neighborhood_const(const Geometry&                                                       geometry,
                                            std::function<void(Point_corner_neighborhood_context_const& context)> callback) const;

    Point_corner_id first_point_corner_id{0};
    uint32_t        corner_count{0};
    uint32_t        reserved_corner_count{0};
};

struct Polygon
{
    Polygon_corner_id first_polygon_corner_id{0};
    uint32_t          corner_count{0};

    // Copies polygon property to corners
    template <typename T>
    void copy_to_corners(const Polygon_id                   this_polygon_id,
                         const Geometry&                    geometry,
                         Property_map<Corner_id, T>&        corner_attribute,
                         const Property_map<Polygon_id, T>& polygon_attribute) const;

    template <typename T>
    void smooth_normalize(const Geometry&                            geometry,
                          Property_map<Corner_id, T>&                corner_attribute,
                          const Property_map<Polygon_id, T>&         polygon_attribute,
                          const Property_map<Polygon_id, glm::vec3>& polygon_normals,
                          const float                                cos_max_smoothing_angle) const;

    template <typename T>
    void smooth_average(const Geometry&                           geometry,
                        Property_map<Corner_id, T>&               new_corner_attribute,
                        const Property_map<Corner_id, T>&         old_corner_attribute,
                        const Property_map<Corner_id, glm::vec3>& corner_normals,
                        const Property_map<Point_id, glm::vec3>&  point_normals) const;

    void compute_normal(const Polygon_id                         this_polygon_id,
                        const Geometry&                          geometry,
                        Property_map<Polygon_id, glm::vec3>&     polygon_normals,
                        const Property_map<Point_id, glm::vec3>& point_locations) const;

    auto compute_normal(const Geometry&                          geometry,
                        const Property_map<Point_id, glm::vec3>& point_locations) const
    -> glm::vec3;

    auto compute_centroid(const Geometry&                          geometry,
                          const Property_map<Point_id, glm::vec3>& point_locations) const
    -> glm::vec3;

    auto compute_edge_midpoint(const Geometry&                          geometry,
                               const Property_map<Point_id, glm::vec3>& point_locations) const
    -> glm::vec3;

    void compute_centroid(const Polygon_id                         this_polygon_id,
                          const Geometry&                          geometry,
                          Property_map<Polygon_id, glm::vec3>&     polygon_centroids,
                          const Property_map<Point_id, glm::vec3>& point_locations) const;

    void compute_planar_texture_coordinates(const Polygon_id                           this_polygon_id,
                                            const Geometry&                            geometry,
                                            Property_map<Corner_id, glm::vec2>&        corner_texcoords,
                                            const Property_map<Polygon_id, glm::vec3>& polygon_centroids,
                                            const Property_map<Polygon_id, glm::vec3>& polygon_normals,
                                            const Property_map<Point_id, glm::vec3>&   point_locations,
                                            const bool                                 overwrite = false) const;

    auto corner(const Geometry& geometry, const Point_id point_id) const -> Corner_id;

    auto next_corner(const Geometry& geometry, const Corner_id anchor_corner_id) const -> Corner_id;

    auto prev_corner(const Geometry& geometry, const Corner_id corner_id) const -> Corner_id;

    void reverse(Geometry& geometry);

    struct Polygon_corner_context
    {
        Geometry&         geometry;
        Polygon_corner_id polygon_corner_id;
        Corner_id         corner_id;
        Corner&           corner;
        bool              break_{false};

        void break_iteration()
        {
            break_ = true;
        }
    };

    struct Polygon_corner_context_const
    {
        const Geometry&   geometry;
        Polygon_corner_id polygon_corner_id;
        Corner_id         corner_id;
        const Corner&     corner;
        bool              break_{false};

        void break_iteration()
        {
            break_ = true;
        }
    };

    void for_each_corner(Geometry&                                            geometry,
                         std::function<void(Polygon_corner_context& context)> callback);

    void for_each_corner_const(const Geometry&                                            geometry,
                               std::function<void(Polygon_corner_context_const& context)> callback) const;

    struct Polygon_corner_neighborhood_context
    {
        Geometry&         geometry;
        Polygon_corner_id prev_polygon_corner_id;
        Polygon_corner_id polygon_corner_id;
        Polygon_corner_id next_polygon_corner_id;
        Corner_id         prev_corner_id;
        Corner_id         corner_id;
        Corner_id         next_corner_id;
        Corner&           prev_corner;
        Corner&           corner;
        Corner&           next_corner;
        bool              break_{false};

        void break_iteration()
        {
            break_ = true;
        }
    };

    struct Polygon_corner_neighborhood_context_const
    {
        const Geometry&   geometry;
        Polygon_corner_id prev_polygon_corner_id;
        Polygon_corner_id polygon_corner_id;
        Polygon_corner_id next_polygon_corner_id;
        Corner_id         prev_corner_id;
        Corner_id         corner_id;
        Corner_id         next_corner_id;
        const Corner&     prev_corner;
        const Corner&     corner;
        const Corner&     next_corner;
        bool              break_{false};

        void break_iteration()
        {
            break_ = true;
        }
    };

    void for_each_corner_neighborhood(Geometry&                                                         geometry,
                                      std::function<void(Polygon_corner_neighborhood_context& context)> callback);

    void for_each_corner_neighborhood_const(const Geometry&                                                         geometry,
                                            std::function<void(Polygon_corner_neighborhood_context_const& context)> callback) const;
};

struct Edge
{
    Point_id        a;
    Point_id        b;
    Edge_polygon_id first_edge_polygon_id;
    uint32_t        polygon_count;

    struct Edge_polygon_context
    {
        Geometry&       geometry;
        Edge_polygon_id edge_polygon_id;
        Polygon_id      polygon_id;
        Polygon&        polygon;
        bool            break_{false};

        void break_iteration()
        {
            break_ = true;
        }
    };

    struct Edge_polygon_context_const
    {
        const Geometry& geometry;
        Edge_polygon_id edge_polygon_id;
        Polygon_id      polygon_id;
        const Polygon&  polygon;
        bool            break_{false};

        void break_iteration()
        {
            break_ = true;
        }
    };

    void for_each_polygon(Geometry&                                          geometry,
                          std::function<void(Edge_polygon_context& context)> callback);

    void for_each_polygon_const(const Geometry&                                          geometry,
                                std::function<void(Edge_polygon_context_const& context)> callback) const;
};

struct Mesh_info
{
    size_t polygon_count              {0};
    size_t corner_count               {0};
    size_t triangle_count             {0};
    size_t edge_count                 {0};
    size_t vertex_count_corners       {0};
    size_t vertex_count_centroids     {0};
    size_t index_count_fill_triangles {0};
    size_t index_count_edge_lines     {0};
    size_t index_count_corner_points  {0};
    size_t index_count_centroid_points{0};

    auto operator+=(const Mesh_info& o)
    -> Mesh_info&;

    void trace(erhe::log::Category& log) const;
};

class Geometry
{
public:
    using Point_property_map_collection   = Property_map_collection<Point_id>;
    using Corner_property_map_collection  = Property_map_collection<Corner_id>;
    using Polygon_property_map_collection = Property_map_collection<Polygon_id>;
    using Edge_property_map_collection    = Property_map_collection<Edge_id>;

    std::string             name;
    std::vector<Corner>     corners;
    std::vector<Point>      points;
    std::vector<Polygon>    polygons;
    std::vector<Edge>       edges;
    std::vector<Corner_id>  point_corners;
    std::vector<Corner_id>  polygon_corners;
    std::vector<Polygon_id> edge_polygons;

    Geometry         ();
    explicit Geometry(std::string_view name);
    Geometry         (std::string_view name, std::function<void(Geometry&)>);
    ~Geometry        ();
    Geometry         (const Geometry&)  = delete;
    void operator=   (const Geometry&)  = delete;
    Geometry         (Geometry&& other) noexcept;
    void operator=   (Geometry&&)       = delete;

    void promise_has_normals()
    {
        m_serial_point_normals  = m_serial;
        m_serial_corner_normals = m_serial;
    }

    void promise_has_polygon_normals()
    {
        m_serial_polygon_normals = m_serial;
    }

    void promise_has_polygon_centroids()
    {
        m_serial_polygon_centroids = m_serial;
    }

    void promise_has_tangents()
    {
        m_serial_polygon_tangents = m_serial;
        m_serial_point_tangents   = m_serial;
        m_serial_corner_tangents  = m_serial;
    }

    void promise_has_bitangents()
    {
        m_serial_polygon_bitangents = m_serial;
        m_serial_point_bitangents   = m_serial;
        m_serial_corner_bitangents  = m_serial;
    }

    void promise_has_texture_coordinates()
    {
        m_serial_point_texture_coordinates = m_serial;
        m_serial_corner_texture_coordinates = m_serial;
    }

    uint32_t corner_count        () const { return m_next_corner_id; }
    uint32_t point_count         () const { return m_next_point_id; }
    uint32_t point_corner_count  () const { return m_next_point_corner_reserve; }
    uint32_t polygon_count       () const { return m_next_polygon_id; }
    uint32_t polygon_corner_count() const { return m_next_polygon_corner_id; }
    uint32_t edge_count          () const { return m_next_edge_id; }

    auto find_edge(Point_id a, Point_id b)
    -> std::optional<Edge>
    {
        if (b < a)
        {
            std::swap(a, b);
        }
        for (const auto& edge : edges)
        {
            if (edge.a == a && edge.b == b)
            {
                return edge;
            }
        }
        return {};
    }

    // Allocates new Corner / Corner_id
    // - Point must be allocated.
    // - Polygon must be allocated
    auto make_corner(const Point_id point_id, const Polygon_id polygon_id) -> Corner_id;

    // Allocates new Point / Point_id
    auto make_point() -> Point_id;

    // Allocates new Polygon / Polygon_id
    auto make_polygon() -> Polygon_id;

    // Allocates new Edge / Edge_id
    // - Points must be already allocated
    // - Points must be ordered
    auto make_edge(const Point_id a, const Point_id b) -> Edge_id;

    // Reserves new point corner.
    // - Point must be already allocated.
    // - Corner must be already allocated.
    // - Does not actually create point corner, only allocates space
    void reserve_point_corner(const Point_id point_id, const Corner_id corner_id);

    // Allocates new edge polygon.
    // - Edge must be already allocated.
    // - Polygon must be already allocated.
    auto make_edge_polygon(const Edge_id edge_id, const Polygon_id polygon_id) -> Edge_polygon_id;

    // Allocates new polygon corner.
    // - Polygon must be already allocated.
    // - Corner must be already allocated.
    auto make_polygon_corner_(const Polygon_id polygon_id, const Corner_id corner_id) -> Polygon_corner_id;

    // Allocates new polygon corner.
    // - Polygon must be already allocated.
    // - Point must be already allocated.
    auto make_polygon_corner(const Polygon_id polygon_id, const Point_id point_id) -> Corner_id;

    auto count_polygon_triangles() const -> size_t;

    void info(Mesh_info& info) const;

    auto point_attributes() -> Point_property_map_collection&
    {
        return m_point_property_map_collection;
    }

    auto corner_attributes() -> Corner_property_map_collection&
    {
        return m_corner_property_map_collection;
    }

    auto polygon_attributes() -> Polygon_property_map_collection&
    {
        return m_polygon_property_map_collection;
    }

    auto edge_attributes() -> Edge_property_map_collection&
    {
        return m_edge_property_map_collection;
    }

    auto point_attributes() const -> const Point_property_map_collection&
    {
        return m_point_property_map_collection;
    }

    auto corner_attributes() const -> const Corner_property_map_collection&
    {
        return m_corner_property_map_collection;
    }

    auto polygon_attributes() const -> const Polygon_property_map_collection&
    {
        return m_polygon_property_map_collection;
    }

    auto edge_attributes() const -> const Edge_property_map_collection&
    {
        return m_edge_property_map_collection;
    }

    void reserve_points(const size_t point_count);

    void reserve_polygons(const size_t polygon_count);

    auto make_point(const float x, const float y, const float z) -> Point_id;

    auto make_point(const float x, const float y, const float z, const float s, const float t) -> Point_id;

    auto make_point(const double x, const double y, const double z) -> Point_id;

    auto make_point(const double x, const double y, const double z, const double s, const double t) -> Point_id;

    auto make_polygon(const std::initializer_list<Point_id> point_list) -> Polygon_id;

    // Requires point locations.
    // Returns false if point locations are not available.
    // Returns true on success.
    auto compute_polygon_normals() -> bool;

    auto has_polygon_normals() const -> bool;

    // Requires point locations.
    // Returns false if point locations are not available.
    // Returns true on success.
    auto compute_polygon_centroids() -> bool;

    auto has_polygon_centroids() const -> bool;

    // Calculates point normal from polygon normals
    // Returns incorrect data if there are missing polygon normals.
    auto compute_point_normal(Point_id point_id) -> glm::vec3;

    auto has_point_normals() const -> bool;

    // Calculates point normals from polygon normals.
    // If polygon normals are not up to date before this call,
    // also updates polygon normals.
    // Returns false if unable to calculate polygon normals
    // (due to missing point locations).
    auto compute_point_normals(const Property_map_descriptor& descriptor) -> bool;

    auto has_polygon_tangents() const -> bool;
    auto has_polygon_bitangents() const -> bool;
    auto has_corner_tangents() const -> bool;
    auto has_corner_bitangents() const -> bool;

    auto compute_tangents(const bool corner_tangents    = true,
                          const bool corner_bitangents  = true,
                          const bool polygon_tangents   = false,
                          const bool polygon_bitangents = false,
                          const bool make_polygons_flat = true,
                          const bool override_existing  = false) -> bool;

    auto generate_polygon_texture_coordinates(const bool overwrite_existing_texture_coordinates = false) -> bool;

    auto has_polygon_texture_coordinates() const -> bool;

    // Experimental
    void generate_texture_coordinates_spherical();

    template <typename T>
    void smooth_normalize(Property_map<Corner_id, T>&                corner_attribute,
                          const Property_map<Polygon_id, T>&         polygon_attribute,
                          const Property_map<Polygon_id, glm::vec3>& polygon_normals,
                          const float                                max_smoothing_angle_radians) const;

    template <typename T>
    void smooth_average(Property_map<Corner_id, T>&                smoothed_corner_attribute,
                        const Property_map<Corner_id, T>&          corner_attribute,
                        const Property_map<Corner_id, glm::vec3>&  corner_normals,
                        const Property_map<Polygon_id, glm::vec3>& point_normals) const;

    void sort_point_corners();

    void make_point_corners();

    void build_edges();

    auto has_edges() const -> bool;

    void transform(const glm::mat4& m);

    void reverse_polygons();

    void debug_trace() const;

    void merge(Geometry& other, const glm::mat4 transform);

    struct Weld_settings
    {
        float max_point_distance    {0.05f};
        float min_normal_dot_product{0.95f};
        float max_texcoord_distance {0.05f};
        float max_color_distance    {0.05f};
    };

    void weld(const Weld_settings& weld_settings);

    void sanity_check() const;

    auto volume() -> float;

    struct Corner_context
    {
        Corner_id corner_id;
        Corner&   corner;
        bool      break_{false};

        void break_iteration()
        {
            break_ = true;
        }
    };
    struct Corner_context_const
    {
        Corner_id     corner_id;
        const Corner& corner;
        bool          break_{false};

        void break_iteration()
        {
            break_ = true;
        }
    };
    struct Point_context
    {
        Point_id point_id;
        Point&   point;
        bool     break_{false};

        void break_iteration()
        {
            break_ = true;
        }
    };
    struct Point_context_const
    {
        Point_id     point_id;
        const Point& point;
        bool         break_{false};

        void break_iteration()
        {
            break_ = true;
        }
    };
    struct Polygon_context
    {
        Polygon_id polygon_id;
        Polygon&   polygon;
        bool       break_{false};

        void break_iteration()
        {
            break_ = true;
        }
    };
    struct Polygon_context_const
    {
        Polygon_id     polygon_id;
        const Polygon& polygon;
        bool           break_{false};

        void break_iteration()
        {
            break_ = true;
        }
    };
    struct Edge_context
    {
        Edge_id edge_id;
        Edge&   edge;
        bool    break_{false};

        void break_iteration()
        {
            break_ = true;
        }
    };

    struct Edge_context_const
    {
        Edge_id     edge_id;
        const Edge& edge;
        bool        break_{false};

        void break_iteration()
        {
            break_ = true;
        }
    };

    void for_each_corner       (std::function<void(Corner_context&       )> callback);
    void for_each_corner_const (std::function<void(Corner_context_const& )> callback) const;
    void for_each_point        (std::function<void(Point_context&        )> callback);
    void for_each_point_const  (std::function<void(Point_context_const&  )> callback) const;
    void for_each_polygon      (std::function<void(Polygon_context&      )> callback);
    void for_each_polygon_const(std::function<void(Polygon_context_const&)> callback) const;
    void for_each_edge         (std::function<void(Edge_context&         )> callback);
    void for_each_edge_const   (std::function<void(Edge_context_const&   )> callback) const;

    constexpr static size_t s_grow = 4096;
    Corner_id                       m_next_corner_id           {0};
    Point_id                        m_next_point_id            {0};
    Polygon_id                      m_next_polygon_id          {0};
    Edge_id                         m_next_edge_id             {0};
    Point_corner_id                 m_next_point_corner_reserve{0};
    Polygon_corner_id               m_next_polygon_corner_id   {0};
    Edge_polygon_id                 m_next_edge_polygon_id     {0};
    Polygon_id                      m_polygon_corner_polygon   {0};
    Edge_id                         m_edge_polygon_edge        {0};
    Point_property_map_collection   m_point_property_map_collection;
    Corner_property_map_collection  m_corner_property_map_collection;
    Polygon_property_map_collection m_polygon_property_map_collection;
    Edge_property_map_collection    m_edge_property_map_collection;
    uint64_t                        m_serial                            {1};
    uint64_t                        m_serial_edges                      {0};
    uint64_t                        m_serial_polygon_normals            {0};
    uint64_t                        m_serial_polygon_centroids          {0};
    uint64_t                        m_serial_polygon_tangents           {0};
    uint64_t                        m_serial_polygon_bitangents         {0};
    uint64_t                        m_serial_polygon_texture_coordinates{0};
    uint64_t                        m_serial_point_normals              {0};
    uint64_t                        m_serial_point_tangents             {0}; // never generated
    uint64_t                        m_serial_point_bitangents           {0}; // never generated
    uint64_t                        m_serial_point_texture_coordinates  {0}; // never generated
    uint64_t                        m_serial_smooth_point_normals       {0};
    uint64_t                        m_serial_corner_normals             {0}; 
    uint64_t                        m_serial_corner_tangents            {0};
    uint64_t                        m_serial_corner_bitangents          {0};
    uint64_t                        m_serial_corner_texture_coordinates {0}; 
};

} // namespace erhe::geometry

#include "corner.inl"
#include "polygon.inl"
#include "geometry.inl"
