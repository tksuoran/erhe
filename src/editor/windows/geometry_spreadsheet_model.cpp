#include "windows/geometry_spreadsheet_model.hpp"
#include "operations/set_geometry_attribute_operation.hpp"

#include "erhe_geometry/geometry.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_verify/verify.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <charconv>
#include <cstring>
#include <type_traits>

namespace editor {

namespace {

// Value type and component count of an Attribute_present<T> value type.
template <typename T>
class Attribute_value_traits;

template <>
class Attribute_value_traits<float>
{
public:
    static constexpr Spreadsheet_value_type value_type     {Spreadsheet_value_type::f32};
    static constexpr std::size_t            component_count{1};
};

template <>
class Attribute_value_traits<GEO::vec2f>
{
public:
    static constexpr Spreadsheet_value_type value_type     {Spreadsheet_value_type::f32};
    static constexpr std::size_t            component_count{2};
};

template <>
class Attribute_value_traits<GEO::vec3f>
{
public:
    static constexpr Spreadsheet_value_type value_type     {Spreadsheet_value_type::f32};
    static constexpr std::size_t            component_count{3};
};

template <>
class Attribute_value_traits<GEO::vec4f>
{
public:
    static constexpr Spreadsheet_value_type value_type     {Spreadsheet_value_type::f32};
    static constexpr std::size_t            component_count{4};
};

template <>
class Attribute_value_traits<GEO::vec4u>
{
public:
    static constexpr Spreadsheet_value_type value_type     {Spreadsheet_value_type::u32};
    static constexpr std::size_t            component_count{4};
};

template <>
class Attribute_value_traits<GEO::vec2i>
{
public:
    static constexpr Spreadsheet_value_type value_type     {Spreadsheet_value_type::i32};
    static constexpr std::size_t            component_count{2};
};

template <typename T>
auto read_attribute_component(const void* attribute, const GEO::index_t element, const std::size_t component, double& out_value) -> bool
{
    const erhe::geometry::Attribute_present<T>& attribute_present = *static_cast<const erhe::geometry::Attribute_present<T>*>(attribute);
    if (!attribute_present.has(element)) {
        return false;
    }
    const T value = attribute_present.get(element);
    if constexpr (std::is_arithmetic_v<T>) {
        static_cast<void>(component);
        out_value = static_cast<double>(value);
    } else {
        out_value = static_cast<double>(value[static_cast<GEO::index_t>(component)]);
    }
    return true;
}

template <typename T>
auto has_any_present(const erhe::geometry::Attribute_present<T>& attribute_present, const std::size_t element_count) -> bool
{
    for (GEO::index_t element = 0; element < element_count; ++element) {
        if (attribute_present.has(element)) {
            return true;
        }
    }
    return false;
}

// Column label suffix for component `component` of attribute `name`.
auto component_suffix(const std::string_view name, const std::size_t component, const std::size_t component_count) -> const char*
{
    static constexpr const char* c_xyzw [] = { "x", "y", "z", "w" };
    static constexpr const char* c_rgba [] = { "r", "g", "b", "a" };
    static constexpr const char* c_uv   [] = { "u", "v" };
    static constexpr const char* c_index[] = { "0", "1", "2", "3" };
    if (component_count == 1) {
        return nullptr;
    }
    if (name.find("color") != std::string_view::npos) {
        return c_rgba[component];
    }
    if ((name.find("texcoord") != std::string_view::npos) && (component < 2)) {
        return c_uv[component];
    }
    if (name.find("joint") != std::string_view::npos) {
        return c_index[component];
    }
    if (name.find("valency_edge_count") != std::string_view::npos) {
        static constexpr const char* c_valency[] = { "valency", "edges" };
        return c_valency[component];
    }
    return c_xyzw[component];
}

// The attribute name without its domain prefix: the tab already names the domain.
auto strip_domain_prefix(const std::string_view name) -> std::string_view
{
    for (const std::string_view prefix : { std::string_view{"vertex_"}, std::string_view{"corner_"}, std::string_view{"facet_"}, std::string_view{"edge_"} }) {
        if (name.starts_with(prefix)) {
            return name.substr(prefix.size());
        }
    }
    return name;
}

void set_label(Spreadsheet_column& column, const std::string_view base, const char* suffix)
{
    const std::size_t capacity = column.label.size() - 1;
    const fmt::format_to_n_result<char*> result = (suffix != nullptr)
        ? fmt::format_to_n(column.label.data(), capacity, "{}.{}", base, suffix)
        : fmt::format_to_n(column.label.data(), capacity, "{}", base);
    *result.out = '\0';
}

auto make_structural_column(
    const Spreadsheet_column_kind kind,
    const Spreadsheet_value_type  value_type,
    const std::string_view        base,
    const char*                   suffix,
    const std::size_t             component,
    const std::size_t             component_count,
    const Spreadsheet_edit        edit
) -> Spreadsheet_column
{
    Spreadsheet_column column{
        .kind            = kind,
        .value_type      = value_type,
        .component       = component,
        .component_count = component_count,
        .edit            = edit
    };
    set_label(column, base, suffix);
    return column;
}

auto format_value(const double value, const Spreadsheet_value_type value_type, const int precision, const std::span<char> buffer) -> std::string_view
{
    char* const first = buffer.data();
    char* const last  = buffer.data() + buffer.size();
    std::to_chars_result result{};
    switch (value_type) {
        case Spreadsheet_value_type::u32: {
            result = std::to_chars(first, last, static_cast<uint32_t>(value));
            break;
        }
        case Spreadsheet_value_type::i32: {
            result = std::to_chars(first, last, static_cast<int32_t>(value));
            break;
        }
        case Spreadsheet_value_type::f32:
        default: {
            const float f = static_cast<float>(value);
            if (precision <= 0) {
                result = std::to_chars(first, last, f);
            } else {
                result = std::to_chars(first, last, f, std::chars_format::fixed, precision);
                if (result.ec != std::errc{}) {
                    // A magnitude too large for fixed notation in the buffer.
                    result = std::to_chars(first, last, f, std::chars_format::scientific, precision);
                }
            }
            break;
        }
    }
    if (result.ec != std::errc{}) {
        return std::string_view{"?"};
    }
    return std::string_view{first, static_cast<std::size_t>(result.ptr - first)};
}

} // anonymous namespace

auto c_str(const Spreadsheet_domain domain) -> const char*
{
    switch (domain) {
        case Spreadsheet_domain::vertex: return "Vertex";
        case Spreadsheet_domain::corner: return "Corner";
        case Spreadsheet_domain::facet:  return "Facet";
        case Spreadsheet_domain::edge:   return "Edge";
        default:                         return "?";
    }
}

auto get_domain_element_count(const erhe::geometry::Geometry& geometry, const Spreadsheet_domain domain) -> std::size_t
{
    const GEO::Mesh& mesh = geometry.get_mesh();
    switch (domain) {
        case Spreadsheet_domain::vertex: return mesh.vertices.nb();
        case Spreadsheet_domain::corner: return mesh.facet_corners.nb();
        case Spreadsheet_domain::facet:  return mesh.facets.nb();
        case Spreadsheet_domain::edge:   return mesh.edges.nb();
        default:                         return 0;
    }
}

auto Geometry_spreadsheet_model::set_geometry(const std::shared_ptr<erhe::geometry::Geometry>& geometry) -> bool
{
    if (geometry == m_geometry) {
        return false;
    }
    m_geometry = geometry;
    invalidate();
    return true;
}

auto Geometry_spreadsheet_model::get_geometry() const -> const std::shared_ptr<erhe::geometry::Geometry>&
{
    return m_geometry;
}

void Geometry_spreadsheet_model::invalidate()
{
    for (Domain_cache& cache : m_domains) {
        cache.layout_valid = false;
        cache.rows_valid   = false;
    }
}

void Geometry_spreadsheet_model::release()
{
    m_geometry.reset();
    std::vector<Sort_key>{}.swap(m_sort_keys);
    for (Domain_cache& cache : m_domains) {
        std::vector<Spreadsheet_column>{}.swap(cache.columns);
        std::vector<GEO::index_t>{}.swap(cache.rows);
        std::vector<GEO::index_t>{}.swap(cache.filter);
        cache.filter_enabled = false;
        cache.rows_identity  = true;
        cache.element_count = 0;
        cache.layout_valid  = false;
        cache.rows_valid    = false;
    }
}

void Geometry_spreadsheet_model::set_precision(const int precision)
{
    m_precision = std::clamp(precision, 0, 9);
}

auto Geometry_spreadsheet_model::get_precision() const -> int
{
    return m_precision;
}

void Geometry_spreadsheet_model::set_sort(const Spreadsheet_domain domain, const int column, const Sort_direction direction)
{
    Domain_cache& cache = m_domains[static_cast<std::size_t>(domain)];
    if ((cache.sort_column == column) && (cache.sort_direction == direction)) {
        return;
    }
    cache.sort_column    = column;
    cache.sort_direction = direction;
    cache.rows_valid     = false;
}

void Geometry_spreadsheet_model::set_row_filter(const Spreadsheet_domain domain, const std::span<const GEO::index_t> elements)
{
    Domain_cache& cache = m_domains[static_cast<std::size_t>(domain)];
    cache.filter.assign(elements.begin(), elements.end());
    cache.filter_enabled = true;
    cache.rows_valid     = false;
}

void Geometry_spreadsheet_model::clear_row_filter(const Spreadsheet_domain domain)
{
    Domain_cache& cache = m_domains[static_cast<std::size_t>(domain)];
    if (!cache.filter_enabled) {
        return;
    }
    cache.filter.clear();
    cache.filter_enabled = false;
    cache.rows_valid     = false;
}

auto Geometry_spreadsheet_model::has_row_filter(const Spreadsheet_domain domain) const -> bool
{
    return m_domains[static_cast<std::size_t>(domain)].filter_enabled;
}

void Geometry_spreadsheet_model::update(const Spreadsheet_domain domain)
{
    Domain_cache& cache = m_domains[static_cast<std::size_t>(domain)];
    if (!m_geometry) {
        cache.columns.clear();
        cache.rows.clear();
        cache.rows_identity = true;
        cache.element_count = 0;
        cache.layout_valid  = false;
        cache.rows_valid    = false;
        return;
    }
    // The element count is part of the cache key: an in-place edit that
    // changes it without a new Geometry object must not leave rows indexing
    // past the end.
    const std::size_t element_count = get_domain_element_count(*m_geometry.get(), domain);
    if (cache.element_count != element_count) {
        cache.layout_valid = false;
        cache.rows_valid   = false;
    }
    if (!cache.layout_valid) {
        build_layout(domain, cache);
        cache.layout_valid = true;
        cache.rows_valid   = false;
    }
    if (!cache.rows_valid) {
        build_rows(cache);
        cache.rows_valid = true;
    }
}

void Geometry_spreadsheet_model::build_layout(const Spreadsheet_domain domain, Domain_cache& cache)
{
    ERHE_PROFILE_FUNCTION();

    erhe::geometry::Geometry&        geometry   = *m_geometry.get();
    erhe::geometry::Mesh_attributes& attributes = geometry.get_attributes();
    const std::size_t                element_count = get_domain_element_count(geometry, domain);

    cache.element_count = element_count;
    ++cache.layout_serial;
    cache.columns.clear();
    cache.columns.push_back(
        make_structural_column(Spreadsheet_column_kind::element_index, Spreadsheet_value_type::u32, "index", nullptr, 0, 1, Spreadsheet_edit::read_only)
    );

    using Kind = Spreadsheet_column_kind;
    using Type = Spreadsheet_value_type;
    using Edit = Spreadsheet_edit;
    switch (domain) {
        case Spreadsheet_domain::vertex: {
            static constexpr const char* c_xyz[] = { "x", "y", "z" };
            for (std::size_t i = 0; i < 3; ++i) {
                cache.columns.push_back(make_structural_column(Kind::vertex_position, Type::f32, "position", c_xyz[i], i, 3, Edit::editable));
            }
            if (geometry.has_connectivity()) {
                cache.columns.push_back(make_structural_column(Kind::vertex_corner_count, Type::u32, "corners", nullptr, 0, 1, Edit::read_only));
            }
            break;
        }
        case Spreadsheet_domain::corner: {
            cache.columns.push_back(make_structural_column(Kind::corner_vertex, Type::u32, "vertex", nullptr, 0, 1, Edit::read_only));
            if (geometry.has_connectivity()) {
                cache.columns.push_back(make_structural_column(Kind::corner_facet, Type::u32, "facet", nullptr, 0, 1, Edit::read_only));
            }
            break;
        }
        case Spreadsheet_domain::facet: {
            cache.columns.push_back(make_structural_column(Kind::facet_corner_count, Type::u32, "corners",      nullptr, 0, 1, Edit::read_only));
            cache.columns.push_back(make_structural_column(Kind::facet_first_corner, Type::u32, "first_corner", nullptr, 0, 1, Edit::read_only));
            break;
        }
        case Spreadsheet_domain::edge: {
            cache.columns.push_back(make_structural_column(Kind::edge_vertex, Type::u32, "v0", nullptr, 0, 2, Edit::read_only));
            cache.columns.push_back(make_structural_column(Kind::edge_vertex, Type::u32, "v1", nullptr, 1, 2, Edit::read_only));
            if (geometry.has_edge_connectivity()) {
                cache.columns.push_back(make_structural_column(Kind::edge_facet_count, Type::u32, "facets", nullptr, 0, 1, Edit::read_only));
            }
            break;
        }
        default: {
            break;
        }
    }

    // One column per component of each attribute present on at least one
    // element. The presence scan stops at the first present element.
    const auto add_attribute_columns = [&]<typename Value>(const char* name, erhe::geometry::Attribute_present<Value>& attribute_present) {
        using Traits = Attribute_value_traits<Value>;
        if (!has_any_present(attribute_present, element_count)) {
            return;
        }
        const std::string_view base = strip_domain_prefix(name);
        const Edit             edit = is_editable_geometry_attribute(name) ? Edit::editable : Edit::read_only;
        for (std::size_t component = 0; component < Traits::component_count; ++component) {
            Spreadsheet_column column{
                .kind            = Kind::attribute,
                .value_type      = Traits::value_type,
                .component       = component,
                .component_count = Traits::component_count,
                .attribute_name  = name,
                .attribute       = &attribute_present,
                .read            = &read_attribute_component<Value>,
                .edit            = edit
            };
            set_label(column, base, component_suffix(name, component, Traits::component_count));
            cache.columns.push_back(column);
        }
    };
    switch (domain) {
        case Spreadsheet_domain::vertex: attributes.for_each_vertex_attribute(add_attribute_columns); break;
        case Spreadsheet_domain::corner: attributes.for_each_corner_attribute(add_attribute_columns); break;
        case Spreadsheet_domain::facet:  attributes.for_each_facet_attribute (add_attribute_columns); break;
        case Spreadsheet_domain::edge:   attributes.for_each_edge_attribute  (add_attribute_columns); break;
        default: break;
    }

    // A sort column index names a column of the previous layout; keep it only
    // while it still addresses a column.
    if (cache.sort_column >= static_cast<int>(cache.columns.size())) {
        cache.sort_column = -1;
    }
}

void Geometry_spreadsheet_model::build_rows(Domain_cache& cache)
{
    ERHE_PROFILE_FUNCTION();

    cache.rows.clear();
    const bool sorted = (cache.sort_column >= 0) && (static_cast<std::size_t>(cache.sort_column) < cache.columns.size());
    if (!cache.filter_enabled && !sorted) {
        cache.rows_identity = true; // element order: row i is element i
        return;
    }
    cache.rows_identity = false;

    // The rows to show: the filter's elements, or every element.
    if (cache.filter_enabled) {
        for (const GEO::index_t element : cache.filter) {
            if (element < cache.element_count) {
                cache.rows.push_back(element);
            }
        }
    } else {
        cache.rows.resize(cache.element_count);
        for (std::size_t row = 0; row < cache.element_count; ++row) {
            cache.rows[row] = static_cast<GEO::index_t>(row);
        }
    }
    if (!sorted) {
        return;
    }

    const Spreadsheet_column& column     = cache.columns[static_cast<std::size_t>(cache.sort_column)];
    const bool                descending = (cache.sort_direction == Sort_direction::descending);
    if (column.kind == Spreadsheet_column_kind::element_index) {
        // Rows are already in ascending element order.
        if (descending) {
            std::reverse(cache.rows.begin(), cache.rows.end());
        }
        return;
    }
    // Each element's key is read once into the persistent scratch, and the
    // sort compares plain keys: reading through the column accessor on every
    // comparison costs O(n log n) accessor calls instead of n. Elements
    // without a value sort last in both directions; ties keep element order
    // so the result does not depend on the sort algorithm.
    m_sort_keys.clear();
    m_sort_keys.reserve(cache.rows.size());
    for (const GEO::index_t element : cache.rows) {
        double     value{0.0};
        const bool present = read_cell(column, element, value);
        m_sort_keys.push_back(Sort_key{.value = descending ? -value : value, .element = element, .present = present});
    }
    std::sort(
        m_sort_keys.begin(),
        m_sort_keys.end(),
        [](const Sort_key& lhs, const Sort_key& rhs) -> bool {
            if (lhs.present != rhs.present) {
                return lhs.present;
            }
            if (lhs.present && (lhs.value != rhs.value)) {
                return lhs.value < rhs.value;
            }
            return lhs.element < rhs.element;
        }
    );
    for (std::size_t row = 0, end = m_sort_keys.size(); row < end; ++row) {
        cache.rows[row] = m_sort_keys[row].element;
    }
}

auto Geometry_spreadsheet_model::get_element_count(const Spreadsheet_domain domain) const -> std::size_t
{
    return m_domains[static_cast<std::size_t>(domain)].element_count;
}

auto Geometry_spreadsheet_model::get_columns(const Spreadsheet_domain domain) const -> std::span<const Spreadsheet_column>
{
    return m_domains[static_cast<std::size_t>(domain)].columns;
}

auto Geometry_spreadsheet_model::get_row_count(const Spreadsheet_domain domain) const -> std::size_t
{
    const Domain_cache& cache = m_domains[static_cast<std::size_t>(domain)];
    return cache.rows_identity ? cache.element_count : cache.rows.size();
}

auto Geometry_spreadsheet_model::get_row_element(const Spreadsheet_domain domain, const std::size_t row) const -> GEO::index_t
{
    const Domain_cache& cache = m_domains[static_cast<std::size_t>(domain)];
    return cache.rows_identity ? static_cast<GEO::index_t>(row) : cache.rows[row];
}

auto Geometry_spreadsheet_model::get_sort_column(const Spreadsheet_domain domain) const -> int
{
    return m_domains[static_cast<std::size_t>(domain)].sort_column;
}

auto Geometry_spreadsheet_model::get_layout_serial(const Spreadsheet_domain domain) const -> uint64_t
{
    return m_domains[static_cast<std::size_t>(domain)].layout_serial;
}

auto Geometry_spreadsheet_model::read_cell(const Spreadsheet_column& column, const GEO::index_t element, double& out_value) const -> bool
{
    ERHE_VERIFY(m_geometry);
    const erhe::geometry::Geometry& geometry = *m_geometry.get();
    const GEO::Mesh&                mesh     = geometry.get_mesh();
    switch (column.kind) {
        case Spreadsheet_column_kind::element_index: {
            out_value = static_cast<double>(element);
            return true;
        }
        case Spreadsheet_column_kind::vertex_position: {
            out_value = static_cast<double>(erhe::geometry::get_pointf(mesh.vertices, element)[static_cast<GEO::index_t>(column.component)]);
            return true;
        }
        case Spreadsheet_column_kind::vertex_corner_count: {
            if (!geometry.has_connectivity()) {
                return false;
            }
            out_value = static_cast<double>(geometry.get_vertex_corners(element).size());
            return true;
        }
        case Spreadsheet_column_kind::corner_vertex: {
            out_value = static_cast<double>(mesh.facet_corners.vertex(element));
            return true;
        }
        case Spreadsheet_column_kind::corner_facet: {
            if (!geometry.has_connectivity()) {
                return false;
            }
            out_value = static_cast<double>(geometry.get_corner_facet(element));
            return true;
        }
        case Spreadsheet_column_kind::facet_corner_count: {
            out_value = static_cast<double>(mesh.facets.nb_corners(element));
            return true;
        }
        case Spreadsheet_column_kind::facet_first_corner: {
            out_value = static_cast<double>(mesh.facets.corners_begin(element));
            return true;
        }
        case Spreadsheet_column_kind::edge_vertex: {
            out_value = static_cast<double>(mesh.edges.vertex(element, static_cast<GEO::index_t>(column.component)));
            return true;
        }
        case Spreadsheet_column_kind::edge_facet_count: {
            if (!geometry.has_edge_connectivity()) {
                return false;
            }
            out_value = static_cast<double>(geometry.get_edge_facets(element).size());
            return true;
        }
        case Spreadsheet_column_kind::attribute: {
            return column.read(column.attribute, element, column.component, out_value);
        }
        default: {
            return false;
        }
    }
}

auto Geometry_spreadsheet_model::format_cell(const Spreadsheet_column& column, const GEO::index_t element, const std::span<char> buffer) const -> Formatted_cell
{
    double value{0.0};
    if (!read_cell(column, element, value)) {
        return Formatted_cell{.text = std::string_view{"-"}, .present = false};
    }
    return Formatted_cell{
        .text    = format_value(value, column.value_type, m_precision, buffer),
        .present = true
    };
}

} // namespace editor
