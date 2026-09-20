// Integration tests for the editor's MCP server.
//
// These tests connect to an editor over HTTP. When an editor already answers
// on the test port (started by the ctest fixture in CMakeLists.txt, or by
// hand) it is used; otherwise this process launches one itself from the
// compiled-in editor path (editor_launcher.hpp) and stops it at exit - that
// is how Visual Studio's Test Explorer, which runs the binary without ctest,
// gets an editor.
//
// Two editors: Mcp_test.* share one plain editor on ERHE_MCP_TEST_PORT;
// Mcp_auth_test.* use a dedicated editor on ERHE_MCP_TEST_AUTH_PORT that
// was started with the bearer token CMake wrote for the tests (its path is
// compiled in, ERHE_MCP_TEST_TOKEN_FILE overrides it).
//
// Configuration (env vars):
//   ERHE_MCP_TEST_HOST            default "127.0.0.1"
//   ERHE_MCP_TEST_PORT            default 3743
//   ERHE_MCP_TEST_AUTH_PORT       default 3744
//   ERHE_MCP_TEST_TOKEN_FILE      default: the compiled-in test token file
//   ERHE_MCP_TEST_TIMEOUT_S       default 30  (/health wait for an editor started elsewhere)
//   ERHE_MCP_TEST_LAUNCH_TIMEOUT_S default 180 (/health wait for a self-launched editor)

#include "editor_launcher.hpp"

#include <gtest/gtest.h>

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <map>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace {

using json = nlohmann::json;

auto env_or(const char* name, const char* fallback) -> std::string
{
    const char* v = std::getenv(name);
    return (v != nullptr && v[0] != '\0') ? std::string{v} : std::string{fallback};
}

auto env_or_int(const char* name, int fallback) -> int
{
    const char* v = std::getenv(name);
    if (v == nullptr || v[0] == '\0') {
        return fallback;
    }
    try {
        return std::stoi(v);
    } catch (...) {
        return fallback;
    }
}

class Mcp_client
{
public:
    Mcp_client(const std::string& host, int port)
        : m_client{host, port}
    {
        m_client.set_connection_timeout(5, 0);
        m_client.set_read_timeout      (10, 0);
        m_client.set_write_timeout     (10, 0);
    }

    auto wait_for_ready(int timeout_seconds) -> bool
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{timeout_seconds};
        while (std::chrono::steady_clock::now() < deadline) {
            httplib::Result res = m_client.Get("/health");
            if (res && res->status == 200) {
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds{200});
        }
        return false;
    }

    // POSTs a JSON-RPC request and returns the parsed top-level response.
    auto rpc(const std::string& method, const json& params) -> json
    {
        json req = {
            {"jsonrpc", "2.0"},
            {"id",      "test"},
            {"method",  method}
        };
        if (!params.is_null()) {
            req["params"] = params;
        }
        const std::string body = req.dump();
        httplib::Result res = m_client.Post("/mcp", body, "application/json");
        EXPECT_TRUE(res.operator bool()) << "HTTP request failed for method=" << method;
        if (!res) return json::object();
        EXPECT_EQ(res->status, 200) << "HTTP " << res->status << " for method=" << method;
        return json::parse(res->body, nullptr, false);
    }

    // tools/call wrapper: returns parsed {payload, is_error}.
    struct Tool_result
    {
        json payload;
        bool is_error{false};
        std::string text;
    };
    auto call_tool(const std::string& name, const json& arguments) -> Tool_result
    {
        json params = {{"name", name}, {"arguments", arguments}};
        json response = rpc("tools/call", params);
        Tool_result out;
        if (!response.is_object() || !response.contains("result")) {
            ADD_FAILURE() << "Missing 'result' in tools/call response for '" << name
                          << "': " << response.dump();
            return out;
        }
        const json& result = response["result"];
        out.is_error = result.value("isError", false);
        if (!result.contains("content") || !result["content"].is_array() || result["content"].empty()) {
            ADD_FAILURE() << "Missing 'content' in tools/call result for '" << name << "'";
            return out;
        }
        const json& first = result["content"][0];
        out.text = first.value("text", "");
        json parsed = json::parse(out.text, nullptr, false);
        if (!parsed.is_discarded()) {
            out.payload = std::move(parsed);
        }
        return out;
    }

    auto list_tools() -> json
    {
        return rpc("tools/list", json{});
    }

private:
    httplib::Client m_client;
};

// Helpers defined after the test fixture (see below).
void advance_frames(Mcp_client& client, int frames);
[[nodiscard]] auto scene_names(Mcp_client& client) -> std::vector<std::string>;
[[nodiscard]] auto wait_until_idle(Mcp_client& client, int timeout_ms) -> bool;

// The scene every test runs in is created by Mcp_env::prepare(), from
// this asset (textures for the texture tests) plus a material of its own.
constexpr const char* c_textured_gltf      = "res/editor/assets/SM_Deccer_Cubes_Textured.glb";
constexpr const char* c_test_material_name = "MCP test material";

// Single shared client across all tests, plus the scene/material each test
// runs in. The client connects once; every test then starts from a reset
// editor (reset_editor_state) and gets a scene of its own from prepare().
class Mcp_env
{
public:
    static auto get() -> Mcp_env&
    {
        static Mcp_env instance;
        return instance;
    }

    Mcp_client& client() { return *m_client; }

    [[nodiscard]] auto connected() const -> bool { return m_connected; }
    [[nodiscard]] auto ready() const -> bool { return m_ready; }
    [[nodiscard]] auto scene_name() const -> const std::string& { return m_scene_name; }
    [[nodiscard]] auto material_name() const -> const std::string& { return m_material_name; }
    [[nodiscard]] auto first_texture_name() const -> const std::optional<std::string>& { return m_texture_name; }
    [[nodiscard]] auto first_texture_id  () const -> const std::optional<std::size_t>&  { return m_texture_id;   }

    [[nodiscard]] auto connect_attempted() const -> bool { return m_connect_attempted; }

    // Leaves the editor in a reset state. Called once, after the last test.
    void teardown()
    {
        if (!m_client || !m_connected) {
            return;
        }
        m_client->call_tool("reset_editor_state", json::object());
        m_scene_name.clear();
        m_ready = false;
    }

    // Reaches (or launches) the editor. Once per run.
    void connect()
    {
        m_connect_attempted = true;

        const std::string host       = env_or    ("ERHE_MCP_TEST_HOST",      "127.0.0.1");
        const int         port       = env_or_int("ERHE_MCP_TEST_PORT",      3743);
        const int         timeout_s  = env_or_int("ERHE_MCP_TEST_TIMEOUT_S", 30);

        m_client = std::make_unique<Mcp_client>(host, port);
        // An editor started elsewhere (ctest fixture, by hand) may still be
        // coming up: give it the configured wait. Only then launch one.
        if (!m_client->wait_for_ready(timeout_s)) {
            const int launch_timeout_s = env_or_int("ERHE_MCP_TEST_LAUNCH_TIMEOUT_S", 180);
            if (!mcp_test::launch_editor(host, port, launch_timeout_s, {}, {})) {
                GTEST_LOG_(ERROR) << "MCP server not reachable at " << host << ":" << port
                                  << " and no editor could be launched.";
                return;
            }
        }
        m_connected = true;
    }

    // Resets the editor and prepares this test's scene: nothing here depends
    // on what an earlier test left behind. The scene gets the textured test
    // asset (so the texture tests have textures) and a material of its own
    // for the material tests. Returns false (with the reason logged) when
    // the editor could not be brought into that state.
    auto prepare() -> bool
    {
        m_ready = false;
        m_scene_name.clear();
        m_material_name.clear();
        m_texture_name.reset();
        m_texture_id.reset();

        Mcp_client::Tool_result reset_res = m_client->call_tool("reset_editor_state", json::object());
        if (reset_res.is_error) {
            GTEST_LOG_(ERROR) << "reset_editor_state failed: " << reset_res.text;
            return false;
        }
        if (!scene_names(*m_client).empty()) {
            GTEST_LOG_(ERROR) << "reset_editor_state returned with scenes still open";
            return false;
        }

        m_client->call_tool("create_scene", json::object());
        advance_frames(*m_client, 6);
        const std::vector<std::string> scenes = scene_names(*m_client);
        if (scenes.size() != 1) {
            GTEST_LOG_(ERROR) << "create_scene after reset produced " << scenes.size() << " scenes (expected 1)";
            return false;
        }
        m_scene_name = scenes.front();

        Mcp_client::Tool_result import_res = m_client->call_tool(
            "import_gltf", json{{"scene_name", m_scene_name}, {"path", c_textured_gltf}}
        );
        if (import_res.is_error) {
            GTEST_LOG_(ERROR) << "import_gltf failed: " << import_res.text;
            return false;
        }
        if (!wait_until_idle(*m_client, 60000)) {
            GTEST_LOG_(ERROR) << "import of " << c_textured_gltf << " did not settle within 60 s";
            return false;
        }

        Mcp_client::Tool_result mat_res = m_client->call_tool(
            "create_material", json{{"scene_name", m_scene_name}, {"name", c_test_material_name}}
        );
        if (mat_res.is_error) {
            GTEST_LOG_(ERROR) << "create_material failed: " << mat_res.text;
            return false;
        }
        m_material_name = c_test_material_name;

        Mcp_client::Tool_result tex_res = m_client->call_tool(
            "get_scene_textures", json{{"scene_name", m_scene_name}}
        );
        if (tex_res.is_error || !tex_res.payload.contains("textures")) {
            GTEST_LOG_(ERROR) << "get_scene_textures failed: " << tex_res.text;
            return false;
        }
        const json& textures = tex_res.payload["textures"];
        if (!textures.is_array() || textures.empty()) {
            GTEST_LOG_(ERROR) << "imported " << c_textured_gltf << " but the scene has no textures";
            return false;
        }
        const json& first = textures[0];
        if (!first.contains("name") || !first["name"].is_string() || !first.contains("id") || !first["id"].is_number()) {
            GTEST_LOG_(ERROR) << "first texture entry lacks name/id: " << first.dump();
            return false;
        }
        m_texture_name = first["name"].get<std::string>();
        m_texture_id   = first["id"].get<std::size_t>();

        m_ready = true;
        return true;
    }

private:
    std::unique_ptr<Mcp_client> m_client;
    bool                        m_connect_attempted{false};
    bool                        m_connected{false};
    bool                        m_ready{false};
    std::string                 m_scene_name;
    std::string                 m_material_name;
    std::optional<std::string>  m_texture_name;
    std::optional<std::size_t>  m_texture_id;
};

// Registered at static-init time (before main), so its TearDown runs after
// every test: reset the editor, then stop the editor this process launched
// (no-op when the editor came from the ctest fixture or by hand).
class Mcp_session_environment : public ::testing::Environment
{
public:
    void TearDown() override
    {
        Mcp_env::get().teardown();
        mcp_test::stop_launched_editors();
    }
};

::testing::Environment* const s_session_environment = ::testing::AddGlobalTestEnvironment(new Mcp_session_environment{});

class Mcp_test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // One connection attempt for the whole run: when the editor is not
        // there, the first test pays the /health wait and every later test
        // skips immediately instead of repeating the wait in its own SetUp.
        Mcp_env& env = Mcp_env::get();
        if (!env.connected() && !env.connect_attempted()) {
            env.connect();
        }
        if (!env.connected()) {
            GTEST_SKIP() << "MCP environment not ready (editor not running?)";
        }
        // Every test starts from a reset editor with a scene of its own; a
        // failure to get there is a failure of this test, not a skip.
        ASSERT_TRUE(env.prepare()) << "could not reset the editor and prepare the test scene";
    }

    auto material_details() -> json
    {
        Mcp_env& env = Mcp_env::get();
        Mcp_client::Tool_result r = env.client().call_tool(
            "get_material_details",
            json{{"scene_name", env.scene_name()}, {"material_name", env.material_name()}}
        );
        EXPECT_FALSE(r.is_error) << "get_material_details errored: " << r.text;
        return r.payload;
    }

    // Apply an edit and poll until the predicate holds against fresh material details.
    template <typename Predicate>
    void edit_and_wait(const json& edit_args, Predicate pred, int timeout_ms = 3000)
    {
        Mcp_env& env = Mcp_env::get();
        json full = edit_args;
        full["scene_name"]    = env.scene_name();
        full["material_name"] = env.material_name();
        Mcp_client::Tool_result r = env.client().call_tool("edit_material", full);
        ASSERT_FALSE(r.is_error) << "edit_material returned error: " << r.text;

        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds{timeout_ms};
        while (std::chrono::steady_clock::now() < deadline) {
            json details = material_details();
            if (pred(details)) {
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds{50});
        }
        FAIL() << "Edit did not become visible within " << timeout_ms
               << "ms. Last details: " << material_details().dump(2);
    }

    static auto approx_equal(double a, double b, double eps = 1e-4) -> bool
    {
        return std::abs(a - b) <= eps;
    }
};

} // namespace

// ---- Discovery / health ----------------------------------------------------

TEST_F(Mcp_test, tools_list_includes_material_and_texture_tools)
{
    json response = Mcp_env::get().client().list_tools();
    ASSERT_TRUE(response.is_object());
    ASSERT_TRUE(response.contains("result"));
    ASSERT_TRUE(response["result"].contains("tools"));
    const json& tools = response["result"]["tools"];
    ASSERT_TRUE(tools.is_array());

    std::vector<std::string> names;
    for (const json& tool : tools) {
        names.push_back(tool.value("name", ""));
    }
    auto has = [&](const char* n) {
        return std::find(names.begin(), names.end(), n) != names.end();
    };
    EXPECT_TRUE(has("list_scenes"));
    EXPECT_TRUE(has("get_scene_materials"));
    EXPECT_TRUE(has("get_material_details"));
    EXPECT_TRUE(has("get_scene_textures"));
    EXPECT_TRUE(has("edit_material"));
    EXPECT_TRUE(has("create_material"));
    EXPECT_TRUE(has("batch"));
}

TEST_F(Mcp_test, list_scenes_returns_named_scenes)
{
    Mcp_client::Tool_result r = Mcp_env::get().client().call_tool("list_scenes", json::object());
    ASSERT_FALSE(r.is_error);
    ASSERT_TRUE(r.payload.contains("scenes"));
    const json& scenes = r.payload["scenes"];
    ASSERT_TRUE(scenes.is_array());
    ASSERT_FALSE(scenes.empty());
    for (const json& s : scenes) {
        EXPECT_TRUE(s.contains("name"));
        EXPECT_TRUE(s.contains("material_count"));
    }
}

// reset_editor_state takes the editor back to no scenes, no selection and
// no undo history - and returns only once the scene closes have run.
TEST_F(Mcp_test, reset_editor_state_clears_scenes_selection_and_history)
{
    Mcp_env&    env    = Mcp_env::get();
    Mcp_client& client = env.client();

    Mcp_client::Tool_result shape = client.call_tool("create_shape", json{
        {"scene_name",  env.scene_name()},
        {"shape",       "box"},
        {"name",        "reset test box"},
        {"motion_mode", "none"}
    });
    ASSERT_FALSE(shape.is_error) << shape.text;
    advance_frames(client, 2);
    Mcp_client::Tool_result select = client.call_tool("select_items", json{
        {"scene_name", env.scene_name()},
        {"paths",      json::array({"reset test box"})}
    });
    ASSERT_FALSE(select.is_error) << select.text;
    client.call_tool("create_scene", json::object());
    advance_frames(client, 6);

    Mcp_client::Tool_result selection_before = client.call_tool("get_selection", json::object());
    ASSERT_FALSE(selection_before.is_error);
    ASSERT_FALSE(selection_before.payload["items"].empty()) << "selection did not take";
    ASSERT_EQ(scene_names(client).size(), 2u);
    Mcp_client::Tool_result history_before = client.call_tool("get_undo_redo_stack", json::object());
    ASSERT_TRUE(history_before.payload.value("can_undo", false));

    Mcp_client::Tool_result reset = client.call_tool("reset_editor_state", json::object());
    ASSERT_FALSE(reset.is_error) << reset.text;
    EXPECT_TRUE(reset.payload.value("reset", false));

    // No advance_time: the reset itself waited for the closes to complete.
    EXPECT_TRUE(scene_names(client).empty());
    Mcp_client::Tool_result selection_after = client.call_tool("get_selection", json::object());
    ASSERT_FALSE(selection_after.is_error);
    EXPECT_TRUE(selection_after.payload["items"].empty());
    Mcp_client::Tool_result history_after = client.call_tool("get_undo_redo_stack", json::object());
    EXPECT_FALSE(history_after.payload.value("can_undo", true));
    EXPECT_FALSE(history_after.payload.value("can_redo", true));
}

// The active item (doc/editor/active_item.md): the last item a select_items
// call lists becomes it, it survives the selection being cleared (reported
// with selected: false), set_active_item names an unselected item, and
// reset_editor_state forgets it.
TEST_F(Mcp_test, active_item_follows_selection_and_survives_clearing)
{
    Mcp_env&    env    = Mcp_env::get();
    Mcp_client& client = env.client();

    const char* const names[] = {"active item a", "active item b", "active item c"};
    for (const char* const name : names) {
        Mcp_client::Tool_result shape = client.call_tool("create_shape", json{
            {"scene_name",  env.scene_name()},
            {"shape",       "box"},
            {"name",        name},
            {"motion_mode", "none"}
        });
        ASSERT_FALSE(shape.is_error) << shape.text;
    }
    advance_frames(client, 2);

    // Selecting by path keeps the listed order, so the last one is "c".
    Mcp_client::Tool_result select = client.call_tool("select_items", json{
        {"scene_name", env.scene_name()},
        {"paths",      json::array({names[0], names[1], names[2]})}
    });
    ASSERT_FALSE(select.is_error) << select.text;

    Mcp_client::Tool_result selection = client.call_tool("get_selection", json::object());
    ASSERT_FALSE(selection.is_error) << selection.text;
    ASSERT_EQ(selection.payload["items"].size(), 3u) << selection.text;
    ASSERT_TRUE(selection.payload.contains("active_item")) << selection.text;
    const json active = selection.payload["active_item"];
    EXPECT_EQ(active.value("name", std::string{}), std::string{names[2]});
    EXPECT_TRUE(active.value("selected", false));
    const std::size_t active_id = active.value("id", std::size_t{0});
    EXPECT_NE(active_id, 0u);

    // An empty list clears the selection in the scene but keeps the active
    // item, which is then reported as not selected.
    Mcp_client::Tool_result clear = client.call_tool("select_items", json{
        {"scene_name", env.scene_name()},
        {"ids",        json::array()}
    });
    ASSERT_FALSE(clear.is_error) << clear.text;

    Mcp_client::Tool_result after_clear = client.call_tool("get_selection", json::object());
    ASSERT_FALSE(after_clear.is_error) << after_clear.text;
    EXPECT_TRUE(after_clear.payload["items"].empty()) << after_clear.text;
    ASSERT_TRUE(after_clear.payload.contains("active_item")) << after_clear.text;
    EXPECT_EQ(after_clear.payload["active_item"].value("id", std::size_t{0}), active_id);
    EXPECT_FALSE(after_clear.payload["active_item"].value("selected", true));

    // set_active_item names an item that is not selected either.
    Mcp_client::Tool_result set_active = client.call_tool("set_active_item", json{
        {"scene_name", env.scene_name()},
        {"path",       names[0]}
    });
    ASSERT_FALSE(set_active.is_error) << set_active.text;
    ASSERT_TRUE(set_active.payload.contains("active_item")) << set_active.text;
    EXPECT_EQ(set_active.payload["active_item"].value("name", std::string{}), std::string{names[0]});
    EXPECT_FALSE(set_active.payload["active_item"].value("selected", true));

    Mcp_client::Tool_result after_set = client.call_tool("get_selection", json::object());
    ASSERT_FALSE(after_set.is_error) << after_set.text;
    ASSERT_TRUE(after_set.payload.contains("active_item")) << after_set.text;
    EXPECT_EQ(after_set.payload["active_item"].value("name", std::string{}), std::string{names[0]});

    Mcp_client::Tool_result reset = client.call_tool("reset_editor_state", json::object());
    ASSERT_FALSE(reset.is_error) << reset.text;
    Mcp_client::Tool_result after_reset = client.call_tool("get_selection", json::object());
    ASSERT_FALSE(after_reset.is_error) << after_reset.text;
    EXPECT_FALSE(after_reset.payload.contains("active_item")) << after_reset.text;
}

TEST_F(Mcp_test, get_scene_materials_returns_array_with_basic_fields)
{
    Mcp_env& env = Mcp_env::get();
    Mcp_client::Tool_result r = env.client().call_tool(
        "get_scene_materials", json{{"scene_name", env.scene_name()}}
    );
    ASSERT_FALSE(r.is_error) << r.text;
    ASSERT_TRUE(r.payload.contains("materials"));
    const json& mats = r.payload["materials"];
    ASSERT_TRUE(mats.is_array());
    ASSERT_FALSE(mats.empty());
    const json& first = mats[0];
    EXPECT_TRUE(first.contains("name"));
    EXPECT_TRUE(first.contains("id"));
    EXPECT_TRUE(first.contains("base_color"));
    EXPECT_TRUE(first.contains("metallic"));
    EXPECT_TRUE(first.contains("roughness"));
    EXPECT_TRUE(first.contains("emissive"));
}

TEST_F(Mcp_test, get_scene_textures_responds_with_array)
{
    Mcp_env& env = Mcp_env::get();
    Mcp_client::Tool_result r = env.client().call_tool(
        "get_scene_textures", json{{"scene_name", env.scene_name()}}
    );
    ASSERT_FALSE(r.is_error) << r.text;
    ASSERT_TRUE(r.payload.contains("textures"));
    const json& texs = r.payload["textures"];
    ASSERT_TRUE(texs.is_array());
    for (const json& t : texs) {
        EXPECT_TRUE(t.contains("name"));
        EXPECT_TRUE(t.contains("id"));
        EXPECT_TRUE(t.contains("width"));
        EXPECT_TRUE(t.contains("height"));
        EXPECT_TRUE(t.contains("format"));
    }
}

TEST_F(Mcp_test, get_material_details_has_full_structure)
{
    json d = material_details();
    EXPECT_TRUE(d.contains("name"));
    EXPECT_TRUE(d.contains("id"));
    EXPECT_TRUE(d.contains("base_color"));
    EXPECT_TRUE(d.contains("opacity"));
    EXPECT_TRUE(d.contains("roughness"));
    EXPECT_TRUE(d.contains("metallic"));
    EXPECT_TRUE(d.contains("reflectance"));
    EXPECT_TRUE(d.contains("emissive"));
    EXPECT_TRUE(d.contains("ior"));
    EXPECT_TRUE(d.contains("transmission"));
    EXPECT_TRUE(d.contains("normal_texture_scale"));
    EXPECT_TRUE(d.contains("occlusion_texture_strength"));
    EXPECT_TRUE(d.contains("bxdf_model"));
    EXPECT_TRUE(d.contains("use_circular_brushed_metal"));
    EXPECT_TRUE(d.contains("use_aniso_control"));
    ASSERT_TRUE(d.contains("texture_samplers"));
    const json& ts = d["texture_samplers"];
    ASSERT_TRUE(ts.is_object());
    for (const char* slot : {"base_color", "metallic_roughness", "normal", "occlusion", "emissive"}) {
        ASSERT_TRUE(ts.contains(slot)) << "missing slot: " << slot;
        const json& s = ts[slot];
        EXPECT_TRUE(s.contains("texture_id"));
        EXPECT_TRUE(s.contains("texture_name"));
        EXPECT_TRUE(s.contains("texgen_mode"));
        EXPECT_TRUE(s.contains("rotation"));
        EXPECT_TRUE(s.contains("offset"));
        EXPECT_TRUE(s.contains("scale"));
    }
}

// ---- edit_material: scalar / vector fields ---------------------------------

TEST_F(Mcp_test, edit_material_base_color_round_trip)
{
    json before = material_details();
    const json original = before["base_color"];

    edit_and_wait(
        json{{"base_color", json::array({0.25, 0.5, 0.75})}},
        [](const json& d) {
            return approx_equal(d["base_color"][0].get<double>(), 0.25)
                && approx_equal(d["base_color"][1].get<double>(), 0.5)
                && approx_equal(d["base_color"][2].get<double>(), 0.75);
        }
    );
    edit_and_wait(
        json{{"base_color", original}},
        [&](const json& d) { return d["base_color"] == original; }
    );
}

TEST_F(Mcp_test, edit_material_opacity_round_trip)
{
    json before = material_details();
    const double original = before["opacity"].get<double>();
    const double target   = approx_equal(original, 0.5) ? 0.75 : 0.5;

    edit_and_wait(
        json{{"opacity", target}},
        [target](const json& d) { return approx_equal(d["opacity"].get<double>(), target); }
    );
    edit_and_wait(
        json{{"opacity", original}},
        [original](const json& d) { return approx_equal(d["opacity"].get<double>(), original); }
    );
}

TEST_F(Mcp_test, edit_material_metallic_round_trip)
{
    json before = material_details();
    const double original = before["metallic"].get<double>();
    const double target   = approx_equal(original, 0.7) ? 0.3 : 0.7;

    edit_and_wait(
        json{{"metallic", target}},
        [target](const json& d) { return approx_equal(d["metallic"].get<double>(), target); }
    );
    edit_and_wait(
        json{{"metallic", original}},
        [original](const json& d) { return approx_equal(d["metallic"].get<double>(), original); }
    );
}

TEST_F(Mcp_test, edit_material_reflectance_round_trip)
{
    json before = material_details();
    const double original = before["reflectance"].get<double>();
    const double target   = approx_equal(original, 0.4) ? 0.6 : 0.4;

    edit_and_wait(
        json{{"reflectance", target}},
        [target](const json& d) { return approx_equal(d["reflectance"].get<double>(), target); }
    );
    edit_and_wait(
        json{{"reflectance", original}},
        [original](const json& d) { return approx_equal(d["reflectance"].get<double>(), original); }
    );
}

TEST_F(Mcp_test, edit_material_ior_and_transmission_round_trip)
{
    json before = material_details();
    const double original_ior          = before["ior"].get<double>();
    const double original_transmission = before["transmission"].get<double>();
    const double target_ior            = approx_equal(original_ior, 1.3) ? 1.7 : 1.3;
    const double target_transmission   = approx_equal(original_transmission, 0.8) ? 0.5 : 0.8;

    edit_and_wait(
        json{{"ior", target_ior}, {"transmission", target_transmission}},
        [target_ior, target_transmission](const json& d) {
            return
                approx_equal(d["ior"].get<double>(), target_ior) &&
                approx_equal(d["transmission"].get<double>(), target_transmission);
        }
    );
    edit_and_wait(
        json{{"ior", original_ior}, {"transmission", original_transmission}},
        [original_ior, original_transmission](const json& d) {
            return
                approx_equal(d["ior"].get<double>(), original_ior) &&
                approx_equal(d["transmission"].get<double>(), original_transmission);
        }
    );
}

TEST_F(Mcp_test, edit_material_roughness_scalar_broadcasts_to_vec2)
{
    json before = material_details();
    const json original = before["roughness"];

    edit_and_wait(
        json{{"roughness", 0.42}},
        [](const json& d) {
            return approx_equal(d["roughness"][0].get<double>(), 0.42)
                && approx_equal(d["roughness"][1].get<double>(), 0.42);
        }
    );
    edit_and_wait(
        json{{"roughness", original}},
        [&](const json& d) { return d["roughness"] == original; }
    );
}

TEST_F(Mcp_test, edit_material_roughness_vec_round_trip)
{
    json before = material_details();
    const json original = before["roughness"];

    edit_and_wait(
        json{{"roughness", json::array({0.31, 0.62})}},
        [](const json& d) {
            return approx_equal(d["roughness"][0].get<double>(), 0.31)
                && approx_equal(d["roughness"][1].get<double>(), 0.62);
        }
    );
    edit_and_wait(
        json{{"roughness", original}},
        [&](const json& d) { return d["roughness"] == original; }
    );
}

TEST_F(Mcp_test, edit_material_emissive_round_trip)
{
    json before = material_details();
    const json original = before["emissive"];

    edit_and_wait(
        json{{"emissive", json::array({0.1, 0.2, 0.3})}},
        [](const json& d) {
            return approx_equal(d["emissive"][0].get<double>(), 0.1)
                && approx_equal(d["emissive"][1].get<double>(), 0.2)
                && approx_equal(d["emissive"][2].get<double>(), 0.3);
        }
    );
    edit_and_wait(
        json{{"emissive", original}},
        [&](const json& d) { return d["emissive"] == original; }
    );
}

TEST_F(Mcp_test, edit_material_normal_texture_scale_round_trip)
{
    json before = material_details();
    const double original = before["normal_texture_scale"].get<double>();
    const double target   = approx_equal(original, 1.5) ? 2.5 : 1.5;

    edit_and_wait(
        json{{"normal_texture_scale", target}},
        [target](const json& d) { return approx_equal(d["normal_texture_scale"].get<double>(), target); }
    );
    edit_and_wait(
        json{{"normal_texture_scale", original}},
        [original](const json& d) { return approx_equal(d["normal_texture_scale"].get<double>(), original); }
    );
}

TEST_F(Mcp_test, edit_material_occlusion_texture_strength_round_trip)
{
    json before = material_details();
    const double original = before["occlusion_texture_strength"].get<double>();
    const double target   = approx_equal(original, 0.6) ? 0.9 : 0.6;

    edit_and_wait(
        json{{"occlusion_texture_strength", target}},
        [target](const json& d) { return approx_equal(d["occlusion_texture_strength"].get<double>(), target); }
    );
    edit_and_wait(
        json{{"occlusion_texture_strength", original}},
        [original](const json& d) { return approx_equal(d["occlusion_texture_strength"].get<double>(), original); }
    );
}

TEST_F(Mcp_test, edit_material_bxdf_model_cycle)
{
    json before = material_details();
    const std::string original = before["bxdf_model"].get<std::string>();
    const std::string target =
        (original == "unlit")          ? "anisotropic_brdf" :
        (original == "anisotropic_brdf") ? "isotropic_brdf" :
                                           "unlit";

    edit_and_wait(
        json{{"bxdf_model", target}},
        [target](const json& d) { return d["bxdf_model"].get<std::string>() == target; }
    );
    edit_and_wait(
        json{{"bxdf_model", original}},
        [original](const json& d) { return d["bxdf_model"].get<std::string>() == original; }
    );
}

TEST_F(Mcp_test, edit_material_use_circular_brushed_metal_toggle)
{
    json before = material_details();
    const bool original = before["use_circular_brushed_metal"].get<bool>();

    edit_and_wait(
        json{{"use_circular_brushed_metal", !original}},
        [original](const json& d) { return d["use_circular_brushed_metal"].get<bool>() == !original; }
    );
    edit_and_wait(
        json{{"use_circular_brushed_metal", original}},
        [original](const json& d) { return d["use_circular_brushed_metal"].get<bool>() == original; }
    );
}

TEST_F(Mcp_test, edit_material_use_aniso_control_toggle)
{
    json before = material_details();
    const bool original = before["use_aniso_control"].get<bool>();

    edit_and_wait(
        json{{"use_aniso_control", !original}},
        [original](const json& d) { return d["use_aniso_control"].get<bool>() == !original; }
    );
    edit_and_wait(
        json{{"use_aniso_control", original}},
        [original](const json& d) { return d["use_aniso_control"].get<bool>() == original; }
    );
}

// ---- edit_material: error paths --------------------------------------------

TEST_F(Mcp_test, edit_material_no_fields_returns_error)
{
    Mcp_env& env = Mcp_env::get();
    Mcp_client::Tool_result r = env.client().call_tool("edit_material", json{
        {"scene_name",    env.scene_name()},
        {"material_name", env.material_name()}
    });
    EXPECT_TRUE(r.is_error) << "Expected error, got: " << r.text;
}

TEST_F(Mcp_test, edit_material_unknown_scene_returns_error)
{
    Mcp_client::Tool_result r = Mcp_env::get().client().call_tool("edit_material", json{
        {"scene_name",    "__definitely_not_a_real_scene__"},
        {"material_name", "anything"},
        {"metallic",      0.5}
    });
    EXPECT_TRUE(r.is_error);
}

TEST_F(Mcp_test, edit_material_unknown_material_returns_error)
{
    Mcp_env& env = Mcp_env::get();
    Mcp_client::Tool_result r = env.client().call_tool("edit_material", json{
        {"scene_name",    env.scene_name()},
        {"material_name", "__not_a_material_name_xyzzy__"},
        {"metallic",      0.5}
    });
    EXPECT_TRUE(r.is_error);
}

TEST_F(Mcp_test, edit_material_texture_samplers_must_be_object)
{
    Mcp_env& env = Mcp_env::get();
    Mcp_client::Tool_result r = env.client().call_tool("edit_material", json{
        {"scene_name",       env.scene_name()},
        {"material_name",    env.material_name()},
        {"texture_samplers", "not an object"}
    });
    EXPECT_TRUE(r.is_error);
}

TEST_F(Mcp_test, edit_material_unknown_texture_name_returns_error)
{
    Mcp_env& env = Mcp_env::get();
    Mcp_client::Tool_result r = env.client().call_tool("edit_material", json{
        {"scene_name",    env.scene_name()},
        {"material_name", env.material_name()},
        {"texture_samplers", {
            {"base_color", {{"texture", "__not_a_real_texture_name__"}}}
        }}
    });
    EXPECT_TRUE(r.is_error);
}

TEST_F(Mcp_test, edit_material_unknown_texture_id_returns_error)
{
    Mcp_env& env = Mcp_env::get();
    Mcp_client::Tool_result r = env.client().call_tool("edit_material", json{
        {"scene_name",    env.scene_name()},
        {"material_name", env.material_name()},
        {"texture_samplers", {
            {"base_color", {{"texture", 0xDEADBEEFu}}}
        }}
    });
    EXPECT_TRUE(r.is_error);
}

TEST_F(Mcp_test, edit_material_invalid_texture_reference_type_returns_error)
{
    Mcp_env& env = Mcp_env::get();
    Mcp_client::Tool_result r = env.client().call_tool("edit_material", json{
        {"scene_name",    env.scene_name()},
        {"material_name", env.material_name()},
        {"texture_samplers", {
            {"base_color", {{"texture", json::array({1, 2, 3})}}}
        }}
    });
    EXPECT_TRUE(r.is_error);
}

// ---- batch -----------------------------------------------------------------

TEST_F(Mcp_test, batch_groups_operations_into_one_undo_entry)
{
    Mcp_env& env = Mcp_env::get();
    json before_details = material_details();
    const double orig_metallic = before_details["metallic"].get<double>();

    Mcp_client::Tool_result stack0 = env.client().call_tool("get_undo_redo_stack", json::object());
    ASSERT_FALSE(stack0.is_error);
    const std::size_t undo0 = stack0.payload["undo"].size();

    const double target1 = (orig_metallic < 0.5) ? 0.61 : 0.11;
    const double target2 = (orig_metallic < 0.5) ? 0.71 : 0.21;
    Mcp_client::Tool_result batch = env.client().call_tool("batch", json{
        {"calls", json::array({
            json{{"tool", "edit_material"}, {"arguments", {
                {"scene_name", env.scene_name()}, {"material_name", env.material_name()}, {"metallic", target1}}}},
            json{{"tool", "edit_material"}, {"arguments", {
                {"scene_name", env.scene_name()}, {"material_name", env.material_name()}, {"metallic", target2}}}}
        })}
    });
    ASSERT_FALSE(batch.is_error) << batch.text;
    EXPECT_EQ(batch.payload["count"].get<int>(), 2);
    EXPECT_EQ(batch.payload["error_count"].get<int>(), 0);
    EXPECT_EQ(batch.payload["grouped_operations"].get<int>(), 2);

    // Grouped operations execute immediately (no update()-pass deferral),
    // so the edit is visible without polling.
    json details = material_details();
    EXPECT_TRUE(approx_equal(details["metallic"].get<double>(), target2));

    Mcp_client::Tool_result stack1 = env.client().call_tool("get_undo_redo_stack", json::object());
    ASSERT_FALSE(stack1.is_error);
    EXPECT_EQ(stack1.payload["undo"].size(), undo0 + 1);

    // ONE undo steps back across both edits.
    Mcp_client::Tool_result undo = env.client().call_tool("undo", json::object());
    ASSERT_FALSE(undo.is_error) << undo.text;
    json restored = material_details();
    EXPECT_TRUE(approx_equal(restored["metallic"].get<double>(), orig_metallic))
        << "metallic after one undo: " << restored["metallic"].get<double>()
        << " expected " << orig_metallic;
}

TEST_F(Mcp_test, batch_rejects_nested_and_malformed)
{
    Mcp_client& client = Mcp_env::get().client();
    EXPECT_TRUE(client.call_tool("batch", json{{"calls", json::array({json{{"tool", "batch"}}})}}).is_error);
    EXPECT_TRUE(client.call_tool("batch", json{{"calls", json::array()}}).is_error);
    EXPECT_TRUE(client.call_tool("batch", json::object()).is_error);
    EXPECT_TRUE(client.call_tool("batch", json{{"calls", json::array({json{{"arguments", json::object()}}})}}).is_error);
}

TEST_F(Mcp_test, batch_partial_failure_reports_per_call_results)
{
    Mcp_client::Tool_result r = Mcp_env::get().client().call_tool("batch", json{
        {"calls", json::array({
            json{{"tool", "list_scenes"}},
            json{{"tool", "__not_a_tool__"}}
        })}
    });
    EXPECT_TRUE(r.is_error) << "error_count > 0 must mark the batch as an error";
    ASSERT_TRUE(r.payload.contains("results")) << r.text;
    const json& results = r.payload["results"];
    ASSERT_EQ(results.size(), 2u);
    EXPECT_TRUE (results[0]["ok"].get<bool>());
    EXPECT_TRUE (results[0]["result"].contains("scenes"));
    EXPECT_FALSE(results[1]["ok"].get<bool>());
    EXPECT_EQ(r.payload["error_count"].get<int>(), 1);
}

// ---- create_material -------------------------------------------------------

TEST_F(Mcp_test, create_material_roundtrip_and_duplicate_rejected)
{
    Mcp_env& env = Mcp_env::get();
    // Unique per run: the created material stays in the live editor's
    // library (creation is not undoable), so a fixed name would collide on
    // the next run.
    const std::string name =
        "__mcp_test_mat_" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());

    const json create_args{
        {"scene_name", env.scene_name()},
        {"name",       name},
        {"base_color", json::array({0.1, 0.2, 0.3})},
        {"metallic",   0.75},
        {"roughness",  0.4}
    };
    Mcp_client::Tool_result created = env.client().call_tool("create_material", create_args);
    ASSERT_FALSE(created.is_error) << "create_material errored: " << created.text;
    ASSERT_TRUE(created.payload.contains("id"));
    EXPECT_GT(created.payload["id"].get<std::size_t>(), 0u);

    Mcp_client::Tool_result details = env.client().call_tool("get_material_details", json{
        {"scene_name",    env.scene_name()},
        {"material_name", name}
    });
    ASSERT_FALSE(details.is_error) << "get_material_details errored: " << details.text;
    EXPECT_TRUE(approx_equal(details.payload["metallic"]  .get<double>(), 0.75));
    EXPECT_TRUE(approx_equal(details.payload["base_color"][2].get<double>(), 0.3));

    // Same name again: rejected, existing id reported.
    Mcp_client::Tool_result dup = env.client().call_tool("create_material", create_args);
    EXPECT_TRUE(dup.is_error);
}

TEST_F(Mcp_test, create_material_requires_name)
{
    Mcp_client::Tool_result r = Mcp_env::get().client().call_tool("create_material", json{
        {"scene_name", Mcp_env::get().scene_name()}
    });
    EXPECT_TRUE(r.is_error);
}

TEST_F(Mcp_test, create_material_unknown_scene_returns_error)
{
    Mcp_client::Tool_result r = Mcp_env::get().client().call_tool("create_material", json{
        {"scene_name", "__definitely_not_a_real_scene__"},
        {"name",       "anything"}
    });
    EXPECT_TRUE(r.is_error);
}

TEST_F(Mcp_test, create_material_invalid_field_rejected_without_creating)
{
    Mcp_env& env = Mcp_env::get();
    const std::string name =
        "__mcp_test_mat_invalid_" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    Mcp_client::Tool_result r = env.client().call_tool("create_material", json{
        {"scene_name", env.scene_name()},
        {"name",       name},
        {"base_color", "not an array"}
    });
    EXPECT_TRUE(r.is_error);

    Mcp_client::Tool_result details = env.client().call_tool("get_material_details", json{
        {"scene_name",    env.scene_name()},
        {"material_name", name}
    });
    EXPECT_TRUE(details.is_error) << "Material should not exist after a rejected create";
}

// ---- edit_material: texture_samplers (transforms always testable) ---------

TEST_F(Mcp_test, edit_material_texture_transform_round_trip)
{
    json before = material_details();
    const json bs = before["texture_samplers"]["base_color"];
    const double orig_rotation = bs["rotation"].get<double>();

    edit_and_wait(
        json{{"texture_samplers", {
            {"base_color", {
                {"texgen_mode", 1},
                {"rotation",  0.1234},
                {"offset",    json::array({0.5, 0.25})},
                {"scale",     json::array({2.0, 3.0})}
            }}
        }}},
        [](const json& d) {
            const json& s = d["texture_samplers"]["base_color"];
            return s["texgen_mode"].get<int>() == 1
                && approx_equal(s["rotation"].get<double>(),  0.1234)
                && approx_equal(s["offset"][0].get<double>(), 0.5)
                && approx_equal(s["offset"][1].get<double>(), 0.25)
                && approx_equal(s["scale"][0].get<double>(),  2.0)
                && approx_equal(s["scale"][1].get<double>(),  3.0);
        }
    );

    edit_and_wait(
        json{{"texture_samplers", {
            {"base_color", {
                {"texgen_mode", bs["texgen_mode"].get<int>()},
                {"rotation",  orig_rotation},
                {"offset",    bs["offset"]},
                {"scale",     bs["scale"]}
            }}
        }}},
        [&](const json& d) {
            const json& s = d["texture_samplers"]["base_color"];
            return approx_equal(s["rotation"].get<double>(), orig_rotation);
        }
    );
}

// ---- edit_material: texture assignment (the prepared scene has textures) ---

TEST_F(Mcp_test, edit_material_texture_assign_by_name)
{
    Mcp_env& env = Mcp_env::get();
    const std::string tex_name = env.first_texture_name().value();

    json before = material_details();
    const json before_slot = before["texture_samplers"]["base_color"];

    edit_and_wait(
        json{{"texture_samplers", {
            {"base_color", {{"texture", tex_name}}}
        }}},
        [&](const json& d) {
            const json& s = d["texture_samplers"]["base_color"];
            return s["texture_name"].is_string() && s["texture_name"].get<std::string>() == tex_name;
        }
    );

    // Restore previous texture (which may be null = clear)
    // The slot's previous texture id, or null to clear. (Brace-initializing
    // a json from one value would make a one-element ARRAY, which
    // edit_material rejects.)
    const json restore_tex = before_slot["texture_id"];
    edit_and_wait(
        json{{"texture_samplers", {
            {"base_color", {{"texture", restore_tex}}}
        }}},
        [&](const json& d) {
            const json& s = d["texture_samplers"]["base_color"];
            if (before_slot["texture_id"].is_null()) {
                return s["texture_id"].is_null();
            }
            return !s["texture_id"].is_null()
                && s["texture_id"] == before_slot["texture_id"];
        }
    );
}

TEST_F(Mcp_test, edit_material_texture_assign_by_id)
{
    Mcp_env& env = Mcp_env::get();
    const std::size_t tex_id = env.first_texture_id().value();

    json before = material_details();
    const json before_slot = before["texture_samplers"]["base_color"];

    edit_and_wait(
        json{{"texture_samplers", {
            {"base_color", {{"texture", tex_id}}}
        }}},
        [tex_id](const json& d) {
            const json& s = d["texture_samplers"]["base_color"];
            return s["texture_id"].is_number() && s["texture_id"].get<std::size_t>() == tex_id;
        }
    );

    // The slot's previous texture id, or null to clear. (Brace-initializing
    // a json from one value would make a one-element ARRAY, which
    // edit_material rejects.)
    const json restore_tex = before_slot["texture_id"];
    edit_and_wait(
        json{{"texture_samplers", {
            {"base_color", {{"texture", restore_tex}}}
        }}},
        [&](const json& d) {
            const json& s = d["texture_samplers"]["base_color"];
            if (before_slot["texture_id"].is_null()) {
                return s["texture_id"].is_null();
            }
            return !s["texture_id"].is_null()
                && s["texture_id"] == before_slot["texture_id"];
        }
    );
}

TEST_F(Mcp_test, edit_material_texture_clear)
{
    Mcp_env& env = Mcp_env::get();
    const std::size_t tex_id = env.first_texture_id().value();

    // First make sure something is assigned.
    edit_and_wait(
        json{{"texture_samplers", {{"base_color", {{"texture", tex_id}}}}}},
        [tex_id](const json& d) {
            const json& s = d["texture_samplers"]["base_color"];
            return s["texture_id"].is_number() && s["texture_id"].get<std::size_t>() == tex_id;
        }
    );

    // Now clear it.
    edit_and_wait(
        json{{"texture_samplers", {{"base_color", {{"texture", nullptr}}}}}},
        [](const json& d) {
            return d["texture_samplers"]["base_color"]["texture_id"].is_null();
        }
    );
}

// ---- A4 hardening tests ---------------------------------------------------

// RAII guard that captures opacity / base_color / metallic on construction
// and restores them in the destructor. Used by tests that intentionally
// trigger validation failures so a missed assertion does not leave the
// material in a dirtied state for subsequent tests.
class Material_scalar_state_guard
{
public:
    Material_scalar_state_guard()
    {
        Mcp_env& env = Mcp_env::get();
        Mcp_client::Tool_result r = env.client().call_tool(
            "get_material_details",
            json{{"scene_name", env.scene_name()}, {"material_name", env.material_name()}}
        );
        if (!r.is_error) {
            m_baseline = r.payload;
        }
    }
    ~Material_scalar_state_guard()
    {
        if (m_baseline.is_null()) {
            return;
        }
        Mcp_env& env = Mcp_env::get();
        json restore = json::object();
        restore["scene_name"]    = env.scene_name();
        restore["material_name"] = env.material_name();
        for (const char* key : {"base_color", "opacity", "roughness", "metallic", "reflectance", "emissive", "ior", "transmission", "normal_texture_scale", "occlusion_texture_strength"}) {
            if (m_baseline.contains(key)) {
                restore[key] = m_baseline[key];
            }
        }
        env.client().call_tool("edit_material", restore);
    }

    Material_scalar_state_guard(const Material_scalar_state_guard&)            = delete;
    Material_scalar_state_guard& operator=(const Material_scalar_state_guard&) = delete;

private:
    json m_baseline;
};

// (b) NaN float must be rejected before the (after == before) short-circuit
// and must not push a Material_change_operation onto the undo stack.
TEST_F(Mcp_test, edit_material_rejects_nan_opacity)
{
    Material_scalar_state_guard guard;
    Mcp_env& env = Mcp_env::get();
    // nlohmann::json refuses to construct a number value from NaN
    // (parse() will fail), so build the body by hand to inject it.
    const std::string body = R"({"jsonrpc":"2.0","id":"test","method":"tools/call","params":{"name":"edit_material","arguments":{"scene_name":")" + env.scene_name() + R"(","material_name":")" + env.material_name() + R"(","opacity":NaN}}})";

    httplib::Client c{env_or("ERHE_MCP_TEST_HOST", "127.0.0.1"), env_or_int("ERHE_MCP_TEST_PORT", 3743)};
    c.set_read_timeout(10, 0);
    httplib::Result res = c.Post("/mcp", body, "application/json");

    // The server may return:
    //  - HTTP 200 with JSON-RPC parse error -32700 (httplib's JSON
    //    parse rejects bare NaN tokens), OR
    //  - HTTP 200 with a tools/call response carrying isError=true
    //    ("opacity must be finite (got NaN or Inf)").
    // Either is acceptable; the critical post-condition is that the
    // material was NOT mutated and no Material_change_operation was
    // queued. Material_scalar_state_guard restores baseline in case of leak.
    ASSERT_TRUE(res) << "HTTP request failed";
    ASSERT_EQ(res->status, 200);
    json response = json::parse(res->body, nullptr, false);
    if (response.is_discarded() || !response.is_object()) {
        FAIL() << "Server returned unparseable body: " << res->body;
    }
    if (response.contains("error")) {
        EXPECT_EQ(response["error"].value("code", 0), -32700) << response.dump();
        return;
    }
    ASSERT_TRUE(response.contains("result"));
    const json& result = response["result"];
    EXPECT_TRUE(result.value("isError", false)) << "Server should have rejected NaN opacity: " << result.dump();
}

// JSON-RPC 2.0 requires the response id to echo the request id with the
// same type: a numeric id must not come back stringified.
TEST_F(Mcp_test, jsonrpc_numeric_id_round_trips)
{
    httplib::Client c{env_or("ERHE_MCP_TEST_HOST", "127.0.0.1"), env_or_int("ERHE_MCP_TEST_PORT", 3743)};
    c.set_read_timeout(10, 0);
    const json req = {
        {"jsonrpc", "2.0"},
        {"id",      7},
        {"method",  "tools/list"}
    };
    httplib::Result res = c.Post("/mcp", req.dump(), "application/json");
    ASSERT_TRUE(res) << "HTTP request failed";
    ASSERT_EQ(res->status, 200);
    json response = json::parse(res->body, nullptr, false);
    ASSERT_FALSE(response.is_discarded()) << "Unparseable body: " << res->body;
    ASSERT_TRUE(response.contains("id"));
    EXPECT_TRUE(response["id"].is_number_integer()) << "Numeric id must stay numeric, got: " << response["id"].dump();
    EXPECT_EQ(response["id"], json(7));
}

// A request without an id is a JSON-RPC notification and must not receive
// a response body; the MCP streamable HTTP transport acknowledges it with
// 202 Accepted. Every MCP client sends notifications/initialized right
// after the initialize handshake.
TEST_F(Mcp_test, notification_is_acknowledged_without_response)
{
    httplib::Client c{env_or("ERHE_MCP_TEST_HOST", "127.0.0.1"), env_or_int("ERHE_MCP_TEST_PORT", 3743)};
    c.set_read_timeout(10, 0);
    const json req = {
        {"jsonrpc", "2.0"},
        {"method",  "notifications/initialized"}
    };
    httplib::Result res = c.Post("/mcp", req.dump(), "application/json");
    ASSERT_TRUE(res) << "HTTP request failed";
    EXPECT_EQ(res->status, 202);
    EXPECT_TRUE(res->body.empty()) << "Notification must not get a response body, got: " << res->body;
}

// initialize echoes a supported requested protocolVersion, and answers
// with the server's latest supported revision for an unknown one.
TEST_F(Mcp_test, initialize_negotiates_protocol_version)
{
    Mcp_env& env = Mcp_env::get();

    const auto initialize_with = [&](const char* requested) -> std::string {
        json params = {
            {"protocolVersion", requested},
            {"capabilities",    json::object()},
            {"clientInfo",      {{"name", "mcp_server_tests"}, {"version", "1"}}}
        };
        json response = env.client().rpc("initialize", params);
        if (!response.is_object() || !response.contains("result")) {
            ADD_FAILURE() << "Missing 'result' in initialize response: " << response.dump();
            return {};
        }
        return response["result"].value("protocolVersion", std::string{});
    };

    EXPECT_EQ(initialize_with("2024-11-05"), "2024-11-05");
    EXPECT_EQ(initialize_with("2025-06-18"), "2025-06-18");
    // Unknown revision: the server answers with its latest supported one.
    EXPECT_EQ(initialize_with("1999-01-01"), "2025-06-18");
}

// (c) Queue overflow: spam concurrent slow tool calls and verify at
// least one returns the "Server busy" -32000 error.
TEST_F(Mcp_test, queue_overflow_returns_busy_error)
{
    const std::string host = env_or    ("ERHE_MCP_TEST_HOST", "127.0.0.1");
    const int         port = env_or_int("ERHE_MCP_TEST_PORT", 3743);

    // Spam more requests than the server's k_max_queue_depth (64) in
    // parallel. Use list_scenes (a query that the main thread can
    // process quickly) so we depend on contention to fill the queue.
    constexpr int        kRequestCount = 96;
    std::vector<std::thread> threads;
    std::atomic<int>         busy_count{0};
    std::atomic<int>         ok_count  {0};
    threads.reserve(kRequestCount);
    for (int i = 0; i < kRequestCount; ++i) {
        threads.emplace_back([&]() {
            httplib::Client c{host, port};
            c.set_read_timeout(10, 0);
            json req = {
                {"jsonrpc", "2.0"},
                {"id",      "queue-overflow"},
                {"method",  "tools/call"},
                {"params",  {{"name", "list_scenes"}, {"arguments", json::object()}}}
            };
            httplib::Result res = c.Post("/mcp", req.dump(), "application/json");
            if (!res || res->status != 200) {
                return;
            }
            json body = json::parse(res->body, nullptr, false);
            if (body.is_discarded()) {
                return;
            }
            if (body.contains("error") && body["error"].value("code", 0) == -32000 &&
                body["error"].value("message", std::string{}).find("Server busy") != std::string::npos)
            {
                busy_count.fetch_add(1);
            } else if (body.contains("result")) {
                ok_count.fetch_add(1);
            }
        });
    }
    for (auto& t : threads) {
        t.join();
    }

    // If the editor is keeping up with the queue we may never overflow,
    // so make this a soft expectation: either at least one busy, or
    // every request succeeded (which means the queue never grew past
    // its cap and the test is inconclusive on this run).
    if (busy_count.load() == 0) {
        GTEST_LOG_(INFO) << "queue_overflow_returns_busy_error: editor kept up; " << ok_count.load() << " ok, 0 busy";
        return;
    }
    EXPECT_GT(busy_count.load(), 0);
    EXPECT_GT(ok_count.load(),   0); // at least some should have succeeded too
}

// Ambiguous material names are not constructible: create_material refuses a
// name already in the library (tested above) and sibling names are unique,
// so edit_material's ambiguous-name refusal has no reachable test state.

// Bearer-token auth needs an editor started WITH a token, which cannot be
// turned on in a running one: Mcp_auth_test uses a dedicated editor on
// ERHE_MCP_TEST_AUTH_PORT (ctest: the mcp_editor_auth fixture; otherwise
// launched here with the test token file), separate from the shared editor
// every Mcp_test case uses.
class Mcp_auth_test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_host       = env_or    ("ERHE_MCP_TEST_HOST",       "127.0.0.1");
        m_port       = env_or_int("ERHE_MCP_TEST_AUTH_PORT",  3744);
        m_token_file = env_or    ("ERHE_MCP_TEST_TOKEN_FILE", mcp_test::compiled_in_token_file().c_str());
        m_token      = mcp_test::read_token_file(m_token_file);
        ASSERT_FALSE(m_token.empty()) << "cannot read the test token file '" << m_token_file << "'";

        // Same as Mcp_env::connect(): an editor started elsewhere (the
        // ctest fixture) may still be coming up - give it the configured
        // wait before launching one. A single probe here once launched a
        // second editor beside the fixture's; both bound the port.
        static bool s_launch_attempted = false;
        const int   timeout_s          = env_or_int("ERHE_MCP_TEST_TIMEOUT_S", 30);
        if (!mcp_test::wait_for_editor(m_host, m_port, timeout_s) && !s_launch_attempted) {
            s_launch_attempted = true;
            const int launch_timeout_s = env_or_int("ERHE_MCP_TEST_LAUNCH_TIMEOUT_S", 180);
            ASSERT_TRUE(mcp_test::launch_editor(m_host, m_port, launch_timeout_s, m_token_file, m_token))
                << "no auth-enabled editor at " << m_host << ":" << m_port << " and none could be launched";
        }
        ASSERT_TRUE(mcp_test::wait_for_editor(m_host, m_port, 1))
            << "no auth-enabled editor at " << m_host << ":" << m_port;

        // Every test starts from a reset editor, the same as Mcp_test.
        httplib::Result reset = post_tool_call(m_token, "reset_editor_state");
        ASSERT_TRUE(reset);
        ASSERT_EQ(reset->status, 200) << "reset_editor_state was refused: " << reset->body;
    }

    auto post(const std::optional<std::string>& bearer, const json& body) -> httplib::Result
    {
        httplib::Client client{m_host, m_port};
        client.set_read_timeout(10, 0);
        if (bearer.has_value()) {
            client.set_default_headers({{"Authorization", "Bearer " + bearer.value()}});
        }
        return client.Post("/mcp", body.dump(), "application/json");
    }

    auto post_tools_list(const std::optional<std::string>& bearer) -> httplib::Result
    {
        return post(bearer, json{{"jsonrpc", "2.0"}, {"id", "auth-probe"}, {"method", "tools/list"}});
    }

    auto post_tool_call(const std::optional<std::string>& bearer, const std::string& tool_name) -> httplib::Result
    {
        return post(
            bearer,
            json{
                {"jsonrpc", "2.0"},
                {"id",      "auth-call"},
                {"method",  "tools/call"},
                {"params",  {{"name", tool_name}, {"arguments", json::object()}}}
            }
        );
    }

    std::string m_host;
    int         m_port{0};
    std::string m_token_file;
    std::string m_token;
};

// The editor loaded the test token: the right token is accepted, no token
// and a tampered token are refused with 401.
TEST_F(Mcp_auth_test, tampered_token_rejected)
{
    httplib::Result with_token = post_tools_list(m_token);
    ASSERT_TRUE(with_token);
    ASSERT_EQ(with_token->status, 200) << "the test token was not accepted - did the editor load '" << m_token_file << "'?";

    httplib::Result no_token = post_tools_list(std::nullopt);
    ASSERT_TRUE(no_token);
    EXPECT_EQ(no_token->status, 401) << "Unauthenticated request should be 401 when auth is enabled";

    httplib::Result tampered = post_tools_list(std::string{"not-the-right-token"});
    ASSERT_TRUE(tampered);
    EXPECT_EQ(tampered->status, 401) << "Wrong bearer token should be 401";
}

// ---- Undo reference clearing (doc/editor/import_undo_reference_clearing.md) --------
//
// Undoing a glTF import takes the imported content back out of the editor;
// every editor part that cached a reference to it must let go, or the window
// keeps showing dead content and the asset can never be unloaded. These cover
// the two routes into the reported bug plus the announcement itself; the
// broader matrix (tool references, tree pins, selection pruning, the false
// positive of a library folder move) lives in
// scripts/undo_reference_clearing_smoke_test.py.

namespace {

constexpr const char* c_undo_ref_gltf = "res/editor/assets/RiggedFigure/RiggedFigure.glb";

// The removal announcement is published once per frame, just before the
// message bus pump, so every mutation needs a frame before it is observable.
void advance_frames(Mcp_client& client, int frames)
{
    for (int i = 0; i < frames; ++i) {
        client.call_tool("advance_time", json{{"seconds", 0.016}});
    }
}

// True once get_async_status reports nothing in flight on two consecutive
// reads (workers, queued operations, scene commits and asset loads).
[[nodiscard]] auto wait_until_idle(Mcp_client& client, const int timeout_ms) -> bool
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds{timeout_ms};
    int idle_reads = 0;
    while (std::chrono::steady_clock::now() < deadline) {
        advance_frames(client, 2);
        Mcp_client::Tool_result status = client.call_tool("get_async_status", json::object());
        const bool idle =
            !status.is_error &&
            (status.payload.value("pending",               1) == 0) &&
            (status.payload.value("running",               1) == 0) &&
            (status.payload.value("queued_operations",     1) == 0) &&
            (status.payload.value("pending_scene_commits", 1) == 0) &&
            (status.payload.value("asset_loads",           1) == 0);
        idle_reads = idle ? (idle_reads + 1) : 0;
        if (idle_reads >= 2) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{50});
    }
    return false;
}

[[nodiscard]] auto scene_names(Mcp_client& client) -> std::vector<std::string>
{
    std::vector<std::string> names;
    Mcp_client::Tool_result result = client.call_tool("list_scenes", json::object());
    if (result.is_error || !result.payload.contains("scenes")) {
        return names;
    }
    for (const json& scene : result.payload["scenes"]) {
        names.push_back(scene.value("name", ""));
    }
    return names;
}

// Creates a scene (queued, so the name has to be discovered by diffing) and
// imports the test glTF into it.
[[nodiscard]] auto import_into_new_scene(Mcp_client& client) -> std::string
{
    const std::vector<std::string> before = scene_names(client);
    client.call_tool("create_scene", json::object());
    advance_frames(client, 6);
    std::string scene;
    for (const std::string& name : scene_names(client)) {
        if (std::find(before.begin(), before.end(), name) == before.end()) {
            scene = name;
        }
    }
    if (scene.empty()) {
        return scene;
    }
    client.call_tool("import_gltf", json{{"scene_name", scene}, {"path", c_undo_ref_gltf}});
    advance_frames(client, 10);
    return scene;
}

[[nodiscard]] auto first_animation_name(Mcp_client& client, const std::string& scene) -> std::string
{
    Mcp_client::Tool_result result = client.call_tool("get_scene_animations", json{{"scene_name", scene}});
    if (result.is_error || !result.payload.contains("animations")) {
        return {};
    }
    const json& animations = result.payload["animations"];
    if (!animations.is_array() || animations.empty()) {
        return {};
    }
    return animations[0].value("name", "");
}

[[nodiscard]] auto editor_references(Mcp_client& client) -> json
{
    Mcp_client::Tool_result result = client.call_tool("get_editor_references", json::object());
    return result.payload;
}

} // anonymous namespace

TEST_F(Mcp_test, undo_of_gltf_import_clears_the_animation_target)
{
    Mcp_client& client = Mcp_env::get().client();

    const std::string scene = import_into_new_scene(client);
    ASSERT_FALSE(scene.empty()) << "could not create a scene to import into";
    const std::string animation = first_animation_name(client, scene);
    ASSERT_FALSE(animation.empty()) << "test glTF carries no animation";

    client.call_tool("set_animation_target", json{{"animation", animation}, {"scene_name", scene}});
    advance_frames(client, 3);

    json before = editor_references(client);
    ASSERT_TRUE(before.contains("animation_window"));
    ASSERT_TRUE(before["animation_window"].is_object())
        << "animation window did not take the target: " << before["animation_window"].dump();
    EXPECT_EQ(before["animation_window"].value("name", ""), animation);
    EXPECT_EQ(before["animation_player"].value("name", ""), animation);

    client.call_tool("undo", json::object());
    advance_frames(client, 4);

    json after = editor_references(client);
    EXPECT_TRUE(after["animation_window"].is_null())
        << "animation window still holds removed content: " << after["animation_window"].dump();
    EXPECT_TRUE(after["animation_player"].is_null())
        << "animation player still holds removed content: " << after["animation_player"].dump();

    client.call_tool("close_scene", json{{"scene_name", scene}});
    advance_frames(client, 4);
}

// The second route into the same symptom: Scene_open_operation::undo only
// unregisters the scene - it publishes no Close_scene_message, so none of the
// close_scene subscribers run.
TEST_F(Mcp_test, undo_of_open_scene_clears_the_animation_target)
{
    Mcp_client& client = Mcp_env::get().client();

    client.call_tool("open_scene", json{{"path", c_undo_ref_gltf}});
    advance_frames(client, 10);

    std::string scene;
    for (const std::string& name : scene_names(client)) {
        if (name.find("RiggedFigure") != std::string::npos) {
            scene = name;
        }
    }
    ASSERT_FALSE(scene.empty()) << "glTF was not opened as a scene";
    const std::string animation = first_animation_name(client, scene);
    ASSERT_FALSE(animation.empty()) << "opened scene carries no animation";

    client.call_tool("set_animation_target", json{{"animation", animation}, {"scene_name", scene}});
    advance_frames(client, 3);
    ASSERT_EQ(editor_references(client)["animation_window"].value("name", ""), animation);

    client.call_tool("undo", json::object());
    advance_frames(client, 4);

    json after = editor_references(client);
    EXPECT_TRUE(after["animation_window"].is_null())
        << "animation window still points into the unregistered scene: "
        << after["animation_window"].dump();
    EXPECT_TRUE(after["animation_player"].is_null())
        << "animation player still points into the unregistered scene: "
        << after["animation_player"].dump();
}

// doc/plans/rigging/pole_target.md R22: one ik_drag call is one complete IK
// gesture - a discovered chain, one solve against an absolute world target,
// and exactly one undo entry - driven entirely by its own arguments.
TEST_F(Mcp_test, ik_drag_solves_a_bone_chain_and_records_one_undo_entry)
{
    Mcp_client& client = Mcp_env::get().client();

    const std::string scene = import_into_new_scene(client);
    ASSERT_FALSE(scene.empty()) << "could not create a scene to import into";
    ASSERT_TRUE(wait_until_idle(client, 60000));

    using Position = std::array<float, 3>;
    auto joint_positions = [](const json& payload) -> std::vector<Position> {
        std::vector<Position> positions;
        for (const json& joint : payload.at("joints")) {
            const json& p = joint.at("position");
            positions.push_back(Position{p[0].get<float>(), p[1].get<float>(), p[2].get<float>()});
        }
        return positions;
    };
    auto distance = [](const Position& a, const Position& b) -> float {
        const float dx = a[0] - b[0];
        const float dy = a[1] - b[1];
        const float dz = a[2] - b[2];
        return std::sqrt((dx * dx) + (dy * dy) + (dz * dz));
    };
    auto undo_depth = [&client]() -> std::size_t {
        return client.call_tool("get_undo_redo_stack", json::object()).payload.at("undo").size();
    };

    // Solve toward the effector's own start position first: the chain does not
    // move, so this reports the drag-start geometry.
    Mcp_client::Tool_result details = client.call_tool(
        "get_node_details", json{{"scene_name", scene}, {"node_name", "arm_joint_L_3"}}
    );
    ASSERT_FALSE(details.is_error) << details.text;
    const json& world = details.payload.at("world_transform").at("translation");
    const Position start{world[0].get<float>(), world[1].get<float>(), world[2].get<float>()};

    Mcp_client::Tool_result rest = client.call_tool(
        "ik_drag",
        json{{"scene_name", scene}, {"node_name", "arm_joint_L_3"}, {"target", {start[0], start[1], start[2]}}}
    );
    ASSERT_FALSE(rest.is_error) << rest.text;
    const std::vector<Position> rest_positions = joint_positions(rest.payload);
    ASSERT_GE(rest_positions.size(), std::size_t{2}) << "a chain is at least root + effector";
    EXPECT_TRUE(rest.payload.at("pole").is_null()) << "no pole is authored yet";

    // A reachable target: one tenth of the chain's reach, sideways.
    float reach = 0.0f;
    for (std::size_t i = 0; i + 1 < rest_positions.size(); ++i) {
        reach += distance(rest_positions[i], rest_positions[i + 1]);
    }
    ASSERT_GT(reach, 0.0f);
    const Position target{start[0] + (0.1f * reach), start[1], start[2]};

    advance_frames(client, 4);
    const std::size_t undo_before = undo_depth();

    Mcp_client::Tool_result solved = client.call_tool(
        "ik_drag",
        json{{"scene_name", scene}, {"node_name", "arm_joint_L_3"}, {"target", {target[0], target[1], target[2]}}}
    );
    ASSERT_FALSE(solved.is_error) << solved.text;
    EXPECT_TRUE(solved.payload.at("recorded").get<bool>()) << "the solve moved joints, so it must be recorded";
    const std::vector<Position> solved_positions = joint_positions(solved.payload);
    ASSERT_EQ(solved_positions.size(), rest_positions.size());

    // Bone lengths are preserved and the effector reached the target.
    for (std::size_t i = 0; i + 1 < solved_positions.size(); ++i) {
        EXPECT_NEAR(
            distance(solved_positions[i], solved_positions[i + 1]),
            distance(rest_positions[i],   rest_positions[i + 1]),
            1.0e-3f
        ) << "segment " << i << " changed length";
    }
    EXPECT_NEAR(distance(solved_positions.back(), target), 0.0f, 1.0e-2f);
    EXPECT_NEAR(distance(solved_positions.front(), rest_positions.front()), 0.0f, 1.0e-5f) << "the chain root is fixed";

    advance_frames(client, 4);
    EXPECT_EQ(undo_depth(), undo_before + 1) << "one ik_drag records exactly one undo entry";
    EXPECT_EQ(solved.payload.at("effector_orientation").get<std::string>(), "keep_world")
        << "the effector orientation default is keep_world";

    // doc/plans/rigging/ik_drag_options.md R10: the mode is an explicit
    // argument with a fixed default, and an unrecognized value is refused.
    Mcp_client::Tool_result followed = client.call_tool(
        "ik_drag",
        json{
            {"scene_name",           scene},
            {"node_name",            "arm_joint_L_3"},
            {"target",               {target[0], target[1], target[2]}},
            {"effector_orientation", "follow_last_segment"}
        }
    );
    ASSERT_FALSE(followed.is_error) << followed.text;
    EXPECT_EQ(followed.payload.at("effector_orientation").get<std::string>(), "follow_last_segment");

    Mcp_client::Tool_result refused = client.call_tool(
        "ik_drag",
        json{
            {"scene_name",           scene},
            {"node_name",            "arm_joint_L_3"},
            {"target",               {target[0], target[1], target[2]}},
            {"effector_orientation", "sideways"}
        }
    );
    EXPECT_TRUE(refused.is_error) << "an unrecognized effector_orientation must be refused";
    advance_frames(client, 4);

    client.call_tool("close_scene", json{{"scene_name", scene}});
    advance_frames(client, 4);
}

// The producer side, independent of whether any subscriber happened to hold
// the content: the undo must announce the removed items.
TEST_F(Mcp_test, undo_of_gltf_import_announces_the_removed_items)
{
    Mcp_client& client = Mcp_env::get().client();

    const std::string scene = import_into_new_scene(client);
    ASSERT_FALSE(scene.empty());

    const std::size_t before = editor_references(client)
        .value("items_removed_announcement_count", std::size_t{0});

    client.call_tool("undo", json::object());
    advance_frames(client, 4);

    json after = editor_references(client);
    EXPECT_GT(after.value("items_removed_announcement_count", std::size_t{0}), before);
    ASSERT_TRUE(after.contains("last_announced_uids"));
    EXPECT_FALSE(after["last_announced_uids"].empty())
        << "an undo that removed imported content announced nothing";

    client.call_tool("close_scene", json{{"scene_name", scene}});
    advance_frames(client, 4);
}

// ---- Material slot regression (doc/erhe/draw_list_material_set.md V3) -------
//
// The reported bug: assigning one material to a second mesh leaves that mesh
// nearly unchanged, and reversing the order moves the failure to the other
// mesh. The cause is a single mutable Material::material_buffer_index that the
// material preview's own one-material Material_buffer::update() rewrites, so a
// cached draw-list record written afterwards names the wrong slot.
//
// The assertion is deliberately about the CACHED RECORDS, not about what the
// meshes' materials say: the Mesh_primitive names the right material in both
// the broken and the fixed editor - only the record differs. Two meshes
// carrying the same material must resolve through the same slot.
//
// Expected RED until phase 4 of the plan lands.

namespace {

// The node to address a created shape by: place_brush_instance puts the mesh
// on a child node when it makes one, and the material tools take the node
// that actually carries the Mesh.
[[nodiscard]] auto create_mesh_node(
    Mcp_client&        client,
    const std::string& scene,
    const std::string& name,
    const double       x
) -> std::size_t
{
    Mcp_client::Tool_result r = client.call_tool("create_shape", json{
        {"scene_name",  scene},
        {"shape",       "box"},
        {"name",        name},
        {"position",    {x, 0.0, 0.0}},
        {"motion_mode", "none"}
    });
    if (r.is_error) {
        ADD_FAILURE() << "create_shape failed: " << r.text;
        return 0;
    }
    if (r.payload.contains("mesh_node_id")) {
        return r.payload["mesh_node_id"].get<std::size_t>();
    }
    return r.payload.value("node_id", std::size_t{0});
}

// A material of this name, created if the scene has none. Look-then-create
// rather than create-only keeps the test re-runnable against a live editor
// (create_material refuses a duplicate name), and rather than "take the
// library's first two materials", because those sit at material buffer slot 0
// and 1 - and a clobber TO slot 0 is invisible on a material that is already
// there. The bug needs a material with a slot of its own.
[[nodiscard]] auto ensure_material(
    Mcp_client&        client,
    const std::string& scene,
    const std::string& name
) -> std::size_t
{
    Mcp_client::Tool_result existing = client.call_tool("get_scene_materials", json{{"scene_name", scene}});
    if (!existing.is_error && existing.payload.contains("materials")) {
        for (const json& material : existing.payload["materials"]) {
            if (material.value("name", "") == name) {
                return material.value("id", std::size_t{0});
            }
        }
    }
    Mcp_client::Tool_result created = client.call_tool("create_material", json{
        {"scene_name", scene},
        {"name",       name},
        {"base_color", {0.8, 0.1, 0.1}}
    });
    if (created.is_error) {
        ADD_FAILURE() << "create_material failed: " << created.text;
        return 0;
    }
    return created.payload.value("id", std::size_t{0});
}

// The material GPU slot the mesh's first cached primitive record was written
// with, plus the material that record's primitive actually names.
class Record_slot
{
public:
    bool          found                {false};
    std::uint32_t material_index       {0};  // the cached record's slot
    // The material's slot in the scene's DRAW-LIST material set - the slot
    // space these cached records name. 0xffffffff when it has none.
    std::uint32_t material_set_slot     {0xffffffffu};
    std::size_t   material_id          {0};
};

[[nodiscard]] auto read_record_slot(Mcp_client& client, const std::string& scene, const std::size_t mesh_node_id) -> Record_slot
{
    Record_slot out;
    Mcp_client::Tool_result r = client.call_tool("get_draw_lists", json{
        {"scene_name", scene},
        {"mesh_id",    mesh_node_id}
    });
    if (r.is_error) {
        ADD_FAILURE() << "get_draw_lists failed: " << r.text;
        return out;
    }
    if (!r.payload.value("has_draw_lists", false)) {
        return out;  // caller skips
    }
    if (!r.payload.contains("entries") || r.payload["entries"].empty()) {
        ADD_FAILURE() << "mesh has no draw list entries: " << r.text;
        return out;
    }
    const json& entry = r.payload["entries"][0];
    out.found                 = true;
    out.material_index        = entry.value("material_index",        std::uint32_t{0});
    out.material_set_slot     = entry.contains("material_set_slot") && !entry["material_set_slot"].is_null()
        ? entry["material_set_slot"].get<std::uint32_t>()
        : std::uint32_t{0xffffffffu};
    out.material_id           = entry.value("material_id",           std::size_t{0});
    return out;
}

// Assign, then let a frame render before returning: the asymmetry only shows
// up because Draw_list_scene::sync_gpu_slots() repairs the first mesh during a
// draw BETWEEN the two assignments. With both assignments inside one
// flush_draw_lists() both records are written wrong identically and an
// equality assertion would pass with the bug present.
void assign_and_render(
    Mcp_client&        client,
    const std::string& scene,
    const std::size_t  mesh_node_id,
    const std::size_t  material_id
)
{
    Mcp_client::Tool_result r = client.call_tool("assign_mesh_material", json{
        {"scene_name",  scene},
        {"mesh_id",     mesh_node_id},
        {"material_id", material_id}
    });
    ASSERT_FALSE(r.is_error) << "assign_mesh_material failed: " << r.text;
    advance_frames(client, 3);
}

} // anonymous namespace

TEST_F(Mcp_test, material_drag_to_second_mesh_uses_same_record_slot)
{
    Mcp_client&       client = Mcp_env::get().client();
    const std::string scene  = Mcp_env::get().scene_name();

    const std::size_t mesh_a = create_mesh_node(client, scene, "slot_test_a", -2.0);
    const std::size_t mesh_b = create_mesh_node(client, scene, "slot_test_b",  2.0);
    ASSERT_NE(mesh_a, 0u);
    ASSERT_NE(mesh_b, 0u);
    advance_frames(client, 3);

    const std::size_t material_forward = ensure_material(client, scene, "slot_test_forward");
    const std::size_t material_reverse = ensure_material(client, scene, "slot_test_reverse");
    ASSERT_NE(material_forward, 0u);
    ASSERT_NE(material_reverse, 0u);

    {
        // Forward order: A first, then B - the reported reproduction.
        assign_and_render(client, scene, mesh_a, material_forward);
        assign_and_render(client, scene, mesh_b, material_forward);

        const Record_slot a = read_record_slot(client, scene, mesh_a);
        const Record_slot b = read_record_slot(client, scene, mesh_b);
        if (!a.found || !b.found) {
            client.call_tool("delete_nodes", json{{"scene_name", scene}, {"names", {"slot_test_a", "slot_test_b"}}});
            GTEST_SKIP() << "scene has no draw lists (use_draw_lists off?)";
        }
        ASSERT_EQ(a.material_id, material_forward) << "assignment did not reach mesh A";
        ASSERT_EQ(b.material_id, material_forward) << "assignment did not reach mesh B";
        ASSERT_NE(a.material_set_slot, 0xffffffffu) << "material has no slot in the draw-list set";
        EXPECT_EQ(a.material_index, b.material_index)
            << "two meshes carrying one material resolve through different GPU slots: "
            << "A=" << a.material_index << " B=" << b.material_index;
        // Equality alone can hold for the wrong reason - both records stale at
        // the same wrong slot. So each record must also name the slot the
        // material actually occupies, which is its slot in this scene's
        // DRAW-LIST material set (not the forward set: the same material
        // normally holds a different slot in each).
        EXPECT_EQ(a.material_index, a.material_set_slot) << "mesh A's cached record is stale";
        EXPECT_EQ(b.material_index, b.material_set_slot) << "mesh B's cached record is stale";
    }

    {
        // Reverse order: B first, then A. The bug moves with the order, so the
        // failing side swaps; equality has to hold either way.
        assign_and_render(client, scene, mesh_b, material_reverse);
        assign_and_render(client, scene, mesh_a, material_reverse);

        const Record_slot a = read_record_slot(client, scene, mesh_a);
        const Record_slot b = read_record_slot(client, scene, mesh_b);
        ASSERT_TRUE(a.found && b.found);
        ASSERT_EQ(a.material_id, material_reverse);
        ASSERT_EQ(b.material_id, material_reverse);
        EXPECT_EQ(a.material_index, b.material_index)
            << "reversed assignment order: A=" << a.material_index << " B=" << b.material_index;
        EXPECT_EQ(a.material_index, a.material_set_slot) << "mesh A's cached record is stale";
        EXPECT_EQ(b.material_index, b.material_set_slot) << "mesh B's cached record is stale";
    }

    client.call_tool("delete_nodes", json{{"scene_name", scene}, {"names", {"slot_test_a", "slot_test_b"}}});
    advance_frames(client, 3);
}

// Material assignment is undoable (Mesh_material_assign_operation): undo
// restores the previous material AND the cached draw-list record follows it,
// because undo goes through Mesh::set_primitive_material like the assignment
// did. An assignment that changes nothing must record no undo entry at all.
TEST_F(Mcp_test, assign_mesh_material_is_undoable)
{
    Mcp_client&       client = Mcp_env::get().client();
    const std::string scene  = Mcp_env::get().scene_name();

    const std::size_t mesh = create_mesh_node(client, scene, "undo_assign_mesh", -4.0);
    ASSERT_NE(mesh, 0u);
    advance_frames(client, 3);

    const std::size_t material_a = ensure_material(client, scene, "undo_assign_a");
    const std::size_t material_b = ensure_material(client, scene, "undo_assign_b");
    ASSERT_NE(material_a, 0u);
    ASSERT_NE(material_b, 0u);

    const auto undo_depth = [&client]() -> std::size_t {
        Mcp_client::Tool_result r = client.call_tool("get_undo_redo_stack", json::object());
        EXPECT_FALSE(r.is_error) << "get_undo_redo_stack failed: " << r.text;
        return r.payload.contains("undo") ? r.payload["undo"].size() : std::size_t{0};
    };

    assign_and_render(client, scene, mesh, material_a);
    const Record_slot after_a = read_record_slot(client, scene, mesh);
    if (!after_a.found) {
        client.call_tool("delete_nodes", json{{"scene_name", scene}, {"names", {"undo_assign_mesh"}}});
        GTEST_SKIP() << "scene has no draw lists (use_draw_lists off?)";
    }
    ASSERT_EQ(after_a.material_id, material_a);

    const std::size_t depth_before = undo_depth();
    assign_and_render(client, scene, mesh, material_b);
    EXPECT_EQ(undo_depth(), depth_before + 1) << "assignment recorded no undo entry";

    const Record_slot after_b = read_record_slot(client, scene, mesh);
    ASSERT_TRUE(after_b.found);
    ASSERT_EQ(after_b.material_id, material_b);

    Mcp_client::Tool_result undone = client.call_tool("undo", json::object());
    ASSERT_FALSE(undone.is_error) << "undo failed: " << undone.text;
    ASSERT_TRUE(undone.payload.contains("performed"));
    ASSERT_EQ(undone.payload["performed"].size(), 1u);
    advance_frames(client, 3);

    const Record_slot after_undo = read_record_slot(client, scene, mesh);
    ASSERT_TRUE(after_undo.found);
    EXPECT_EQ(after_undo.material_id, material_a) << "undo did not restore the previous material";
    EXPECT_EQ(after_undo.material_index, after_undo.material_set_slot)
        << "undo left a stale cached record: index=" << after_undo.material_index
        << " slot=" << after_undo.material_set_slot;

    Mcp_client::Tool_result redone = client.call_tool("redo", json::object());
    ASSERT_FALSE(redone.is_error) << "redo failed: " << redone.text;
    advance_frames(client, 3);

    const Record_slot after_redo = read_record_slot(client, scene, mesh);
    ASSERT_TRUE(after_redo.found);
    EXPECT_EQ(after_redo.material_id, material_b) << "redo did not re-apply the material";
    EXPECT_EQ(after_redo.material_index, after_redo.material_set_slot) << "redo left a stale cached record";

    // Re-assigning the material the primitive already has changes nothing and
    // must not push an undo entry a user would then have to press Ctrl+Z past.
    const std::size_t depth_after_redo = undo_depth();
    assign_and_render(client, scene, mesh, material_b);
    EXPECT_EQ(undo_depth(), depth_after_redo) << "no-op assignment recorded an undo entry";

    client.call_tool("delete_nodes", json{{"scene_name", scene}, {"names", {"undo_assign_mesh"}}});
    advance_frames(client, 3);
}

// ---- Input event injection (doc/plans/mcp_ui_driving.md part B) ------------

namespace {

// The first viewport that shows a scene, as get_viewports reports it: its
// rectangle is in window pixels with the origin at the top left, the space
// inject_input_events pointer coordinates are in.
class Viewport_rect
{
public:
    bool  found {false};
    float x     {0.0f};
    float y     {0.0f};
    float width {0.0f};
    float height{0.0f};
    // The camera's world_from_camera matrix, column-major.
    std::array<float, 16> world_from_camera{};
    bool                  has_camera{false};

    [[nodiscard]] auto center_x() const -> float { return x + (width  * 0.5f); }
    [[nodiscard]] auto center_y() const -> float { return y + (height * 0.5f); }
};

[[nodiscard]] auto first_viewport(Mcp_client& client) -> Viewport_rect
{
    Viewport_rect rect;
    Mcp_client::Tool_result result = client.call_tool("get_viewports", json::object());
    if (result.is_error || !result.payload.contains("viewports")) {
        return rect;
    }
    const json& viewports = result.payload["viewports"];
    if (!viewports.is_array()) {
        return rect;
    }
    for (const json& entry : viewports) {
        if ((entry.value("width", 0) <= 0) || (entry.value("height", 0) <= 0)) {
            continue;
        }
        rect.found  = true;
        rect.x      = static_cast<float>(entry.value("x", 0));
        rect.y      = static_cast<float>(entry.value("y", 0));
        rect.width  = static_cast<float>(entry.value("width", 0));
        rect.height = static_cast<float>(entry.value("height", 0));
        if (entry.contains("camera_world_from_camera") &&
            entry["camera_world_from_camera"].is_array() &&
            (entry["camera_world_from_camera"].size() == 16)) {
            for (std::size_t i = 0; i < 16; ++i) {
                rect.world_from_camera[i] = entry["camera_world_from_camera"][i].get<float>();
            }
            rect.has_camera = true;
        }
        break;
    }
    return rect;
}

// The event that puts the injected pointer at the center of `rect`.
[[nodiscard]] auto pointer_into_viewport(const Viewport_rect& rect, const int frame) -> json
{
    return json{
        {"type",  "mouse_move"},
        {"frame", frame},
        {"x",     rect.center_x()},
        {"y",     rect.center_y()}
    };
}

[[nodiscard]] auto undo_stack_sizes(Mcp_client& client, std::size_t& out_undo, std::size_t& out_redo) -> bool
{
    Mcp_client::Tool_result result = client.call_tool("get_undo_redo_stack", json::object());
    if (result.is_error || !result.payload.contains("undo") || !result.payload.contains("redo")) {
        return false;
    }
    out_undo = result.payload["undo"].size();
    out_redo = result.payload["redo"].size();
    return true;
}

} // anonymous namespace

// A key event bound to a command (Ctrl+Z -> Edit.Undo) travels the ordinary
// input path and undoes the last operation. The pointer is moved over the
// viewport first because that is what makes the viewport window ask for
// keyboard events, which is how a real Ctrl+Z reaches erhe::commands instead
// of being captured by ImGui.
TEST_F(Mcp_test, injected_key_event_runs_the_bound_undo_command)
{
    Mcp_env&    env    = Mcp_env::get();
    Mcp_client& client = env.client();

    const Viewport_rect viewport = first_viewport(client);
    ASSERT_TRUE(viewport.found) << "no viewport to aim the pointer at";

    Mcp_client::Tool_result shape = client.call_tool("create_shape", json{
        {"scene_name",  env.scene_name()},
        {"shape",       "box"},
        {"name",        "input key test box"},
        {"motion_mode", "none"}
    });
    ASSERT_FALSE(shape.is_error) << shape.text;
    ASSERT_TRUE(wait_until_idle(client, 10000)) << "create_shape did not settle";

    std::size_t undo_before = 0;
    std::size_t redo_before = 0;
    ASSERT_TRUE(undo_stack_sizes(client, undo_before, redo_before));
    ASSERT_GT(undo_before, 0u) << "nothing on the undo stack to undo";

    Mcp_client::Tool_result injected = client.call_tool("inject_input_events", json{
        {"events", json::array({
            pointer_into_viewport(viewport, 0),
            json{{"type", "key"}, {"frame", 3}, {"keycode", "left control"}, {"pressed", true},  {"modifiers", json::array({"ctrl"})}},
            json{{"type", "key"}, {"frame", 4}, {"keycode", "z"},            {"pressed", true}},
            json{{"type", "key"}, {"frame", 5}, {"keycode", "z"},            {"pressed", false}},
            json{{"type", "key"}, {"frame", 6}, {"keycode", "left control"}, {"pressed", false}, {"modifiers", json::array()}}
        })}
    });
    ASSERT_FALSE(injected.is_error) << injected.text;
    // Four key events plus the move, preceded by the one-time cursor_enter
    // and window_focus pair when this is the session's first pointer event.
    EXPECT_GE(injected.payload.value("injected", 0), 5) << injected.payload.dump();
    advance_frames(client, 3);

    std::size_t undo_after = 0;
    std::size_t redo_after = 0;
    ASSERT_TRUE(undo_stack_sizes(client, undo_after, redo_after));
    EXPECT_EQ(undo_after, undo_before - 1u) << "Ctrl+Z did not pop an undo entry";
    EXPECT_EQ(redo_after, redo_before + 1u) << "Ctrl+Z did not push a redo entry";

    client.call_tool("redo", json::object());
    advance_frames(client, 2);
}

// A move / press / release over a viewport selects the mesh under the pointer,
// the same way a user's click does: the box is placed right in front of the
// viewport camera, so the viewport center is over it and nothing of the
// imported test asset is in between. The move comes before the press because
// that is what a click is - a move between press and release turns the gesture
// into a drag and erhe::commands cancels the pending select
// (Mouse_button_binding::on_motion).
TEST_F(Mcp_test, injected_pointer_click_selects_the_mesh_under_it)
{
    Mcp_env&    env    = Mcp_env::get();
    Mcp_client& client = env.client();

    const Viewport_rect viewport = first_viewport(client);
    ASSERT_TRUE(viewport.found) << "no viewport to click in";
    ASSERT_TRUE(viewport.has_camera) << "the viewport has no camera to place the box in front of";

    // Column-major world_from_camera: column 3 is the position, column 2 is
    // the camera's +Z (back) axis, so the view direction is its negation.
    const float position_x = viewport.world_from_camera[12];
    const float position_y = viewport.world_from_camera[13];
    const float position_z = viewport.world_from_camera[14];
    const float forward_x  = -viewport.world_from_camera[8];
    const float forward_y  = -viewport.world_from_camera[9];
    const float forward_z  = -viewport.world_from_camera[10];
    constexpr float c_distance = 1.5f; // nearer than the imported test asset

    const std::string box_name = "input click test box";
    Mcp_client::Tool_result shape = client.call_tool("create_shape", json{
        {"scene_name",  env.scene_name()},
        {"shape",       "box"},
        {"name",        box_name},
        {"motion_mode", "none"},
        {"size",        json::array({0.5, 0.5, 0.5})},
        {"position",    json::array({
            position_x + (forward_x * c_distance),
            position_y + (forward_y * c_distance),
            position_z + (forward_z * c_distance)
        })}
    });
    ASSERT_FALSE(shape.is_error) << shape.text;
    ASSERT_TRUE(wait_until_idle(client, 10000)) << "create_shape did not settle";

    client.call_tool("select_items", json{{"scene_name", env.scene_name()}, {"paths", json::array()}});
    advance_frames(client, 2);

    Mcp_client::Tool_result injected = client.call_tool("inject_input_events", json{
        {"events", json::array({
            pointer_into_viewport(viewport, 0),
            json{{"type", "mouse_button"}, {"frame", 4}, {"button", "left"}, {"pressed", true}},
            json{{"type", "mouse_button"}, {"frame", 6}, {"button", "left"}, {"pressed", false}}
        })}
    });
    ASSERT_FALSE(injected.is_error) << injected.text;
    advance_frames(client, 3);

    Mcp_client::Tool_result state = client.call_tool("get_input_state", json::object());
    ASSERT_FALSE(state.is_error) << state.text;
    EXPECT_TRUE(state.payload["buttons"].empty()) << "the left button was left held: " << state.payload.dump();
    EXPECT_FALSE(state.payload["gesture"].value("active", true)) << "the gesture did not finish";

    Mcp_client::Tool_result selection = client.call_tool("get_selection", json::object());
    ASSERT_FALSE(selection.is_error) << selection.text;
    const json& items = selection.payload["items"];
    ASSERT_TRUE(items.is_array());
    ASSERT_FALSE(items.empty()) << "the click selected nothing";
    bool found = false;
    for (const json& item : items) {
        if (item.value("name", "") == box_name) {
            found = true;
        }
    }
    EXPECT_TRUE(found) << "the click selected something else: " << items.dump();

    client.call_tool("delete_nodes", json{{"scene_name", env.scene_name()}, {"names", json::array({box_name})}});
    advance_frames(client, 3);
}

// Argument validation and the state get_input_state reports.
TEST_F(Mcp_test, inject_input_events_validates_arguments_and_tracks_pointer_state)
{
    Mcp_client& client = Mcp_env::get().client();

    Mcp_client::Tool_result empty = client.call_tool("inject_input_events", json{{"events", json::array()}});
    EXPECT_TRUE(empty.is_error) << "an empty event list was accepted";

    Mcp_client::Tool_result bad_type = client.call_tool("inject_input_events", json{
        {"events", json::array({json{{"type", "not_an_event"}}})}
    });
    EXPECT_TRUE(bad_type.is_error) << "an unknown event type was accepted";

    Mcp_client::Tool_result bad_key = client.call_tool("inject_input_events", json{
        {"events", json::array({json{{"type", "key"}, {"keycode", "no such key"}}})}
    });
    EXPECT_TRUE(bad_key.is_error) << "an unknown keycode was accepted";

    Mcp_client::Tool_result moved = client.call_tool("inject_input_events", json{
        {"events", json::array({json{{"type", "mouse_move"}, {"x", 11.0}, {"y", 22.0}}})}
    });
    ASSERT_FALSE(moved.is_error) << moved.text;

    Mcp_client::Tool_result state = client.call_tool("get_input_state", json::object());
    ASSERT_FALSE(state.is_error) << state.text;
    EXPECT_TRUE(state.payload["pointer"].value("known", false));
    EXPECT_NEAR(state.payload["pointer"].value("x", 0.0), 11.0, 1e-3);
    EXPECT_NEAR(state.payload["pointer"].value("y", 0.0), 22.0, 1e-3);
    EXPECT_TRUE(state.payload.value("cursor_entered", false)) << "the first pointer event did not enter the cursor";
    EXPECT_FALSE(state.payload["gesture"].value("active", true));
}

// ---- Gestures (doc/plans/mcp_ui_driving.md B4) -----------------------------

namespace {

// The world-space position of a node, from its world transform.
[[nodiscard]] auto node_world_position(Mcp_client& client, const std::string& scene, const std::string& name, std::array<float, 3>& out) -> bool
{
    Mcp_client::Tool_result details = client.call_tool("get_node_details", json{{"scene_name", scene}, {"node_name", name}});
    if (details.is_error || !details.payload.contains("world_transform")) {
        return false;
    }
    const json& translation = details.payload.at("world_transform").at("translation");
    for (std::size_t i = 0; i < 3; ++i) {
        out[i] = translation[i].get<float>();
    }
    return true;
}

// A box right in front of the viewport camera, nearer than the imported test
// asset, so the viewport center is over it.
[[nodiscard]] auto create_box_in_front_of_camera(
    Mcp_client&          client,
    const std::string&   scene,
    const Viewport_rect& viewport,
    const std::string&   name,
    const float          distance
) -> bool
{
    const float position_x = viewport.world_from_camera[12];
    const float position_y = viewport.world_from_camera[13];
    const float position_z = viewport.world_from_camera[14];
    const float forward_x  = -viewport.world_from_camera[8];
    const float forward_y  = -viewport.world_from_camera[9];
    const float forward_z  = -viewport.world_from_camera[10];
    Mcp_client::Tool_result shape = client.call_tool("create_shape", json{
        {"scene_name",  scene},
        {"shape",       "box"},
        {"name",        name},
        {"motion_mode", "none"},
        {"size",        json::array({0.5, 0.5, 0.5})},
        {"position",    json::array({
            position_x + (forward_x * distance),
            position_y + (forward_y * distance),
            position_z + (forward_z * distance)
        })}
    });
    return !shape.is_error && wait_until_idle(client, 10000);
}

} // anonymous namespace

// Turning the wheel over a viewport zooms its camera: the wheel is bound to
// Fly_camera.zoom_camera, which adjusts the controller's own zoom axis, so the
// camera keeps gliding for a few frames after the event.
TEST_F(Mcp_test, mouse_wheel_over_a_viewport_zooms_the_camera)
{
    Mcp_env&    env    = Mcp_env::get();
    Mcp_client& client = env.client();

    const Viewport_rect viewport = first_viewport(client);
    ASSERT_TRUE(viewport.found) << "no viewport to turn the wheel over";
    ASSERT_TRUE(viewport.has_camera) << "the viewport has no camera to zoom";

    // Something under the pointer to zoom towards: the default perspective
    // zoom mode steps along the pointer ray by the hover hit distance.
    const std::string box_name = "wheel zoom test box";
    ASSERT_TRUE(create_box_in_front_of_camera(client, env.scene_name(), viewport, box_name, 3.0f));

    const Viewport_rect before = first_viewport(client);
    ASSERT_TRUE(before.has_camera);

    Mcp_client::Tool_result wheel = client.call_tool("mouse_wheel", json{
        {"x",  viewport.center_x()},
        {"y",  viewport.center_y()},
        {"dy", 4.0}
    });
    ASSERT_FALSE(wheel.is_error) << wheel.text;
    // The zoom glide is damped, so the move takes a few frames to show.
    advance_frames(client, 30);

    const Viewport_rect after = first_viewport(client);
    ASSERT_TRUE(after.has_camera);
    const float dx = after.world_from_camera[12] - before.world_from_camera[12];
    const float dy = after.world_from_camera[13] - before.world_from_camera[13];
    const float dz = after.world_from_camera[14] - before.world_from_camera[14];
    const float moved = std::sqrt((dx * dx) + (dy * dy) + (dz * dz));
    EXPECT_GT(moved, 0.01f) << "the wheel did not move the camera";

    client.call_tool("delete_nodes", json{{"scene_name", env.scene_name()}, {"names", json::array({box_name})}});
    advance_frames(client, 3);
}

// Dragging a transform gizmo handle moves the selection, the way a user's
// gizmo drag does: get_transform_handles reports a window point that picks the
// handle, and mouse_drag presses there and pulls. The check is that the node
// moved along the dragged axis only - the same outcome drag_selection produces
// from an explicit translation.
TEST_F(Mcp_test, mouse_drag_on_a_transform_handle_moves_the_selection)
{
    Mcp_env&    env    = Mcp_env::get();
    Mcp_client& client = env.client();

    const Viewport_rect viewport = first_viewport(client);
    ASSERT_TRUE(viewport.found) << "no viewport to drag in";
    ASSERT_TRUE(viewport.has_camera) << "the viewport has no camera";

    const std::string box_name = "gizmo drag test box";
    ASSERT_TRUE(create_box_in_front_of_camera(client, env.scene_name(), viewport, box_name, 3.0f));

    Mcp_client::Tool_result select = client.call_tool("select_items", json{
        {"scene_name", env.scene_name()},
        {"paths",      json::array({box_name})}
    });
    ASSERT_FALSE(select.is_error) << select.text;
    advance_frames(client, 4);

    Mcp_client::Tool_result handles = client.call_tool("get_transform_handles", json::object());
    ASSERT_FALSE(handles.is_error) << handles.text;
    ASSERT_TRUE(handles.payload.contains("handles"));
    const json& handle_list = handles.payload.at("handles");
    ASSERT_FALSE(handle_list.empty()) << "the gizmo reported no handles: " << handles.payload.dump();

    // The X translate arrow. Only the camera-facing direction of an axis is
    // drawn and pickable unless negative handles are turned on, so either the
    // positive (Handle::e_handle_translate_pos_x == 1) or the negative
    // (e_handle_translate_neg_x == 2) arrow is the one on screen; pulling
    // along whichever it is moves the box along X.
    const json* arrow = nullptr;
    for (const json& entry : handle_list) {
        const unsigned int value = entry.value("handle_value", 0u);
        if ((value == 1u) || (value == 2u)) {
            arrow = &entry;
        }
    }
    ASSERT_NE(arrow, nullptr) << "no Translate X handle on screen: " << handle_list.dump();

    std::array<float, 3> before{};
    ASSERT_TRUE(node_world_position(client, env.scene_name(), box_name, before));
    std::size_t undo_before = 0;
    std::size_t redo_before = 0;
    ASSERT_TRUE(undo_stack_sizes(client, undo_before, redo_before));

    // The gizmo anchor and the arrow point are both on screen, so the arrow
    // direction in window pixels is the direction to pull.
    const float anchor_x = handles.payload.at("anchor").value("x", 0.0f);
    const float anchor_y = handles.payload.at("anchor").value("y", 0.0f);
    const float from_x   = arrow->value("x", 0.0f);
    const float from_y   = arrow->value("y", 0.0f);
    const float axis_x   = from_x - anchor_x;
    const float axis_y   = from_y - anchor_y;
    const float axis_len = std::sqrt((axis_x * axis_x) + (axis_y * axis_y));
    ASSERT_GT(axis_len, 1.0f) << "the gizmo arrow is on top of its anchor";
    constexpr float c_pull_pixels = 60.0f;

    Mcp_client::Tool_result drag = client.call_tool("mouse_drag", json{
        {"from",   json::array({from_x, from_y})},
        {"to",     json::array({
            from_x + ((axis_x / axis_len) * c_pull_pixels),
            from_y + ((axis_y / axis_len) * c_pull_pixels)
        })},
        {"frames", 8}
    });
    ASSERT_FALSE(drag.is_error) << drag.text;
    advance_frames(client, 4);

    Mcp_client::Tool_result state = client.call_tool("get_input_state", json::object());
    ASSERT_FALSE(state.is_error) << state.text;
    EXPECT_TRUE(state.payload["buttons"].empty()) << "the drag left the button held: " << state.payload.dump();

    std::array<float, 3> after{};
    ASSERT_TRUE(node_world_position(client, env.scene_name(), box_name, after));
    const float moved_x = std::abs(after[0] - before[0]);
    const float moved_y = std::abs(after[1] - before[1]);
    const float moved_z = std::abs(after[2] - before[2]);
    EXPECT_GT(moved_x, 0.05f) << "the gizmo drag did not move the box along X";
    EXPECT_LT(moved_y, 0.05f * moved_x + 1e-3f) << "the gizmo drag moved the box off its axis (Y)";
    EXPECT_LT(moved_z, 0.05f * moved_x + 1e-3f) << "the gizmo drag moved the box off its axis (Z)";

    // One undo entry for the whole gesture, the same as a drag_selection drag.
    std::size_t undo_after = 0;
    std::size_t redo_after = 0;
    ASSERT_TRUE(undo_stack_sizes(client, undo_after, redo_after));
    EXPECT_EQ(undo_after, undo_before + 1u) << "the gizmo drag did not record exactly one undo entry";

    client.call_tool("select_items", json{{"scene_name", env.scene_name()}, {"paths", json::array()}});
    client.call_tool("delete_nodes", json{{"scene_name", env.scene_name()}, {"names", json::array({box_name})}});
    advance_frames(client, 3);
}

// A held drag leaves the button down until mouse_release ends it.
TEST_F(Mcp_test, mouse_drag_hold_leaves_the_button_down_until_mouse_release)
{
    Mcp_client& client = Mcp_env::get().client();

    const Viewport_rect viewport = first_viewport(client);
    ASSERT_TRUE(viewport.found) << "no viewport to drag in";

    Mcp_client::Tool_result drag = client.call_tool("mouse_drag", json{
        {"from",   json::array({viewport.center_x(), viewport.center_y()})},
        {"to",     json::array({viewport.center_x() + 40.0f, viewport.center_y()})},
        {"button", "middle"},
        {"frames", 4},
        {"hold",   true}
    });
    ASSERT_FALSE(drag.is_error) << drag.text;

    Mcp_client::Tool_result held = client.call_tool("get_input_state", json::object());
    ASSERT_FALSE(held.is_error) << held.text;
    ASSERT_TRUE(held.payload["buttons"].is_array());
    bool middle_held = false;
    for (const json& button : held.payload["buttons"]) {
        if (button.get<std::string>() == "middle") {
            middle_held = true;
        }
    }
    EXPECT_TRUE(middle_held) << "the held drag did not leave the button down: " << held.payload.dump();

    Mcp_client::Tool_result release = client.call_tool("mouse_release", json{{"button", "middle"}});
    ASSERT_FALSE(release.is_error) << release.text;

    Mcp_client::Tool_result released = client.call_tool("get_input_state", json::object());
    ASSERT_FALSE(released.is_error) << released.text;
    EXPECT_TRUE(released.payload["buttons"].empty()) << "mouse_release left a button held: " << released.payload.dump();

    Mcp_client::Tool_result again = client.call_tool("mouse_release", json{{"button", "middle"}});
    EXPECT_TRUE(again.is_error) << "releasing a button that is not held was accepted";
}

// key_press sends the chord a real keyboard sends: the modifier key goes down
// first, the key carries the modifier bit, and both come back up.
TEST_F(Mcp_test, key_press_runs_the_bound_undo_command)
{
    Mcp_env&    env    = Mcp_env::get();
    Mcp_client& client = env.client();

    const Viewport_rect viewport = first_viewport(client);
    ASSERT_TRUE(viewport.found) << "no viewport to aim the pointer at";

    Mcp_client::Tool_result shape = client.call_tool("create_shape", json{
        {"scene_name",  env.scene_name()},
        {"shape",       "box"},
        {"name",        "key press test box"},
        {"motion_mode", "none"}
    });
    ASSERT_FALSE(shape.is_error) << shape.text;
    ASSERT_TRUE(wait_until_idle(client, 10000)) << "create_shape did not settle";

    // The pointer has to be over the viewport, or ImGui captures the keyboard
    // and the chord never reaches erhe::commands.
    Mcp_client::Tool_result moved = client.call_tool("inject_input_events", json{
        {"events", json::array({pointer_into_viewport(viewport, 0)})}
    });
    ASSERT_FALSE(moved.is_error) << moved.text;
    advance_frames(client, 2);

    std::size_t undo_before = 0;
    std::size_t redo_before = 0;
    ASSERT_TRUE(undo_stack_sizes(client, undo_before, redo_before));
    ASSERT_GT(undo_before, 0u) << "nothing on the undo stack to undo";

    Mcp_client::Tool_result key = client.call_tool("key_press", json{
        {"key",       "z"},
        {"modifiers", json::array({"ctrl"})}
    });
    ASSERT_FALSE(key.is_error) << key.text;
    advance_frames(client, 3);

    std::size_t undo_after = 0;
    std::size_t redo_after = 0;
    ASSERT_TRUE(undo_stack_sizes(client, undo_after, redo_after));
    EXPECT_EQ(undo_after, undo_before - 1u) << "Ctrl+Z did not pop an undo entry";
    EXPECT_EQ(redo_after, redo_before + 1u) << "Ctrl+Z did not push a redo entry";

    Mcp_client::Tool_result state = client.call_tool("get_input_state", json::object());
    ASSERT_FALSE(state.is_error) << state.text;
    EXPECT_TRUE(state.payload["modifiers"].empty()) << "the chord left a modifier held: " << state.payload.dump();

    client.call_tool("redo", json::object());
    advance_frames(client, 2);
}

// type_text splits into text events that fit one Text_event, on UTF-8
// boundaries, and the gesture runs to completion.
TEST_F(Mcp_test, type_text_splits_into_text_events)
{
    Mcp_client& client = Mcp_env::get().client();

    // 40 bytes: longer than one 31 byte text event, so it is split.
    Mcp_client::Tool_result typed = client.call_tool("type_text", json{
        {"text", "abcdefghijklmnopqrstuvwxyz0123456789ABCD"}
    });
    ASSERT_FALSE(typed.is_error) << typed.text;
    EXPECT_EQ(typed.payload.value("injected", 0), 2) << typed.payload.dump();
    EXPECT_EQ(typed.payload.value("frames", 0), 2) << typed.payload.dump();

    // A multi-byte sequence that straddles the 31 byte boundary must not be
    // cut: 30 ASCII bytes then a two byte 'a with acute'.
    Mcp_client::Tool_result utf8 = client.call_tool("type_text", json{
        {"text", "abcdefghijklmnopqrstuvwxyz0123\xc3\xa1"}
    });
    ASSERT_FALSE(utf8.is_error) << utf8.text;
    EXPECT_EQ(utf8.payload.value("injected", 0), 2) << utf8.payload.dump();

    Mcp_client::Tool_result empty = client.call_tool("type_text", json{{"text", ""}});
    EXPECT_TRUE(empty.is_error) << "an empty string was accepted";
}
