#include "level/kit_item_slot.hpp"
#include "level/kit_shape.hpp"
#include "level/kit_shape_library.hpp"
#include "kit_shape_test_support.hpp"

#include "config/generated/inventory_slot.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace {

using editor::Kit_item;
using editor::Kit_item_slot_reading;
using editor::Kit_parameter_values;
using editor::Kit_shape_definition;
using editor::Kit_shape_library;
using editor::kit_test::get_library;
using editor::kit_test::get_shape;

// A kit item of the shape at its defaults, each slot taking the palette's last material.
auto make_item(const Kit_shape_definition& shape) -> Kit_item
{
    const std::string&       last_material = get_library().get_palette().back().name;
    std::vector<std::string> materials(shape.material_slots.size(), last_material);
    return Kit_item{editor::kit_test::defaults(shape), materials};
}

TEST(Kit_item_slot, write_then_read_is_the_identity)
{
    for (const char* id : {"architecture.wall", "nature.tree", "basic.block"}) {
        const Kit_item item = make_item(get_shape(id));
        ASSERT_TRUE(item.is_valid()) << id;
        Inventory_slot slot;
        editor::write_kit_item_slot(item, slot);
        EXPECT_EQ(slot.kit_shape, id);
        EXPECT_EQ(slot.kit_parameters, item.get_values().to_canonical_text());
        EXPECT_EQ(slot.kit_materials, item.get_slot_materials());

        const Kit_item_slot_reading reading = editor::read_kit_item_slot(get_library(), slot);
        ASSERT_TRUE(reading.item.has_value()) << id << ": " << reading.problem;
        EXPECT_TRUE(reading.problem.empty());
        EXPECT_EQ(reading.item->get_shape(), item.get_shape());
        EXPECT_TRUE(reading.item->get_values() == item.get_values()) << id;
        EXPECT_EQ(reading.item->get_slot_materials(), item.get_slot_materials());

        Inventory_slot written_again;
        editor::write_kit_item_slot(reading.item.value(), written_again);
        EXPECT_EQ(written_again.kit_shape,      slot.kit_shape);
        EXPECT_EQ(written_again.kit_parameters, slot.kit_parameters);
        EXPECT_EQ(written_again.kit_materials,  slot.kit_materials);
    }
}

TEST(Kit_item_slot, edited_values_and_derive_mask_survive)
{
    const Kit_shape_definition& shape  = get_shape("architecture.wall");
    Kit_parameter_values        values = editor::kit_test::defaults(shape);
    ASSERT_TRUE(values.set_float("length", 1.5f));
    const Kit_item item{values, std::vector<std::string>{}};
    Inventory_slot slot;
    editor::write_kit_item_slot(item, slot);
    const Kit_item_slot_reading reading = editor::read_kit_item_slot(get_library(), slot);
    ASSERT_TRUE(reading.item.has_value()) << reading.problem;
    EXPECT_FLOAT_EQ(reading.item->get_values().get_float("length"), 1.5f);
    EXPECT_TRUE(reading.item->get_values() == values);
    EXPECT_TRUE(reading.item->get_slot_materials().empty());
}

TEST(Kit_item_slot, a_former_id_reads_as_its_shape)
{
    const Kit_shape_definition& shape = get_shape("basic.block");
    ASSERT_FALSE(shape.former_ids.empty());
    const Kit_item item = make_item(shape);
    Inventory_slot slot;
    editor::write_kit_item_slot(item, slot);
    slot.kit_shape = shape.former_ids.front();
    const Kit_item_slot_reading reading = editor::read_kit_item_slot(get_library(), slot);
    ASSERT_TRUE(reading.item.has_value()) << reading.problem;
    EXPECT_EQ(reading.item->get_shape(), &shape);
    Inventory_slot written;
    editor::write_kit_item_slot(reading.item.value(), written);
    EXPECT_EQ(written.kit_shape, "basic.block");
}

TEST(Kit_item_slot, a_slot_without_a_kit_shape_reads_as_no_item_without_a_problem)
{
    Inventory_slot slot;
    slot.tool_name = "Brush";
    const Kit_item_slot_reading reading = editor::read_kit_item_slot(get_library(), slot);
    EXPECT_FALSE(reading.item.has_value());
    EXPECT_TRUE(reading.problem.empty());
}

TEST(Kit_item_slot, unknown_ids_malformed_text_and_bad_materials_give_no_item_and_a_reason)
{
    const Kit_item item = make_item(get_shape("architecture.wall"));
    Inventory_slot     good;
    editor::write_kit_item_slot(item, good);

    Inventory_slot unknown = good;
    unknown.kit_shape = "basic.no_such_shape";
    const Kit_item_slot_reading unknown_reading = editor::read_kit_item_slot(get_library(), unknown);
    EXPECT_FALSE(unknown_reading.item.has_value());
    EXPECT_NE(unknown_reading.problem.find("basic.no_such_shape"), std::string::npos) << unknown_reading.problem;

    Inventory_slot malformed = good;
    malformed.kit_parameters = "{\"width\": ";
    const Kit_item_slot_reading malformed_reading = editor::read_kit_item_slot(get_library(), malformed);
    EXPECT_FALSE(malformed_reading.item.has_value());
    EXPECT_FALSE(malformed_reading.problem.empty());

    Inventory_slot out_of_range = good; // a wall length must be above zero
    Kit_parameter_values values = item.get_values();
    const std::size_t    length = get_shape("architecture.wall").find_parameter("length").value();
    values.set_value_source(length, editor::Kit_value_source::explicit_value); // a derived value is not read from the text
    ASSERT_TRUE(values.set_float("length", -1.0f));
    out_of_range.kit_parameters = values.to_canonical_text();
    const Kit_item_slot_reading range_reading = editor::read_kit_item_slot(get_library(), out_of_range);
    EXPECT_FALSE(range_reading.item.has_value());
    EXPECT_FALSE(range_reading.problem.empty());

    Inventory_slot unknown_material = good;
    ASSERT_FALSE(unknown_material.kit_materials.empty());
    unknown_material.kit_materials.front() = "No Such Material";
    const Kit_item_slot_reading material_reading = editor::read_kit_item_slot(get_library(), unknown_material);
    EXPECT_FALSE(material_reading.item.has_value());
    EXPECT_NE(material_reading.problem.find("No Such Material"), std::string::npos) << material_reading.problem;

    Inventory_slot too_many = good;
    too_many.kit_materials.push_back(too_many.kit_materials.front());
    const Kit_item_slot_reading too_many_reading = editor::read_kit_item_slot(get_library(), too_many);
    EXPECT_FALSE(too_many_reading.item.has_value());
    EXPECT_FALSE(too_many_reading.problem.empty());
}

TEST(Kit_item_slot, the_tooltip_names_the_shape_values_and_materials)
{
    const Kit_shape_definition& shape = get_shape("architecture.wall");
    const Kit_item              item  = make_item(shape);
    const std::string           text  = editor::make_kit_item_tooltip(item);
    EXPECT_NE(text.find(shape.display_name), std::string::npos);
    EXPECT_NE(text.find(shape.id), std::string::npos);
    EXPECT_NE(text.find("length: "), std::string::npos) << text;
    EXPECT_NE(text.find(get_library().get_palette().back().name), std::string::npos) << text;
}

} // namespace
