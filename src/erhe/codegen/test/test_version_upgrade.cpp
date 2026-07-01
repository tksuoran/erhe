#include "demo_params_serialization.hpp"
#include "demo_slot_serialization.hpp"

#include <erhe_codegen/migration.hpp>

#include <cassert>
#include <cstdio>
#include <string>

namespace {

// Clears the old-version flag, deserializes json into a fresh Demo_params, and returns
// whether deserialization detected an older-than-current schema version. This mirrors
// what erhe::codegen::load_config() does around deserialize().
auto params_upgrade_flag(const std::string& json) -> bool
{
    simdjson::ondemand::parser parser;
    simdjson::padded_string padded(json.data(), json.size());
    simdjson::ondemand::document doc;
    assert(!parser.iterate(padded).get(doc));
    simdjson::ondemand::object obj;
    assert(!doc.get_object().get(obj));
    erhe::codegen::clear_deserialized_old_version_flag();
    Demo_params out{};
    deserialize(obj, out);
    return erhe::codegen::deserialized_old_version();
}

auto slot_upgrade_flag(const std::string& json) -> bool
{
    simdjson::ondemand::parser parser;
    simdjson::padded_string padded(json.data(), json.size());
    simdjson::ondemand::document doc;
    assert(!parser.iterate(padded).get(doc));
    simdjson::ondemand::object obj;
    assert(!doc.get_object().get(obj));
    erhe::codegen::clear_deserialized_old_version_flag();
    Demo_slot out{};
    deserialize(obj, out);
    return erhe::codegen::deserialized_old_version();
}

// Demo_params is version 2, so a document at _version 1 is older -> flag set.
void test_old_version_sets_flag()
{
    std::printf("test_old_version_sets_flag... ");
    assert(params_upgrade_flag(R"({ "_version": 1, "scale": 1.5 })") == true);
    std::printf("PASSED\n");
}

// A document already at the current version does not set the flag; an absent _version
// defaults to 1, which for a v2 struct is still older -> flag set.
void test_current_version_no_flag()
{
    std::printf("test_current_version_no_flag... ");
    assert(params_upgrade_flag(R"({ "_version": 2, "scale": 1.5 })") == false);
    assert(params_upgrade_flag(R"({ "scale": 1.5 })") == true);
    std::printf("PASSED\n");
}

// The flag is tree-wide: Demo_slot is version 1 (current), but an old nested Demo_params
// (_version 1, current 2) still sets the flag; a current nested version does not.
void test_nested_old_version_sets_flag()
{
    std::printf("test_nested_old_version_sets_flag... ");
    assert(slot_upgrade_flag(R"({ "params": { "_version": 1, "scale": 1.5 } })") == true);
    assert(slot_upgrade_flag(R"({ "params": { "_version": 2, "scale": 1.5 } })") == false);
    std::printf("PASSED\n");
}

} // namespace

void run_version_upgrade_tests()
{
    test_old_version_sets_flag();
    test_current_version_no_flag();
    test_nested_old_version_sets_flag();
}
