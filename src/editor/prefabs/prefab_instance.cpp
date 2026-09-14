#include "prefabs/prefab_instance.hpp"

#include "prefabs/instance_structure.hpp"

#include "erhe_scene/node.hpp"

#include "erhe_property/property_metadata.hpp"

#include <fmt/format.h>

#include <tuple>

namespace editor {

auto Prefab_variant_selection::operator<(const Prefab_variant_selection& rhs) const -> bool
{
    return std::tie(relative_path, set_name, variant_name) < std::tie(rhs.relative_path, rhs.set_name, rhs.variant_name);
}

auto Prefab_variant_selection::operator==(const Prefab_variant_selection& rhs) const -> bool
{
    return std::tie(relative_path, set_name, variant_name) == std::tie(rhs.relative_path, rhs.set_name, rhs.variant_name);
}

auto Prefab_variant_set_key::operator<(const Prefab_variant_set_key& rhs) const -> bool
{
    return std::tie(relative_path, set_name) < std::tie(rhs.relative_path, rhs.set_name);
}

auto Prefab_variant_set_key::operator==(const Prefab_variant_set_key& rhs) const -> bool
{
    return std::tie(relative_path, set_name) == std::tie(rhs.relative_path, rhs.set_name);
}

auto to_string(const std::vector<Prefab_variant_selection>& variant_selections) -> std::string
{
    std::string result;
    for (const Prefab_variant_selection& entry : variant_selections) {
        if (!result.empty()) {
            result += "; ";
        }
        result += fmt::format(
            "{} {} = {}",
            entry.relative_path.empty() ? std::string{"."} : entry.relative_path,
            entry.set_name,
            entry.variant_name
        );
    }
    return result;
}

const erhe::property::Property<std::string> Prefab_instance::variant_selections_property =
    erhe::property::Property<std::string>::register_computed(
        "variant_selections",
        Prefab_instance::property_owner_type(),
        [](const erhe::property::Dependency_object& object) -> erhe::property::Property_value {
            return static_cast<const Prefab_instance&>(object).get_prefab_variant_selections_text();
        },
        erhe::property::Property_metadata{
            .flags = erhe::property::Property_flags::none,
            .ui    = erhe::property::Property_ui{
                .group   = "Prefab",
                .tooltip = "The `variants` selection this composition arc carries into the file it references, one entry per variant set as \"<prim path below the target> <set> = <variant>\"",
                .label   = "Variant Selections"
            }
        }
    );

Prefab_instance::Prefab_instance()
    : Item{"prefab instance"}
{
}

Prefab_instance::Prefab_instance(
    const std::filesystem::path& source_path,
    const std::string&           prefab_name,
    const std::string&           prim_path,
    const Prefab_arc_kind        arc_kind,
    const std::vector<Prefab_variant_selection>& variant_selections
)
    : Item                        {prefab_name}
    , m_prefab_source_path        {source_path}
    , m_prefab_name               {prefab_name}
    , m_prefab_prim_path          {prim_path}
    , m_prefab_arc_kind           {arc_kind}
    , m_prefab_variant_selections {variant_selections}
{
}

Prefab_instance::Prefab_instance(const Prefab_instance& src, erhe::for_clone)
    : Item                 {src, erhe::for_clone{}}
    , m_prefab_source_path {src.m_prefab_source_path}
    , m_prefab_name        {src.m_prefab_name}
    , m_prefab_prim_path   {src.m_prefab_prim_path}
    , m_prefab_arc_kind    {src.m_prefab_arc_kind}
    , m_prefab_variant_selections{src.m_prefab_variant_selections}
{
}

Prefab_instance::Prefab_instance(const Prefab_instance&)            = default;
Prefab_instance& Prefab_instance::operator=(const Prefab_instance&) = default;
Prefab_instance::~Prefab_instance() noexcept                        = default;

auto Prefab_instance::get_prefab_source_path() const -> const std::filesystem::path&
{
    return m_prefab_source_path;
}

auto Prefab_instance::get_prefab_name() const -> const std::string&
{
    return m_prefab_name;
}

auto Prefab_instance::get_prefab_prim_path() const -> const std::string&
{
    return m_prefab_prim_path;
}

auto Prefab_instance::get_prefab_arc_kind() const -> Prefab_arc_kind
{
    return m_prefab_arc_kind;
}

auto Prefab_instance::get_prefab_variant_selections() const -> const std::vector<Prefab_variant_selection>&
{
    return m_prefab_variant_selections;
}

auto Prefab_instance::get_prefab_variant_selections_text() const -> std::string
{
    return to_string(m_prefab_variant_selections);
}

auto get_outermost_prefab_instance_node(erhe::scene::Node* node) -> erhe::scene::Node*
{
    erhe::scene::Node* outermost = nullptr;
    for (erhe::scene::Node* ancestor = node; ancestor != nullptr; ancestor = ancestor->get_parent_node().get()) {
        const std::shared_ptr<Prefab_instance> prefab_instance = erhe::scene::get_attachment<Prefab_instance>(ancestor);
        if (prefab_instance && is_sealed_prefab_instance(*prefab_instance)) {
            outermost = ancestor;
        }
    }
    return outermost;
}

}
