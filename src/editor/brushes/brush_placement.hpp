#pragma once

#include "erhe_scene/node_attachment.hpp"
#include "erhe_property/dependency_property.hpp"

#include <geogram/mesh/mesh.h>

#include <memory>

namespace erhe::scene { class Xformable; using Node = Xformable; }

namespace editor {

class Brush;

class Brush_placement : public erhe::Item<erhe::Item_base, erhe::scene::Node_attachment, Brush_placement, erhe::Item_kind::not_clonable>{
public:
    Brush_placement(const std::shared_ptr<Brush>& brush, GEO::index_t facet, GEO::index_t corner);
    Brush_placement(const Brush_placement&);
    Brush_placement& operator=(const Brush_placement&);
    ~Brush_placement() noexcept override;

    Brush_placement();

    static constexpr std::string_view static_type_name{"Brush_placement"};
    [[nodiscard]] static constexpr auto get_static_type() -> uint64_t { return erhe::Item_type::node_attachment | erhe::Item_type::brush_placement; }

    // TODO Consider if Brush_placement is clonable or not
    auto clone() const -> std::shared_ptr<erhe::Item_base> override;

    // Registered properties (doc/property-system.md section 4.11), stored
    // in the entry store and inheriting from the node chain (D30): the
    // brush as an object reference (D28) and the facet and corner as
    // developer-only integers (-1 = NO_INDEX). The members below are a
    // mirror of the effective values kept current by on_property_changed.
    static const erhe::property::Property<erhe::property::Object_reference> brush_property;
    static const erhe::property::Property<int>                              facet_property;
    static const erhe::property::Property<int>                              corner_property;

    // Public API
    [[nodiscard]] auto get_brush () const -> std::shared_ptr<Brush>;
    [[nodiscard]] auto get_facet () const -> GEO::index_t;
    [[nodiscard]] auto get_corner() const -> GEO::index_t;
    void set_corner(GEO::index_t corner);

    // Implements erhe::property::Dependency_object: refreshes the mirror
    // on every change of a Brush_placement property, whatever its source.
    void on_property_changed(const erhe::property::Property_changed_args& args) override;

private:
    void refresh_mirror();

    std::shared_ptr<Brush> m_brush;
    GEO::index_t           m_facet;
    GEO::index_t           m_corner;
};

}
