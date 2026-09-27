#pragma once

#include <geogram/basic/numeric.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

namespace erhe::geometry { class Geometry; }

namespace editor {

// Element domains of a Geometry, one Geometry Spreadsheet tab each
// (doc/editor/geometry_spreadsheet.md).
enum class Spreadsheet_domain : unsigned int {
    vertex = 0,
    corner = 1,
    facet  = 2,
    edge   = 3
};
inline constexpr std::size_t c_spreadsheet_domain_count = 4;

[[nodiscard]] auto c_str(Spreadsheet_domain domain) -> const char*;

enum class Spreadsheet_value_type : unsigned int {
    f32 = 0,
    u32 = 1,
    i32 = 2
};

// What a column shows: the element index, one of the structural columns read
// straight from GEO::Mesh / Geometry connectivity, or one component of a
// Mesh_attributes attribute.
enum class Spreadsheet_column_kind : unsigned int {
    element_index,
    vertex_position,
    vertex_corner_count,
    corner_vertex,
    corner_facet,
    facet_corner_count,
    facet_first_corner,
    edge_vertex,
    edge_facet_count,
    attribute
};

enum class Spreadsheet_edit : unsigned int {
    read_only,
    editable
};

enum class Sort_direction : unsigned int {
    ascending,
    descending
};

class Spreadsheet_column
{
public:
    // Reads one component of the attribute at `element`; false when the
    // element carries no value for the attribute (its present flag is clear).
    using Read_function = auto (*)(const void* attribute, GEO::index_t element, std::size_t component, double& out_value) -> bool;

    Spreadsheet_column_kind kind           {Spreadsheet_column_kind::element_index};
    Spreadsheet_value_type  value_type     {Spreadsheet_value_type::u32};
    std::size_t             component      {0}; // vector component, or edge endpoint
    std::size_t             component_count{1};
    const char*             attribute_name {nullptr}; // Mesh_attributes member name, attribute columns only
    const void*             attribute      {nullptr}; // Attribute_present<T>*, attribute columns only
    Read_function           read           {nullptr};
    Spreadsheet_edit        edit           {Spreadsheet_edit::read_only};
    std::array<char, 48>    label          {};

    [[nodiscard]] auto get_label() const -> const char* { return label.data(); }
};

class Formatted_cell
{
public:
    std::string_view text;
    bool             present{false};
};

// Column layout and row order of one Geometry, per domain, for the Geometry
// Spreadsheet window. Both caches are rebuilt only when invalidated (a new
// Geometry, a changed element count, invalidate(), a sort change); cell values
// are never cached, they are read live for the cells the window draws.
class Geometry_spreadsheet_model
{
public:
    // Selects the Geometry the model describes. A different Geometry object
    // (identity), or a changed element count of the same one, invalidates the
    // domain's caches. The model holds the Geometry until the next
    // set_geometry() or release(). Returns true when the Geometry changed.
    auto set_geometry(const std::shared_ptr<erhe::geometry::Geometry>& geometry) -> bool;
    [[nodiscard]] auto get_geometry() const -> const std::shared_ptr<erhe::geometry::Geometry>&;

    // Content of the current Geometry changed in place: rebuild all layouts.
    void invalidate();
    // Drop the caches and give back their memory (hidden window).
    void release();

    void set_precision(int precision); // 0 = shortest round-trip form
    [[nodiscard]] auto get_precision() const -> int;

    // Sort rows of `domain` by `column` (index into get_columns()); a negative
    // column restores element order.
    void set_sort(Spreadsheet_domain domain, int column, Sort_direction direction);

    // Limit the rows of `domain` to `elements` (ascending, unique element
    // indices; indices past the element count are skipped), in the current
    // sort order. clear_row_filter() shows every element again.
    void set_row_filter  (Spreadsheet_domain domain, std::span<const GEO::index_t> elements);
    void clear_row_filter(Spreadsheet_domain domain);
    [[nodiscard]] auto has_row_filter(Spreadsheet_domain domain) const -> bool;

    // Rebuild the domain's caches when invalid. Call once per frame for the
    // domain being drawn, before the accessors below.
    void update(Spreadsheet_domain domain);

    [[nodiscard]] auto get_element_count(Spreadsheet_domain domain) const -> std::size_t;
    [[nodiscard]] auto get_columns      (Spreadsheet_domain domain) const -> std::span<const Spreadsheet_column>;
    [[nodiscard]] auto get_row_count    (Spreadsheet_domain domain) const -> std::size_t;
    [[nodiscard]] auto get_row_element  (Spreadsheet_domain domain, std::size_t row) const -> GEO::index_t;
    [[nodiscard]] auto get_sort_column  (Spreadsheet_domain domain) const -> int;
    // Increments on every column layout rebuild of the domain, so a consumer
    // can key its own per-column caches (widths) on it.
    [[nodiscard]] auto get_layout_serial(Spreadsheet_domain domain) const -> uint64_t;

    [[nodiscard]] auto read_cell  (const Spreadsheet_column& column, GEO::index_t element, double& out_value) const -> bool;
    [[nodiscard]] auto format_cell(const Spreadsheet_column& column, GEO::index_t element, std::span<char> buffer) const -> Formatted_cell;

private:
    class Domain_cache
    {
    public:
        std::vector<Spreadsheet_column> columns;
        std::vector<GEO::index_t>       rows;          // display order, unless rows_identity
        std::vector<GEO::index_t>       filter;        // row filter elements, when filter_enabled
        bool                            filter_enabled{false};
        bool                            rows_identity {true}; // row i is element i; rows unused
        std::size_t                     element_count{0};
        int                             sort_column  {-1};
        Sort_direction                  sort_direction{Sort_direction::ascending};
        uint64_t                        layout_serial{0};
        bool                            layout_valid {false};
        bool                            rows_valid   {false};
    };

    class Sort_key
    {
    public:
        double       value  {0.0}; // negated for a descending sort
        GEO::index_t element{0};
        bool         present{false};
    };

    void build_layout(Spreadsheet_domain domain, Domain_cache& cache);
    void build_rows  (Domain_cache& cache);

    std::shared_ptr<erhe::geometry::Geometry>                  m_geometry;
    std::array<Domain_cache, c_spreadsheet_domain_count>       m_domains;
    int                                                        m_precision{4};
    std::vector<Sort_key>                                      m_sort_keys; // build_rows() scratch
};

[[nodiscard]] auto get_domain_element_count(const erhe::geometry::Geometry& geometry, Spreadsheet_domain domain) -> std::size_t;

} // namespace editor
