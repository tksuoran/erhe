#include "demo_params_serialization.hpp"
#include "demo_slot_serialization.hpp"

#include "test_check.hpp"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

namespace {

auto approx(float a, float b) -> bool
{
    return std::fabs(a - b) < 0.0001f;
}

auto contains(const std::string& haystack, const char* needle) -> bool
{
    return haystack.find(needle) != std::string::npos;
}

auto parse_demo_params(const std::string& json, Demo_params& out) -> bool
{
    simdjson::ondemand::parser parser;
    simdjson::padded_string padded(json.data(), json.size());
    simdjson::ondemand::document doc;
    if (parser.iterate(padded).get(doc)) {
        return false;
    }
    simdjson::ondemand::object obj;
    if (doc.get_object().get(obj)) {
        return false;
    }
    return deserialize(obj, out) == simdjson::SUCCESS;
}

auto parse_demo_slot(const std::string& json, Demo_slot& out) -> bool
{
    simdjson::ondemand::parser parser;
    simdjson::padded_string padded(json.data(), json.size());
    simdjson::ondemand::document doc;
    if (parser.iterate(padded).get(doc)) {
        return false;
    }
    simdjson::ondemand::object obj;
    if (doc.get_object().get(obj)) {
        return false;
    }
    return deserialize(obj, out) == simdjson::SUCCESS;
}

// An all-default slot serializes to an empty object: no field keys at all,
// and in particular no nested "params" object.
void test_all_default_omitted()
{
    std::printf("test_all_default_omitted... ");

    Demo_slot slot{};
    const std::string json = serialize(slot);

    CODEGEN_TEST_CHECK(!contains(json, "name"));
    CODEGEN_TEST_CHECK(!contains(json, "params"));
    CODEGEN_TEST_CHECK(!contains(json, "scale"));
    CODEGEN_TEST_CHECK(!contains(json, "_version")); // Demo_slot is version 1

    // Deserializing into a fresh struct keeps every member-initializer default
    // (absent fields are not touched -- the editor always loads into a default object).
    Demo_slot loaded{};
    CODEGEN_TEST_CHECK(parse_demo_slot(json, loaded));
    CODEGEN_TEST_CHECK(loaded.name.empty());
    CODEGEN_TEST_CHECK(approx(loaded.params.scale, 1.0f));
    CODEGEN_TEST_CHECK(loaded.params.count == 3);
    CODEGEN_TEST_CHECK(approx(loaded.params.bias, 0.5f));
    CODEGEN_TEST_CHECK(loaded.params.enabled == true);

    std::printf("PASSED\n");
}

// A slot that tweaks a single nested field emits only that field (plus the
// nested struct's _version), and nothing else.
void test_partial_emission()
{
    std::printf("test_partial_emission... ");

    Demo_slot slot{};
    slot.params.scale = 2.0f;
    const std::string json = serialize(slot);

    CODEGEN_TEST_CHECK(contains(json, "params"));
    CODEGEN_TEST_CHECK(contains(json, "scale"));
    CODEGEN_TEST_CHECK(!contains(json, "name"));
    CODEGEN_TEST_CHECK(!contains(json, "count"));
    CODEGEN_TEST_CHECK(!contains(json, "bias"));
    CODEGEN_TEST_CHECK(!contains(json, "enabled"));

    Demo_slot loaded{};
    CODEGEN_TEST_CHECK(parse_demo_slot(json, loaded));
    CODEGEN_TEST_CHECK(loaded.name.empty());
    CODEGEN_TEST_CHECK(approx(loaded.params.scale, 2.0f));
    CODEGEN_TEST_CHECK(loaded.params.count == 3);
    CODEGEN_TEST_CHECK(approx(loaded.params.bias, 0.5f));
    CODEGEN_TEST_CHECK(loaded.params.enabled == true);

    std::printf("PASSED\n");
}

// A v1 document with bias absent reconstructs the v1 historical default (0.25f),
// NOT the current v2 default (0.5f). Re-serializing then emits bias explicitly,
// because 0.25f differs from the current default.
void test_versioned_default_old_document()
{
    std::printf("test_versioned_default_old_document... ");

    const std::string json = R"({ "_version": 1, "scale": 1.5 })";

    Demo_params params{};
    CODEGEN_TEST_CHECK(parse_demo_params(json, params));

    CODEGEN_TEST_CHECK(approx(params.scale, 1.5f));
    CODEGEN_TEST_CHECK(approx(params.bias, 0.25f)); // historical v1 default, not 0.5f
    CODEGEN_TEST_CHECK(params.count == 3);
    CODEGEN_TEST_CHECK(params.enabled == true);

    const std::string reserialized = serialize(params);
    CODEGEN_TEST_CHECK(contains(reserialized, "bias"));   // now differs from current default
    CODEGEN_TEST_CHECK(contains(reserialized, "scale"));
    CODEGEN_TEST_CHECK(!contains(reserialized, "count")); // still at default

    std::printf("PASSED\n");
}

// A current-version document with bias absent keeps the current default (0.5f),
// and re-serializing omits bias again.
void test_versioned_default_current_document()
{
    std::printf("test_versioned_default_current_document... ");

    const std::string json = R"({ "_version": 2, "scale": 1.5 })";

    Demo_params params{};
    CODEGEN_TEST_CHECK(parse_demo_params(json, params));

    CODEGEN_TEST_CHECK(approx(params.scale, 1.5f));
    CODEGEN_TEST_CHECK(approx(params.bias, 0.5f)); // current default kept

    const std::string reserialized = serialize(params);
    CODEGEN_TEST_CHECK(!contains(reserialized, "bias")); // omitted: equals current default
    CODEGEN_TEST_CHECK(contains(reserialized, "scale"));

    std::printf("PASSED\n");
}

// A struct that reduces to a single non-object member (here just its version key)
// serializes onto one line: "{ "_version": 2 }". A struct with more than one member
// stays multi-line.
void test_single_member_collapse()
{
    std::printf("test_single_member_collapse... ");

    Demo_params all_default{};
    CODEGEN_TEST_CHECK(serialize(all_default) == "{ \"_version\": 2 }");

    Demo_params two{};
    two.scale = 2.0f; // now _version + scale -> two members
    const std::string two_json = serialize(two);
    CODEGEN_TEST_CHECK(two_json.find('\n') != std::string::npos); // multi-line
    CODEGEN_TEST_CHECK(contains(two_json, "_version"));
    CODEGEN_TEST_CHECK(contains(two_json, "scale"));

    std::printf("PASSED\n");
}

} // namespace

void run_omit_defaults_tests()
{
    test_all_default_omitted();
    test_partial_emission();
    test_versioned_default_old_document();
    test_versioned_default_current_document();
    test_single_member_collapse();
}
