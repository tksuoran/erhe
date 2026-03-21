#include "settings.hpp"

#include <erhe_codegen/migration.hpp>

#include <cassert>
#include <cmath>
#include <cstdio>

#if defined(_MSC_VER)
#   pragma warning(push)
#   pragma warning(disable : 4996)
#elif defined(__GNUC__) || defined(__clang__)
#   pragma GCC diagnostic push
#   pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif

static bool migration_was_called = false;

void register_settings_migrations()
{
    erhe::codegen::register_migration<Settings>(
        [](Settings& s, uint32_t old_version, uint32_t /*new_version*/) -> bool
        {
            migration_was_called = true;

            // v1/v2 -> v3: gamma was removed, use it to derive custom_dpi
            // (fictional migration for testing purposes)
            if (old_version < 3) {
                s.custom_dpi = s.gamma * 40.0f; // e.g. gamma 2.2 -> DPI 88
            }
            return true;
        }
    );
}

void test_migration_from_v2()
{
    std::printf("test_migration_from_v2... ");

    // v2 JSON: has gamma (active in v1..v3), render_scale (added in v2), no custom_dpi
    const char* json = R"({
        "_version": 2,
        "window_width": 1280,
        "window_height": 720,
        "fullscreen": true,
        "render_scale": 0.75,
        "username": "testuser",
        "gamma": 2.5
    })";

    simdjson::ondemand::parser parser;
    simdjson::padded_string padded(json, std::strlen(json));
    simdjson::ondemand::document doc;
    auto error = parser.iterate(padded).get(doc);
    assert(!error);

    simdjson::ondemand::object obj;
    error = doc.get_object().get(obj);
    assert(!error);

    Settings settings{};
    migration_was_called = false;
    error = deserialize(obj, settings);
    assert(!error);

    // Verify fields were deserialized correctly
    assert(settings.window_width == 1280);
    assert(settings.window_height == 720);
    assert(settings.fullscreen == true);
    assert(std::abs(settings.render_scale - 0.75f) < 0.001f);
    assert(settings.username == "testuser");

    // Verify deprecated gamma field was read (active in v2)
    assert(std::abs(settings.gamma - 2.5f) < 0.001f);

    // Verify migration callback was called
    assert(migration_was_called);

    // Verify migration transformed the struct: gamma 2.5 * 40 = 100.0
    assert(settings.custom_dpi.has_value());
    assert(std::abs(settings.custom_dpi.value() - 100.0f) < 0.001f);

    std::printf("PASSED\n");
}

void test_migration_from_v1()
{
    std::printf("test_migration_from_v1... ");

    // v1 JSON: no render_scale (added in v2), no custom_dpi (added in v3)
    const char* json = R"({
        "_version": 1,
        "window_width": 800,
        "window_height": 600,
        "fullscreen": false,
        "username": "olduser",
        "gamma": 1.8
    })";

    simdjson::ondemand::parser parser;
    simdjson::padded_string padded(json, std::strlen(json));
    simdjson::ondemand::document doc;
    auto error = parser.iterate(padded).get(doc);
    assert(!error);

    simdjson::ondemand::object obj;
    error = doc.get_object().get(obj);
    assert(!error);

    Settings settings{};
    migration_was_called = false;
    error = deserialize(obj, settings);
    assert(!error);

    // Verify v1 fields
    assert(settings.window_width == 800);
    assert(settings.window_height == 600);
    assert(settings.fullscreen == false);
    assert(settings.username == "olduser");

    // render_scale was not in v1, should keep default
    assert(std::abs(settings.render_scale - 1.0f) < 0.001f);

    // gamma was read (active in v1)
    assert(std::abs(settings.gamma - 1.8f) < 0.001f);

    // Migration ran
    assert(migration_was_called);

    // gamma 1.8 * 40 = 72.0
    assert(settings.custom_dpi.has_value());
    assert(std::abs(settings.custom_dpi.value() - 72.0f) < 0.001f);

    std::printf("PASSED\n");
}

void test_no_migration_for_current_version()
{
    std::printf("test_no_migration_for_current_version... ");

    // v3 JSON: current version, no migration should run
    const char* json = R"({
        "_version": 3,
        "window_width": 1920,
        "window_height": 1080,
        "fullscreen": false,
        "render_scale": 1.0,
        "username": "user3",
        "custom_dpi": 96.0,
        "display_name": "User Three"
    })";

    simdjson::ondemand::parser parser;
    simdjson::padded_string padded(json, std::strlen(json));
    simdjson::ondemand::document doc;
    auto error = parser.iterate(padded).get(doc);
    assert(!error);

    simdjson::ondemand::object obj;
    error = doc.get_object().get(obj);
    assert(!error);

    Settings settings{};
    migration_was_called = false;
    error = deserialize(obj, settings);
    assert(!error);

    // Migration should NOT have been called (version matches current)
    assert(!migration_was_called);

    // Verify fields
    assert(settings.window_width == 1920);
    assert(settings.window_height == 1080);
    assert(std::abs(settings.render_scale - 1.0f) < 0.001f);
    assert(settings.username == "user3");
    assert(settings.custom_dpi.has_value());
    assert(std::abs(settings.custom_dpi.value() - 96.0f) < 0.001f);
    assert(settings.display_name.has_value());
    assert(settings.display_name.value() == "User Three");

    std::printf("PASSED\n");
}

void test_no_version_key_defaults_to_v1()
{
    std::printf("test_no_version_key_defaults_to_v1... ");

    // No _version key: should default to v1 and trigger migration
    const char* json = R"({
        "window_width": 640,
        "window_height": 480,
        "fullscreen": false,
        "username": "legacy",
        "gamma": 2.2
    })";

    simdjson::ondemand::parser parser;
    simdjson::padded_string padded(json, std::strlen(json));
    simdjson::ondemand::document doc;
    auto error = parser.iterate(padded).get(doc);
    assert(!error);

    simdjson::ondemand::object obj;
    error = doc.get_object().get(obj);
    assert(!error);

    Settings settings{};
    migration_was_called = false;
    error = deserialize(obj, settings);
    assert(!error);

    // Migration should have been called (default v1 != current v3)
    assert(migration_was_called);

    // gamma 2.2 * 40 = 88.0
    assert(settings.custom_dpi.has_value());
    assert(std::abs(settings.custom_dpi.value() - 88.0f) < 0.001f);

    std::printf("PASSED\n");
}

#if defined(_MSC_VER)
#   pragma warning(pop)
#elif defined(__GNUC__) || defined(__clang__)
#   pragma GCC diagnostic pop
#endif

int main()
{
    register_settings_migrations();

    test_migration_from_v2();
    test_migration_from_v1();
    test_no_migration_for_current_version();
    test_no_version_key_defaults_to_v1();

    std::printf("\nAll migration tests passed!\n");
    return 0;
}
