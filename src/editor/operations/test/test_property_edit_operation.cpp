// Property_edit_operation (doc/editor/operations.md "Property_edit_operation"):
// an edit function's property writes recorded on the first execute, redone
// from their after states and undone from their before states in record
// order, with the editor consequence hook per record; a refused write or an
// edit that wrote nothing leaves the operation in error with nothing changed.

#include "editor_glue.hpp"

#include "app_context.hpp"
#include "operations/compound_operation.hpp"
#include "operations/property_edit_operation.hpp"

#include "erhe_item/item.hpp"
#include "erhe_primitive/buffer_mesh.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_primitive/primitive.hpp"
#include "erhe_property/dependency_property.hpp"
#include "erhe_property/property_value.hpp"
#include "erhe_scene/light.hpp"
#include "erhe_scene/mesh.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>

namespace {

using erhe::property::Local_state;
using erhe::property::Expression_text;
using erhe::property::Object_reference;
using erhe::property::Property;
using erhe::property::Property_metadata;
using erhe::property::Property_value;
using erhe::scene::Light;
using editor::Property_edit_operation;
using editor::test::get_editor_glue;

// A changed callback that writes another local layer: test_follower follows
// test_leader with an offset (registered on Light for this test only).
const Property<float> test_follower = Property<float>::register_property("test_follower", Light::property_owner_type());
const Property<float> test_leader   = Property<float>::register_property(
    "test_leader", Light::property_owner_type(),
    Property_metadata{
        .property_changed = [](erhe::property::Dependency_object& o, const erhe::property::Property_changed_args& args) {
            o.set_value(test_follower, std::get<float>(args.new_value) + 100.0f);
        }
    }
);

[[nodiscard]] auto make_primitive() -> std::shared_ptr<erhe::primitive::Primitive>
{
    return std::make_shared<erhe::primitive::Primitive>(erhe::primitive::Buffer_mesh{});
}

[[nodiscard]] auto local_float(const std::optional<Local_state>& state) -> float
{
    EXPECT_TRUE(state.has_value());
    if (!state.has_value()) {
        return -1.0f;
    }
    EXPECT_TRUE(std::holds_alternative<Property_value>(state.value()));
    return std::get<float>(std::get<Property_value>(state.value()));
}

[[nodiscard]] auto local_expression(const std::optional<Local_state>& state) -> std::string
{
    if (!state.has_value()) {
        return {};
    }
    const Expression_text* text = std::get_if<Expression_text>(&state.value());
    return (text != nullptr) ? text->text : std::string{};
}

[[nodiscard]] auto local_object(const std::optional<Local_state>& state) -> const erhe::property::Dependency_object*
{
    if (!state.has_value()) {
        return nullptr;
    }
    const Property_value* value = std::get_if<Property_value>(&state.value());
    return (value != nullptr) ? erhe::property::get_referenced_object(*value).get() : nullptr;
}

class Property_edit_operation_test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        get_editor_glue().reset();
    }

    [[nodiscard]] auto hook_count() const -> std::size_t
    {
        return get_editor_glue().property_changed.size();
    }

    editor::App_context context;
};

} // anonymous namespace

TEST_F(Property_edit_operation_test, records_undoes_and_redoes_several_properties_on_two_items)
{
    const std::shared_ptr<Light> a = std::make_shared<Light>("a");
    const std::shared_ptr<Light> b = std::make_shared<Light>("b");
    a->set_value(Light::intensity_property, 2.0f);
    ASSERT_TRUE(a->set_expression(Light::range_property.get(), "{intensity} * 3"));

    std::shared_ptr<int> captured = std::make_shared<int>(0);
    Property_edit_operation operation{
        "edit two lights",
        [a, b, captured]() {
            a->set_value(Light::intensity_property, 5.0f);
            a->set_value(Light::range_property, 10.0f);  // replaces the expression
            b->set_value(Light::color_property, glm::vec3{0.5f, 0.25f, 1.0f});
            b->set_value(Light::intensity_property, 7.0f);
            b->set_value(Light::intensity_property, 8.0f); // same property again: one record
        }
    };
    EXPECT_EQ(captured.use_count(), 2);

    operation.execute(context);
    ASSERT_FALSE(operation.has_error()) << operation.get_error();
    EXPECT_EQ(captured.use_count(), 1) << "the edit function is released after the first execute";
    ASSERT_EQ(operation.get_records().size(), std::size_t{4});
    EXPECT_EQ(operation.get_records()[0].property, &Light::intensity_property.get());
    EXPECT_EQ(operation.get_records()[0].item.get(), a.get());
    EXPECT_EQ(local_float(operation.get_records()[3].after), 8.0f);
    EXPECT_EQ(hook_count(), std::size_t{4}) << "the first execute runs the consequence hook per record";
    EXPECT_EQ(a->get_value(Light::range_property), 10.0f);

    std::unordered_set<const erhe::Item_base*> references;
    operation.collect_item_references(references);
    EXPECT_EQ(references, (std::unordered_set<const erhe::Item_base*>{a.get(), b.get()}));

    operation.undo(context);
    EXPECT_EQ(hook_count(), std::size_t{8});
    EXPECT_EQ(local_float(a->read_local_state(Light::intensity_property.get())), 2.0f);
    EXPECT_EQ(local_expression(a->read_local_state(Light::range_property.get())), "{intensity} * 3") << "the expression survives undo";
    EXPECT_EQ(a->get_value(Light::range_property), 6.0f);
    EXPECT_FALSE(b->read_local_state(Light::color_property.get()).has_value());
    EXPECT_FALSE(b->read_local_state(Light::intensity_property.get()).has_value());

    operation.execute(context); // redo
    EXPECT_EQ(hook_count(), std::size_t{12});
    EXPECT_EQ(local_float(a->read_local_state(Light::intensity_property.get())), 5.0f);
    EXPECT_EQ(local_float(a->read_local_state(Light::range_property.get())), 10.0f);
    EXPECT_EQ(b->get_value(Light::color_property), (glm::vec3{0.5f, 0.25f, 1.0f}));
    EXPECT_EQ(local_float(b->read_local_state(Light::intensity_property.get())), 8.0f);

    operation.undo(context);
    EXPECT_EQ(local_expression(a->read_local_state(Light::range_property.get())), "{intensity} * 3");
}

TEST_F(Property_edit_operation_test, mesh_primitive_property_resolves_to_its_mesh_and_index)
{
    const std::shared_ptr<erhe::primitive::Material> m1 = std::make_shared<erhe::primitive::Material>("m1");
    const std::shared_ptr<erhe::primitive::Material> m2 = std::make_shared<erhe::primitive::Material>("m2");
    const std::shared_ptr<erhe::scene::Mesh> mesh = std::make_shared<erhe::scene::Mesh>("mesh");
    mesh->add_primitive(make_primitive(), m1);
    mesh->add_primitive(make_primitive(), m1);
    erhe::property::Dependency_object* const primitive_1 = mesh->get_property_sub_object(1);
    ASSERT_NE(primitive_1, nullptr);

    Property_edit_operation operation{
        "assign m2",
        [primitive_1, m2]() {
            primitive_1->set_value(erhe::scene::Mesh_primitive::material_property, Object_reference{m2});
        }
    };
    operation.execute(context);
    ASSERT_FALSE(operation.has_error()) << operation.get_error();
    ASSERT_EQ(operation.get_records().size(), std::size_t{1});
    const Property_edit_operation::Record& record = operation.get_records()[0];
    EXPECT_EQ(record.item.get(), mesh.get());
    ASSERT_TRUE(record.sub_object.has_value());
    EXPECT_EQ(record.sub_object.value(), std::size_t{1});
    EXPECT_EQ(local_object(record.before), m1.get());
    EXPECT_EQ(local_object(record.after),  m2.get());
    ASSERT_EQ(hook_count(), std::size_t{1});
    EXPECT_EQ(get_editor_glue().property_changed[0].item, mesh.get()) << "the hook runs with the owning item";
    EXPECT_EQ(get_editor_glue().adopted_userships, (std::unordered_set<const erhe::Item_base*>{m1.get(), m2.get()}));

    std::unordered_set<const erhe::Item_base*> references;
    operation.collect_item_references(references);
    EXPECT_EQ(references, (std::unordered_set<const erhe::Item_base*>{mesh.get(), m1.get(), m2.get()}));

    operation.undo(context);
    EXPECT_EQ(mesh->get_primitives()[1].material, m1);
    operation.execute(context);
    EXPECT_EQ(mesh->get_primitives()[1].material, m2);
    EXPECT_EQ(mesh->get_primitives()[0].material, m1);
}

// The edit writes A; A's changed callback writes B, which was authored
// before the edit. Undo in record order restores A (the callback rewrites
// B from A's old value) and then B's own authored value.
TEST_F(Property_edit_operation_test, cascade_undo_restores_both_local_layers)
{
    const std::shared_ptr<Light> light = std::make_shared<Light>("light");
    light->set_value(test_follower, 7.0f);
    ASSERT_FALSE(light->read_local_state(test_leader.get()).has_value());

    Property_edit_operation operation{
        "lead",
        [light]() {
            light->set_value(test_leader, 1.0f);
        }
    };
    operation.execute(context);
    ASSERT_FALSE(operation.has_error()) << operation.get_error();
    ASSERT_EQ(operation.get_records().size(), std::size_t{2});
    EXPECT_EQ(operation.get_records()[0].property, &test_leader.get());
    EXPECT_EQ(operation.get_records()[1].property, &test_follower.get());
    EXPECT_EQ(light->get_value(test_follower), 101.0f);

    operation.undo(context);
    EXPECT_FALSE(light->read_local_state(test_leader.get()).has_value());
    EXPECT_EQ(local_float(light->read_local_state(test_follower.get())), 7.0f) << "the independently authored follower is restored";

    operation.execute(context);
    EXPECT_EQ(local_float(light->read_local_state(test_leader.get())), 1.0f);
    EXPECT_EQ(local_float(light->read_local_state(test_follower.get())), 101.0f);

    operation.undo(context);
    EXPECT_EQ(local_float(light->read_local_state(test_follower.get())), 7.0f);
}

TEST_F(Property_edit_operation_test, refused_write_is_an_error_and_restores_the_accepted_writes)
{
    const std::shared_ptr<Light> a = std::make_shared<Light>("a");
    const std::shared_ptr<Light> b = std::make_shared<Light>("b");
    a->set_value(Light::intensity_property, 2.0f);
    b->seal();

    Property_edit_operation operation{
        "partly sealed",
        [a, b]() {
            a->set_value(Light::intensity_property, 5.0f);
            a->set_value(Light::range_property, 3.0f);
            b->set_value(Light::intensity_property, 6.0f); // refused: sealed
        }
    };
    operation.execute(context);
    ASSERT_TRUE(operation.has_error());
    EXPECT_NE(operation.get_error().find("intensity"), std::string::npos) << operation.get_error();
    EXPECT_NE(operation.get_error().find("'b'"), std::string::npos) << operation.get_error();
    EXPECT_TRUE(operation.get_records().empty());
    EXPECT_EQ(hook_count(), std::size_t{0});
    EXPECT_EQ(local_float(a->read_local_state(Light::intensity_property.get())), 2.0f);
    EXPECT_FALSE(a->read_local_state(Light::range_property.get()).has_value());
    EXPECT_FALSE(b->read_local_state(Light::intensity_property.get()).has_value());

    std::unordered_set<const erhe::Item_base*> references;
    operation.collect_item_references(references);
    EXPECT_TRUE(references.empty());
}

TEST_F(Property_edit_operation_test, refused_reference_is_an_error_and_restores_the_write)
{
    const std::shared_ptr<erhe::primitive::Material> local   = std::make_shared<erhe::primitive::Material>("local");
    const std::shared_ptr<erhe::primitive::Material> foreign = std::make_shared<erhe::primitive::Material>("foreign");
    const std::shared_ptr<erhe::scene::Mesh> mesh = std::make_shared<erhe::scene::Mesh>("mesh");
    mesh->add_primitive(make_primitive(), local);
    get_editor_glue().foreign_items.insert(foreign.get());

    erhe::property::Dependency_object* const primitive = mesh->get_property_sub_object(0);
    Property_edit_operation operation{
        "assign foreign",
        [primitive, foreign]() {
            primitive->set_value(erhe::scene::Mesh_primitive::material_property, Object_reference{foreign});
        }
    };
    operation.execute(context);
    ASSERT_TRUE(operation.has_error());
    EXPECT_NE(operation.get_error().find("material"), std::string::npos) << operation.get_error();
    EXPECT_NE(operation.get_error().find("foreign"), std::string::npos) << operation.get_error();
    EXPECT_EQ(mesh->get_primitives()[0].material, local);
    EXPECT_EQ(hook_count(), std::size_t{0});
    EXPECT_TRUE(get_editor_glue().adopted_userships.empty());
}

TEST_F(Property_edit_operation_test, edit_that_writes_nothing_is_an_error)
{
    const std::shared_ptr<Light> light = std::make_shared<Light>("light");
    Property_edit_operation operation{
        "nothing",
        [light]() {
            static_cast<void>(light->get_value(Light::intensity_property));
        }
    };
    operation.execute(context);
    EXPECT_TRUE(operation.has_error());
    EXPECT_TRUE(operation.get_records().empty());
    EXPECT_EQ(hook_count(), std::size_t{0});
}

// D24: a mesh primitive has no seal of its own, so the store accepts the
// write, but apply_item_property would refuse it on undo / redo.
TEST_F(Property_edit_operation_test, primitive_write_on_a_sealed_mesh_is_an_error)
{
    const std::shared_ptr<erhe::primitive::Material> m1 = std::make_shared<erhe::primitive::Material>("m1");
    const std::shared_ptr<erhe::primitive::Material> m2 = std::make_shared<erhe::primitive::Material>("m2");
    const std::shared_ptr<erhe::scene::Mesh> mesh = std::make_shared<erhe::scene::Mesh>("mesh");
    mesh->add_primitive(make_primitive(), m1);
    mesh->seal();

    erhe::property::Dependency_object* const primitive = mesh->get_property_sub_object(0);
    Property_edit_operation operation{
        "assign on sealed",
        [primitive, m2]() {
            primitive->set_value(erhe::scene::Mesh_primitive::material_property, Object_reference{m2});
        }
    };
    operation.execute(context);
    ASSERT_TRUE(operation.has_error());
    EXPECT_NE(operation.get_error().find("sealed"), std::string::npos) << operation.get_error();
    EXPECT_TRUE(operation.get_records().empty());
    EXPECT_EQ(mesh->get_primitives()[0].material, m1);
    EXPECT_EQ(hook_count(), std::size_t{0});
}

// The edit function's capture may be the only owner of what it edits: the
// operation resolves the records before it releases the function.
TEST_F(Property_edit_operation_test, edit_function_capture_as_only_owner)
{
    std::weak_ptr<Light> weak;
    std::unique_ptr<Property_edit_operation> operation;
    {
        std::shared_ptr<Light> light = std::make_shared<Light>("owned_by_edit");
        weak = light;
        operation = std::make_unique<Property_edit_operation>(
            "owned",
            [light = std::move(light)]() {
                light->set_value(Light::intensity_property, 4.0f);
            }
        );
    }
    operation->execute(context);
    ASSERT_FALSE(operation->has_error()) << operation->get_error();
    ASSERT_EQ(operation->get_records().size(), std::size_t{1});
    const std::shared_ptr<Light> light = weak.lock();
    ASSERT_TRUE(light) << "the record keeps the item";
    operation->undo(context);
    EXPECT_FALSE(light->read_local_state(Light::intensity_property.get()).has_value());

    // An edit whose only item is refused: rollback runs before the release.
    std::weak_ptr<Light> refused_weak;
    std::unique_ptr<Property_edit_operation> refused;
    {
        std::shared_ptr<Light> sealed = std::make_shared<Light>("sealed_owned_by_edit");
        std::shared_ptr<Light> other  = std::make_shared<Light>("other_owned_by_edit");
        sealed->seal();
        refused_weak = other;
        refused = std::make_unique<Property_edit_operation>(
            "owned refused",
            [sealed = std::move(sealed), other = std::move(other)]() {
                other->set_value(Light::intensity_property, 4.0f);
                sealed->set_value(Light::intensity_property, 4.0f);
            }
        );
    }
    refused->execute(context);
    EXPECT_TRUE(refused->has_error());
    EXPECT_TRUE(refused_weak.expired()) << "released after the rollback";
}

// Compound_operation with Compound_child_error::roll_back (doc/editor/operations.md
// "Compound_operation"): a child in error after its first execute undoes the
// children before it and puts the compound in error; keep_siblings leaves
// them applied.
TEST_F(Property_edit_operation_test, compound_roll_back_undoes_the_children_before_a_refused_one)
{
    const std::shared_ptr<Light> a = std::make_shared<Light>("a");
    const std::shared_ptr<Light> b = std::make_shared<Light>("b");
    a->set_value(Light::intensity_property, 2.0f);
    b->seal();

    editor::Compound_operation compound{
        editor::Compound_operation::Parameters{
            .operations = {
                std::make_shared<Property_edit_operation>("accepted", [a]() { a->set_value(Light::intensity_property, 5.0f); }),
                std::make_shared<Property_edit_operation>("refused",  [b]() { b->set_value(Light::intensity_property, 6.0f); })
            },
            .child_error = editor::Compound_child_error::roll_back
        }
    };
    compound.execute(context);
    ASSERT_TRUE(compound.has_error());
    EXPECT_NE(compound.get_error().find("'b'"), std::string::npos) << compound.get_error();
    EXPECT_EQ(local_float(a->read_local_state(Light::intensity_property.get())), 2.0f);
    EXPECT_FALSE(b->read_local_state(Light::intensity_property.get()).has_value());
}

TEST_F(Property_edit_operation_test, compound_keep_siblings_leaves_the_accepted_children_applied)
{
    const std::shared_ptr<Light> a = std::make_shared<Light>("a");
    const std::shared_ptr<Light> b = std::make_shared<Light>("b");
    a->set_value(Light::intensity_property, 2.0f);
    b->seal();

    editor::Compound_operation compound{
        editor::Compound_operation::Parameters{
            .operations = {
                std::make_shared<Property_edit_operation>("accepted", [a]() { a->set_value(Light::intensity_property, 5.0f); }),
                std::make_shared<Property_edit_operation>("refused",  [b]() { b->set_value(Light::intensity_property, 6.0f); })
            }
        }
    };
    compound.execute(context);
    EXPECT_FALSE(compound.has_error());
    EXPECT_EQ(local_float(a->read_local_state(Light::intensity_property.get())), 5.0f);
    compound.undo(context);
    EXPECT_EQ(local_float(a->read_local_state(Light::intensity_property.get())), 2.0f);
}

// The seal (D24) orders an item's records (doc/editor/operations.md
// "Property_edit_operation"): lock_edit written after another property
// records [color, lock_edit]; undo lifts the seal before it restores the
// color, redo writes the color before it seals.
TEST_F(Property_edit_operation_test, seal_written_after_another_property_undoes_and_redoes_both)
{
    const std::shared_ptr<Light> light = std::make_shared<Light>("light");
    const glm::vec3 red{1.0f, 0.0f, 0.0f};

    Property_edit_operation operation{
        "color and seal",
        [light, red]() {
            light->set_value(Light::color_property, red);
            light->set_value(erhe::Item_base::lock_edit_property, true);
        }
    };
    operation.execute(context);
    ASSERT_FALSE(operation.has_error()) << operation.get_error();
    ASSERT_EQ(operation.get_records().size(), std::size_t{2});
    EXPECT_EQ(operation.get_records()[1].property, &erhe::Item_base::lock_edit_property.get());
    EXPECT_TRUE(light->is_sealed());

    operation.undo(context);
    EXPECT_FALSE(light->is_sealed());
    EXPECT_FALSE(light->is_lock_edit());
    EXPECT_FALSE(light->read_local_state(Light::color_property.get()).has_value()) << "the color is restored although the seal was recorded after it";

    operation.execute(context); // redo
    EXPECT_TRUE(light->is_sealed());
    EXPECT_EQ(light->get_value(Light::color_property), red) << "the color is written before the seal";

    operation.undo(context);
    EXPECT_FALSE(light->is_sealed());
    EXPECT_FALSE(light->read_local_state(Light::color_property.get()).has_value());
}

// The mirror case: on a sealed item the edit lifts the seal and then
// renames the item; undo restores the name before it re-seals, redo lifts
// the seal before it renames.
TEST_F(Property_edit_operation_test, unseal_followed_by_another_property_undoes_and_redoes_both)
{
    const std::shared_ptr<Light> light = std::make_shared<Light>("light");
    light->set_lock_edit(true);
    ASSERT_TRUE(light->is_sealed());

    Property_edit_operation operation{
        "unseal and rename",
        [light]() {
            light->set_value(erhe::Item_base::lock_edit_property, false);
            light->set_value(erhe::Item_base::name_property, std::string{"renamed"});
        }
    };
    operation.execute(context);
    ASSERT_FALSE(operation.has_error()) << operation.get_error();
    ASSERT_EQ(operation.get_records().size(), std::size_t{2});
    EXPECT_FALSE(light->is_sealed());
    EXPECT_EQ(light->get_name(), "renamed");

    operation.undo(context);
    EXPECT_TRUE(light->is_sealed());
    EXPECT_EQ(light->get_name(), "light") << "the name is restored before the seal";

    operation.execute(context); // redo
    EXPECT_FALSE(light->is_sealed());
    EXPECT_EQ(light->get_name(), "renamed") << "the seal is lifted before the name is written";

    operation.undo(context);
    EXPECT_TRUE(light->is_sealed());
    EXPECT_EQ(light->get_name(), "light");
}

// A write refused after the edit sealed the item: the rollback lifts the
// seal before it restores the accepted write, so nothing stays written.
TEST_F(Property_edit_operation_test, refused_write_after_a_seal_rolls_back_every_write)
{
    const std::shared_ptr<Light> light = std::make_shared<Light>("light");
    light->set_value(Light::intensity_property, 2.0f);

    Property_edit_operation operation{
        "intensity, seal, name",
        [light]() {
            light->set_value(Light::intensity_property, 5.0f);
            light->set_value(erhe::Item_base::lock_edit_property, true);
            light->set_value(erhe::Item_base::name_property, std::string{"renamed"}); // refused: sealed
        }
    };
    operation.execute(context);
    ASSERT_TRUE(operation.has_error());
    EXPECT_NE(operation.get_error().find("name"), std::string::npos) << operation.get_error();
    EXPECT_TRUE(operation.get_records().empty());
    EXPECT_EQ(hook_count(), std::size_t{0});
    EXPECT_FALSE(light->is_sealed());
    EXPECT_FALSE(light->is_lock_edit());
    EXPECT_EQ(local_float(light->read_local_state(Light::intensity_property.get())), 2.0f) << "the intensity written before the seal is restored";
    EXPECT_EQ(light->get_name(), "light");
}
