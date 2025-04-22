#pragma once

#include <geogram/mesh/mesh.h>

#include <glm/glm.hpp>

#include <array>
#include <functional>
#include <memory>
#include <optional>

namespace spdlog {
    class logger;
}

namespace GEO {
    typedef Matrix<4, GEO::Numeric::float32> mat4f;

    inline float det(const mat4f& M) {
        return det4x4(
            M(0,0), M(0,1), M(0,2), M(0,3),
            M(1,0), M(1,1), M(1,2), M(1,3),
            M(2,0), M(2,1), M(2,2), M(2,3),
            M(3,0), M(3,1), M(3,2), M(3,3)
        );
    }

    inline mat4f create_scaling_matrix(float s) {
        mat4f result;
        result.load_identity();
        result(0,0) = s;
        result(1,1) = s;
        result(2,2) = s;
        return result;
    }

}

enum class Transform_mode : unsigned int {
    none = 0,                              // texture coordinates, colors, ...
    mat_mul_vec3_one,                      // position vectors
    mat_mul_vec3_zero,                     // transform using vec4(v, 0.0)
    normalize_mat_mul_vec3_zero,           // transform using vec4(v, 0.0), normalize
    normalize_mat_mul_vec3_zero_and_float, // tangent and bitangent with extra float that is not to be transformed
    normal_mat_mul_vec3_zero,              // normal - transform using normal matrix
    normalize_normal_mat_mul_vec3_zero     // normal - transform using normal matrix, then normalize
};

enum class Interpolation_mode : unsigned int {
    none = 0,
    linear,                // standard linear interpolation
    normalized,            // linear interpolation then normalize
    normalized_vec3_float, // linear interpolation then normalize for .xyz, linear for .w
};

class Attribute_descriptor
{
public:
    Attribute_descriptor(
        int                usage_index,
        const std::string& name,
        Transform_mode     transform_mode,
        Interpolation_mode interpolation_mode
    );

    int                usage_index       {0};
    std::string        name              {};
    Transform_mode     transform_mode    {Transform_mode::none};
    Interpolation_mode interpolation_mode{Interpolation_mode::none};
    std::string        present_name      {};
};

class Mesh_info
{
public:
    std::size_t facet_count                {0};
    std::size_t corner_count               {0};
    std::size_t triangle_count             {0};
    std::size_t edge_count                 {0};
    std::size_t vertex_count_corners       {0};
    std::size_t vertex_count_centroids     {0};
    std::size_t index_count_fill_triangles {0};
    std::size_t index_count_edge_lines     {0};
    std::size_t index_count_corner_points  {0};
    std::size_t index_count_centroid_points{0};

    void trace(const std::shared_ptr<spdlog::logger>& log) const;
};

class Mesh_serials
{
public:
    uint64_t serial                     {1};
    uint64_t edges                      {0};
    uint64_t facet_normals              {0};
    uint64_t facet_centroids            {0};
    uint64_t facet_tangents             {0};
    uint64_t facet_bitangents           {0};
    uint64_t facet_texture_coordinates  {0};
    uint64_t vertex_normals             {0};
    uint64_t vertex_tangents            {0}; // never generated
    uint64_t vertex_bitangents          {0}; // never generated
    uint64_t vertex_texture_coordinates {0}; // never generated
    uint64_t vertex_smooth_normals      {0};
    uint64_t corner_normals             {0};
    uint64_t corner_tangents            {0};
    uint64_t corner_bitangents          {0};
    uint64_t corner_texture_coordinates {0};
};

// Standard glTF attributes
static constexpr const char* c_normal          = "normal"         ;
static constexpr const char* c_tangent         = "tangent"        ;
static constexpr const char* c_bitangent       = "bitangent"      ;
static constexpr const char* c_texcoord_0      = "texcoord_0"     ;
static constexpr const char* c_texcoord_1      = "texcoord_1"     ;
static constexpr const char* c_color_0         = "color_0"        ;
static constexpr const char* c_color_1         = "color_1"        ;
static constexpr const char* c_joint_indices_0 = "joint_indices_0";
static constexpr const char* c_joint_indices_1 = "joint_indices_1";
static constexpr const char* c_joint_weights_0 = "joint_weights_0";
static constexpr const char* c_joint_weights_1 = "joint_weights_1";

// Extra attributes used by erhe
static constexpr const char* c_valency_edge_count = "valency_edge_count"; // vertex valency and edge count (per vertex)
static constexpr const char* c_id                 = "id"                ; // unique color for identifying facet
static constexpr const char* c_normal_smooth      = "normal_smooth"     ; // vertex normal always averaged from facets
static constexpr const char* c_centroid           = "centroid"          ; // centroid position for facet
static constexpr const char* c_aniso_control      = "aniso_control"     ; // controls anisotropy amount

// Anisotropy control:
// X is used to modulate anisotropy level:
//   0.0 -- Anisotropic
//   1.0 -- Isotropic when approaching texcoord (0, 0)
// Y is used for tangent space selection/control:
//   0.0 -- Use geometry T and B (from vertex attribute
//   1.0 -- Use T and B derived from texcoord - circular brushed metal effect

class Attribute_descriptors
{
public:
    static inline const Attribute_descriptor s_normal         { 0, c_normal         , Transform_mode::normalize_normal_mat_mul_vec3_zero,    Interpolation_mode::normalized };
    static inline const Attribute_descriptor s_tangent        { 0, c_tangent        , Transform_mode::normalize_mat_mul_vec3_zero_and_float, Interpolation_mode::normalized_vec3_float };
    static inline const Attribute_descriptor s_bitangent      { 0, c_bitangent      , Transform_mode::normalize_mat_mul_vec3_zero,           Interpolation_mode::normalized_vec3_float };
    static inline const Attribute_descriptor s_texcoord_0     { 0, c_texcoord_0     , Transform_mode::none,                                  Interpolation_mode::linear };
    static inline const Attribute_descriptor s_texcoord_1     { 1, c_texcoord_1     , Transform_mode::none,                                  Interpolation_mode::linear };
    static inline const Attribute_descriptor s_color_0        { 0, c_color_0        , Transform_mode::none,                                  Interpolation_mode::linear };
    static inline const Attribute_descriptor s_color_1        { 1, c_color_1        , Transform_mode::none,                                  Interpolation_mode::linear };
    static inline const Attribute_descriptor s_joint_indices_0{ 0, c_joint_indices_0, Transform_mode::none,                                  Interpolation_mode::none };
    static inline const Attribute_descriptor s_joint_indices_1{ 1, c_joint_indices_1, Transform_mode::none,                                  Interpolation_mode::none };
    static inline const Attribute_descriptor s_joint_weights_0{ 0, c_joint_weights_0, Transform_mode::none,                                  Interpolation_mode::none };
    static inline const Attribute_descriptor s_joint_weights_1{ 1, c_joint_weights_1, Transform_mode::none,                                  Interpolation_mode::none };

    static inline const Attribute_descriptor s_valency_edge_count{ 0, c_valency_edge_count, Transform_mode::none,                               Interpolation_mode::none };
    static inline const Attribute_descriptor s_id                { 0, c_id                , Transform_mode::none,                               Interpolation_mode::none };
    static inline const Attribute_descriptor s_normal_smooth     { 0, c_normal_smooth     , Transform_mode::normalize_normal_mat_mul_vec3_zero, Interpolation_mode::normalized };
    static inline const Attribute_descriptor s_centroid          { 0, c_centroid          , Transform_mode::mat_mul_vec3_one,                   Interpolation_mode::linear };
    static inline const Attribute_descriptor s_aniso_control     { 0, c_aniso_control     , Transform_mode::none,                               Interpolation_mode::linear };

    static void init  ();
    static void insert(const Attribute_descriptor& descriptor);
    static auto get   (int usage_index, const char* name) -> const Attribute_descriptor*;

private:
    static std::vector<Attribute_descriptor> s_descriptors;
};

template <typename T>
class Attribute_present
{
public:
    Attribute_present(GEO::AttributesManager& attributes_manager, const Attribute_descriptor& descriptor)
        : descriptor{descriptor}
        , attribute {attributes_manager, descriptor.name.c_str()}
        , present   {attributes_manager, descriptor.present_name.c_str()}
    {
    }

    void unbind()
    {
        if (attribute.is_bound()) {
            attribute.unbind();
        }
        if (present.is_bound()) {
            present.unbind();
        }
    }

    void bind(GEO::AttributesManager& attributes_manager)
    {
        if (!attribute.is_bound()) {
            attribute.bind(attributes_manager, descriptor.name.c_str());
        }
        if (!present.is_bound()) {
            present.bind(attributes_manager, descriptor.present_name.c_str());
        }
    }

    void clear()
    {
        present.fill(false);
    }
    void fill(T value)
    {
        attribute.fill(value);
        present.fill(true);
    }
    void set(GEO::index_t key, const T value)
    {
        attribute[key] = value;
        present[key] = true;
    }
    auto has(GEO::index_t key) const
    {
        if ((key >= present.size()) || (key >= attribute.size())) {
            return false;
        }
        bool result = present[key];
        return result;
    }
    auto get(GEO::index_t key) const -> T
    {
        geo_assert(present[key]);
        return attribute[key];
    }
    auto try_get(GEO::index_t key) const -> std::optional<T>
    {
        if ((key >= present.size()) || (key >= attribute.size())) {
            return std::optional<T>{};
        }
        return present[key] ? attribute[key] : std::optional<T>{};
    }

    const Attribute_descriptor& descriptor;
    GEO::Attribute<T>           attribute;
    GEO::Attribute<bool>        present;
};

class Mesh_attributes
{
public:
    Mesh_attributes(const GEO::Mesh& mesh);
    ~Mesh_attributes();

    void unbind();
    void bind();

    const GEO::Mesh& m_mesh;
    Attribute_present<GEO::vec3f> facet_id                 ;
    Attribute_present<GEO::vec3f> facet_centroid           ;
    Attribute_present<GEO::vec3f> facet_normal             ;
    Attribute_present<GEO::vec4f> facet_tangent            ;
    Attribute_present<GEO::vec3f> facet_bitangent          ;
    Attribute_present<GEO::vec4f> facet_color_0            ;
    Attribute_present<GEO::vec4f> facet_color_1            ;
    Attribute_present<GEO::vec2f> facet_aniso_control      ;
    Attribute_present<GEO::vec3f> vertex_normal            ;
    Attribute_present<GEO::vec3f> vertex_normal_smooth     ;
    Attribute_present<GEO::vec2f> vertex_texcoord_0        ;
    Attribute_present<GEO::vec2f> vertex_texcoord_1        ;
    Attribute_present<GEO::vec4f> vertex_tangent           ;
    Attribute_present<GEO::vec3f> vertex_bitangent         ;
    Attribute_present<GEO::vec4f> vertex_color_0           ;
    Attribute_present<GEO::vec4f> vertex_color_1           ;
    Attribute_present<GEO::vec4u> vertex_joint_indices_0   ;
    Attribute_present<GEO::vec4u> vertex_joint_indices_1   ;
    Attribute_present<GEO::vec4f> vertex_joint_weights_0   ;
    Attribute_present<GEO::vec4f> vertex_joint_weights_1   ;
    Attribute_present<GEO::vec2f> vertex_aniso_control     ;
    Attribute_present<GEO::vec2i> vertex_valency_edge_count;
    Attribute_present<GEO::vec3f> corner_normal            ;
    Attribute_present<GEO::vec2f> corner_texcoord_0        ;
    Attribute_present<GEO::vec2f> corner_texcoord_1        ;
    Attribute_present<GEO::vec4f> corner_tangent           ;
    Attribute_present<GEO::vec3f> corner_bitangent         ;
    Attribute_present<GEO::vec4f> corner_color_0           ;
    Attribute_present<GEO::vec4f> corner_color_1           ;
    Attribute_present<GEO::vec2f> corner_aniso_control     ;
    inline auto facet_color         (size_t i) -> Attribute_present<GEO::vec4f>& { return (i == 0) ? facet_color_0          : facet_color_1         ; }
    inline auto vertex_texcoord     (size_t i) -> Attribute_present<GEO::vec2f>& { return (i == 0) ? vertex_texcoord_0      : vertex_texcoord_1     ; }
    inline auto vertex_color        (size_t i) -> Attribute_present<GEO::vec4f>& { return (i == 0) ? vertex_color_0         : vertex_color_1        ; }
    inline auto vertex_joint_indices(size_t i) -> Attribute_present<GEO::vec4u>& { return (i == 0) ? vertex_joint_indices_0 : vertex_joint_indices_1; }
    inline auto vertex_joint_weights(size_t i) -> Attribute_present<GEO::vec4f>& { return (i == 0) ? vertex_joint_weights_0 : vertex_joint_weights_1; }
    inline auto corner_texcoord     (size_t i) -> Attribute_present<GEO::vec2f>& { return (i == 0) ? corner_texcoord_0      : corner_texcoord_1     ; }
    inline auto corner_color        (size_t i) -> Attribute_present<GEO::vec4f>& { return (i == 0) ? corner_color_0         : corner_color_1        ; }
};

[[nodiscard]] auto count_mesh_facet_triangles(const GEO::Mesh& mesh) -> std::size_t;
[[nodiscard]] auto get_mesh_info             (const GEO::Mesh& mesh) -> Mesh_info;
void transform_mesh(
    const GEO::Mesh&       source_mesh,
    const Mesh_attributes& source_attributes,
    GEO::Mesh&             destination_mesh,
    Mesh_attributes&       destination_attributes,
    const GEO::mat4f&      transform
);
void compute_facet_normals                   (GEO::Mesh& mesh, Mesh_attributes& attributes);
void compute_facet_centroids                 (GEO::Mesh& mesh, Mesh_attributes& attributes);
void compute_mesh_vertex_normal_smooth       (GEO::Mesh& mesh, Mesh_attributes& attributes);
auto compute_mesh_tangents                   (GEO::Mesh& mesh, bool make_facets_flat) -> bool;
void generate_mesh_facet_texture_coordinates (GEO::Mesh& mesh, GEO::index_t facet, Mesh_attributes& attributes);
void generate_mesh_facet_texture_coordinates (GEO::Mesh& mesh, Mesh_attributes& attributes);

[[nodiscard]] inline auto min_axis(const GEO::vec3f v) -> GEO::vec3f
{
    return
        (std::abs(v.x) <= std::abs(v.y)) && (std::abs(v.x) <= std::abs(v.z)) ? GEO::vec3f{1.0f, 0.0f, 0.0f} :
        (std::abs(v.y) <= std::abs(v.x)) && (std::abs(v.y) <= std::abs(v.z)) ? GEO::vec3f{0.0f, 1.0f, 0.0f} :
                                                                               GEO::vec3f{0.0f, 0.0f, 1.0f};
}

[[nodiscard]] inline auto max_axis_index(const GEO::vec3 v) -> GEO::index_t
{
    return 
        (std::abs(v.x) >= std::abs(v.y)) && (std::abs(v.x) >= std::abs(v.z)) ? 0 :
        (std::abs(v.y) >= std::abs(v.x)) && (std::abs(v.y) >= std::abs(v.z)) ? 1 :
                                                                               2;
}

[[nodiscard]] auto inline safe_normalize_cross(const GEO::vec3f& lhs, const GEO::vec3f& rhs) -> GEO::vec3f
{
    const float d = GEO::dot(lhs, rhs);
    if (std::abs(d) > 0.999f) {
        return min_axis(lhs);
    }

    const GEO::vec3f c0 = GEO::cross(lhs, rhs);
    if (GEO::length(c0) < std::numeric_limits<float>::epsilon()) {
        return min_axis(lhs);
    }
    return GEO::normalize(c0);
}

[[nodiscard]] inline auto project(const GEO::vec3f& a, const GEO::vec3f& b) -> const GEO::vec3f
{
	return GEO::dot(a, b) / GEO::dot(b, b) * b;
}

inline void gram_schmidt(const GEO::vec3f& a, const GEO::vec3f& b, const GEO::vec3f& c, GEO::vec3f& out_a, GEO::vec3f& out_b, GEO::vec3f& out_c)
{
    out_a = a;
    out_b = b - project(b, a);
    out_c = c - project(c, out_b) - project(c, out_a);
    out_a = GEO::normalize(out_a);
    out_b = GEO::normalize(out_b);
    out_c = GEO::normalize(out_c);
}

[[nodiscard]] inline auto vec3_from_index(const GEO::index_t i) -> GEO::vec3f
{
    const GEO::index_t r = (i >> 16u) & 0xffu;
    const GEO::index_t g = (i >>  8u) & 0xffu;
    const GEO::index_t b = (i >>  0u) & 0xffu;

    return GEO::vec3f{
        r / 255.0f,
        g / 255.0f,
        b / 255.0f
    };
}

[[nodiscard]] inline auto to_geo_vec3f(glm::vec3 v) -> GEO::vec3f
{
    return GEO::vec3f{v.x, v.y, v.z};
}

[[nodiscard]] inline auto to_geo_vec3(glm::vec3 v) -> GEO::vec3
{
    return GEO::vec3{v.x, v.y, v.z};
}

[[nodiscard]] inline auto to_geo_vec3i(glm::ivec3 v) -> GEO::vec3i
{
    return GEO::vec3i{v.x, v.y, v.z};
}

[[nodiscard]] inline auto to_glm_vec4(GEO::vec4f v) -> glm::vec4
{
    return glm::vec4{v.x, v.y, v.z, v.w};
}

[[nodiscard]] inline auto to_glm_vec3(GEO::vec3f v) -> glm::vec3
{
    return glm::vec3{v.x, v.y, v.z};
}

[[nodiscard]] inline auto to_glm_vec3(GEO::vec3 v) -> glm::vec3
{
    return glm::vec3{v.x, v.y, v.z};
}

[[nodiscard]] inline auto to_geo_vec4(GEO::vec4f v) -> glm::vec4
{
    return glm::vec4{v.x, v.y, v.z, v.w};
}

// GEO::mat4::operator() (index_t i, index_t j)  i = row, j = column
// glm::mat4::operator[i] i = column
[[nodiscard]] inline auto to_geo_mat4(const glm::mat4& m) -> GEO::mat4
{
    glm::vec4 column_0 = m[0];
    glm::vec4 column_1 = m[1];
    glm::vec4 column_2 = m[2];
    glm::vec4 column_3 = m[3];
    return GEO::mat4{
        { column_0[0], column_1[0], column_2[0], column_3[0] },
        { column_0[1], column_1[1], column_2[1], column_3[1] },
        { column_0[2], column_1[2], column_2[2], column_3[2] },
        { column_0[3], column_1[3], column_2[3], column_3[3] }
    };
}

[[nodiscard]] inline auto to_geo_mat4f(const glm::mat4& m) -> GEO::mat4f
{
    glm::vec4 column_0 = m[0];
    glm::vec4 column_1 = m[1];
    glm::vec4 column_2 = m[2];
    glm::vec4 column_3 = m[3];
    return GEO::mat4f{
        { column_0[0], column_1[0], column_2[0], column_3[0] },
        { column_0[1], column_1[1], column_2[1], column_3[1] },
        { column_0[2], column_1[2], column_2[2], column_3[2] },
        { column_0[3], column_1[3], column_2[3], column_3[3] }
    };
}

[[nodiscard]] inline auto to_glm_mat4(const GEO::mat4& m) -> glm::mat4
{
    glm::vec4 column_0{m(0, 0), m(1, 0), m(2, 0), m(3, 0)};
    glm::vec4 column_1{m(0, 1), m(1, 1), m(2, 1), m(3, 1)};
    glm::vec4 column_2{m(0, 2), m(1, 2), m(2, 2), m(3, 2)};
    glm::vec4 column_3{m(0, 3), m(1, 3), m(2, 3), m(3, 3)};
    return glm::mat4{column_0, column_1, column_2, column_3};
}

[[nodiscard]] inline auto to_glm_mat4(const GEO::mat4f& m) -> glm::mat4
{
    glm::vec4 column_0{m(0, 0), m(1, 0), m(2, 0), m(3, 0)};
    glm::vec4 column_1{m(0, 1), m(1, 1), m(2, 1), m(3, 1)};
    glm::vec4 column_2{m(0, 2), m(1, 2), m(2, 2), m(3, 2)};
    glm::vec4 column_3{m(0, 3), m(1, 3), m(2, 3), m(3, 3)};
    return glm::mat4{column_0, column_1, column_2, column_3};
}

[[nodiscard]] auto make_convex_hull(const GEO::Mesh& source, GEO::Mesh& destination) -> bool;

template <typename T>
inline void interpolate_attribute(
    const Attribute_present<T>&                                     source_,
    Attribute_present<T>&                                           destination_,
    const std::vector<std::vector<std::pair<float, GEO::index_t>>>& key_dst_to_src
) 
{
    const GEO::Attribute<T>&    source              = source_.attribute;
    const GEO::Attribute<bool>& source_present      = source_.present;
    const Attribute_descriptor& source_descriptor   = source_.descriptor;
    GEO::Attribute<T>&          destination         = destination_.attribute;
    GEO::Attribute<bool>&       destination_present = destination_.present;

    if (source_descriptor.interpolation_mode == Interpolation_mode::none) {
        return;
    }

    for (GEO::index_t dst_key = 0, end = static_cast<GEO::index_t>(key_dst_to_src.size()); dst_key < end; ++dst_key) {
        const std::vector<std::pair<float, GEO::index_t>>& src_keys = key_dst_to_src[dst_key];
        float sum_weights{0.0f};
        for (auto j : src_keys) {
            const GEO::index_t src_key = j.second;
            if (!source_present[src_key]) {
                continue;
            }
            sum_weights += j.first;
        }

        if (sum_weights == 0.0f) {
            continue;
        }

        T dst_value{0};

        if constexpr (!std::is_same_v<T, GEO::vec4u>) { // std::is_same_v<T::value_type, Numeric::uint32> ?
            for (auto j : src_keys) {
                const GEO::index_t src_key = j.second;
                if (!source_present[src_key]) {
                    continue;
                }

                const float weight    = j.first;
                const T     src_value = source[src_key];
                dst_value += static_cast<T>((weight / sum_weights) * static_cast<T>(src_value));
            }
        }

        constexpr bool is_vec3 = std::is_same_v<T, GEO::vec3> || std::is_same_v<T, GEO::vec3f>; // T::dim == 3 ?
        if constexpr (is_vec3) {
            if (source_descriptor.interpolation_mode == Interpolation_mode::normalized) {
                dst_value = GEO::normalize(dst_value);
            }
        }

        constexpr bool is_vec4 = std::is_same_v<T, GEO::vec4> || std::is_same_v<T, GEO::vec4f>; // T::dim == 4 ?
        if constexpr (is_vec4) {
            if (source_descriptor.interpolation_mode == Interpolation_mode::normalized_vec3_float) {
                using T_vec3       = GEO::vecng<3, typename T::value_type>;
                using value_type   = T::value_type;
                const value_type x = dst_value[0];
                const value_type y = dst_value[1];
                const value_type z = dst_value[2];
                const T_vec3 vec3_value{x, y, z};
                const T_vec3 vec3_normalized = GEO::normalize(vec3_value);
                dst_value = T{vec3_normalized.x, vec3_normalized.y, vec3_normalized.z, dst_value.w};
            }
        }

        destination[dst_key] = dst_value;
        destination_present[dst_key] = true;
    }
}

template <typename T>
inline void copy_attribute(const Attribute_present<T>& source, Attribute_present<T>& destination)
{
    destination.attribute.copy(source.attribute);
    destination.present.copy(source.present);
}

template <typename T> struct attribute_transform_traits             { static const bool is_transformable = false; };
template <>           struct attribute_transform_traits<GEO::vec2f> { static const bool is_transformable = true;  };
template <>           struct attribute_transform_traits<GEO::vec3f> { static const bool is_transformable = true;  };
template <>           struct attribute_transform_traits<GEO::vec4f> { static const bool is_transformable = true;  };
template <>           struct attribute_transform_traits<GEO::vec2u> { static const bool is_transformable = false; };
template <>           struct attribute_transform_traits<GEO::vec3u> { static const bool is_transformable = false; };
template <>           struct attribute_transform_traits<GEO::vec4u> { static const bool is_transformable = false; };
template <>           struct attribute_transform_traits<GEO::vec2i> { static const bool is_transformable = false; };
template <>           struct attribute_transform_traits<GEO::vec3i> { static const bool is_transformable = false; };
template <>           struct attribute_transform_traits<GEO::vec4i> { static const bool is_transformable = false; };

static inline auto apply_transform(const GEO::mat4f transform, const float value, const float w) -> float
{
    const GEO::vec4f result4 = transform * GEO::vec4f{value, 0.0f, 0.0f, w};
    return result4.x;
}

static inline auto apply_transform(const GEO::mat4f transform, const GEO::vec2f value, const float w) -> GEO::vec2f
{
    const GEO::vec4f result4 = transform * GEO::vec4f{value.x, value.y, 0.0f, w};
    return GEO::vec2f{result4.x, result4.y};
}

static inline auto apply_transform(const GEO::mat4f transform, const GEO::vec3f value, const float w) -> GEO::vec3f
{
    const GEO::vec4f result4 = transform * GEO::vec4f{value.x, value.y, value.z, w};
    return GEO::vec3f{result4.x, result4.y, result4.z};
}

static inline auto apply_transform(const GEO::mat4f transform, const GEO::vec4f value, float) -> GEO::vec4f
{
    return transform * value;
}

template <typename T>
inline void transform_attribute(
    const Attribute_present<T>& source_,
    Attribute_present<T>&       destination_,
    const GEO::mat4f&           transform
) 
{
    const GEO::Attribute<T>&    source              = source_.attribute;
    const GEO::Attribute<bool>& source_present      = source_.present;
    const Attribute_descriptor& source_descriptor   = source_.descriptor;
    GEO::Attribute<T>&          destination         = destination_.attribute;
    GEO::Attribute<bool>&       destination_present = destination_.present;

    // TODO use adjugate for normal_transform
    GEO::mat4f inverse;
    transform.compute_inverse(inverse);
    const GEO::mat4f normal_transform = inverse.transpose();

    if constexpr(attribute_transform_traits<T>::is_transformable) switch (source_descriptor.transform_mode) {
        //using enum Transform_mode;
        default:
        case Transform_mode::none: {
            if (&source_ != &destination_) {
                destination.copy(source);
                destination_present.copy(source_present);
            }
            break;
        }

        case Transform_mode::mat_mul_vec3_one: {
            for (GEO::index_t key = 0, end = source.size(); key < end; ++key) {
                destination_present[key] = source_present[key];
                if (source_present[key]) {
                    destination[key] = apply_transform(transform, source[key], 1.0f);
                }
            }
            break;
        }

        case Transform_mode::mat_mul_vec3_zero: {
            for (GEO::index_t key = 0, end = source.size(); key < end; ++key) {
                destination_present[key] = source_present[key];
                if (source_present[key]) {
                    destination[key] = apply_transform(transform, source[key], 0.0f);
                }
            }
            break;
        }

        case Transform_mode::normalize_mat_mul_vec3_zero: {
            for (GEO::index_t key = 0, end = source.size(); key < end; ++key) {
                destination_present[key] = source_present[key];
                if (source_present[key]) {
                    destination[key] = GEO::normalize(apply_transform(transform, source[key], 0.0f));
                }
            }
            break;
        }

        case Transform_mode::normalize_mat_mul_vec3_zero_and_float: {
            if constexpr (std::is_same_v<T, GEO::vec4f>) {
                for (GEO::index_t key = 0, end = source.size(); key < end; ++key) {
                    destination_present[key] = source_present[key];
                    if (source_present[key]) {
                        const GEO::vec4f source_value4 = source[key];
                        const GEO::vec3f source_value3{source_value4.x, source_value4.y, source_value4.z};
                        const GEO::vec3f transformed = apply_transform(transform, source_value3, 0.0f);
                        const GEO::vec3f normalized  = GEO::normalize(transformed);
                        destination[key] = GEO::vec4f{normalized.x, normalized.y, normalized.z, source_value4.w};
                    }
                }
            } else {
                geo_assert(false);
            }
            break;
        }

        case Transform_mode::normal_mat_mul_vec3_zero: {
            for (GEO::index_t key = 0, end = source.size(); key < end; ++key) {
                destination_present[key] = source_present[key];
                if (source_present[key]) {
                    destination[key] = apply_transform(normal_transform, source[key], 0.0f);
                }
            }
            break;
        }

        case Transform_mode::normalize_normal_mat_mul_vec3_zero: {
            for (GEO::index_t key = 0, end = source.size(); key < end; ++key) {
                destination_present[key] = source_present[key];
                if (source_present[key]) {
                    const T source_value = source[key];
                    const T transformed  = apply_transform(normal_transform, source_value, 0.0f);
                    const T normalized   = GEO::normalize(transformed);
                    destination[key] = normalized;
                }
            }
            break;
        }
    } else {
        if (&source_ != &destination_) {
            destination.copy(source);
            destination_present.copy(source_present);
        }
    }
}

void copy_attributes(const Mesh_attributes& source, Mesh_attributes& destination);
void transform_mesh(const GEO::Mesh& source_mesh, GEO::Mesh& destination_mesh, const GEO::mat4f& transform);
void transform_mesh(GEO::Mesh& mesh, const GEO::mat4f& transform);

using pos_scalar = float;
using pos_vec3 = GEO::vec3f;
static constexpr bool pos_float = true;

void set_point (GEO::MeshVertices& mesh_vertices, GEO::index_t vertex, GEO::vec3 p);
void set_pointf(GEO::MeshVertices& mesh_vertices, GEO::index_t vertex, GEO::vec3f p);
auto get_point (const GEO::MeshVertices& mesh_vertices, GEO::index_t vertex) -> GEO::vec3;
auto get_pointf(const GEO::MeshVertices& mesh_vertices, GEO::index_t vertex) -> GEO::vec3f;

auto mesh_facet_normalf(const GEO::Mesh& M, GEO::index_t f) -> GEO::vec3f;
auto mesh_facet_centerf(const GEO::Mesh& M, GEO::index_t f) -> GEO::vec3f;

namespace erhe::geometry {

class Geometry
{
public:
    Geometry();
    explicit Geometry(std::string_view name);

    [[nodiscard]] auto get_name          () const -> const std::string&;
    [[nodiscard]] auto get_mesh          () -> GEO::Mesh&;
    [[nodiscard]] auto get_mesh          () const -> const GEO::Mesh&;
    [[nodiscard]] auto get_vertex_corners(GEO::index_t vertex) const -> const std::vector<GEO::index_t>&;
    [[nodiscard]] auto get_vertex_edges  (GEO::index_t vertex) const -> const std::vector<GEO::index_t>&;
    [[nodiscard]] auto get_corner_facet  (GEO::index_t corner) const -> GEO::index_t;
    [[nodiscard]] auto get_edge_facets   (GEO::index_t edge) const -> const std::vector<GEO::index_t>&;
    [[nodiscard]] auto get_edge          (GEO::index_t v0, GEO::index_t v1) const -> GEO::index_t;
    [[nodiscard]] auto get_attributes    () -> Mesh_attributes&;
    [[nodiscard]] auto get_attributes    () const -> const Mesh_attributes&;

    void merge_with_transform(const Geometry& src, const GEO::mat4f& transform);
    void copy_with_transform(const Geometry& source, const GEO::mat4f& transform);

    void set_name(std::string_view name);

    static constexpr uint64_t process_flag_connect                            = (1u << 0u);
    static constexpr uint64_t process_flag_build_edges                        = (1u << 1u);
    static constexpr uint64_t process_flag_compute_facet_centroids            = (1u << 2u);
    static constexpr uint64_t process_flag_compute_smooth_vertex_normals      = (1u << 3u);
    static constexpr uint64_t process_flag_generate_facet_texture_coordinates = (1u << 4u);
    static constexpr uint64_t process_flag_debug_trace                        = (1u << 5u);
    static constexpr uint64_t process_flag_merge_coplanar_neighbors           = (1u << 6u);

    void process(uint64_t flags);
    void generate_mesh_facet_texture_coordinates();
    void build_edges();
    void update_connectivity();
    void merge_coplanar_neighbors();

    void debug_trace() const;

    struct Debug_text
    {
        GEO::index_t reference_vertex;
        GEO::index_t reference_facet;
        glm::vec3 position;
        uint32_t color;
        std::string text;
    };
    struct Debug_vertex
    {
        glm::vec3 position;
        glm::vec4 color;
        float width;
    };
    struct Debug_line
    {
        GEO::index_t reference_vertex;
        GEO::index_t reference_facet;
        std::array<Debug_vertex, 2> vertices;
    };

    void clear_debug();
    void add_debug_text(GEO::index_t reference_vertex, GEO::index_t reference_facet, glm::vec3 position, uint32_t color, std::string_view text) const;
    void add_debug_line(GEO::index_t reference_vertex, GEO::index_t reference_facet, glm::vec3 p0, glm::vec3 p1, glm::vec4 color, float width) const;
    void access_debug_entries(std::function<void(std::vector<Debug_text>& debug_texts, std::vector<Debug_line>& debug_lines)> op);

private:
    class Edge_collapse_context
    {
    public:
        inline auto is_edge_facet(GEO::index_t facet) -> bool {
            return std::find(edge_facets.begin(), edge_facets.end(), facet) != edge_facets.end();
        };
        inline auto is_started(GEO::index_t facet) -> bool {
            return std::find(facets_to_delete.begin(), facets_to_delete.end(), facet) != facets_to_delete.end();
        };

        GEO::index_t                     edge;
        GEO::index_t                     v0;
        GEO::index_t                     v1;
        GEO::vector<GEO::index_t>&       facets_to_delete;
        std::vector<GEO::index_t>        merged_face_corners;
        const std::vector<GEO::index_t>& edge_facets;
    };
    void collect_corners_from_facet(Edge_collapse_context& edge_collapse_context, GEO::index_t facet, std::optional<GEO::index_t> trigger_vertex);

    GEO::Mesh                              m_mesh;
    Mesh_attributes                        m_attributes;
    std::string                            m_name;
    std::vector<std::vector<GEO::index_t>> m_vertex_to_corners;
    std::vector<GEO::index_t>              m_corner_to_facet;
    std::vector<std::vector<GEO::index_t>> m_edge_to_facets;
    std::vector<std::vector<GEO::index_t>> m_vertex_to_edges;

    struct Edge_hash
    {
        std::size_t operator()(const std::pair<GEO::index_t, GEO::index_t>& edge) const
        {
            return
                std::hash<GEO::index_t>()(edge.first) ^
                std::hash<GEO::index_t>()(edge.second ^ 0xcafecafe);
        }
    };

    std::unordered_map<std::pair<GEO::index_t, GEO::index_t>, GEO::index_t, Edge_hash> m_vertex_pair_to_edge;

    mutable std::vector<Debug_text> m_debug_texts;
    mutable std::vector<Debug_line> m_debug_lines;
};

void transform(const Geometry& source, Geometry& destination, const GEO::mat4f& transform);

}
