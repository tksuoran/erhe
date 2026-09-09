#include "erhe_item/item.hpp"
#include "erhe_item/hierarchy.hpp"
#include "erhe_item/item_host.hpp"
#include "erhe_item/item_log.hpp"
#include "erhe_utility/bit_helpers.hpp"
#include "erhe_verify/verify.hpp"

#include <fmt/format.h>

#include <filesystem>
#include <sstream>

namespace erhe {

namespace {
uint64_t s_item_mutation_serial{0};
}

auto get_item_mutation_serial() -> uint64_t
{
    return s_item_mutation_serial;
}

void bump_item_mutation_serial()
{
    ++s_item_mutation_serial;
}

auto Item_flags::to_string(const uint64_t flags) -> std::string
{
    std::stringstream ss;

    using Item_flags = Item_flags;
    bool first = true;
    for (uint64_t bit_position = 0; bit_position < Item_flags::count; ++ bit_position) {
        const uint64_t bit_mask = (uint64_t{1} << bit_position);
        const bool     value    = erhe::utility::test_bit_set(flags, bit_mask);
        if (value) {
            if (!first) {
                ss << " | ";
            }
            ss << Item_flags::c_bit_labels[bit_position];
            first = false;
        }
    }
    return ss.str();
}

auto Item_filter::operator()(const uint64_t visibility_mask) const -> bool
{
    if ((visibility_mask & require_all_bits_set) != require_all_bits_set) {
        return false;
    }
    if (require_at_least_one_bit_set != 0u) {
        if ((visibility_mask & require_at_least_one_bit_set) == 0u) {
            return false;
        }
    }
    if ((visibility_mask & require_all_bits_clear) != 0u) {
        return false;
    }
    if (require_at_least_one_bit_clear != 0u) {
        if ((visibility_mask & require_at_least_one_bit_clear) == require_at_least_one_bit_clear) {
            return false;
        }
    }
    return true;
}

auto Item_filter::describe() const -> std::string
{
    bool first = true;
    std::stringstream ss;
    if (require_all_bits_set != 0) {
        ss << "require_all_bits_set = " << Item_flags::to_string(this->require_all_bits_set);
        first = false;
    }
    if (require_at_least_one_bit_set != 0) {
        if (!first) {
            ss << ", ";
        }
        ss << "require_at_least_one_bit_set = " << Item_flags::to_string(this->require_at_least_one_bit_set);
        first = false;
    }
    if (require_all_bits_clear != 0) {
        if (!first) {
            ss << ", ";
        }
        ss << "require_all_bits_clear = " << Item_flags::to_string(this->require_all_bits_clear);
        first = false;
    }
    if (require_at_least_one_bit_clear != 0) {
        if (!first) {
            ss << ", ";
        }
        ss << "require_at_least_one_bit_clear = " << Item_flags::to_string(this->require_at_least_one_bit_clear);
    }
    return ss.str();
}

// -----------------------------------------------------------------------------

const erhe::property::Property<bool> Item_base::visible_property = erhe::property::Property<bool>::register_property(
    "visible", Item_base::property_owner_type(),
    erhe::property::Property_metadata{.default_value = true, .property_changed = Item_base::on_flag_property_changed, .inherits = true, .ui = erhe::property::Property_ui{.label = "Visible"}}
);

// USD prim `active` metadata (doc/usd-compatibility-plan.md X2). Not
// inherits-flagged: the value is the item's own opinion, and the subtree
// effect USD gives it is carried by the derived Item_flags::active bit
// (rederive_active_flag_bits).
const erhe::property::Property<bool> Item_base::active_property = erhe::property::Property<bool>::register_property(
    "active", Item_base::property_owner_type(),
    erhe::property::Property_metadata{.default_value = true, .property_changed = Item_base::on_flag_property_changed, .ui = erhe::property::Property_ui{.label = "Active"}}
);

// The prim's composed USD specifier (doc/usd-compatibility-plan.md X2): true
// for `def`, false for the `over` a prim keeps when no layer defines it. USD's
// default traversal predicate requires a defined prim, so an undefined prim
// and its whole subtree are out of render, pick and simulation; like `active`
// that subtree effect is the derived Item_flags::active bit, so this property
// is the item's own opinion and is not inherits-flagged.
const erhe::property::Property<bool> Item_base::defined_property = erhe::property::Property<bool>::register_property(
    "defined", Item_base::property_owner_type(),
    erhe::property::Property_metadata{.default_value = true, .property_changed = Item_base::on_flag_property_changed, .ui = erhe::property::Property_ui{.label = "Defined"}}
);

namespace {

constexpr erhe::property::Enum_entry c_purpose_entries[] = {
    { "Default", static_cast<int32_t>(Purpose::default_) },
    { "Render",  static_cast<int32_t>(Purpose::render)   },
    { "Proxy",   static_cast<int32_t>(Purpose::proxy)    },
    { "Guide",   static_cast<int32_t>(Purpose::guide)    }
};

} // anonymous namespace

const erhe::property::Enum_info c_purpose_enum_info{"Purpose", c_purpose_entries};

// USD purpose (doc/usd-compatibility-plan.md M3). The default layer is the
// value the editor-only flag bits imply (D31), so no item needs a local
// value to report the purpose it already has, and nothing is written to a
// file for an item that authored none.
const erhe::property::Property<Purpose> Item_base::purpose_property = erhe::property::Property<Purpose>::register_property(
    "purpose", Item_base::property_owner_type(), c_purpose_enum_info,
    erhe::property::Property_metadata{
        .default_value   = erhe::property::make_value(Purpose::default_),
        .inherits        = true,
        .ui              = erhe::property::Property_ui{.tooltip = "What the item is drawn for; guide is editor-only content", .label = "Purpose"},
        .compute_default = [](const erhe::property::Dependency_object& object) -> erhe::property::Property_value {
            return erhe::property::make_value(Item_base::derive_purpose_from_flags(static_cast<const Item_base&>(object).get_flag_bits()));
        }
    }
);

const erhe::property::Property<erhe::property::Object_reference> Item_base::style_property = erhe::property::Property<erhe::property::Object_reference>::register_property(
    "style", Item_base::property_owner_type(),
    erhe::property::Property_metadata{
        // A style can carry partition- and variant-affecting values, so an
        // assignment through Property_set_operation rebuilds the draw
        // lists (D11); serialize marks the item's container dirty.
        .flags  = erhe::property::Property_flags::serialize | erhe::property::Property_flags::affects_draw_list_partition | erhe::property::Property_flags::affects_shader_variant,
        .ui     = erhe::property::Property_ui{.label = "Style", .reference_item_types = Item_type::style, .show_clear_button = true},
        .bridge = erhe::property::Property_bridge{
            .get = [](const erhe::property::Dependency_object& object) -> erhe::property::Property_value {
                return erhe::property::Object_reference{std::const_pointer_cast<erhe::property::Dependency_object>(object.get_style())};
            },
            .set = [](erhe::property::Dependency_object& object, const erhe::property::Property_value& value) {
                const std::shared_ptr<erhe::property::Dependency_object>& source = std::get<erhe::property::Object_reference>(value).object;
                if (source) {
                    if (!Item_base::style_applies(*source, object)) {
                        const std::optional<erhe::property::Owner_type> target = source->get_secondary_property_owner_type();
                        erhe::item::log->error(
                            "style '{}' targets '{}', which a {} cannot use",
                            source->get_reference_path(),
                            target.has_value() ? erhe::property::get_owner_type_name(target.value()) : std::string_view{"(no target)"},
                            erhe::property::get_owner_type_name(object.get_property_owner_type())
                        );
                        return;
                    }
                }
                object.set_style(source);
            }
        }
    }
);

// Item-level authored state as bridged properties (D18): the name, the
// tags and the persistent flag bits stay in their members - every reader
// is a bit test or a string reference - and the Properties window draws
// them through the registered path, so a multi-selection edits them
// together with mixed-value display and one undo entry per edit.

const erhe::property::Property<std::string> Item_base::name_property = erhe::property::Property<std::string>::register_property(
    "name", Item_base::property_owner_type(),
    erhe::property::Property_metadata{
        .ui     = erhe::property::Property_ui{.label = "Name"},
        .bridge = erhe::property::Property_bridge{
            .get = [](const erhe::property::Dependency_object& object) -> erhe::property::Property_value {
                return static_cast<const Item_base&>(object).get_name();
            },
            .set = [](erhe::property::Dependency_object& object, const erhe::property::Property_value& value) {
                Item_base&         item = static_cast<Item_base&>(object);
                const std::string& name = std::get<std::string>(value);
                if (name != item.get_name()) {
                    item.set_name(name);
                }
            },
            // Sibling-unique names (doc/usd-compatibility-plan.md M2): every
            // writer of the name - the Properties window row, the MCP
            // set_item_property - goes through set_value, so the refusal of a
            // name a sibling already holds lives here alone.
            .validate = [](
                const erhe::property::Dependency_object& object,
                const erhe::property::Property_value&    value,
                std::string&                             out_error
            ) -> bool {
                const Item_base&   item = static_cast<const Item_base&>(object);
                const std::string& name = std::get<std::string>(value);
                if ((name == item.get_name()) || item.is_name_available(name)) {
                    return true;
                }
                out_error = fmt::format("'{}' is already the name of a sibling of '{}'", name, item.get_name());
                return false;
            }
        }
    }
);

// The tag set as one comma-separated string; set splits on commas and
// drops surrounding whitespace and empty entries.
const erhe::property::Property<std::string> Item_base::tags_property = erhe::property::Property<std::string>::register_property(
    "tags", Item_base::property_owner_type(),
    erhe::property::Property_metadata{
        .ui     = erhe::property::Property_ui{.tooltip = "Comma-separated tags", .label = "Tags"},
        .bridge = erhe::property::Property_bridge{
            .get = [](const erhe::property::Dependency_object& object) -> erhe::property::Property_value {
                return Item_base::tags_to_string(static_cast<const Item_base&>(object).get_tags());
            },
            .set = [](erhe::property::Dependency_object& object, const erhe::property::Property_value& value) {
                Item_base&            item = static_cast<Item_base&>(object);
                std::set<std::string> tags;
                Item_base::tags_from_string(std::get<std::string>(value), tags);
                if (tags != item.get_tags()) {
                    item.set_tags(tags);
                }
            }
        }
    }
);

auto Item_base::tags_to_string(const std::set<std::string>& tags) -> std::string
{
    std::string text;
    for (const std::string& tag : tags) {
        if (!text.empty()) {
            text += ", ";
        }
        text += tag;
    }
    return text;
}

void Item_base::tags_from_string(const std::string_view text, std::set<std::string>& out_tags)
{
    out_tags.clear();
    std::size_t start = 0;
    while (start <= text.size()) {
        std::size_t end = text.find(',', start);
        if (end == std::string_view::npos) {
            end = text.size();
        }
        std::string_view tag = text.substr(start, end - start);
        while (!tag.empty() && ((tag.front() == ' ') || (tag.front() == '\t'))) {
            tag.remove_prefix(1);
        }
        while (!tag.empty() && ((tag.back() == ' ') || (tag.back() == '\t'))) {
            tag.remove_suffix(1);
        }
        if (!tag.empty()) {
            out_tags.emplace(tag);
        }
        start = end + 1;
    }
}

namespace {

// A persistent flag bit as a bridged boolean: get is the bit test, set is
// Item_base::set_flag_bits (which runs handle_flag_bits_update and, for
// lock_edit, the seal).
[[nodiscard]] auto register_flag_property(
    const std::string_view name,
    const uint64_t         bit,
    const std::string_view label,
    const std::string_view group,
    const std::string_view tooltip,
    const bool             developer_only,
    const uint32_t         extra_flags = erhe::property::Property_flags::none
) -> erhe::property::Property<bool>
{
    return erhe::property::Property<bool>::register_property(
        name, Item_base::property_owner_type(),
        erhe::property::Property_metadata{
            .default_value = false,
            .flags         = erhe::property::Property_flags::serialize | extra_flags,
            .ui            = erhe::property::Property_ui{.group = group, .tooltip = tooltip, .developer_only = developer_only, .label = label},
            .bridge        = erhe::property::Property_bridge{
                .get = [bit](const erhe::property::Dependency_object& object) -> erhe::property::Property_value {
                    return erhe::utility::test_bit_set(static_cast<const Item_base&>(object).get_flag_bits(), bit);
                },
                .set = [bit](erhe::property::Dependency_object& object, const erhe::property::Property_value& value) {
                    static_cast<Item_base&>(object).set_flag_bits(bit, std::get<bool>(value));
                }
            }
        }
    );
}

} // anonymous namespace

const erhe::property::Property<bool> Item_base::lock_viewport_transform_property = register_flag_property(
    "lock_viewport_transform", Item_flags::lock_viewport_transform, "Transform", "Locks", "Viewport transform tools leave the item alone", false
);
const erhe::property::Property<bool> Item_base::lock_edit_property = register_flag_property(
    "lock_edit", Item_flags::lock_edit, "Edit", "Locks", "Seals the item: every other property is read-only while set", false,
    erhe::property::Property_flags::writable_when_sealed
);
const erhe::property::Property<bool> Item_base::lock_viewport_selection_property = register_flag_property(
    "lock_viewport_selection", Item_flags::lock_viewport_selection, "Selection", "Locks", "Viewport picking skips the item", false
);
const erhe::property::Property<bool> Item_base::show_in_ui_property = register_flag_property(
    "show_in_ui", Item_flags::show_in_ui, "Show In UI", "", "Listed in the item tree and the pickers", false
);
const erhe::property::Property<bool> Item_base::show_debug_visualizations_property = register_flag_property(
    "show_debug_visualizations", Item_flags::show_debug_visualizations, "Show Debug Visualizations", "", "", false
);
const erhe::property::Property<bool> Item_base::exclude_from_prefab_property = register_flag_property(
    "exclude_from_prefab", Item_flags::exclude_from_prefab, "Exclude From Prefab", "", "Left out when the subtree is saved as a prefab", true
);
const erhe::property::Property<bool> Item_base::no_message_property = register_flag_property(
    "no_message", Item_flags::no_message, "No Message", "", "", true
);
const erhe::property::Property<bool> Item_base::no_transform_update_property = register_flag_property(
    "no_transform_update", Item_flags::no_transform_update, "No Transform Update", "", "", true
);
const erhe::property::Property<bool> Item_base::transform_world_normative_property = register_flag_property(
    "transform_world_normative", Item_flags::transform_world_normative, "Transform World Normative", "", "", true
);
const erhe::property::Property<bool> Item_base::show_in_developer_ui_property = register_flag_property(
    "show_in_developer_ui", Item_flags::show_in_developer_ui, "Show In Developer UI", "", "", true
);
const erhe::property::Property<bool> Item_base::ik_lock_property = register_flag_property(
    "ik_lock", Item_flags::ik_lock, "IK Lock", "", "", true
);

auto Item_base::style_applies(const erhe::property::Dependency_object& source, const erhe::property::Dependency_object& object) -> bool
{
    const std::optional<erhe::property::Owner_type> target = source.get_secondary_property_owner_type();
    if (!target.has_value()) {
        return false;
    }
    if (erhe::property::is_owner_type_or_descendant(object.get_property_owner_type(), target.value())) {
        return true;
    }
    const std::optional<erhe::property::Owner_type> secondary = object.get_secondary_property_owner_type();
    return secondary.has_value() && erhe::property::is_owner_type_or_descendant(target.value(), secondary.value());
}

void Item_base::on_flag_property_changed(erhe::property::Dependency_object& object, const erhe::property::Property_changed_args& args)
{
    Item_base& item = static_cast<Item_base&>(object);
    const bool value = erhe::property::get_as<bool>(args.new_value);
    if (&args.property == &visible_property.get()) {
        item.set_derived_flag_bit(Item_flags::visible, value);
    } else if ((&args.property == &active_property.get()) || (&args.property == &defined_property.get())) {
        item.rederive_active_flag_bits();
    }
}

auto Item_base::is_parent_active() const -> bool
{
    const erhe::property::Dependency_object* const parent      = get_inheritance_parent();
    const Item_base* const                        parent_item  = dynamic_cast<const Item_base*>(parent);
    return (parent_item == nullptr) || parent_item->is_active();
}

void Item_base::rederive_active_flag_bits()
{
    const bool active     = is_parent_active() && get_value(active_property) && get_value(defined_property);
    const bool was_active = erhe::utility::test_bit_set(m_flag_bits, Item_flags::active);
    if (active == was_active) {
        // Every descendant's bit is a function of this one, so an unchanged
        // bit leaves the whole subtree unchanged.
        return;
    }
    set_derived_flag_bit(Item_flags::active, active);
    for_each_inheritance_child(
        [](erhe::property::Dependency_object& child) {
            Item_base* const child_item = dynamic_cast<Item_base*>(&child);
            if (child_item != nullptr) {
                child_item->rederive_active_flag_bits();
            }
        }
    );
}

void Item_base::set_derived_flag_bit(const uint64_t bit, const bool value)
{
    const uint64_t old_flag_bits = m_flag_bits;
    m_flag_bits = value ? (m_flag_bits | bit) : (m_flag_bits & ~bit);
    if (m_flag_bits != old_flag_bits) {
        bump_item_mutation_serial();
        handle_flag_bits_update(old_flag_bits, m_flag_bits);
    }
}

// After a copy: the copy has no parent, so an inherited value of the source
// does not survive; the derived bits must agree with the copied entries
// (Mesh rederives its own shadow_cast / lightmapped bits after this).
// The copied lock_edit flag re-seals the copy (Dependency_object's copy is
// unsealed).
void Item_base::rederive_flag_bits()
{
    m_flag_bits = (m_flag_bits & ~Item_flags::derived)
        | (get_value(visible_property) ? Item_flags::visible : 0u)
        | ((get_value(active_property) && get_value(defined_property)) ? Item_flags::active : 0u);
    sync_seal_with_lock_edit();
}

void Item_base::sync_seal_with_lock_edit()
{
    if (erhe::utility::test_bit_set(m_flag_bits, Item_flags::lock_edit)) {
        seal();
    } else {
        unseal();
    }
}

Item_base::Item_base() = default;

Item_base::Item_base(const std::string_view name)
    : m_name       {name}
    , m_debug_label{erhe::utility::Debug_label{fmt::format("{}##{}", name, get_id())}}
{
}

// Copies (clones) keep the source name; call sites that want a distinct
// name for a duplicate (e.g. clipboard paste) rename the clone themselves.
// m_gltf_uid is intentionally NOT copied (see its declaration): a clone is
// a new object and gets its own uid at first export.
Item_base::Item_base(const Item_base& other)
    : enable_shared_from_this{other}
    , erhe::property::Dependency_object{other}
    , m_flag_bits  {other.m_flag_bits & ~Item_flags::selected}
    , m_name       {other.m_name}
    , m_debug_label{erhe::utility::Debug_label{fmt::format("{}##{}", m_name, get_id())}}
    , m_source_path{other.m_source_path ? std::make_unique<std::filesystem::path>(*other.m_source_path) : nullptr}
{
    rederive_flag_bits();
}

auto Item_base::operator=(const Item_base& other) -> Item_base&
{
    erhe::property::Dependency_object::operator=(other);
    m_flag_bits   = other.m_flag_bits & ~Item_flags::selected;
    m_name        = other.m_name;
    m_debug_label = erhe::utility::Debug_label{fmt::format("{}##{}", m_name, get_id())};
    m_source_path = other.m_source_path ? std::make_unique<std::filesystem::path>(*other.m_source_path) : nullptr;
    rederive_flag_bits();
    return *this;
}

Item_base::~Item_base() noexcept = default;

auto Item_base::get_name() const -> const std::string&
{
    return m_name;
}

auto Item_base::get_debug_label() const -> erhe::utility::Debug_label
{
    return m_debug_label;
}

auto Item_base::get_flag_bits() const -> uint64_t
{
    return m_flag_bits;
}

void Item_base::set_flag_bits(const uint64_t requested_mask, const bool value)
{
    uint64_t mask = requested_mask;
    if ((mask & Item_flags::derived) != 0u) {
        erhe::item::log->error(
            "Item_base::set_flag_bits({}) on '{}': {} is a property (Item_base::visible_property / Item_base::active_property / Item_base::defined_property / Mesh::shadow_cast_property / Mesh::lightmapped_property); the derived bits are dropped from the mask",
            value, m_name, Item_flags::to_string(mask & Item_flags::derived)
        );
        mask &= ~Item_flags::derived;
    }
    const auto old_flag_bits = m_flag_bits;
    // M3: the purpose property's default layer is derived from these bits
    // (D31), so its effective value has to be read before they move.
    const bool purpose_inputs_change = ((mask & Item_flags::purpose_inputs) != 0u);
    erhe::property::Value_source   old_purpose_source{};
    erhe::property::Property_value old_purpose{};
    if (purpose_inputs_change) {
        old_purpose_source = get_value_source(purpose_property.get());
        old_purpose        = get_value(purpose_property.get());
    }
    if (value) {
        m_flag_bits = m_flag_bits | mask;
    } else {
        m_flag_bits = m_flag_bits & ~mask;
    }
    if (purpose_inputs_change && (((old_flag_bits ^ m_flag_bits) & Item_flags::purpose_inputs) != 0u)) {
        refresh_computed_default(purpose_property.get(), old_purpose, old_purpose_source);
    }

    if (m_flag_bits != old_flag_bits) {
        if (((old_flag_bits ^ m_flag_bits) & Item_flags::lock_edit) != 0u) {
            sync_seal_with_lock_edit();
        }
        if (((old_flag_bits ^ m_flag_bits) & ~Item_flags::transient) != 0u) {
            bump_item_mutation_serial();
        }
        handle_flag_bits_update(old_flag_bits, m_flag_bits);
    }
}

void Item_base::enable_flag_bits(const uint64_t mask)
{
    set_flag_bits(mask, true);
}

void Item_base::disable_flag_bits(const uint64_t mask)
{
    set_flag_bits(mask, false);
}

auto Item_base::is_no_transform_update() const -> bool
{
    return erhe::utility::test_bit_set(m_flag_bits, Item_flags::no_transform_update);
}

auto Item_base::is_transform_world_normative() const -> bool
{
    return erhe::utility::test_bit_set(m_flag_bits, Item_flags::transform_world_normative);
}

auto Item_base::is_selected() const -> bool
{
    return erhe::utility::test_bit_set(m_flag_bits, Item_flags::selected);
}

auto Item_base::is_hovered() const -> bool
{
    return erhe::utility::test_any_rhs_bits_set(m_flag_bits, Item_flags::hovered_in_viewport | Item_flags::hovered_in_item_tree);
}

void Item_base::set_selected(const bool selected)
{
    set_flag_bits(Item_flags::selected, selected);
}

void Item_base::set_visible(const bool value)
{
    set_value(visible_property, value);
}

void Item_base::show()
{
    set_value(visible_property, true);
}

void Item_base::hide()
{
    set_value(visible_property, false);
}

auto Item_base::is_visible() const -> bool
{
    return erhe::utility::test_bit_set(m_flag_bits, Item_flags::visible);
}

auto Item_base::is_active() const -> bool
{
    return erhe::utility::test_bit_set(m_flag_bits, Item_flags::active);
}

auto Item_base::get_purpose() const -> Purpose
{
    return get_value(purpose_property);
}

auto Item_base::is_shown_in_ui() const -> bool
{
    return erhe::utility::test_bit_set(m_flag_bits, Item_flags::show_in_ui);
}

auto Item_base::is_hidden() const -> bool
{
    return !is_visible();
}

auto Item_base::is_lock_edit() const -> bool
{
    return (m_flag_bits & Item_flags::lock_edit) == Item_flags::lock_edit;
}

auto Item_base::is_lock_viewport_selection() const -> bool
{
    return (m_flag_bits & Item_flags::lock_viewport_selection) == Item_flags::lock_viewport_selection;
}

auto Item_base::is_lock_viewport_transform() const -> bool
{
    return (m_flag_bits & Item_flags::lock_viewport_transform) == Item_flags::lock_viewport_transform;
}

void Item_base::set_lock_edit(bool value)
{
    set_flag_bits(Item_flags::lock_edit, value);
}

auto Item_base::get_tags() const -> const std::set<std::string>&
{
    return m_tags;
}

auto Item_base::has_tag(const std::string_view tag) const -> bool
{
    return m_tags.contains(std::string{tag});
}

void Item_base::add_tag(const std::string_view tag)
{
    m_tags.emplace(tag);
}

void Item_base::remove_tag(const std::string_view tag)
{
    m_tags.erase(std::string{tag});
}

void Item_base::clear_tags()
{
    m_tags.clear();
}

void Item_base::set_tags(const std::set<std::string>& tags)
{
    m_tags = tags;
}

void Item_base::set_source_path(const std::filesystem::path& path)
{
    m_source_path = std::make_unique<std::filesystem::path>(path);
}

auto Item_base::get_source_path() const -> const std::filesystem::path*
{
    return m_source_path.get();
}

void Item_base::set_gltf_uid(const std::string_view uid)
{
    m_gltf_uid = uid;
}

auto Item_base::get_gltf_uid() const -> const std::string&
{
    return m_gltf_uid;
}

void Item_base::set_name(const std::string_view name)
{
    m_name = name;
    m_debug_label = erhe::utility::Debug_label{fmt::format("{}##{}", name, get_id())};
    bump_item_mutation_serial();

    // Sibling-unique names (doc/usd-compatibility-plan.md M2): a
    // content-library entry node's name mirrors the name of the item it
    // wraps, so renaming the item renames the entry node with it. The item's
    // name is the one the user sees, and the entry node's name is the one the
    // path is built from, so the two must not drift apart.
    Hierarchy* const container = dynamic_cast<Hierarchy*>(m_inheritance_container);
    if ((container != nullptr) && (container->get_name() != m_name)) {
        container->set_name(m_name);
    }
}

auto Item_base::is_name_available(const std::string_view name) const -> bool
{
    const Hierarchy* const container = dynamic_cast<const Hierarchy*>(m_inheritance_container);
    if (container == nullptr) {
        return true;
    }
    return container->is_name_available(name);
}

auto Item_base::describe(int level) const -> std::string
{
    switch (level) {
        case 0:  return get_name();
        case 1:  return fmt::format("{} {}", get_type_name(), get_name());
        case 2:  return fmt::format("{} {}, id = {}", get_type_name(), get_name(), get_id());
        default: return fmt::format("{} {}, id = {}, flags = {}", get_type_name(), get_name(), get_id(), Item_flags::to_string(get_flag_bits()));
    }
}

auto Item_base::property_owner_type() -> erhe::property::Owner_type
{
    static const erhe::property::Owner_type s_id = erhe::property::allocate_owner_type(erhe::property::root_owner_type, "Item_base");
    return s_id;
}

auto Item_base::resolve_expression_object(const std::string_view path) const -> erhe::property::Dependency_object*
{
    if (path.empty()) {
        return const_cast<Item_base*>(this);
    }
    if (path == "..") {
        return const_cast<erhe::property::Dependency_object*>(get_inheritance_parent());
    }
    Item_host* const host = get_item_host();
    return (host != nullptr) ? host->find_hosted_item(path) : nullptr;
}

auto Item_base::get_reference_path() const -> std::string
{
    return get_name();
}

auto Item_base::get_shared_reference() const -> std::shared_ptr<erhe::property::Dependency_object>
{
    return const_cast<Item_base*>(this)->weak_from_this().lock();
}

auto Item_base::get_id() const -> std::size_t
{
    return m_id.get_id();
}

void Item_base::set_inheritance_container(erhe::property::Dependency_object* const container)
{
    m_inheritance_container = container;
    // The container is the item's inheritance parent, so the effective
    // active state can change with it (X2).
    rederive_active_flag_bits();
}

auto Item_base::get_inheritance_container() const -> erhe::property::Dependency_object*
{
    return m_inheritance_container;
}

auto Item_base::get_inheritance_parent() const -> const erhe::property::Dependency_object*
{
    return m_inheritance_container;
}

void Item_base::set_item_host(Item_host* const item_host)
{
    m_item_host = item_host;
}

} // namespace erhe

