#include "level/kit_piece.hpp"

#include "erhe_property/dependency_property.hpp"
#include "erhe_property/property_string.hpp"
#include "erhe_scene/xform.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <string>

namespace {

using erhe::property::Property_registry;
using erhe::property::Property_value;

[[nodiscard]] auto make_attachment(const std::string& owner) -> editor::Kit_attachment
{
    return editor::Kit_attachment{
        .owner            = owner,
        .target           = "TargetUid0000001",
        .connector        = "start",
        .target_connector = "end",
        .twist_steps      = 3,
        .gap              = 0.1f
    };
}

[[nodiscard]] auto make_piece(const std::string& name) -> std::shared_ptr<erhe::scene::Xform>
{
    std::shared_ptr<erhe::scene::Xform> node = std::make_shared<erhe::scene::Xform>(name);
    editor::write_kit_piece(*node, editor::Kit_piece_data{.shape = "architecture.wall", .parameters = "{}"});
    return node;
}

} // anonymous namespace

TEST(Kit_attachment, text_round_trips_bit_exact)
{
    const editor::Kit_attachment attachment = make_attachment("OwnerUid00000001");
    const std::string text = editor::make_attachment_text(attachment);
    editor::Kit_attachment read;
    EXPECT_EQ(editor::parse_attachment_text(text, read), "");
    EXPECT_EQ(read, attachment);
    EXPECT_EQ(editor::make_attachment_text(read), text);
}

TEST(Kit_attachment, text_refusals)
{
    editor::Kit_attachment read;
    EXPECT_NE(editor::parse_attachment_text("", read), "");
    EXPECT_NE(editor::parse_attachment_text("[]", read), "");
    EXPECT_NE(editor::parse_attachment_text(R"({"owner":"a","target":"b","connector":"c","twist_steps":0,"gap":"0x0p+0"})", read), "");
    EXPECT_NE(editor::parse_attachment_text(R"({"owner":"a","target":"b","connector":"c","target_connector":"d","twist_steps":0.5,"gap":"0x0p+0"})", read), "");
    EXPECT_NE(editor::parse_attachment_text(R"({"owner":"a","target":"b","connector":"c","target_connector":"d","twist_steps":0,"gap":0.0})", read), "");
    EXPECT_NE(editor::parse_attachment_text(R"({"owner":"a","target":"b","connector":"c","target_connector":"d","twist_steps":4,"gap":"0x0p+0"})", read), "");
    EXPECT_NE(editor::parse_attachment_text(R"({"owner":"a","target":"b","connector":"c","target_connector":"d","twist_steps":-1,"gap":"0x0p+0"})", read), "");
    EXPECT_NE(editor::parse_attachment_text(R"({"owner":"","target":"b","connector":"c","target_connector":"d","twist_steps":0,"gap":"0x0p+0"})", read), "");
}

// The record rides the node's saved values as a scene save writes them, and reads back on the loaded node of the same uid.
TEST(Kit_attachment, record_round_trips_through_the_saved_text)
{
    std::shared_ptr<erhe::scene::Xform> node = make_piece("attached");
    const editor::Kit_attachment attachment = make_attachment(node->get_gltf_uid());
    node->set_value(editor::Kit_piece::attachment_property, editor::make_attachment_text(attachment));
    ASSERT_EQ(editor::read_kit_attachment(*node), std::optional<editor::Kit_attachment>{attachment});

    const Property_registry&                   registry = Property_registry::get();
    const erhe::property::Dependency_property& property = editor::Kit_piece::attachment_property.get();
    const std::optional<Property_value>        value    = node->read_local_value(property);
    ASSERT_TRUE(value.has_value());
    const std::string name = registry.qualified_name(*node, property);
    const std::string text = erhe::property::to_string(property, value.value());
    EXPECT_EQ(name, "Kit_piece.attachment");

    std::shared_ptr<erhe::scene::Xform> loaded = make_piece("loaded");
    loaded->set_gltf_uid(node->get_gltf_uid());
    const erhe::property::Dependency_property* const found = registry.find_for_object(*loaded, name);
    ASSERT_EQ(found, &property);
    const std::optional<Property_value> parsed = erhe::property::parse_value(property, text);
    ASSERT_TRUE(parsed.has_value());
    ASSERT_TRUE(loaded->set_value(property, parsed.value()));
    EXPECT_EQ(editor::read_kit_attachment(*loaded), std::optional<editor::Kit_attachment>{attachment});
}

// A copy of the record on another node (a clone, a paste) names another owner: the copy follows nothing.
TEST(Kit_attachment, a_record_of_another_owner_is_inert)
{
    std::shared_ptr<erhe::scene::Xform> node = make_piece("attached");
    std::shared_ptr<erhe::scene::Xform> copy = make_piece("copy");
    ASSERT_NE(node->get_gltf_uid(), copy->get_gltf_uid());
    copy->set_value(editor::Kit_piece::attachment_property, editor::make_attachment_text(make_attachment(node->get_gltf_uid())));
    EXPECT_FALSE(editor::read_kit_attachment(*copy).has_value());

    std::shared_ptr<erhe::scene::Xform> plain = std::make_shared<erhe::scene::Xform>("not a piece");
    EXPECT_FALSE(editor::read_kit_attachment(*plain).has_value());
}
