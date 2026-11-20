#include "level/kit_layout.hpp"

#include "erhe_property/dependency_property.hpp"
#include "erhe_property/property_string.hpp"
#include "erhe_scene/xform.hpp"

#include <gtest/gtest.h>

#include <map>
#include <memory>
#include <optional>
#include <string>

// The kit layout node values as ERHE_node properties carry them (doc/editor/level_tools.md "Kit layout nodes"): each
// value with a local value is written as its qualified name and text, and read back by name (former names included).
namespace {

using editor::Kit_layout;
using editor::Kit_layout_data;
using erhe::property::Dependency_property;
using erhe::property::Property_registry;
using erhe::property::Property_value;

[[nodiscard]] auto make_line_layout_data() -> Kit_layout_data
{
    Kit_layout_data data;
    data.items.push_back(editor::Kit_layout_item{.shape = "basic.block", .parameters = "{}"});
    data.surface_snap = editor::Surface_snap::on;
    return data;
}

// The node's local Kit_layout values as name -> text, as a scene save writes them.
[[nodiscard]] auto save_values(const erhe::scene::Xform& node) -> std::map<std::string, std::string>
{
    std::map<std::string, std::string> saved;
    const Property_registry& registry = Property_registry::get();
    registry.for_each_attached_property([&](const Dependency_property& property) {
        if (!editor::is_kit_layout_property(property)) {
            return;
        }
        const std::optional<Property_value> value = node.read_local_value(property);
        if (value.has_value()) {
            saved[registry.qualified_name(node, property)] = erhe::property::to_string(property, value.value());
        }
    });
    return saved;
}

// A scene load: each name resolved on the node (a former name included), its text parsed and set.
void load_values(erhe::scene::Xform& node, const std::map<std::string, std::string>& saved)
{
    const Property_registry& registry = Property_registry::get();
    for (const auto& [name, text] : saved) {
        const Dependency_property* const property = registry.find_for_object(node, name);
        ASSERT_NE(property, nullptr) << name;
        const std::optional<Property_value> value = erhe::property::parse_value(*property, text);
        ASSERT_TRUE(value.has_value()) << name << " = " << text;
        ASSERT_TRUE(node.set_value(*property, value.value())) << name;
    }
}

TEST(Kit_layout, surface_search_values_round_trip_through_the_saved_text)
{
    Kit_layout_data data = make_line_layout_data();
    data.surface_direction         = glm::vec3{0.0f, 1.0f, 0.0f};
    data.surface_forward_distance  = 2.5f;
    data.surface_backward_distance = 0.75f;
    std::shared_ptr<erhe::scene::Xform> node = std::make_shared<erhe::scene::Xform>("layout");
    editor::write_kit_layout(*node, data);

    const std::map<std::string, std::string> saved = save_values(*node);
    EXPECT_EQ(saved.count("Kit_layout.surface_direction"), 1u);
    EXPECT_EQ(saved.count("Kit_layout.surface_forward_distance"), 1u);
    EXPECT_EQ(saved.count("Kit_layout.surface_backward_distance"), 1u);
    EXPECT_EQ(saved.count("Kit_layout.surface_max_distance"), 0u);

    std::shared_ptr<erhe::scene::Xform> loaded = std::make_shared<erhe::scene::Xform>("loaded");
    load_values(*loaded, saved);
    Kit_layout_data read;
    ASSERT_EQ(editor::read_kit_layout(*loaded, read), "");
    EXPECT_EQ(read.surface_direction, (glm::vec3{0.0f, 1.0f, 0.0f}));
    EXPECT_EQ(read.surface_forward_distance, 2.5f);
    EXPECT_EQ(read.surface_backward_distance, 0.75f);

    const editor::Surface_options options = editor::get_layout_surface_options(read);
    EXPECT_EQ(options.direction, (glm::vec3{0.0f, 1.0f, 0.0f}));
    EXPECT_EQ(options.forward_distance, 2.5f);
    EXPECT_EQ(options.backward_distance, 0.75f);
}

TEST(Kit_layout, a_node_saved_before_the_search_values_reads_its_max_distance_both_ways)
{
    // A file written before U51: pattern, items and surface_max_distance, nothing else.
    std::shared_ptr<erhe::scene::Xform> written = std::make_shared<erhe::scene::Xform>("layout");
    editor::write_kit_layout(*written, make_line_layout_data());
    std::map<std::string, std::string> saved{
        {"Kit_layout.pattern",              erhe::property::to_string(Kit_layout::pattern_property.get(), written->get_value(Kit_layout::pattern_property.get()))},
        {"Kit_layout.items",                erhe::property::to_string(Kit_layout::items_property.get(),   written->get_value(Kit_layout::items_property.get()))},
        {"Kit_layout.surface_snap",         "true"},
        {"Kit_layout.surface_max_distance", "3"}
    };
    std::shared_ptr<erhe::scene::Xform> loaded = std::make_shared<erhe::scene::Xform>("loaded");
    load_values(*loaded, saved);
    Kit_layout_data read;
    ASSERT_EQ(editor::read_kit_layout(*loaded, read), "");
    EXPECT_EQ(read.surface_direction, editor::c_default_surface_direction);
    EXPECT_EQ(read.surface_forward_distance, 3.0f);
    EXPECT_EQ(read.surface_backward_distance, 3.0f);

    // A distance the node holds wins over the legacy one; written values no longer fall back on it.
    loaded->set_value(Kit_layout::surface_backward_distance_property, 0.5f);
    ASSERT_EQ(editor::read_kit_layout(*loaded, read), "");
    EXPECT_EQ(read.surface_forward_distance, 3.0f);
    EXPECT_EQ(read.surface_backward_distance, 0.5f);
    editor::write_kit_layout(*loaded, make_line_layout_data());
    ASSERT_EQ(editor::read_kit_layout(*loaded, read), "");
    EXPECT_EQ(read.surface_forward_distance, 1.0f);
    EXPECT_EQ(read.surface_backward_distance, 0.0f);

    // Without surface_max_distance every search value is its default: down, 1 m forward, 0 m backward.
    saved.erase("Kit_layout.surface_max_distance");
    std::shared_ptr<erhe::scene::Xform> defaults = std::make_shared<erhe::scene::Xform>("defaults");
    load_values(*defaults, saved);
    ASSERT_EQ(editor::read_kit_layout(*defaults, read), "");
    EXPECT_EQ(read.surface_direction, (glm::vec3{0.0f, -1.0f, 0.0f}));
    EXPECT_EQ(read.surface_forward_distance, 1.0f);
    EXPECT_EQ(read.surface_backward_distance, 0.0f);
}

TEST(Kit_layout, the_surface_support_rule_round_trips_and_reads_five_lines_when_absent)
{
    Kit_layout_data data = make_line_layout_data();
    data.surface_support = Surface_support::center_fallback;
    std::shared_ptr<erhe::scene::Xform> node = std::make_shared<erhe::scene::Xform>("layout");
    editor::write_kit_layout(*node, data);

    std::map<std::string, std::string> saved = save_values(*node);
    ASSERT_EQ(saved.count("Kit_layout.surface_support"), 1u);
    EXPECT_EQ(saved.at("Kit_layout.surface_support"), "center_fallback");

    std::shared_ptr<erhe::scene::Xform> loaded = std::make_shared<erhe::scene::Xform>("loaded");
    load_values(*loaded, saved);
    Kit_layout_data read;
    ASSERT_EQ(editor::read_kit_layout(*loaded, read), "");
    EXPECT_EQ(read.surface_support, Surface_support::center_fallback);
    EXPECT_EQ(editor::get_layout_surface_options(read).support, Surface_support::center_fallback);

    // A node saved before U52 holds no support rule: five_lines, as built before.
    saved.erase("Kit_layout.surface_support");
    std::shared_ptr<erhe::scene::Xform> older = std::make_shared<erhe::scene::Xform>("older");
    load_values(*older, saved);
    ASSERT_EQ(editor::read_kit_layout(*older, read), "");
    EXPECT_EQ(read.surface_support, Surface_support::five_lines);
}

TEST(Kit_layout, a_zero_direction_or_negative_distance_does_not_read)
{
    std::shared_ptr<erhe::scene::Xform> node = std::make_shared<erhe::scene::Xform>("layout");
    Kit_layout_data data = make_line_layout_data();
    data.surface_direction = glm::vec3{0.0f};
    editor::write_kit_layout(*node, data);
    Kit_layout_data read;
    EXPECT_NE(editor::read_kit_layout(*node, read).find("direction"), std::string::npos);

    data.surface_direction         = glm::vec3{0.0f, -2.0f, 0.0f};
    data.surface_backward_distance = -1.0f;
    editor::write_kit_layout(*node, data);
    EXPECT_NE(editor::read_kit_layout(*node, read), "");

    // A direction of any length reads and is normalized on use.
    data.surface_backward_distance = 0.0f;
    editor::write_kit_layout(*node, data);
    ASSERT_EQ(editor::read_kit_layout(*node, read), "");
    EXPECT_EQ(editor::get_layout_surface_options(read).direction, (glm::vec3{0.0f, -1.0f, 0.0f}));
}

} // anonymous namespace
