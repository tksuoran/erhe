#include "demo_renamed_serialization.hpp"
#include "blend_mode.hpp"

#include "test_check.hpp"
#include <cstdio>
#include <string>

namespace {

auto read_renamed(const std::string& json) -> Demo_renamed
{
    simdjson::ondemand::parser parser;
    simdjson::padded_string padded(json.data(), json.size());
    simdjson::ondemand::document doc;
    CODEGEN_TEST_CHECK(!parser.iterate(padded).get(doc));
    simdjson::ondemand::object obj;
    CODEGEN_TEST_CHECK(!doc.get_object().get(obj));
    Demo_renamed out{};
    CODEGEN_TEST_CHECK(!deserialize(obj, out));
    return out;
}

// Each document version reads its field under the key name it was written with.
void test_previous_key_names_read()
{
    std::printf("test_previous_key_names_read... ");
    const Demo_renamed v1 = read_renamed(R"({ "title": "a" })");
    CODEGEN_TEST_CHECK(v1.label == "a");
    const Demo_renamed v2 = read_renamed(R"({ "_version": 2, "caption": "b", "random": true })");
    CODEGEN_TEST_CHECK((v2.label == "b") && v2.per_piece);
    const Demo_renamed v3 = read_renamed(R"({ "_version": 3, "label": "c", "per_piece": true })");
    CODEGEN_TEST_CHECK((v3.label == "c") && v3.per_piece);
    std::printf("PASSED\n");
}

// A current document's previous key names are not fields any more.
void test_previous_key_names_ignored_at_current_version()
{
    std::printf("test_previous_key_names_ignored_at_current_version... ");
    const Demo_renamed current = read_renamed(R"({ "_version": 3, "caption": "b", "random": true })");
    CODEGEN_TEST_CHECK(current.label.empty() && !current.per_piece);
    const Demo_renamed v2 = read_renamed(R"({ "_version": 2, "label": "c", "per_piece": true })");
    CODEGEN_TEST_CHECK(v2.label.empty() && !v2.per_piece);
    std::printf("PASSED\n");
}

// Serialization writes the current key names.
void test_current_key_names_written()
{
    std::printf("test_current_key_names_written... ");
    Demo_renamed value{};
    value.label     = "d";
    value.per_piece = true;
    const std::string text = serialize(value);
    CODEGEN_TEST_CHECK(text.find("\"label\"") != std::string::npos);
    CODEGEN_TEST_CHECK(text.find("\"per_piece\"") != std::string::npos);
    CODEGEN_TEST_CHECK(text.find("\"random\"") == std::string::npos);
    const Demo_renamed round_trip = read_renamed(text);
    CODEGEN_TEST_CHECK((round_trip.label == "d") && round_trip.per_piece);
    std::printf("PASSED\n");
}

// An enum value's former name reads as the value; to_string() gives the current name.
void test_enum_former_names_read()
{
    std::printf("test_enum_former_names_read... ");
    Blend_mode mode{Blend_mode::opaque};
    CODEGEN_TEST_CHECK(from_string("alpha", mode) && (mode == Blend_mode::alpha_blend));
    mode = Blend_mode::opaque;
    CODEGEN_TEST_CHECK(from_string("alpha_blend", mode) && (mode == Blend_mode::alpha_blend));
    CODEGEN_TEST_CHECK(to_string(Blend_mode::alpha_blend) == "alpha_blend");
    mode = Blend_mode::additive;
    CODEGEN_TEST_CHECK(!from_string("alpha_blend_old", mode) && (mode == Blend_mode::additive));
    std::printf("PASSED\n");
}

} // namespace

void run_renamed_field_tests()
{
    test_previous_key_names_read();
    test_previous_key_names_ignored_at_current_version();
    test_current_key_names_written();
    test_enum_former_names_read();
}
