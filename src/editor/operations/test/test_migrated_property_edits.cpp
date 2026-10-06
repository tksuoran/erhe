// The operations that moved onto Property_edit_operation
// (doc/plans/property_undo_and_reflective_mcp.md section 3.3): the
// property-bag write of Paste Properties and edit_material
// (make_property_set_edit_operation), the material edit with its texture
// slot fields (make_material_edit_operation) and the no_transform_update
// flag written through its bridge property. Each is gated by an undo / redo
// round trip of the item's full property dump, and an expression on a
// written property survives undo.

#include "editor_glue.hpp"

#include "app_context.hpp"
#include "operations/material_edit_operation.hpp"
#include "operations/compound_operation.hpp"
#include "operations/property_edit_operation.hpp"

#include "erhe_item/item.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_property/dependency_property.hpp"
#include "erhe_property/property_metadata.hpp"
#include "erhe_property/property_set.hpp"
#include "erhe_property/property_string.hpp"
#include "erhe_property/property_value.hpp"
#include "erhe_scene/node.hpp"

#include <fmt/format.h>
#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

namespace {

using erhe::primitive::Material;
using erhe::property::Dependency_object;
using erhe::property::Dependency_property;
using erhe::property::Expression_text;
using erhe::property::Local_state;
using erhe::property::Property_registry;
using erhe::property::Property_value;
using editor::Property_edit_operation;
using editor::test::get_editor_glue;

[[nodiscard]] auto describe_local(const Dependency_property& property, const std::optional<Local_state>& state) -> std::string
{
    if (!state.has_value()) {
        return "<none>";
    }
    if (const Expression_text* text = std::get_if<Expression_text>(&state.value()); text != nullptr) {
        return "expression '" + text->text + "'";
    }
    return erhe::property::to_string(property, std::get<Property_value>(state.value()));
}

// Every registered property of the object, and every local value (an
// attached property is listed only while it has one): name, effective
// value, value source and the exact local layer.
[[nodiscard]] auto dump_properties(const Dependency_object& object) -> std::vector<std::string>
{
    std::vector<std::string> dump;
    const Property_registry& registry = Property_registry::get();
    const auto describe = [&object, &registry](const Dependency_property& property) -> std::string {
        const Property_value               value  = object.get_value(property);
        const erhe::property::Value_source source = object.get_value_source(property);
        return fmt::format(
            "{} = {} [{}] local {}",
            registry.qualified_name(object, property),
            erhe::property::to_string(property, value),
            erhe::property::c_str(source),
            describe_local(property, object.read_local_state(property))
        );
    };
    registry.for_each_property_of_object(
        object.get_property_owner_type(),
        [&dump, &describe](const Dependency_property& property) {
            dump.push_back(describe(property));
        }
    );
    object.for_each_local_value(
        [&dump, &describe](const Dependency_property& property, const Property_value&) {
            dump.push_back("local: " + describe(property));
        }
    );
    return dump;
}

[[nodiscard]] auto local_expression(const std::optional<Local_state>& state) -> std::string
{
    if (!state.has_value()) {
        return {};
    }
    const Expression_text* text = std::get_if<Expression_text>(&state.value());
    return (text != nullptr) ? text->text : std::string{};
}

class Migrated_property_edit_test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        get_editor_glue().reset();
    }

    editor::App_context context;
};

} // anonymous namespace

// Paste Properties / edit_material property fields: a multi-property bag
// that overwrites an expression; undo puts the expression back.
TEST_F(Migrated_property_edit_test, property_set_edit_round_trips_and_keeps_the_expression)
{
    const std::shared_ptr<Material> material = std::make_shared<Material>("material");
    material->set_value(Material::metallic_property, 0.4f);
    ASSERT_TRUE(material->set_expression(Material::opacity_property.get(), "{metallic} * 0.5"));
    const std::vector<std::string> dump_before = dump_properties(*material);

    erhe::property::Property_set values;
    values.set(Material::opacity_property.get(),  Property_value{0.9f});
    values.set(Material::metallic_property.get(), Property_value{0.8f});
    values.set(Material::ior_property.get(),      Property_value{1.7f});
    const std::shared_ptr<Property_edit_operation> operation = editor::make_property_set_edit_operation(context, material, values);
    ASSERT_TRUE(operation);
    operation->execute(context);
    ASSERT_FALSE(operation->has_error()) << operation->get_error();
    EXPECT_EQ(operation->get_records().size(), std::size_t{3});
    EXPECT_EQ(material->get_opacity(), 0.9f);
    EXPECT_TRUE(local_expression(material->read_local_state(Material::opacity_property.get())).empty());
    const std::vector<std::string> dump_after = dump_properties(*material);
    EXPECT_NE(dump_after, dump_before);

    operation->undo(context);
    EXPECT_EQ(dump_properties(*material), dump_before);
    EXPECT_EQ(local_expression(material->read_local_state(Material::opacity_property.get())), "{metallic} * 0.5") << "the expression survives undo";
    EXPECT_EQ(material->get_opacity(), 0.2f);

    operation->execute(context); // redo
    EXPECT_EQ(dump_properties(*material), dump_after);
    operation->undo(context);
    EXPECT_EQ(dump_properties(*material), dump_before);

    EXPECT_FALSE(editor::make_property_set_edit_operation(context, material, erhe::property::Property_set{})) << "an empty bag is no operation";
}

// Paste Properties onto two siblings: the pasted name is free for the first
// and held by it when the second is written, so the second skips only the
// name - checked against the live state - and keeps every other entry.
TEST_F(Migrated_property_edit_test, property_set_edit_skips_an_entry_refused_by_the_live_state)
{
    const std::shared_ptr<erhe::scene::Node> parent = std::make_shared<erhe::scene::Node>("parent");
    const std::shared_ptr<erhe::scene::Node> a      = std::make_shared<erhe::scene::Node>("a");
    const std::shared_ptr<erhe::scene::Node> b      = std::make_shared<erhe::scene::Node>("b");
    a->set_parent(parent);
    b->set_parent(parent);

    erhe::property::Property_set values;
    values.set(erhe::Item_base::name_property.get(),                Property_value{std::string{"pasted"}});
    values.set(erhe::Item_base::no_transform_update_property.get(), Property_value{true});
    values.set(erhe::Item_base::visible_property.get(),             Property_value{false});

    editor::Compound_operation::Parameters parameters;
    parameters.operations.push_back(editor::make_property_set_edit_operation(context, a, values));
    parameters.operations.push_back(editor::make_property_set_edit_operation(context, b, values));
    editor::Compound_operation compound{std::move(parameters)};
    compound.execute(context);
    ASSERT_FALSE(compound.has_error()) << compound.get_error();

    EXPECT_EQ(a->get_name(), "pasted");
    EXPECT_EQ(b->get_name(), "b") << "the name a sibling holds is skipped";
    EXPECT_TRUE(b->is_no_transform_update()) << "the other entries are written";
    EXPECT_FALSE(b->get_value(erhe::Item_base::visible_property));

    compound.undo(context);
    EXPECT_EQ(a->get_name(), "a");
    EXPECT_FALSE(b->is_no_transform_update());
    EXPECT_TRUE(b->get_value(erhe::Item_base::visible_property));
}

// edit_material: the changed value fields and the changed texture slot
// fields, each through its own property; untouched fields keep their local
// layer (an expression) through the edit itself.
TEST_F(Migrated_property_edit_test, material_edit_round_trips_and_writes_only_changed_fields)
{
    const std::shared_ptr<Material> material = std::make_shared<Material>("material");
    material->set_value(Material::metallic_property, 0.4f);
    ASSERT_TRUE(material->set_expression(Material::ior_property.get(), "{metallic} + 1.0"));
    ASSERT_TRUE(material->set_expression(Material::transmission_property.get(), "{metallic} * 0.25"));
    material->set_value(Material::base_color_texture_uv_scale_property, glm::vec2{2.0f, 2.0f});
    const std::vector<std::string> dump_before = dump_properties(*material);

    const erhe::primitive::Material_values before_values = material->get_values();
    erhe::primitive::Material_values       after_values  = before_values;
    after_values.ior      = 1.9f;
    after_values.metallic = 0.7f;
    const erhe::primitive::Material_data before_data = material->get_data();
    erhe::primitive::Material_data       after_data  = before_data;
    after_data.texture_samplers.base_color.rotation      = 0.5f;
    after_data.texture_samplers.base_color.scale         = glm::vec2{1.0f, 1.0f}; // the default, still written as a local value
    after_data.texture_samplers.normal.offset            = glm::vec2{0.25f, 0.0f};
    after_data.texture_samplers.normal.sampler.wrap_u    = erhe::graphics::Sampler_address_mode::clamp_to_edge;

    EXPECT_FALSE(editor::make_material_edit_operation(material, before_values, before_values, before_data, before_data)) << "an edit that changes nothing is no operation";

    const std::shared_ptr<Property_edit_operation> operation = editor::make_material_edit_operation(material, before_values, after_values, before_data, after_data);
    ASSERT_TRUE(operation);
    operation->execute(context);
    ASSERT_FALSE(operation->has_error()) << operation->get_error();
    EXPECT_EQ(operation->get_records().size(), std::size_t{6}) << "ior, metallic, two base color slot fields, two normal slot fields";
    EXPECT_EQ(local_expression(material->read_local_state(Material::transmission_property.get())), "{metallic} * 0.25") << "an unchanged field is not written";
    EXPECT_EQ(material->get_transmission(), 0.7f * 0.25f);
    EXPECT_EQ(material->get_ior(), 1.9f);
    EXPECT_EQ(material->get_data().texture_samplers.base_color.rotation, 0.5f);
    ASSERT_TRUE(material->read_local_state(Material::base_color_texture_uv_scale_property.get()).has_value()) << "a changed field is written even at its default";
    EXPECT_EQ(material->get_data().texture_samplers.base_color.scale, (glm::vec2{1.0f, 1.0f}));
    EXPECT_EQ(material->get_data().texture_samplers.normal.offset, (glm::vec2{0.25f, 0.0f}));
    EXPECT_EQ(material->get_data().texture_samplers.normal.sampler.wrap_u, erhe::graphics::Sampler_address_mode::clamp_to_edge);
    EXPECT_FALSE(material->read_local_state(Material::normal_texture_wrap_v_property.get()).has_value()) << "an unchanged sampler field is not written";
    const std::vector<std::string> dump_after = dump_properties(*material);

    operation->undo(context);
    EXPECT_EQ(dump_properties(*material), dump_before);
    EXPECT_EQ(local_expression(material->read_local_state(Material::ior_property.get())), "{metallic} + 1.0") << "the expression survives undo";
    EXPECT_EQ(material->get_data().texture_samplers.base_color.scale, (glm::vec2{2.0f, 2.0f}));

    operation->execute(context); // redo
    EXPECT_EQ(dump_properties(*material), dump_after);
    operation->undo(context);
    EXPECT_EQ(dump_properties(*material), dump_before);
}

// Hierarchy window "Set / Clear No Transform Update (Recursive)": the flag
// through its bridge property (Item_base::set_flag_bits is a member write
// the recording would not see).
TEST_F(Migrated_property_edit_test, no_transform_update_flag_round_trips_through_its_property)
{
    const std::shared_ptr<erhe::scene::Node> a = std::make_shared<erhe::scene::Node>("a");
    const std::shared_ptr<erhe::scene::Node> b = std::make_shared<erhe::scene::Node>("b");
    const std::vector<std::string> dump_a_before = dump_properties(*a);
    const std::vector<std::string> dump_b_before = dump_properties(*b);

    const std::vector<std::shared_ptr<erhe::Item_base>> nodes{a, b};
    Property_edit_operation operation{
        "Set No Transform Update",
        [nodes]() {
            for (const std::shared_ptr<erhe::Item_base>& item : nodes) {
                item->set_value(erhe::Item_base::no_transform_update_property, true);
            }
        }
    };
    operation.execute(context);
    ASSERT_FALSE(operation.has_error()) << operation.get_error();
    EXPECT_EQ(operation.get_records().size(), std::size_t{2});
    EXPECT_TRUE(a->is_no_transform_update());
    EXPECT_TRUE(b->is_no_transform_update());
    const std::vector<std::string> dump_a_after = dump_properties(*a);

    operation.undo(context);
    EXPECT_FALSE(a->is_no_transform_update());
    EXPECT_FALSE(b->is_no_transform_update());
    EXPECT_EQ(dump_properties(*a), dump_a_before);
    EXPECT_EQ(dump_properties(*b), dump_b_before);

    operation.execute(context); // redo
    EXPECT_TRUE(b->is_no_transform_update());
    EXPECT_EQ(dump_properties(*a), dump_a_after);
    operation.undo(context);
    EXPECT_EQ(dump_properties(*a), dump_a_before);
}
