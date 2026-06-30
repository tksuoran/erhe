#include "demo_params_serialization.hpp"
#include "demo_slot_serialization.hpp"

#include <cassert>
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

    assert(!contains(json, "name"));
    assert(!contains(json, "params"));
    assert(!contains(json, "scale"));
    assert(!contains(json, "_version")); // Demo_slot is version 1

    // Deserializing into a fresh struct keeps every member-initializer default
    // (absent fields are not touched -- the editor always loads into a default object).
    Demo_slot loaded{};
    assert(parse_demo_slot(json, loaded));
    assert(loaded.name.empty());
    assert(approx(loaded.params.scale, 1.0f));
    assert(loaded.params.count == 3);
    assert(approx(loaded.params.bias, 0.5f));
    assert(loaded.params.enabled == true);

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

    assert(contains(json, "params"));
    assert(contains(json, "scale"));
    assert(!contains(json, "name"));
    assert(!contains(json, "count"));
    assert(!contains(json, "bias"));
    assert(!contains(json, "enabled"));

    Demo_slot loaded{};
    assert(parse_demo_slot(json, loaded));
    assert(loaded.name.empty());
    assert(approx(loaded.params.scale, 2.0f));
    assert(loaded.params.count == 3);
    assert(approx(loaded.params.bias, 0.5f));
    assert(loaded.params.enabled == true);

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
    assert(parse_demo_params(json, params));

    assert(approx(params.scale, 1.5f));
    assert(approx(params.bias, 0.25f)); // historical v1 default, not 0.5f
    assert(params.count == 3);
    assert(params.enabled == true);

    const std::string reserialized = serialize(params);
    assert(contains(reserialized, "bias"));   // now differs from current default
    assert(contains(reserialized, "scale"));
    assert(!contains(reserialized, "count")); // still at default

    std::printf("PASSED\n");
}

// A current-version document with bias absent keeps the current default (0.5f),
// and re-serializing omits bias again.
void test_versioned_default_current_document()
{
    std::printf("test_versioned_default_current_document... ");

    const std::string json = R"({ "_version": 2, "scale": 1.5 })";

    Demo_params params{};
    assert(parse_demo_params(json, params));

    assert(approx(params.scale, 1.5f));
    assert(approx(params.bias, 0.5f)); // current default kept

    const std::string reserialized = serialize(params);
    assert(!contains(reserialized, "bias")); // omitted: equals current default
    assert(contains(reserialized, "scale"));

    std::printf("PASSED\n");
}

} // namespace

void run_omit_defaults_tests()
{
    test_all_default_omitted();
    test_partial_emission();
    test_versioned_default_old_document();
    test_versioned_default_current_document();
}
