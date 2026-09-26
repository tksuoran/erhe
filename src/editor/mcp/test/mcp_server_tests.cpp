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
#include <filesystem>
#include <map>
#include <optional>
#include <sstream>
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

// get_node_details addresses a prim by its id as readily as by its name: the
// ids get_scene_nodes hands out are what a caller holds after a walk of the
// tree, and a name may repeat between subtrees.
TEST_F(Mcp_test, get_node_details_finds_a_node_by_id)
{
    Mcp_env&    env    = Mcp_env::get();
    Mcp_client& client = env.client();

    Mcp_client::Tool_result shape = client.call_tool("create_shape", json{
        {"scene_name",  env.scene_name()},
        {"shape",       "box"},
        {"name",        "node id lookup box"},
        {"motion_mode", "none"}
    });
    ASSERT_FALSE(shape.is_error) << shape.text;
    advance_frames(client, 2);

    Mcp_client::Tool_result nodes = client.call_tool("get_scene_nodes", json{{"scene_name", env.scene_name()}});
    ASSERT_FALSE(nodes.is_error) << nodes.text;
    std::size_t node_id = 0;
    for (const json& node : nodes.payload.at("nodes")) {
        if (node.value("name", "") == "node id lookup box") {
            node_id = node.value("id", std::size_t{0});
            break;
        }
    }
    ASSERT_NE(node_id, std::size_t{0}) << "the created box is not in the node list";

    Mcp_client::Tool_result by_id = client.call_tool("get_node_details", json{
        {"scene_name", env.scene_name()},
        {"node_id",    node_id}
    });
    ASSERT_FALSE(by_id.is_error) << by_id.text;
    EXPECT_EQ(by_id.payload.value("name", ""), "node id lookup box");
    EXPECT_EQ(by_id.payload.value("id", std::size_t{0}), node_id);

    Mcp_client::Tool_result missing = client.call_tool("get_node_details", json{
        {"scene_name", env.scene_name()},
        {"node_id",    std::size_t{0x7fffffff}}
    });
    EXPECT_TRUE(missing.is_error) << "an unknown id was answered with a node";
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

// doc/plans/rigging/ik_drag_options.md R25, R26, R31, R32: a path of targets
// is one gesture and one undo entry; solve_from picks the pose each step
// solves from, is echoed, and an unrecognized value is refused; target and
// path are exclusive.
TEST_F(Mcp_test, ik_drag_path_and_solve_from)
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
    auto largest_difference = [&distance](const std::vector<Position>& a, const std::vector<Position>& b) -> float {
        float largest = 0.0f;
        for (std::size_t i = 0; (i < a.size()) && (i < b.size()); ++i) {
            largest = std::max(largest, distance(a[i], b[i]));
        }
        return largest;
    };
    auto undo_depth = [&client]() -> std::size_t {
        return client.call_tool("get_undo_redo_stack", json::object()).payload.at("undo").size();
    };
    auto as_json = [](const Position& p) -> json { return json::array({p[0], p[1], p[2]}); };

    Mcp_client::Tool_result details = client.call_tool(
        "get_node_details", json{{"scene_name", scene}, {"node_name", "arm_joint_L_3"}}
    );
    ASSERT_FALSE(details.is_error) << details.text;
    const json& world = details.payload.at("world_transform").at("translation");
    const Position start{world[0].get<float>(), world[1].get<float>(), world[2].get<float>()};

    // The drag-start geometry: a solve toward the effector's own position.
    Mcp_client::Tool_result rest = client.call_tool(
        "ik_drag", json{{"scene_name", scene}, {"node_name", "arm_joint_L_3"}, {"target", as_json(start)}}
    );
    ASSERT_FALSE(rest.is_error) << rest.text;
    const std::vector<Position> rest_positions = joint_positions(rest.payload);
    ASSERT_GE(rest_positions.size(), std::size_t{3}) << "the arm chain has an intermediate joint";
    EXPECT_EQ(rest.payload.at("solve_from").get<std::string>(), "drag_start") << "the solve_from default is drag_start";
    EXPECT_EQ(rest.payload.at("steps").get<std::size_t>(), std::size_t{1});

    // A path pulling the chain straight out of reach and back to the start.
    float reach = 0.0f;
    for (std::size_t i = 0; i + 1 < rest_positions.size(); ++i) {
        reach += distance(rest_positions[i], rest_positions[i + 1]);
    }
    ASSERT_GT(reach, 0.0f);
    const Position root = rest_positions.front();
    const float root_distance = distance(start, root);
    ASSERT_GT(root_distance, 0.0f);
    Position far_target{};
    for (std::size_t i = 0; i < 3; ++i) {
        far_target[i] = root[i] + (((start[i] - root[i]) / root_distance) * (2.0f * reach));
    }
    const json out_and_back = json::array({as_json(far_target), as_json(start)});

    advance_frames(client, 4);
    std::size_t undo_before = undo_depth();
    Mcp_client::Tool_result from_start = client.call_tool(
        "ik_drag", json{{"scene_name", scene}, {"node_name", "arm_joint_L_3"}, {"path", out_and_back}}
    );
    ASSERT_FALSE(from_start.is_error) << from_start.text;
    EXPECT_EQ(from_start.payload.at("steps").get<std::size_t>(), std::size_t{2});
    EXPECT_LT(largest_difference(joint_positions(from_start.payload), rest_positions), 1.0e-4f)
        << "under drag_start, back at the start is the start pose (R25)";
    advance_frames(client, 4);
    if (from_start.payload.at("recorded").get<bool>()) {
        EXPECT_EQ(undo_depth(), undo_before + 1) << "a path is one gesture and one undo entry (R32)";
        client.call_tool("undo", json::object());
        advance_frames(client, 4);
    }

    // The out steps move joints: a path is one undo entry however many steps it has.
    undo_before = undo_depth();
    Mcp_client::Tool_result out_path = client.call_tool(
        "ik_drag",
        json{
            {"scene_name", scene},
            {"node_name",  "arm_joint_L_3"},
            {"path",       json::array({as_json(start), as_json(far_target), as_json(far_target)})}
        }
    );
    ASSERT_FALSE(out_path.is_error) << out_path.text;
    EXPECT_TRUE(out_path.payload.at("recorded").get<bool>());
    EXPECT_EQ(out_path.payload.at("steps").get<std::size_t>(), std::size_t{3});
    advance_frames(client, 4);
    EXPECT_EQ(undo_depth(), undo_before + 1) << "a path is one gesture and one undo entry (R32)";
    client.call_tool("undo", json::object());
    advance_frames(client, 4);

    undo_before = undo_depth();
    Mcp_client::Tool_result from_previous = client.call_tool(
        "ik_drag",
        json{
            {"scene_name", scene},
            {"node_name",  "arm_joint_L_3"},
            {"path",       out_and_back},
            {"solve_from", "previous_step"}
        }
    );
    ASSERT_FALSE(from_previous.is_error) << from_previous.text;
    EXPECT_EQ(from_previous.payload.at("solve_from").get<std::string>(), "previous_step");
    const std::vector<Position> previous_positions = joint_positions(from_previous.payload);
    EXPECT_GT(largest_difference(previous_positions, rest_positions), 1.0e-3f)
        << "under previous_step, back at the start keeps what the drag picked up (R26)";
    EXPECT_LT(distance(previous_positions.back(), start), 1.0e-2f) << "the effector still reaches the final target";
    for (std::size_t i = 0; i + 1 < previous_positions.size(); ++i) {
        EXPECT_NEAR(
            distance(previous_positions[i], previous_positions[i + 1]),
            distance(rest_positions[i],     rest_positions[i + 1]),
            1.0e-3f
        ) << "segment " << i << " changed length";
    }
    advance_frames(client, 4);
    EXPECT_EQ(undo_depth(), undo_before + 1) << "a previous_step path is one undo entry";
    client.call_tool("undo", json::object());
    advance_frames(client, 4);

    // Refusals change nothing.
    Mcp_client::Tool_result bad_solve_from = client.call_tool(
        "ik_drag",
        json{{"scene_name", scene}, {"node_name", "arm_joint_L_3"}, {"target", as_json(far_target)}, {"solve_from", "sometimes"}}
    );
    EXPECT_TRUE(bad_solve_from.is_error) << "an unrecognized solve_from must be refused";
    Mcp_client::Tool_result both = client.call_tool(
        "ik_drag",
        json{{"scene_name", scene}, {"node_name", "arm_joint_L_3"}, {"target", as_json(far_target)}, {"path", out_and_back}}
    );
    EXPECT_TRUE(both.is_error) << "target and path together must be refused";
    Mcp_client::Tool_result neither = client.call_tool(
        "ik_drag", json{{"scene_name", scene}, {"node_name", "arm_joint_L_3"}}
    );
    EXPECT_TRUE(neither.is_error) << "neither target nor path must be refused";
    Mcp_client::Tool_result empty_path = client.call_tool(
        "ik_drag", json{{"scene_name", scene}, {"node_name", "arm_joint_L_3"}, {"path", json::array()}}
    );
    EXPECT_TRUE(empty_path.is_error) << "an empty path must be refused";
    advance_frames(client, 4);
    Mcp_client::Tool_result after = client.call_tool(
        "get_node_details", json{{"scene_name", scene}, {"node_name", "arm_joint_L_3"}}
    );
    ASSERT_FALSE(after.is_error) << after.text;
    const json& after_world = after.payload.at("world_transform").at("translation");
    const Position after_position{after_world[0].get<float>(), after_world[1].get<float>(), after_world[2].get<float>()};
    EXPECT_LT(distance(after_position, start), 1.0e-4f) << "refused calls move no joint";

    client.call_tool("close_scene", json{{"scene_name", scene}});
    advance_frames(client, 4);
}

// doc/plans/rigging/ik_drag_options.md R27, R28, R31: pole_alignment and
// pole_ease_distance are explicit arguments, echoed, and refused before any
// joint moves when unrecognized or out of range. Under ease_in a drag shorter
// than the ease distance leaves the bend partly swiveled toward the pole
// (by w = d / (pole_ease_distance * reach)); at or past it the bend is on the
// pole, as under snap.
TEST_F(Mcp_test, ik_drag_pole_alignment_ease_in)
{
    Mcp_client& client = Mcp_env::get().client();

    const std::string scene = import_into_new_scene(client);
    ASSERT_FALSE(scene.empty()) << "could not create a scene to import into";
    ASSERT_TRUE(wait_until_idle(client, 60000));

    using Vec = std::array<float, 3>;
    auto sub   = [](const Vec& a, const Vec& b) -> Vec { return Vec{a[0] - b[0], a[1] - b[1], a[2] - b[2]}; };
    auto add   = [](const Vec& a, const Vec& b) -> Vec { return Vec{a[0] + b[0], a[1] + b[1], a[2] + b[2]}; };
    auto scale = [](const Vec& a, const float s) -> Vec { return Vec{a[0] * s, a[1] * s, a[2] * s}; };
    auto dot   = [](const Vec& a, const Vec& b) -> float { return (a[0] * b[0]) + (a[1] * b[1]) + (a[2] * b[2]); };
    auto cross = [](const Vec& a, const Vec& b) -> Vec {
        return Vec{(a[1] * b[2]) - (a[2] * b[1]), (a[2] * b[0]) - (a[0] * b[2]), (a[0] * b[1]) - (a[1] * b[0])};
    };
    auto norm      = [&dot](const Vec& a) -> float { return std::sqrt(dot(a, a)); };
    auto normalize = [&norm, &scale](const Vec& a) -> Vec { return scale(a, 1.0f / norm(a)); };
    auto as_json   = [](const Vec& p) -> json { return json::array({p[0], p[1], p[2]}); };
    auto joint_positions = [](const json& payload) -> std::vector<Vec> {
        std::vector<Vec> positions;
        for (const json& joint : payload.at("joints")) {
            const json& p = joint.at("position");
            positions.push_back(Vec{p[0].get<float>(), p[1].get<float>(), p[2].get<float>()});
        }
        return positions;
    };
    // Perpendicular offset of point from the root-to-effector line (pole_target.md R11 step 2).
    auto perpendicular = [&](const std::vector<Vec>& positions, const Vec& point) -> Vec {
        const Vec axis   = normalize(sub(positions.back(), positions.front()));
        const Vec offset = sub(point, positions.front());
        return sub(offset, scale(axis, dot(offset, axis)));
    };
    // Mean bend direction (R11 step 3).
    auto bend = [&](const std::vector<Vec>& positions) -> Vec {
        Vec sum{0.0f, 0.0f, 0.0f};
        for (std::size_t i = 1; i + 1 < positions.size(); ++i) {
            sum = add(sum, perpendicular(positions, positions[i]));
        }
        return normalize(sum);
    };
    // Signed angle (degrees) about the root-to-effector line from the pole's
    // direction to the bend.
    auto off_pole_deg = [&](const std::vector<Vec>& positions, const Vec& pole_position) -> float {
        const Vec axis    = normalize(sub(positions.back(), positions.front()));
        const Vec to_pole = normalize(perpendicular(positions, pole_position));
        const Vec b       = bend(positions);
        return std::atan2(dot(cross(to_pole, b), axis), dot(to_pole, b)) * (180.0f / 3.14159265f);
    };
    auto undo_depth = [&client]() -> std::size_t {
        return client.call_tool("get_undo_redo_stack", json::object()).payload.at("undo").size();
    };
    auto effector_position = [&]() -> Vec {
        Mcp_client::Tool_result details = client.call_tool(
            "get_node_details", json{{"scene_name", scene}, {"node_name", "arm_joint_L_3"}}
        );
        const json& t = details.payload.at("world_transform").at("translation");
        return Vec{t[0].get<float>(), t[1].get<float>(), t[2].get<float>()};
    };
    // One measured gesture, undone again, so every drag starts from the same pose.
    auto drag = [&](const Vec& target, const json& extra) -> Mcp_client::Tool_result {
        json args{{"scene_name", scene}, {"node_name", "arm_joint_L_3"}, {"target", as_json(target)}};
        for (auto it = extra.begin(); it != extra.end(); ++it) {
            args[it.key()] = it.value();
        }
        Mcp_client::Tool_result result = client.call_tool("ik_drag", args);
        advance_frames(client, 4);
        if (!result.is_error && result.payload.at("recorded").get<bool>()) {
            client.call_tool("undo", json::object());
            advance_frames(client, 4);
        }
        return result;
    };

    const Vec start = effector_position();
    Mcp_client::Tool_result rest = drag(start, json::object());
    ASSERT_FALSE(rest.is_error) << rest.text;
    const std::vector<Vec> rest_positions = joint_positions(rest.payload);
    ASSERT_GE(rest_positions.size(), std::size_t{3}) << "the arm chain has an intermediate joint";
    float reach = 0.0f;
    for (std::size_t i = 0; i + 1 < rest_positions.size(); ++i) {
        reach += norm(sub(rest_positions[i + 1], rest_positions[i]));
    }
    ASSERT_GT(reach, 0.0f);

    // The unpoled bend toward a target 0.15 reach sideways; the pole is
    // placed 60 degrees about the axis away from it.
    const float drag_fraction = 0.15f;
    const Vec   target{start[0] + (drag_fraction * reach), start[1], start[2]};
    Mcp_client::Tool_result unpoled = drag(target, json::object());
    ASSERT_FALSE(unpoled.is_error) << unpoled.text;
    const std::vector<Vec> unpoled_positions = joint_positions(unpoled.payload);
    const Vec   axis     = normalize(sub(unpoled_positions.back(), unpoled_positions.front()));
    const Vec   b        = bend(unpoled_positions);
    const Vec   side     = cross(axis, b);
    const Vec   pole_dir = add(scale(b, 0.5f), scale(side, 0.8660254f)); // 60 degrees from b
    const Vec   pole     = add(unpoled_positions.front(), scale(pole_dir, reach));
    const float unpoled_off_pole = off_pole_deg(unpoled_positions, pole);
    ASSERT_GT(std::abs(unpoled_off_pole), 50.0f);

    Mcp_client::Tool_result created = client.call_tool(
        "create_node", json{{"scene_name", scene}, {"name", "ik_pole"}, {"position", as_json(pole)}}
    );
    ASSERT_FALSE(created.is_error) << created.text;
    advance_frames(client, 6);
    Mcp_client::Tool_result pole_details = client.call_tool(
        "get_node_details", json{{"scene_name", scene}, {"node_name", "ik_pole"}}
    );
    ASSERT_FALSE(pole_details.is_error) << pole_details.text;
    Mcp_client::Tool_result effector_details = client.call_tool(
        "get_node_details", json{{"scene_name", scene}, {"node_name", "arm_joint_L_3"}}
    );
    ASSERT_FALSE(effector_details.is_error) << effector_details.text;
    Mcp_client::Tool_result set_pole = client.call_tool(
        "set_item_property",
        json{
            {"item_id",      effector_details.payload.at("id")},
            {"property",     "Ik.pole_target"},
            {"reference_id", pole_details.payload.at("id")}
        }
    );
    ASSERT_FALSE(set_pole.is_error) << set_pole.text;
    advance_frames(client, 6);

    // Snap (the default): the bend is on the pole, the options are echoed.
    Mcp_client::Tool_result snapped = drag(target, json::object());
    ASSERT_FALSE(snapped.is_error) << snapped.text;
    ASSERT_FALSE(snapped.payload.at("pole").is_null()) << "the pole governs the drag";
    EXPECT_EQ(snapped.payload.at("pole_alignment").get<std::string>(), "snap") << "the pole_alignment default is snap";
    EXPECT_NEAR(snapped.payload.at("pole_ease_distance").get<float>(), 0.5f, 1.0e-6f);
    EXPECT_EQ(snapped.payload.at("pole_weight").get<float>(), 1.0f);
    EXPECT_NEAR(off_pole_deg(joint_positions(snapped.payload), pole), 0.0f, 2.0f) << "snap puts the bend on the pole";

    // Ease In with the drag at half the ease distance: w = 0.5, so the bend
    // sits half way between the unpoled bend and the pole.
    const float half_ease = 2.0f * drag_fraction;
    Mcp_client::Tool_result half = drag(target, json{{"pole_alignment", "ease_in"}, {"pole_ease_distance", half_ease}});
    ASSERT_FALSE(half.is_error) << half.text;
    EXPECT_EQ(half.payload.at("pole_alignment").get<std::string>(), "ease_in");
    EXPECT_NEAR(half.payload.at("pole_ease_distance").get<float>(), half_ease, 1.0e-6f);
    const float half_weight = half.payload.at("pole_weight").get<float>();
    EXPECT_NEAR(half_weight, 0.5f, 1.0e-3f);
    const float half_off_pole = off_pole_deg(joint_positions(half.payload), pole);
    EXPECT_NEAR(half_off_pole, (1.0f - half_weight) * unpoled_off_pole, 2.0f)
        << "below the ease distance the bend is partly swiveled (unpoled " << unpoled_off_pole << " deg off the pole)";
    EXPECT_GT(std::abs(half_off_pole), 10.0f) << "not on the pole yet";
    EXPECT_LT(std::abs(half_off_pole), std::abs(unpoled_off_pole) - 10.0f) << "swiveled toward the pole";

    // At and past the ease distance the bend is fully on the pole.
    for (const float ease : { drag_fraction, 0.5f * drag_fraction }) {
        Mcp_client::Tool_result full = drag(target, json{{"pole_alignment", "ease_in"}, {"pole_ease_distance", ease}});
        ASSERT_FALSE(full.is_error) << full.text;
        EXPECT_NEAR(full.payload.at("pole_weight").get<float>(), 1.0f, 1.0e-3f) << "ease distance " << ease;
        EXPECT_NEAR(off_pole_deg(joint_positions(full.payload), pole), 0.0f, 2.0f) << "ease distance " << ease;
    }

    // Refusals change nothing.
    const std::size_t undo_before = undo_depth();
    const std::array<json, 4> refused_arguments{
        json{{"pole_alignment", "gradual"}},
        json{{"pole_alignment", "ease_in"}, {"pole_ease_distance", 0.01f}},
        json{{"pole_alignment", "ease_in"}, {"pole_ease_distance", 2.5f}},
        json{{"pole_alignment", "ease_in"}, {"pole_ease_distance", "far"}}
    };
    for (const json& bad : refused_arguments) {
        Mcp_client::Tool_result refused = drag(target, bad);
        EXPECT_TRUE(refused.is_error) << "must be refused: " << bad.dump();
    }
    advance_frames(client, 4);
    EXPECT_EQ(undo_depth(), undo_before) << "refused calls record nothing";
    EXPECT_LT(norm(sub(effector_position(), start)), 1.0e-4f) << "refused calls move no joint";

    client.call_tool("close_scene", json{{"scene_name", scene}});
    advance_frames(client, 4);
}

// doc/plans/rigging/ik_drag_options.md R21-R24, R31: mid_chain_drag is an
// explicit argument, echoed, and refused before any joint moves when
// unrecognized. Under pin_chain_end a drag of arm_joint_L_2 (whose only bone
// child arm_joint_L_3 has no bone child, so the lower chain is L_2..L_3) along
// positions that keep L_3 in reach leaves L_3's world position and rotation
// where they were, and the one undo entry covers every moved joint, the lower
// chain's included. Under rigid_children L_3 rides rigidly on L_2.
TEST_F(Mcp_test, ik_drag_mid_chain_drag_pin_chain_end)
{
    Mcp_client& client = Mcp_env::get().client();

    const std::string scene = import_into_new_scene(client);
    ASSERT_FALSE(scene.empty()) << "could not create a scene to import into";
    ASSERT_TRUE(wait_until_idle(client, 60000));

    using Vec  = std::array<float, 3>;
    using Quat = std::array<float, 4>; // x, y, z, w
    auto sub   = [](const Vec& a, const Vec& b) -> Vec { return Vec{a[0] - b[0], a[1] - b[1], a[2] - b[2]}; };
    auto add   = [](const Vec& a, const Vec& b) -> Vec { return Vec{a[0] + b[0], a[1] + b[1], a[2] + b[2]}; };
    auto scale = [](const Vec& a, const float s) -> Vec { return Vec{a[0] * s, a[1] * s, a[2] * s}; };
    auto dot   = [](const Vec& a, const Vec& b) -> float { return (a[0] * b[0]) + (a[1] * b[1]) + (a[2] * b[2]); };
    auto cross = [](const Vec& a, const Vec& b) -> Vec {
        return Vec{(a[1] * b[2]) - (a[2] * b[1]), (a[2] * b[0]) - (a[0] * b[2]), (a[0] * b[1]) - (a[1] * b[0])};
    };
    auto norm      = [&dot](const Vec& a) -> float { return std::sqrt(dot(a, a)); };
    auto normalize = [&norm, &scale](const Vec& a) -> Vec { return scale(a, 1.0f / norm(a)); };
    auto as_json   = [](const Vec& p) -> json { return json::array({p[0], p[1], p[2]}); };
    auto as_vec    = [](const json& p) -> Vec { return Vec{p[0].get<float>(), p[1].get<float>(), p[2].get<float>()}; };
    auto as_quat   = [](const json& q) -> Quat { return Quat{q[0].get<float>(), q[1].get<float>(), q[2].get<float>(), q[3].get<float>()}; };
    // The angle between two rotations, in degrees.
    auto angle_deg = [](const Quat& a, const Quat& b) -> float {
        const float d = std::min(1.0f, std::abs((a[0] * b[0]) + (a[1] * b[1]) + (a[2] * b[2]) + (a[3] * b[3])));
        return 2.0f * std::acos(d) * (180.0f / 3.14159265f);
    };
    auto undo_depth = [&client]() -> std::size_t {
        return client.call_tool("get_undo_redo_stack", json::object()).payload.at("undo").size();
    };
    auto details = [&client, &scene](const char* node_name) -> json {
        Mcp_client::Tool_result result = client.call_tool("get_node_details", json{{"scene_name", scene}, {"node_name", node_name}});
        EXPECT_FALSE(result.is_error) << result.text;
        return result.payload;
    };
    auto drag = [&client, &scene, &as_json](const Vec& target, const json& extra) -> Mcp_client::Tool_result {
        json args{{"scene_name", scene}, {"node_name", "arm_joint_L_2"}, {"target", as_json(target)}};
        for (const auto& [key, value] : extra.items()) {
            args[key] = value;
        }
        return client.call_tool("ik_drag", args);
    };

    // Drag-start state.
    const json l1_start = details("arm_joint_L_1");
    const json l2_start = details("arm_joint_L_2");
    const json l3_start = details("arm_joint_L_3");
    const Vec  l1       = as_vec(l1_start.at("world_transform").at("translation"));
    const Vec  l2       = as_vec(l2_start.at("world_transform").at("translation"));
    const Vec  l3       = as_vec(l3_start.at("world_transform").at("translation"));
    const Quat l3_world_rotation = as_quat(l3_start.at("world_transform").at("rotation_xyzw"));
    const Quat l2_local_rotation = as_quat(l2_start.at("local_transform").at("rotation_xyzw"));
    const Quat l3_local_rotation = as_quat(l3_start.at("local_transform").at("rotation_xyzw"));

    // A target that keeps L_3 in reach: L_2's start position rotated 30
    // degrees about the L_1-to-L_3 line keeps |L_2 - L_1| and |L_2 - L_3|
    // (Rodrigues' rotation of the offset from the line's foot point).
    const Vec   axis   = normalize(sub(l3, l1));
    const Vec   offset = sub(l2, l1);
    const Vec   along  = scale(axis, dot(offset, axis));
    const Vec   radial = sub(offset, along);
    ASSERT_GT(norm(radial), 1.0e-3f) << "arm_joint_L_2 must not lie on the L_1-to-L_3 line";
    const float angle  = 30.0f * (3.14159265f / 180.0f);
    const Vec   target = add(add(l1, along), add(scale(radial, std::cos(angle)), scale(cross(axis, radial), std::sin(angle))));

    // pin_chain_end: echoed, the lower chain reported, L_3 pinned.
    advance_frames(client, 4);
    std::size_t undo_before = undo_depth();
    Mcp_client::Tool_result pinned = drag(target, json{{"mid_chain_drag", "pin_chain_end"}});
    ASSERT_FALSE(pinned.is_error) << pinned.text;
    EXPECT_EQ(pinned.payload.at("mid_chain_drag").get<std::string>(), "pin_chain_end");
    EXPECT_TRUE(pinned.payload.at("recorded").get<bool>());
    const json& lower_joints = pinned.payload.at("lower_joints");
    ASSERT_EQ(lower_joints.size(), std::size_t{2}) << "the lower chain is arm_joint_L_2 .. arm_joint_L_3";
    EXPECT_EQ(lower_joints[0].at("name").get<std::string>(), "arm_joint_L_2");
    EXPECT_EQ(lower_joints[1].at("name").get<std::string>(), "arm_joint_L_3");
    EXPECT_LT(norm(sub(as_vec(pinned.payload.at("joints").back().at("position")), target)), 1.0e-2f)
        << "the effector reaches the target";
    advance_frames(client, 4);
    const json l2_pinned = details("arm_joint_L_2");
    const json l3_pinned = details("arm_joint_L_3");
    EXPECT_LT(norm(sub(as_vec(l3_pinned.at("world_transform").at("translation")), l3)), 1.0e-3f)
        << "the end joint keeps its drag-start world position";
    EXPECT_LT(angle_deg(as_quat(l3_pinned.at("world_transform").at("rotation_xyzw")), l3_world_rotation), 0.05f)
        << "the end joint keeps its drag-start world rotation";
    EXPECT_GT(angle_deg(as_quat(l3_pinned.at("local_transform").at("rotation_xyzw")), l3_local_rotation), 1.0f)
        << "the end joint's local rotation changed, so the undo entry must cover it";
    EXPECT_GT(angle_deg(as_quat(l2_pinned.at("local_transform").at("rotation_xyzw")), l2_local_rotation), 1.0f)
        << "the effector turned to aim at the end joint";
    EXPECT_EQ(undo_depth(), undo_before + 1) << "one ik_drag records exactly one undo entry";

    // Undo restores every moved joint, the lower chain's included.
    client.call_tool("undo", json::object());
    advance_frames(client, 4);
    EXPECT_LT(angle_deg(as_quat(details("arm_joint_L_2").at("local_transform").at("rotation_xyzw")), l2_local_rotation), 0.01f);
    EXPECT_LT(angle_deg(as_quat(details("arm_joint_L_3").at("local_transform").at("rotation_xyzw")), l3_local_rotation), 0.01f);
    EXPECT_LT(norm(sub(as_vec(details("arm_joint_L_2").at("world_transform").at("translation")), l2)), 1.0e-4f);

    // rigid_children (the default): no lower chain, L_3's local transform
    // unchanged, so it follows L_2.
    undo_before = undo_depth();
    Mcp_client::Tool_result rigid = drag(target, json::object());
    ASSERT_FALSE(rigid.is_error) << rigid.text;
    EXPECT_EQ(rigid.payload.at("mid_chain_drag").get<std::string>(), "rigid_children") << "the mid_chain_drag default";
    EXPECT_TRUE(rigid.payload.at("lower_joints").empty());
    advance_frames(client, 4);
    const json l3_rigid = details("arm_joint_L_3");
    EXPECT_LT(angle_deg(as_quat(l3_rigid.at("local_transform").at("rotation_xyzw")), l3_local_rotation), 0.01f)
        << "under rigid_children the bones below the effector keep their local transforms";
    EXPECT_GT(norm(sub(as_vec(l3_rigid.at("world_transform").at("translation")), l3)), 1.0e-3f)
        << "under rigid_children the end joint moves with the effector";
    EXPECT_EQ(undo_depth(), undo_before + 1);
    client.call_tool("undo", json::object());
    advance_frames(client, 4);

    // Refusal changes nothing.
    undo_before = undo_depth();
    Mcp_client::Tool_result refused = drag(target, json{{"mid_chain_drag", "pinned"}});
    EXPECT_TRUE(refused.is_error) << "an unrecognized mid_chain_drag must be refused";
    advance_frames(client, 4);
    EXPECT_EQ(undo_depth(), undo_before) << "refused calls record nothing";
    EXPECT_LT(norm(sub(as_vec(details("arm_joint_L_2").at("world_transform").at("translation")), l2)), 1.0e-4f)
        << "refused calls move no joint";

    client.call_tool("close_scene", json{{"scene_name", scene}});
    advance_frames(client, 4);
}

// doc/plans/rigging/skeleton_editing.md R10, R12, R13 (slice A): the
// select_bones modes on RiggedFigure (torso_joint_1 is its skeleton root;
// torso_joint_3 branches into neck and both arms; torso_joint_1 into the
// torso and both legs; the side marker sits before a trailing index,
// arm_joint_L_1), and flip_bone_names as one undo step that undo restores.
TEST_F(Mcp_test, select_bones_modes_and_flip_bone_names_undo)
{
    Mcp_client& client = Mcp_env::get().client();

    const std::string scene = import_into_new_scene(client);
    ASSERT_FALSE(scene.empty()) << "could not create a scene to import into";
    ASSERT_TRUE(wait_until_idle(client, 60000));

    auto selected_names = [&client, &scene]() -> std::vector<std::string> {
        std::vector<std::string> names;
        Mcp_client::Tool_result selection = client.call_tool("get_selection", json::object());
        for (const json& item : selection.payload.at("items")) {
            if (item.value("scene_name", "") == scene) {
                names.push_back(item.value("name", ""));
            }
        }
        std::sort(names.begin(), names.end());
        return names;
    };
    auto select = [&client, &scene](const std::vector<std::string>& bones, const char* mode) -> Mcp_client::Tool_result {
        Mcp_client::Tool_result result = client.call_tool(
            "select_bones", json{{"scene_name", scene}, {"bones", bones}, {"mode", mode}}
        );
        advance_frames(client, 2);
        return result;
    };
    using Names = std::vector<std::string>;

    Mcp_client::Tool_result parent = select({"arm_joint_L_2"}, "parent");
    ASSERT_FALSE(parent.is_error) << parent.text;
    EXPECT_EQ(selected_names(), (Names{"arm_joint_L_1"}));
    EXPECT_EQ(parent.payload.at("active_item").value("name", ""), "arm_joint_L_1");

    ASSERT_FALSE(select({"torso_joint_3"}, "children").is_error);
    EXPECT_EQ(selected_names(), (Names{"arm_joint_L_1", "arm_joint_R_1", "neck_joint_1"}));

    ASSERT_FALSE(select({"torso_joint_3"}, "children_recursive").is_error);
    EXPECT_EQ(selected_names(), (Names{"arm_joint_L_1", "arm_joint_L_2", "arm_joint_L_3", "arm_joint_R_1", "arm_joint_R_2", "arm_joint_R_3", "neck_joint_1", "neck_joint_2"}));

    Mcp_client::Tool_result chain = select({"arm_joint_R_2"}, "chain");
    ASSERT_FALSE(chain.is_error) << chain.text;
    EXPECT_EQ(selected_names(), (Names{"arm_joint_R_1", "arm_joint_R_2", "arm_joint_R_3"}));
    EXPECT_EQ(chain.payload.at("active_item").value("name", ""), "arm_joint_R_2") << "chain keeps the named bone active";

    ASSERT_FALSE(select({"torso_joint_2"}, "chain").is_error);
    EXPECT_EQ(selected_names(), (Names{"torso_joint_2", "torso_joint_3"})) << "torso_joint_1 branches above, torso_joint_3 below";

    ASSERT_FALSE(select({"arm_joint_L_2", "leg_joint_R_5"}, "mirror").is_error);
    EXPECT_EQ(selected_names(), (Names{"arm_joint_R_2", "leg_joint_L_5"}));

    // Nothing to select: the selection stays.
    Mcp_client::Tool_result nothing = select({"torso_joint_2"}, "mirror");
    ASSERT_FALSE(nothing.is_error) << nothing.text;
    EXPECT_FALSE(nothing.payload.at("changed").get<bool>());
    EXPECT_EQ(selected_names(), (Names{"arm_joint_R_2", "leg_joint_L_5"}));

    EXPECT_TRUE(select({"torso_joint_2"}, "sideways").is_error) << "an unrecognized mode is refused";
    EXPECT_TRUE(select({"Armature"}, "parent").is_error) << "a non-bone is refused";

    // Flip Names: the two leg roots are siblings, so they swap; the arm hand
    // bone flips onto a name its (non-sibling) counterpart also has; the
    // torso bone has no side.
    auto undo_depth = [&client]() -> std::size_t {
        return client.call_tool("get_undo_redo_stack", json::object()).payload.at("undo").size();
    };
    auto node_exists = [&client, &scene](const std::string& name, const int id) -> bool {
        Mcp_client::Tool_result details = client.call_tool("get_node_details", json{{"scene_name", scene}, {"node_id", id}});
        return !details.is_error && (details.payload.value("name", "") == name);
    };
    auto id_of = [&client, &scene](const std::string& name) -> int {
        Mcp_client::Tool_result selected = client.call_tool(
            "select_bones", json{{"scene_name", scene}, {"bones", {name}}, {"mode", "chain"}}
        );
        return selected.payload.at("active_item").value("id", 0);
    };
    const int leg_l = id_of("leg_joint_L_1");
    const int leg_r = id_of("leg_joint_R_1");
    const int hand  = id_of("arm_joint_L_3");
    advance_frames(client, 4);
    const std::size_t undo_before = undo_depth();

    Mcp_client::Tool_result flipped = client.call_tool(
        "flip_bone_names",
        json{{"scene_name", scene}, {"bones", {"leg_joint_L_1", "leg_joint_R_1", "arm_joint_L_3", "torso_joint_2"}}}
    );
    ASSERT_FALSE(flipped.is_error) << flipped.text;
    EXPECT_EQ(flipped.payload.at("renamed").size(), std::size_t{3});
    ASSERT_EQ(flipped.payload.at("skipped").size(), std::size_t{1});
    EXPECT_EQ(flipped.payload.at("skipped")[0].value("name", ""), "torso_joint_2");
    advance_frames(client, 4);

    EXPECT_EQ(undo_depth(), undo_before + 1) << "Flip Names is one undo step";
    EXPECT_TRUE(node_exists("leg_joint_R_1", leg_l));
    EXPECT_TRUE(node_exists("leg_joint_L_1", leg_r));
    EXPECT_TRUE(node_exists("arm_joint_R_3", hand));

    client.call_tool("undo", json::object());
    advance_frames(client, 4);
    EXPECT_TRUE(node_exists("leg_joint_L_1", leg_l)) << "undo restores the names";
    EXPECT_TRUE(node_exists("leg_joint_R_1", leg_r));
    EXPECT_TRUE(node_exists("arm_joint_L_3", hand));
    EXPECT_EQ(undo_depth(), undo_before);

    client.call_tool("close_scene", json{{"scene_name", scene}});
    advance_frames(client, 4);
}

// doc/plans/rigging/skeleton_editing.md R14, R15 (slice B) on RiggedFigure:
// clear_pose restores the rest (the imported pose is the bind pose, which the
// Rig.rest_* values default to) while a locked channel keeps its value, and
// paste_pose 'flipped' of copy_pose's left-arm pose gives the right arm world
// positions mirrored across the skeleton root's X = 0 plane. Each is one
// undo step that undo reverts.
TEST_F(Mcp_test, clear_pose_respects_locks_and_paste_pose_flipped_mirrors_the_arm)
{
    Mcp_client& client = Mcp_env::get().client();

    const std::string scene = import_into_new_scene(client);
    ASSERT_FALSE(scene.empty()) << "could not create a scene to import into";
    ASSERT_TRUE(wait_until_idle(client, 60000));

    using Vec  = std::array<float, 3>;
    using Quat = std::array<float, 4>; // x, y, z, w
    auto as_vec  = [](const json& p) -> Vec { return Vec{p[0].get<float>(), p[1].get<float>(), p[2].get<float>()}; };
    auto as_quat = [](const json& q) -> Quat { return Quat{q[0].get<float>(), q[1].get<float>(), q[2].get<float>(), q[3].get<float>()}; };
    auto distance = [](const Vec& a, const Vec& b) -> float {
        return std::sqrt(((a[0] - b[0]) * (a[0] - b[0])) + ((a[1] - b[1]) * (a[1] - b[1])) + ((a[2] - b[2]) * (a[2] - b[2])));
    };
    auto angle_deg = [](const Quat& a, const Quat& b) -> float {
        const float d = std::min(1.0f, std::abs((a[0] * b[0]) + (a[1] * b[1]) + (a[2] * b[2]) + (a[3] * b[3])));
        return 2.0f * std::acos(d) * (180.0f / 3.14159265f);
    };
    // Rotates v by the unit quaternion q (x, y, z, w).
    auto rotate = [](const Quat& q, const Vec& v) -> Vec {
        const Vec   u{q[0], q[1], q[2]};
        const float w = q[3];
        const Vec   t{
            2.0f * ((u[1] * v[2]) - (u[2] * v[1])),
            2.0f * ((u[2] * v[0]) - (u[0] * v[2])),
            2.0f * ((u[0] * v[1]) - (u[1] * v[0]))
        };
        return Vec{
            v[0] + (w * t[0]) + ((u[1] * t[2]) - (u[2] * t[1])),
            v[1] + (w * t[1]) + ((u[2] * t[0]) - (u[0] * t[2])),
            v[2] + (w * t[2]) + ((u[0] * t[1]) - (u[1] * t[0]))
        };
    };
    auto undo_depth = [&client]() -> std::size_t {
        return client.call_tool("get_undo_redo_stack", json::object()).payload.at("undo").size();
    };
    auto details = [&client, &scene](const char* node_name) -> json {
        Mcp_client::Tool_result result = client.call_tool("get_node_details", json{{"scene_name", scene}, {"node_name", node_name}});
        EXPECT_FALSE(result.is_error) << result.text;
        return result.payload;
    };
    auto local_rotation    = [&](const char* name) -> Quat { return as_quat(details(name).at("local_transform").at("rotation_xyzw")); };
    auto local_translation = [&](const char* name) -> Vec  { return as_vec (details(name).at("local_transform").at("translation")); };
    auto world_position    = [&](const char* name) -> Vec  { return as_vec (details(name).at("world_transform").at("translation")); };
    auto set_local = [&client, &scene](const char* name, const json& components) {
        json args{{"scene_name", scene}, {"node_name", name}, {"space", "local"}};
        for (const auto& [key, value] : components.items()) {
            args[key] = value;
        }
        Mcp_client::Tool_result result = client.call_tool("set_node_transform", args);
        EXPECT_FALSE(result.is_error) << result.text;
        advance_frames(client, 2);
    };

    // Rest (= imported bind pose) values.
    const Quat l1_rest   = local_rotation("arm_joint_L_1");
    const Quat l2_rest   = local_rotation("arm_joint_L_2");
    const Vec  l3_rest_t = local_translation("arm_joint_L_3");
    const Quat r1_rest   = local_rotation("arm_joint_R_1");
    const Quat r2_rest   = local_rotation("arm_joint_R_2");

    const json root = details("torso_joint_1").at("world_transform");
    const Vec  root_t = as_vec(root.at("translation"));
    const Quat root_q = as_quat(root.at("rotation_xyzw"));
    auto mirrored = [&](const Vec& p) -> Vec {
        const Quat inverse_q{-root_q[0], -root_q[1], -root_q[2], root_q[3]};
        Vec local = rotate(inverse_q, Vec{p[0] - root_t[0], p[1] - root_t[1], p[2] - root_t[2]});
        local[0] = -local[0];
        const Vec back = rotate(root_q, local);
        return Vec{back[0] + root_t[0], back[1] + root_t[1], back[2] + root_t[2]};
    };
    const std::array<Vec, 2> right_rest_positions{world_position("arm_joint_R_2"), world_position("arm_joint_R_3")};
    const std::array<float, 2> right_rest_errors{
        distance(right_rest_positions[0], mirrored(world_position("arm_joint_L_2"))),
        distance(right_rest_positions[1], mirrored(world_position("arm_joint_L_3")))
    };

    // Pose the left arm; lock every rotation axis of L_2 and translation Y of L_3.
    const Quat bent{0.2588190f, 0.0f, 0.0f, 0.9659258f}; // 30 degrees about X
    set_local("arm_joint_L_1", json{{"rotation_xyzw", {0.0f, 0.0f, 0.3826834f, 0.9238795f}}}); // 45 degrees about Z
    set_local("arm_joint_L_2", json{{"rotation_xyzw", {bent[0], bent[1], bent[2], bent[3]}}});
    set_local("arm_joint_L_3", json{{"translation", {0.05f, 0.25f, 0.02f}}});
    const Quat l2_posed = local_rotation("arm_joint_L_2");
    ASSERT_GT(angle_deg(local_rotation("arm_joint_L_1"), l1_rest), 5.0f);
    ASSERT_GT(angle_deg(l2_posed, l2_rest), 5.0f);

    // Copy the posed left arm (explicit JSON; the editor pose buffer is not involved).
    Mcp_client::Tool_result copied = client.call_tool(
        "copy_pose", json{{"scene_name", scene}, {"bones", {"arm_joint_L_1", "arm_joint_L_2", "arm_joint_L_3"}}}
    );
    ASSERT_FALSE(copied.is_error) << copied.text;
    const json pose = copied.payload.at("pose");
    ASSERT_EQ(pose.size(), std::size_t{3});
    EXPECT_EQ(pose[1].value("name", ""), "arm_joint_L_2");

    // Paste flipped onto the right arm: one undo entry, world positions mirrored.
    std::size_t undo_before = undo_depth();
    Mcp_client::Tool_result pasted = client.call_tool(
        "paste_pose", json{{"scene_name", scene}, {"skeleton", "torso_joint_1"}, {"pose", pose}, {"mode", "flipped"}}
    );
    ASSERT_FALSE(pasted.is_error) << pasted.text;
    EXPECT_TRUE(pasted.payload.at("unmatched").empty());
    EXPECT_EQ(pasted.payload.at("changed").size(), std::size_t{3});
    advance_frames(client, 4);
    EXPECT_EQ(undo_depth(), undo_before + 1) << "paste_pose is one undo step";

    // RiggedFigure is symmetric about the world X = 0 plane, and its
    // skeleton root is tilted from it by about a third of a degree, so its
    // rest pose is itself mirror-asymmetric by a few millimeters in the root
    // frame: the pasted arm must mirror the posed arm to within that.
    for (std::size_t i = 0; i < 2; ++i) {
        const char* const left  = (i == 0) ? "arm_joint_L_2" : "arm_joint_L_3";
        const char* const right = (i == 0) ? "arm_joint_R_2" : "arm_joint_R_3";
        const float error = distance(world_position(right), mirrored(world_position(left)));
        EXPECT_GT(distance(world_position(right), right_rest_positions[i]), 2.0e-2f) << right << " moved";
        EXPECT_LT(error, right_rest_errors[i] + 1.0e-3f)
            << right << " sits at the mirror of " << left << " (rest asymmetry " << right_rest_errors[i] << ")";
    }

    client.call_tool("undo", json::object());
    advance_frames(client, 4);
    EXPECT_LT(angle_deg(local_rotation("arm_joint_R_1"), r1_rest), 0.1f) << "undo restores the right arm";
    EXPECT_LT(angle_deg(local_rotation("arm_joint_R_2"), r2_rest), 0.1f);
    EXPECT_EQ(undo_depth(), undo_before);

    // Clear: locks set after posing.
    client.call_tool("set_item_flags", json{{"scene_name", scene}, {"ids", {details("arm_joint_L_2").value("id", 0)}}, {"flags", {"lock_rotation_x", "lock_rotation_y", "lock_rotation_z"}}});
    client.call_tool("set_item_flags", json{{"scene_name", scene}, {"ids", {details("arm_joint_L_3").value("id", 0)}}, {"flags", {"lock_translation_y"}}});
    advance_frames(client, 2);

    EXPECT_TRUE(client.call_tool("clear_pose", json{{"scene_name", scene}, {"bones", {"arm_joint_L_1"}}, {"channels", "sideways"}}).is_error)
        << "an unrecognized channel is refused";
    undo_before = undo_depth();
    Mcp_client::Tool_result cleared = client.call_tool(
        "clear_pose", json{{"scene_name", scene}, {"bones", {"arm_joint_L_1", "arm_joint_L_2", "arm_joint_L_3"}}, {"channels", "all"}}
    );
    ASSERT_FALSE(cleared.is_error) << cleared.text;
    advance_frames(client, 4);
    EXPECT_EQ(undo_depth(), undo_before + 1) << "clear_pose is one undo step";
    EXPECT_LT(angle_deg(local_rotation("arm_joint_L_1"), l1_rest), 0.1f) << "Clear restores the rest rotation";
    EXPECT_LT(angle_deg(local_rotation("arm_joint_L_2"), l2_posed), 0.1f) << "a bone with every rotation axis locked keeps its rotation";
    const Vec l3_cleared = local_translation("arm_joint_L_3");
    EXPECT_LT(std::abs(l3_cleared[0] - l3_rest_t[0]), 1.0e-4f) << "unlocked translation x returns to rest";
    EXPECT_LT(std::abs(l3_cleared[1] - 0.25f), 1.0e-4f)         << "locked translation y keeps its value";
    EXPECT_LT(std::abs(l3_cleared[2] - l3_rest_t[2]), 1.0e-4f)  << "unlocked translation z returns to rest";

    client.call_tool("undo", json::object());
    advance_frames(client, 4);
    EXPECT_GT(angle_deg(local_rotation("arm_joint_L_1"), l1_rest), 5.0f) << "undo restores the pose";
    EXPECT_EQ(undo_depth(), undo_before);

    client.call_tool("close_scene", json{{"scene_name", scene}});
    advance_frames(client, 4);
}

// doc/plans/rigging/skeleton_editing.md R2: Ik.rest_rotation defaults to
// Rig.rest_rotation (D31 default_from), so a Rig.rest_rotation local value
// moves the IK limits frame - a joint whose limits are all zero ends an
// ik_drag exactly on it; checked on an authored, unskinned chain (a bound
// bone refuses the write, R9), which the IK drag routes through as bones. R14 / R15: Clear and Paste Pose stop an animation
// playing on the bones first, as Reset Bones to Bind Pose does.
TEST_F(Mcp_test, rig_rest_rotation_is_the_ik_limits_frame_and_posing_stops_the_animation)
{
    Mcp_client& client = Mcp_env::get().client();

    const std::string scene = import_into_new_scene(client);
    ASSERT_FALSE(scene.empty()) << "could not create a scene to import into";
    ASSERT_TRUE(wait_until_idle(client, 60000));

    using Quat = std::array<float, 4>; // x, y, z, w
    auto as_quat = [](const json& q) -> Quat { return Quat{q[0].get<float>(), q[1].get<float>(), q[2].get<float>(), q[3].get<float>()}; };
    auto parse_quat = [](const std::string& text) -> Quat {
        Quat q{};
        std::istringstream stream{text};
        stream >> q[0] >> q[1] >> q[2] >> q[3];
        return q;
    };
    auto angle_deg = [](const Quat& a, const Quat& b) -> float {
        const float d = std::min(1.0f, std::abs((a[0] * b[0]) + (a[1] * b[1]) + (a[2] * b[2]) + (a[3] * b[3])));
        return 2.0f * std::acos(d) * (180.0f / 3.14159265f);
    };
    // a * b, both (x, y, z, w).
    auto multiply = [](const Quat& a, const Quat& b) -> Quat {
        return Quat{
            (a[3] * b[0]) + (a[0] * b[3]) + (a[1] * b[2]) - (a[2] * b[1]),
            (a[3] * b[1]) - (a[0] * b[2]) + (a[1] * b[3]) + (a[2] * b[0]),
            (a[3] * b[2]) + (a[0] * b[1]) - (a[1] * b[0]) + (a[2] * b[3]),
            (a[3] * b[3]) - (a[0] * b[0]) - (a[1] * b[1]) - (a[2] * b[2])
        };
    };
    auto details = [&client, &scene](const char* node_name) -> json {
        Mcp_client::Tool_result result = client.call_tool("get_node_details", json{{"scene_name", scene}, {"node_name", node_name}});
        EXPECT_FALSE(result.is_error) << result.text;
        return result.payload;
    };
    auto property = [&client](const int item_id, const char* name) -> json {
        Mcp_client::Tool_result result = client.call_tool("get_item_properties", json{{"item_id", item_id}});
        EXPECT_FALSE(result.is_error) << result.text;
        for (const json& entry : result.payload.at("properties")) {
            if (entry.value("name", "") == name) {
                return entry;
            }
        }
        return json{};
    };
    auto set_property = [&client](const int item_id, const char* name, const json& value) {
        Mcp_client::Tool_result result = client.call_tool("set_item_property", json{{"item_id", item_id}, {"property", name}, {"value", value}});
        EXPECT_FALSE(result.is_error) << name << ": " << result.text;
    };

    // The limits frame check runs on an authored, unskinned chain: a bone a
    // skin lists refuses a Rig.rest_rotation write (R9).
    Mcp_client::Tool_result refused_rest = client.call_tool(
        "set_item_property", json{{"item_id", details("arm_joint_L_2").value("id", 0)}, {"property", "Rig.rest_rotation"}, {"value", "0 0 0.3826834 0.9238795"}}
    );
    EXPECT_TRUE(refused_rest.is_error) << "the rest of a bound bone is its bind pose";
    EXPECT_NE(refused_rest.text.find("skin"), std::string::npos) << refused_rest.text;

    Mcp_client::Tool_result created = client.call_tool("create_bone", json{{"scene_name", scene}, {"name", "ik_chain"}});
    ASSERT_FALSE(created.is_error) << created.text;
    advance_frames(client, 2);
    for (const char* from : {"ik_chain", "ik_chain.001"}) {
        Mcp_client::Tool_result extruded = client.call_tool("extrude_bones", json{{"scene_name", scene}, {"bones", {from}}});
        ASSERT_FALSE(extruded.is_error) << extruded.text;
        advance_frames(client, 2);
    }

    const json l2      = details("ik_chain.001");
    const int  l2_id   = l2.value("id", 0);
    const Quat l2_rest = as_quat(l2.at("local_transform").at("rotation_xyzw"));

    // Every IK limit of the middle bone closed to zero: the constrained solve
    // holds it exactly on its limits frame.
    for (const char* name : {"Ik.limit_x", "Ik.limit_y", "Ik.limit_z"}) {
        set_property(l2_id, name, true);
    }
    set_property(l2_id, "Ik.limit_min", "0 0 0");
    set_property(l2_id, "Ik.limit_max", "0 0 0");

    // A rest rotation 25 degrees about the bone's own X away from its creation rest.
    const Quat turn{0.2164396f, 0.0f, 0.0f, 0.9762960f};
    const Quat rest = multiply(l2_rest, turn);
    std::ostringstream rest_text;
    rest_text.precision(9);
    rest_text << rest[0] << " " << rest[1] << " " << rest[2] << " " << rest[3];
    set_property(l2_id, "Rig.rest_rotation", rest_text.str());
    advance_frames(client, 2);

    const json ik_rest = property(l2_id, "Ik.rest_rotation");
    ASSERT_TRUE(ik_rest.is_object());
    EXPECT_EQ(ik_rest.value("source", ""), "default") << "Ik.rest_rotation follows its default";
    EXPECT_LT(angle_deg(parse_quat(ik_rest.value("value", "")), rest), 0.05f) << "Ik.rest_rotation is Rig.rest_rotation";

    // The drag starts on the new rest: the solve never teleports a joint
    // into its limits (the region is extended to hold the drag-start pose),
    // so a joint starting on the limits frame is held there, while a limits
    // frame on the creation rest would leave it room to turn back toward it.
    Mcp_client::Tool_result placed = client.call_tool(
        "set_node_transform",
        json{{"scene_name", scene}, {"node_name", "ik_chain.001"}, {"space", "local"}, {"rotation_xyzw", {rest[0], rest[1], rest[2], rest[3]}}}
    );
    ASSERT_FALSE(placed.is_error) << placed.text;
    advance_frames(client, 2);

    // The authored chain is unskinned: the IK drag routes through its bones
    // all the same (erhe::scene::is_bone, the persistent flag).
    const json l3 = details("ik_chain.002").at("world_transform").at("translation");
    const json drag_target = {l3[0].get<float>() + 0.1f, l3[1].get<float>() + 0.1f, l3[2].get<float>() - 0.05f};
    Mcp_client::Tool_result dragged = client.call_tool(
        "ik_drag", json{{"scene_name", scene}, {"node_name", "ik_chain.002"}, {"target", drag_target}}
    );
    ASSERT_FALSE(dragged.is_error) << dragged.text;
    advance_frames(client, 4);
    const Quat l2_after = as_quat(details("ik_chain.001").at("local_transform").at("rotation_xyzw"));
    EXPECT_LT(angle_deg(l2_after, rest), 0.5f) << "the drag holds the joint on the Rig.rest_rotation limits frame";
    EXPECT_GT(angle_deg(l2_after, l2_rest), 20.0f) << "not on the creation rest";
    // Control: with the Rig value cleared the limits frame is the creation
    // rest again, and the same drag from the same start turns the joint.
    client.call_tool("undo", json::object()); // the drag
    advance_frames(client, 4);
    Mcp_client::Tool_result unset = client.call_tool("set_item_property", json{{"item_id", l2_id}, {"property", "Rig.rest_rotation"}, {"value", nullptr}});
    ASSERT_FALSE(unset.is_error) << unset.text;
    advance_frames(client, 2);
    EXPECT_LT(angle_deg(parse_quat(property(l2_id, "Ik.rest_rotation").value("value", "")), l2_rest), 0.05f) << "Ik.rest_rotation follows back to the creation rest";
    ASSERT_LT(angle_deg(as_quat(details("ik_chain.001").at("local_transform").at("rotation_xyzw")), rest), 0.05f);
    Mcp_client::Tool_result control = client.call_tool(
        "ik_drag", json{{"scene_name", scene}, {"node_name", "ik_chain.002"}, {"target", drag_target}}
    );
    ASSERT_FALSE(control.is_error) << control.text;
    advance_frames(client, 4);
    EXPECT_GT(angle_deg(as_quat(details("ik_chain.001").at("local_transform").at("rotation_xyzw")), rest), 1.0f)
        << "on the creation-rest limits frame the joint is not held at the former rest";

    // Clear and Paste Pose stop an animation playing on the bones.
    const std::string animation = first_animation_name(client, scene);
    ASSERT_FALSE(animation.empty()) << "test glTF carries no animation";
    auto play = [&client, &scene, &animation]() -> bool {
        Mcp_client::Tool_result result = client.call_tool(
            "animation_playback", json{{"animation", animation}, {"scene_name", scene}, {"action", "play"}}
        );
        advance_frames(client, 3);
        return !result.is_error && result.payload.value("playing", false);
    };
    auto playing = [&client]() -> bool {
        return client.call_tool("animation_playback", json::object()).payload.value("playing", false);
    };

    ASSERT_TRUE(play());
    Mcp_client::Tool_result cleared = client.call_tool(
        "clear_pose", json{{"scene_name", scene}, {"bones", {"arm_joint_L_1"}}, {"channels", "rotation"}}
    );
    ASSERT_FALSE(cleared.is_error) << cleared.text;
    advance_frames(client, 2);
    EXPECT_FALSE(playing()) << "clear_pose stops the animation playing on the bone";

    ASSERT_TRUE(play());
    Mcp_client::Tool_result copied = client.call_tool("copy_pose", json{{"scene_name", scene}, {"bones", {"arm_joint_L_1"}}});
    ASSERT_FALSE(copied.is_error) << copied.text;
    Mcp_client::Tool_result pasted = client.call_tool(
        "paste_pose", json{{"scene_name", scene}, {"skeleton", "torso_joint_1"}, {"pose", copied.payload.at("pose")}, {"mode", "normal"}}
    );
    ASSERT_FALSE(pasted.is_error) << pasted.text;
    advance_frames(client, 2);
    EXPECT_FALSE(playing()) << "paste_pose stops the animation playing on the skeleton";

    client.call_tool("close_scene", json{{"scene_name", scene}});
    advance_frames(client, 4);
}

namespace {

using Rig_vec = std::array<float, 3>;

[[nodiscard]] auto parse_rig_vec(const std::string& text) -> Rig_vec
{
    Rig_vec v{};
    std::istringstream stream{text};
    stream >> v[0] >> v[1] >> v[2];
    return v;
}

[[nodiscard]] auto rig_vec_distance(const Rig_vec& a, const Rig_vec& b) -> float
{
    return std::sqrt(((a[0] - b[0]) * (a[0] - b[0])) + ((a[1] - b[1]) * (a[1] - b[1])) + ((a[2] - b[2]) * (a[2] - b[2])));
}

// One entry of get_item_properties by its qualified name; null when absent.
[[nodiscard]] auto item_property(Mcp_client& client, const int item_id, const std::string& name) -> json
{
    Mcp_client::Tool_result result = client.call_tool("get_item_properties", json{{"item_id", item_id}});
    EXPECT_FALSE(result.is_error) << result.text;
    if (result.is_error) {
        return json{};
    }
    for (const json& entry : result.payload.at("properties")) {
        if (entry.value("name", "") == name) {
            return entry;
        }
    }
    return json{};
}

} // anonymous namespace

// doc/plans/rigging/skeleton_editing.md R1, R3, R4 on a skeleton no skin
// lists: the authored bone flag, Rig.tail and Rig.connected survive a save
// and reopen; a connected child follows its parent's tail edit and snaps to
// the parent's tail when connected, each edit one undo step.
TEST_F(Mcp_test, unskinned_skeleton_keeps_its_bones_and_connected_children_follow_the_tail)
{
    Mcp_client& client = Mcp_env::get().client();

    const std::vector<std::string> before = scene_names(client);
    client.call_tool("create_scene", json::object());
    advance_frames(client, 6);
    std::string scene;
    for (const std::string& name : scene_names(client)) {
        if (std::find(before.begin(), before.end(), name) == before.end()) {
            scene = name;
        }
    }
    ASSERT_FALSE(scene.empty()) << "could not create a scene";

    auto create = [&client, &scene](const char* name, const json& parent, const Rig_vec& position) -> int {
        json args{{"scene_name", scene}, {"name", name}, {"position", {position[0], position[1], position[2]}}};
        if (parent.is_number()) {
            args["parent_node_id"] = parent;
        }
        Mcp_client::Tool_result result = client.call_tool("create_node", args);
        EXPECT_FALSE(result.is_error) << result.text;
        advance_frames(client, 2);
        return result.payload.value("node_id", 0);
    };
    auto set_property = [&client](const int item_id, const char* name, const json& value) -> Mcp_client::Tool_result {
        Mcp_client::Tool_result result = client.call_tool("set_item_property", json{{"item_id", item_id}, {"property", name}, {"value", value}});
        advance_frames(client, 2);
        return result;
    };
    auto undo_depth = [&client]() -> std::size_t {
        return client.call_tool("get_undo_redo_stack", json::object()).payload.at("undo").size();
    };
    auto local_translation = [&client](const std::string& scene_name, const char* name) -> Rig_vec {
        Mcp_client::Tool_result result = client.call_tool("get_node_details", json{{"scene_name", scene_name}, {"node_name", name}});
        EXPECT_FALSE(result.is_error) << result.text;
        const json& t = result.payload.at("local_transform").at("translation");
        return Rig_vec{t[0].get<float>(), t[1].get<float>(), t[2].get<float>()};
    };

    const int root  = create("rig_root",  json(),     Rig_vec{0.0f, 0.0f, 0.0f});
    const int upper = create("rig_upper", json(root), Rig_vec{0.0f, 1.0f, 0.0f});
    const int side  = create("rig_side",  json(root), Rig_vec{1.0f, 0.0f, 0.0f});
    for (const int id : {root, upper, side}) {
        Mcp_client::Tool_result flagged = set_property(id, "bone", true);
        ASSERT_FALSE(flagged.is_error) << flagged.text;
    }
    EXPECT_EQ(item_property(client, root, "bone").value("value", ""), "true");
    const json default_tail = item_property(client, root, "Rig.tail");
    EXPECT_EQ(default_tail.value("source", ""), "default");
    EXPECT_LT(rig_vec_distance(parse_rig_vec(default_tail.value("value", "")), Rig_vec{0.0f, 1.0f, 0.0f}), 1.0e-4f)
        << "an unskinned bone's default tail is its first bone child's head";

    // Connecting rig_side snaps it onto the root's tail (the first child's head).
    std::size_t undo_before = undo_depth();
    ASSERT_FALSE(set_property(side, "Rig.connected", true).is_error);
    EXPECT_EQ(undo_depth(), undo_before + 1) << "connect is one undo step";
    EXPECT_LT(rig_vec_distance(local_translation(scene, "rig_side"), Rig_vec{0.0f, 1.0f, 0.0f}), 1.0e-4f) << "connected snaps the head onto the parent's tail";
    client.call_tool("undo", json::object());
    advance_frames(client, 4);
    EXPECT_LT(rig_vec_distance(local_translation(scene, "rig_side"), Rig_vec{1.0f, 0.0f, 0.0f}), 1.0e-4f) << "undo of connect restores the head";
    EXPECT_EQ(undo_depth(), undo_before);

    // A tail edit moves the connected child with it; the other child stays.
    ASSERT_FALSE(set_property(upper, "Rig.connected", true).is_error);
    undo_before = undo_depth();
    Mcp_client::Tool_result tail_edit = set_property(root, "Rig.tail", "0.5 2 0");
    ASSERT_FALSE(tail_edit.is_error) << tail_edit.text;
    EXPECT_EQ(undo_depth(), undo_before + 1) << "the tail edit and the child move are one undo step";
    EXPECT_LT(rig_vec_distance(local_translation(scene, "rig_upper"), Rig_vec{0.5f, 2.0f, 0.0f}), 1.0e-4f) << "the connected child follows the tail";
    EXPECT_LT(rig_vec_distance(local_translation(scene, "rig_side"), Rig_vec{1.0f, 0.0f, 0.0f}), 1.0e-4f) << "an unconnected child stays";
    client.call_tool("undo", json::object());
    advance_frames(client, 4);
    EXPECT_LT(rig_vec_distance(local_translation(scene, "rig_upper"), Rig_vec{0.0f, 1.0f, 0.0f}), 1.0e-4f) << "undo moves the child back";
    EXPECT_EQ(item_property(client, root, "Rig.tail").value("source", ""), "default") << "undo restores the default tail";
    client.call_tool("redo", json::object());
    advance_frames(client, 4);
    EXPECT_LT(rig_vec_distance(local_translation(scene, "rig_upper"), Rig_vec{0.5f, 2.0f, 0.0f}), 1.0e-4f) << "redo moves it again";

    // Save, close, reopen: the bones, the tail and the connection persist.
    const std::string path = (std::filesystem::temp_directory_path() / "erhe_mcp_unskinned_skeleton.glb").string();
    Mcp_client::Tool_result saved = client.call_tool("save_scene", json{{"scene_name", scene}, {"path", path}});
    ASSERT_FALSE(saved.is_error) << saved.text;
    client.call_tool("close_scene", json{{"scene_name", scene}});
    advance_frames(client, 4);
    Mcp_client::Tool_result opened = client.call_tool("open_scene", json{{"path", path}});
    ASSERT_FALSE(opened.is_error) << opened.text;
    advance_frames(client, 10);
    ASSERT_TRUE(wait_until_idle(client, 60000));
    const std::string reopened = opened.payload.value("scene_name", "");
    ASSERT_FALSE(reopened.empty());
    auto id_in = [&client, &reopened](const char* name) -> int {
        Mcp_client::Tool_result result = client.call_tool("get_node_details", json{{"scene_name", reopened}, {"node_name", name}});
        EXPECT_FALSE(result.is_error) << result.text;
        return result.payload.value("id", 0);
    };
    for (const char* name : {"rig_root", "rig_upper", "rig_side"}) {
        EXPECT_EQ(item_property(client, id_in(name), "bone").value("value", ""), "true") << name << " is still a bone after reload";
    }
    const json reloaded_tail = item_property(client, id_in("rig_root"), "Rig.tail");
    EXPECT_EQ(reloaded_tail.value("source", ""), "local");
    EXPECT_LT(rig_vec_distance(parse_rig_vec(reloaded_tail.value("value", "")), Rig_vec{0.5f, 2.0f, 0.0f}), 1.0e-4f);
    EXPECT_EQ(item_property(client, id_in("rig_upper"), "Rig.connected").value("value", ""), "true");
    EXPECT_LT(rig_vec_distance(local_translation(reopened, "rig_upper"), Rig_vec{0.5f, 2.0f, 0.0f}), 1.0e-4f);

    client.call_tool("close_scene", json{{"scene_name", reopened}});
    advance_frames(client, 4);
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
}

// R3 on RiggedFigure: Rig.tail defaults to the skinned inference (a joint
// with one child joint points at it; the hand, a leaf, points along its +Y);
// R9: a Rig.tail write on a joint a skin lists is refused, naming the skin.
TEST_F(Mcp_test, rig_tail_defaults_to_the_skinned_inference_and_is_refused_on_a_bound_bone)
{
    Mcp_client& client = Mcp_env::get().client();

    const std::string scene = import_into_new_scene(client);
    ASSERT_FALSE(scene.empty()) << "could not create a scene to import into";
    ASSERT_TRUE(wait_until_idle(client, 60000));

    auto details = [&client, &scene](const char* node_name) -> json {
        Mcp_client::Tool_result result = client.call_tool("get_node_details", json{{"scene_name", scene}, {"node_name", node_name}});
        EXPECT_FALSE(result.is_error) << result.text;
        return result.payload;
    };
    auto as_vec = [](const json& p) -> Rig_vec { return Rig_vec{p[0].get<float>(), p[1].get<float>(), p[2].get<float>()}; };

    const int  upper_id = details("arm_joint_L_1").value("id", 0);
    const json tail     = item_property(client, upper_id, "Rig.tail");
    EXPECT_EQ(tail.value("source", ""), "default");
    EXPECT_LT(
        rig_vec_distance(parse_rig_vec(tail.value("value", "")), as_vec(details("arm_joint_L_2").at("local_transform").at("translation"))),
        1.0e-4f
    ) << "a joint with one child joint: the child's head";

    const int     hand_id   = details("arm_joint_L_3").value("id", 0);
    const Rig_vec hand_tail = parse_rig_vec(item_property(client, hand_id, "Rig.tail").value("value", ""));
    EXPECT_LT(std::abs(hand_tail[0]) + std::abs(hand_tail[2]), 1.0e-4f) << "a leaf joint points along its +Y";
    EXPECT_GT(hand_tail[1], 0.01f);

    Mcp_client::Tool_result refused = client.call_tool("set_item_property", json{{"item_id", upper_id}, {"property", "Rig.tail"}, {"value", "0 1 0"}});
    EXPECT_TRUE(refused.is_error) << "a bound bone's tail is fixed by its bind";
    EXPECT_NE(refused.text.find("skin"), std::string::npos) << refused.text;
    advance_frames(client, 2);
    EXPECT_EQ(item_property(client, upper_id, "Rig.tail").value("source", ""), "default");

    client.call_tool("close_scene", json{{"scene_name", scene}});
    advance_frames(client, 4);
}

// doc/plans/rigging/skeleton_editing.md R5-R8: an unskinned chain built by
// create_bone + extrude_bones x2, then subdivided, dissolved and deleted;
// every verb is one undo step whose undo restores parents, local
// transforms, tails, rest values and the connected flag.
TEST_F(Mcp_test, bone_structure_verbs_build_an_unskinned_chain_and_undo_exactly)
{
    Mcp_client& client = Mcp_env::get().client();

    const std::vector<std::string> before = scene_names(client);
    client.call_tool("create_scene", json::object());
    advance_frames(client, 6);
    std::string scene;
    for (const std::string& name : scene_names(client)) {
        if (std::find(before.begin(), before.end(), name) == before.end()) {
            scene = name;
        }
    }
    ASSERT_FALSE(scene.empty()) << "could not create a scene";

    auto undo_depth = [&client]() -> std::size_t {
        return client.call_tool("get_undo_redo_stack", json::object()).payload.at("undo").size();
    };
    auto details = [&client, &scene](const char* name) -> json {
        Mcp_client::Tool_result result = client.call_tool("get_node_details", json{{"scene_name", scene}, {"node_name", name}});
        return result.is_error ? json::object() : result.payload;
    };
    auto exists = [&details](const char* name) -> bool {
        return details(name).contains("id");
    };
    auto vec_of = [](const json& v) -> Rig_vec { return Rig_vec{v[0].get<float>(), v[1].get<float>(), v[2].get<float>()}; };
    auto local_t = [&details, &vec_of](const char* name) -> Rig_vec { return vec_of(details(name).at("local_transform").at("translation")); };
    auto world_t = [&details, &vec_of](const char* name) -> Rig_vec { return vec_of(details(name).at("world_transform").at("translation")); };
    auto parent_of = [&details](const char* name) -> std::string { return details(name).value("parent", ""); };
    auto rig = [&client, &details](const char* name, const char* property) -> json {
        return item_property(client, details(name).value("id", 0), property);
    };
    auto rig_vec = [&rig](const char* name, const char* property) -> Rig_vec { return parse_rig_vec(rig(name, property).value("value", "")); };
    auto call = [&client](const char* tool, const json& args) -> Mcp_client::Tool_result {
        Mcp_client::Tool_result result = client.call_tool(tool, args);
        advance_frames(client, 3);
        return result;
    };
    auto undo = [&client]() { client.call_tool("undo", json::object()); advance_frames(client, 4); };
    auto redo = [&client]() { client.call_tool("redo", json::object()); advance_frames(client, 4); };
    constexpr float eps = 1.0e-4f;

    // R5: at the scene root the head is the origin and the tail +Y, one unit.
    std::size_t depth = undo_depth();
    Mcp_client::Tool_result created = call("create_bone", json{{"scene_name", scene}, {"name", "chain"}});
    ASSERT_FALSE(created.is_error) << created.text;
    EXPECT_EQ(undo_depth(), depth + 1) << "create_bone is one undo step";
    ASSERT_TRUE(exists("chain"));
    EXPECT_EQ(rig("chain", "bone").value("value", ""), "true");
    EXPECT_LT(rig_vec_distance(local_t("chain"), Rig_vec{0.0f, 0.0f, 0.0f}), eps);
    EXPECT_LT(rig_vec_distance(rig_vec("chain", "Rig.tail"), Rig_vec{0.0f, 1.0f, 0.0f}), eps);
    EXPECT_EQ(rig("chain", "Rig.tail").value("source", ""), "local") << "the creation records the tail";
    EXPECT_EQ(rig("chain", "Rig.rest_translation").value("source", ""), "local") << "the creation records the rest";
    EXPECT_TRUE(details("chain").value("selected", false)) << "the new bone is selected";

    // R6: two extrudes grow a connected chain along +Y.
    for (const char* from : {"chain", "chain.001"}) {
        depth = undo_depth();
        Mcp_client::Tool_result extruded = call("extrude_bones", json{{"scene_name", scene}, {"bones", {from}}});
        ASSERT_FALSE(extruded.is_error) << extruded.text;
        EXPECT_EQ(undo_depth(), depth + 1) << "extrude_bones is one undo step";
    }
    ASSERT_TRUE(exists("chain.001"));
    ASSERT_TRUE(exists("chain.002"));
    EXPECT_EQ(parent_of("chain.001"), "chain");
    EXPECT_EQ(parent_of("chain.002"), "chain.001");
    EXPECT_LT(rig_vec_distance(world_t("chain.001"), Rig_vec{0.0f, 1.0f, 0.0f}), eps);
    EXPECT_LT(rig_vec_distance(world_t("chain.002"), Rig_vec{0.0f, 2.0f, 0.0f}), eps);
    EXPECT_LT(rig_vec_distance(rig_vec("chain.002", "Rig.tail"), Rig_vec{0.0f, 1.0f, 0.0f}), eps) << "same direction and length as the parent";
    EXPECT_EQ(rig("chain.002", "Rig.connected").value("value", ""), "true");
    EXPECT_TRUE(details("chain.002").value("selected", false)) << "the extruded bone is the selection";
    EXPECT_FALSE(details("chain.001").value("selected", false));
    undo();
    EXPECT_FALSE(exists("chain.002")) << "undo removes the extruded bone";
    EXPECT_TRUE(details("chain.001").value("selected", false)) << "undo restores the selection";
    redo();
    ASSERT_TRUE(exists("chain.002"));
    EXPECT_LT(rig_vec_distance(world_t("chain.002"), Rig_vec{0.0f, 2.0f, 0.0f}), eps);

    // R7: chain.001 into three; chain.002 moves to the last piece, world kept.
    depth = undo_depth();
    Mcp_client::Tool_result divided = call("subdivide_bones", json{{"scene_name", scene}, {"bones", {"chain.001"}}, {"count", 3}});
    ASSERT_FALSE(divided.is_error) << divided.text;
    EXPECT_EQ(undo_depth(), depth + 1) << "subdivide_bones is one undo step";
    ASSERT_EQ(divided.payload.at("created").size(), 2u);
    ASSERT_TRUE(exists("chain.003"));
    ASSERT_TRUE(exists("chain.004"));
    EXPECT_EQ(parent_of("chain.003"), "chain.001");
    EXPECT_EQ(parent_of("chain.004"), "chain.003");
    EXPECT_EQ(parent_of("chain.002"), "chain.004") << "children re-parent to the last piece";
    EXPECT_LT(rig_vec_distance(rig_vec("chain.001", "Rig.tail"), Rig_vec{0.0f, 1.0f / 3.0f, 0.0f}), eps);
    EXPECT_LT(rig_vec_distance(world_t("chain.004"), Rig_vec{0.0f, 1.0f + (2.0f / 3.0f), 0.0f}), eps);
    EXPECT_LT(rig_vec_distance(world_t("chain.002"), Rig_vec{0.0f, 2.0f, 0.0f}), eps) << "the child keeps its world transform";
    EXPECT_LT(rig_vec_distance(local_t("chain.002"), Rig_vec{0.0f, 1.0f / 3.0f, 0.0f}), eps) << "a connected child sits on the last piece's tail";
    EXPECT_LT(rig_vec_distance(rig_vec("chain.002", "Rig.rest_translation"), Rig_vec{0.0f, 1.0f / 3.0f, 0.0f}), eps) << "the child's rest moves with it";
    undo();
    EXPECT_FALSE(exists("chain.003"));
    EXPECT_FALSE(exists("chain.004"));
    EXPECT_EQ(parent_of("chain.002"), "chain.001");
    EXPECT_LT(rig_vec_distance(local_t("chain.002"), Rig_vec{0.0f, 1.0f, 0.0f}), eps) << "undo restores the local transform";
    EXPECT_LT(rig_vec_distance(rig_vec("chain.001", "Rig.tail"), Rig_vec{0.0f, 1.0f, 0.0f}), eps) << "undo restores the tail";
    EXPECT_LT(rig_vec_distance(rig_vec("chain.002", "Rig.rest_translation"), Rig_vec{0.0f, 1.0f, 0.0f}), eps);

    // R8 dissolve: chain.001 was chain's only connected child, so chain's
    // tail reaches chain.001's tail and chain.002 stays connected under it.
    depth = undo_depth();
    Mcp_client::Tool_result dissolved = call("delete_bones", json{{"scene_name", scene}, {"bones", {"chain.001"}}, {"mode", "dissolve"}});
    ASSERT_FALSE(dissolved.is_error) << dissolved.text;
    EXPECT_EQ(undo_depth(), depth + 1) << "dissolve is one undo step";
    EXPECT_FALSE(exists("chain.001"));
    EXPECT_EQ(parent_of("chain.002"), "chain");
    EXPECT_LT(rig_vec_distance(rig_vec("chain", "Rig.tail"), Rig_vec{0.0f, 2.0f, 0.0f}), eps) << "the parent's tail is extended";
    EXPECT_LT(rig_vec_distance(local_t("chain.002"), Rig_vec{0.0f, 2.0f, 0.0f}), eps);
    EXPECT_EQ(rig("chain.002", "Rig.connected").value("value", ""), "true");
    undo();
    ASSERT_TRUE(exists("chain.001"));
    EXPECT_EQ(parent_of("chain.002"), "chain.001");
    EXPECT_LT(rig_vec_distance(rig_vec("chain", "Rig.tail"), Rig_vec{0.0f, 1.0f, 0.0f}), eps);
    EXPECT_LT(rig_vec_distance(local_t("chain.002"), Rig_vec{0.0f, 1.0f, 0.0f}), eps);
    EXPECT_LT(rig_vec_distance(local_t("chain.001"), Rig_vec{0.0f, 1.0f, 0.0f}), eps);

    // R8 delete: chain.002 moves to chain keeping its world transform, and is
    // disconnected (its head is not on chain's tail).
    depth = undo_depth();
    Mcp_client::Tool_result deleted = call("delete_bones", json{{"scene_name", scene}, {"bones", {"chain.001"}}, {"mode", "delete"}});
    ASSERT_FALSE(deleted.is_error) << deleted.text;
    EXPECT_EQ(undo_depth(), depth + 1) << "delete is one undo step";
    EXPECT_FALSE(exists("chain.001"));
    EXPECT_EQ(parent_of("chain.002"), "chain");
    EXPECT_LT(rig_vec_distance(world_t("chain.002"), Rig_vec{0.0f, 2.0f, 0.0f}), eps);
    EXPECT_LT(rig_vec_distance(rig_vec("chain", "Rig.tail"), Rig_vec{0.0f, 1.0f, 0.0f}), eps) << "delete leaves the parent's tail";
    EXPECT_EQ(rig("chain.002", "Rig.connected").value("value", ""), "false");
    EXPECT_LT(rig_vec_distance(rig_vec("chain.002", "Rig.rest_translation"), Rig_vec{0.0f, 2.0f, 0.0f}), eps) << "rest(removed) * rest(child)";
    undo();
    ASSERT_TRUE(exists("chain.001"));
    EXPECT_EQ(parent_of("chain.002"), "chain.001");
    EXPECT_EQ(rig("chain.002", "Rig.connected").value("value", ""), "true");
    EXPECT_LT(rig_vec_distance(local_t("chain.002"), Rig_vec{0.0f, 1.0f, 0.0f}), eps);
    EXPECT_LT(rig_vec_distance(rig_vec("chain.002", "Rig.rest_translation"), Rig_vec{0.0f, 1.0f, 0.0f}), eps);

    // R5 under a bone: the head is the parent's tail; not connected.
    Mcp_client::Tool_result child = call("create_bone", json{{"scene_name", scene}, {"parent", "chain.002"}});
    ASSERT_FALSE(child.is_error) << child.text;
    ASSERT_TRUE(exists("Bone"));
    EXPECT_EQ(parent_of("Bone"), "chain.002");
    EXPECT_LT(rig_vec_distance(world_t("Bone"), Rig_vec{0.0f, 3.0f, 0.0f}), eps);
    EXPECT_EQ(rig("Bone", "Rig.connected").value("value", ""), "false");

    // The authored chain is unskinned: an IK drag of its leaf routes through
    // its bones (erhe::scene::is_bone, the persistent flag) and reaches a
    // reachable target.
    const Rig_vec head = world_t("Bone");
    const Rig_vec target{head[0] + 0.5f, head[1] - 0.5f, head[2] + 0.3f};
    Mcp_client::Tool_result dragged = call("ik_drag", json{{"scene_name", scene}, {"node_name", "Bone"}, {"target", {target[0], target[1], target[2]}}});
    ASSERT_FALSE(dragged.is_error) << dragged.text;
    EXPECT_EQ(dragged.payload.at("joints").size(), 4u) << "the chain is Bone and its three bone ancestors";
    EXPECT_LT(rig_vec_distance(world_t("Bone"), target), 1.0e-2f) << "the drag solves";

    client.call_tool("close_scene", json{{"scene_name", scene}});
    advance_frames(client, 4);
}

// R9: every structure verb (Symmetrize and the roll verbs included) is
// refused on a bone a skin lists, naming the skin, and queues nothing.
TEST_F(Mcp_test, bone_structure_verbs_are_refused_on_a_bound_bone)
{
    Mcp_client& client = Mcp_env::get().client();

    const std::string scene = import_into_new_scene(client);
    ASSERT_FALSE(scene.empty()) << "could not create a scene to import into";
    ASSERT_TRUE(wait_until_idle(client, 60000));

    auto undo_depth = [&client]() -> std::size_t {
        return client.call_tool("get_undo_redo_stack", json::object()).payload.at("undo").size();
    };
    const std::size_t depth = undo_depth();
    const std::vector<std::pair<std::string, json>> calls{
        {"create_bone",     json{{"scene_name", scene}, {"parent", "arm_joint_L_1"}}},
        {"extrude_bones",   json{{"scene_name", scene}, {"bones", {"arm_joint_L_3"}}}},
        {"subdivide_bones", json{{"scene_name", scene}, {"bones", {"arm_joint_L_1"}}, {"count", 2}}},
        {"delete_bones",    json{{"scene_name", scene}, {"bones", {"arm_joint_L_2"}}, {"mode", "delete"}}},
        {"delete_bones",    json{{"scene_name", scene}, {"bones", {"arm_joint_L_2"}}, {"mode", "dissolve"}}},
        {"symmetrize_bones",      json{{"scene_name", scene}, {"bones", {"arm_joint_L_2"}}}},
        {"recalculate_bone_roll", json{{"scene_name", scene}, {"bones", {"arm_joint_L_2"}}, {"axis", "x"}, {"reference", "z"}}},
        {"align_bones",           json{{"scene_name", scene}, {"bones", {"arm_joint_L_2"}}, {"active", "arm_joint_L_1"}}}
    };
    for (const std::pair<std::string, json>& entry : calls) {
        Mcp_client::Tool_result refused = client.call_tool(entry.first, entry.second);
        EXPECT_TRUE(refused.is_error) << entry.first << " is refused on a bound bone";
        EXPECT_NE(refused.text.find("is a joint of skin '"), std::string::npos) << entry.first << ": " << refused.text;
        advance_frames(client, 2);
    }
    EXPECT_EQ(undo_depth(), depth) << "a refused verb queues nothing";
    Mcp_client::Tool_result node = client.call_tool("get_node_details", json{{"scene_name", scene}, {"node_name", "arm_joint_L_2"}});
    EXPECT_FALSE(node.is_error) << "the bound bone is still there";

    client.call_tool("close_scene", json{{"scene_name", scene}});
    advance_frames(client, 4);
}

// doc/plans/rigging/skeleton_editing.md R17: Rig.display_shape swaps the bone
// proxy's shape (octahedron: 6 vertices, stick / box: the 8-vertex prism, the
// stick a quarter as wide) and Rig.display_color_mode / display_color its
// material; the selected material still wins; each edit is one undo step.
// Display is not bind-affecting, so a bone a skin lists accepts the values.
TEST_F(Mcp_test, bone_display_properties_reshape_and_recolor_the_proxy)
{
    Mcp_client& client = Mcp_env::get().client();

    const std::vector<std::string> before = scene_names(client);
    client.call_tool("create_scene", json::object());
    advance_frames(client, 6);
    std::string scene;
    for (const std::string& name : scene_names(client)) {
        if (std::find(before.begin(), before.end(), name) == before.end()) {
            scene = name;
        }
    }
    ASSERT_FALSE(scene.empty()) << "could not create a scene";

    Mcp_client::Tool_result created = client.call_tool("create_bone", json{{"scene_name", scene}, {"name", "shown"}});
    ASSERT_FALSE(created.is_error) << created.text;
    advance_frames(client, 4);
    client.call_tool("select_items", json{{"scene_name", scene}, {"ids", json::array()}});
    advance_frames(client, 2);

    auto undo_depth = [&client]() -> std::size_t {
        return client.call_tool("get_undo_redo_stack", json::object()).payload.at("undo").size();
    };
    const int bone_id = client.call_tool("get_node_details", json{{"scene_name", scene}, {"node_name", "shown"}}).payload.value("id", 0);
    ASSERT_NE(bone_id, 0);
    // The proxy: the Mesh under the bone's "bone proxy <name>" node.
    auto proxy = [&client, &scene]() -> json {
        const json nodes = client.call_tool("get_scene_nodes", json{{"scene_name", scene}}).payload.value("nodes", json::array());
        int proxy_node_id = 0;
        int proxy_mesh_id = 0;
        for (const json& node : nodes) {
            if ((node.value("name", "") == "bone proxy shown") && (node.value("parent", "") == "shown")) {
                proxy_node_id = node.value("id", 0);
            }
            if ((node.value("type", "") == "Mesh") && (node.value("parent", "") == "bone proxy shown")) {
                proxy_mesh_id = node.value("id", 0);
            }
        }
        const json node_details = client.call_tool("get_node_details", json{{"scene_name", scene}, {"node_id", proxy_node_id}}).payload;
        const json mesh_details = client.call_tool("get_node_details", json{{"scene_name", scene}, {"node_id", proxy_mesh_id}}).payload;
        return json{
            {"half_width",   node_details.at("local_transform").at("scale")[0].get<float>()},
            {"vertex_count", mesh_details.at("mesh").value("vertex_count", 0)},
            {"material",     mesh_details.at("mesh").at("materials")[0].get<std::string>()}
        };
    };
    auto set = [&client, bone_id](const char* property, const json& value) -> Mcp_client::Tool_result {
        Mcp_client::Tool_result result = client.call_tool("set_item_property", json{{"item_id", bone_id}, {"property", property}, {"value", value}});
        advance_frames(client, 4);
        return result;
    };

    const json initial = proxy();
    EXPECT_EQ(initial.value("vertex_count", 0), 6) << "octahedral by default";
    EXPECT_EQ(initial.value("material", ""), "bone") << "the style colors by default";
    const float octahedral_width = initial.value("half_width", 0.0f);
    EXPECT_GT(octahedral_width, 0.0f);

    std::size_t depth = undo_depth();
    Mcp_client::Tool_result boxed = set("Rig.display_shape", "box");
    ASSERT_FALSE(boxed.is_error) << boxed.text;
    EXPECT_EQ(undo_depth(), depth + 1) << "a display edit is one undo step";
    EXPECT_EQ(proxy().value("vertex_count", 0), 8) << "box: the prism";
    EXPECT_NEAR(proxy().value("half_width", 0.0f), octahedral_width, 1.0e-5f) << "box: as wide as the octahedron's ring";
    ASSERT_FALSE(set("Rig.display_shape", "stick").is_error);
    EXPECT_EQ(proxy().value("vertex_count", 0), 8) << "stick: the prism";
    EXPECT_NEAR(proxy().value("half_width", 0.0f), 0.25f * octahedral_width, 1.0e-5f) << "stick: a quarter as wide";

    depth = undo_depth();
    ASSERT_FALSE(set("Rig.display_color_mode", "custom").is_error);
    ASSERT_FALSE(set("Rig.display_color", "1 0 0").is_error);
    EXPECT_EQ(undo_depth(), depth + 2);
    EXPECT_EQ(proxy().value("material", ""), "bone color #ff0000") << "custom: a material of the display color";

    client.call_tool("select_items", json{{"scene_name", scene}, {"paths", {"shown"}}});
    advance_frames(client, 3);
    EXPECT_EQ(proxy().value("material", ""), "bone selected") << "the selected color wins over the display color";
    client.call_tool("select_items", json{{"scene_name", scene}, {"ids", json::array()}});
    advance_frames(client, 3);
    EXPECT_EQ(proxy().value("material", ""), "bone color #ff0000") << "deselected: the display color again";

    client.call_tool("undo", json::object());
    advance_frames(client, 4);
    client.call_tool("undo", json::object());
    advance_frames(client, 4);
    EXPECT_EQ(proxy().value("material", ""), "bone") << "undo restores the style colors";
    client.call_tool("undo", json::object());
    advance_frames(client, 4);
    EXPECT_EQ(proxy().value("vertex_count", 0), 8) << "undo of stick: box again";
    EXPECT_NEAR(proxy().value("half_width", 0.0f), octahedral_width, 1.0e-5f);
    client.call_tool("undo", json::object());
    advance_frames(client, 4);
    EXPECT_EQ(proxy().value("vertex_count", 0), 6) << "undo of box: octahedral again";

    client.call_tool("close_scene", json{{"scene_name", scene}});
    advance_frames(client, 4);

    // A bone a skin lists accepts the display values (R9 covers only the
    // bind-affecting Rig.tail and Rig.rest_*).
    const std::string figure = import_into_new_scene(client);
    ASSERT_FALSE(figure.empty()) << "could not create a scene to import into";
    ASSERT_TRUE(wait_until_idle(client, 60000));
    const int joint_id = client.call_tool("get_node_details", json{{"scene_name", figure}, {"node_name", "arm_joint_L_2"}}).payload.value("id", 0);
    for (const std::pair<const char*, json>& write : std::vector<std::pair<const char*, json>>{
        {"Rig.display_shape", "stick"}, {"Rig.display_color_mode", "custom"}, {"Rig.display_color", "0 1 0"}
    }) {
        Mcp_client::Tool_result accepted = client.call_tool("set_item_property", json{{"item_id", joint_id}, {"property", write.first}, {"value", write.second}});
        EXPECT_FALSE(accepted.is_error) << write.first << " on a bound bone: " << accepted.text;
        advance_frames(client, 2);
    }
    EXPECT_EQ(item_property(client, joint_id, "Rig.display_shape").value("value", ""), "stick");
    client.call_tool("close_scene", json{{"scene_name", figure}});
    advance_frames(client, 4);
}

// R18 Bind (rigid): a box along an authored three-bone chain binds with the
// inverse binds of the bones' REST world transforms (the chain is posed off
// rest while binding), each vertex rigidly to its nearest head-tail segment,
// as one undo step; posing a bone moves the vertices it owns; afterwards the
// bones are bound (R9 refusals, a second bind refused) and undo takes the
// skin away again.
TEST_F(Mcp_test, bind_mesh_to_bones_skins_rigidly_from_the_rest_pose_in_one_undo_step)
{
    Mcp_client& client = Mcp_env::get().client();

    const std::vector<std::string> before = scene_names(client);
    client.call_tool("create_scene", json::object());
    advance_frames(client, 6);
    std::string scene;
    for (const std::string& name : scene_names(client)) {
        if (std::find(before.begin(), before.end(), name) == before.end()) {
            scene = name;
        }
    }
    ASSERT_FALSE(scene.empty()) << "could not create a scene";

    auto call = [&client](const char* tool, const json& args) -> Mcp_client::Tool_result {
        Mcp_client::Tool_result result = client.call_tool(tool, args);
        advance_frames(client, 4);
        return result;
    };
    auto undo_depth = [&client]() -> std::size_t {
        return client.call_tool("get_undo_redo_stack", json::object()).payload.at("undo").size();
    };
    auto details = [&client, &scene](const char* name) -> json {
        Mcp_client::Tool_result result = client.call_tool("get_node_details", json{{"scene_name", scene}, {"node_name", name}});
        return result.is_error ? json::object() : result.payload;
    };
    auto set_rotation_z = [&call, &scene](const char* bone, const float angle) {
        const float half = 0.5f * angle;
        call("set_node_transform", json{{"scene_name", scene}, {"node_name", bone}, {"space", "local"}, {"rotation_xyzw", {0.0f, 0.0f, std::sin(half), std::cos(half)}}});
    };

    // Three bones along +Y: segments [0, 1], [1, 2], [2, 3].
    ASSERT_FALSE(call("create_bone", json{{"scene_name", scene}, {"name", "spine"}}).is_error);
    ASSERT_FALSE(call("extrude_bones", json{{"scene_name", scene}, {"bones", {"spine"}}}).is_error);
    ASSERT_FALSE(call("extrude_bones", json{{"scene_name", scene}, {"bones", {"spine.001"}}}).is_error);
    Mcp_client::Tool_result shape = call(
        "create_shape",
        json{{"scene_name", scene}, {"shape", "box"}, {"name", "body"}, {"size", {0.5, 3.0, 0.5}}, {"steps", {1, 6, 1}}, {"position", {0.0, 1.5, 0.0}}, {"motion_mode", "none"}}
    );
    ASSERT_FALSE(shape.is_error) << shape.text;
    // Posed off rest while binding: the inverse binds come from the rest.
    set_rotation_z("spine.001", 0.7f);

    std::size_t depth = undo_depth();
    Mcp_client::Tool_result bound = call("bind_mesh_to_bones", json{{"scene_name", scene}, {"mesh", "body"}, {"bones", {"spine", "spine.001", "spine.002"}}});
    ASSERT_FALSE(bound.is_error) << bound.text;
    const std::size_t bound_depth = undo_depth();
    EXPECT_EQ(bound_depth, depth + 1) << "the bind is one undo step";
    const json body = details("body");
    ASSERT_TRUE(body.contains("mesh")) << "the skinned mesh keeps the name 'body'";
    EXPECT_TRUE(body.at("mesh").value("skinned", false));
    EXPECT_EQ(body.at("mesh").value("skin_name", ""), "body skin");
    const json joints = body.at("mesh").at("joints");
    ASSERT_EQ(joints.size(), 3u);
    for (std::size_t i = 0; i < 3; ++i) {
        // inverse(rest world) of a bone at (0, i, 0) with identity rotation:
        // a translation by -i along Y (column-major: element 13).
        const json& matrix = joints[i].at("inverse_bind_matrix");
        ASSERT_EQ(matrix.size(), 16u);
        for (std::size_t k = 0; k < 16; ++k) {
            const float expected = ((k % 5) == 0) ? 1.0f : (k == 13) ? -static_cast<float>(i) : 0.0f;
            EXPECT_NEAR(matrix[k].get<float>(), expected, 1.0e-5f) << "joint " << i << " element " << k;
        }
    }

    // Every vertex bound with weight 1 to its nearest segment (ties to the
    // lower joint): y <= 1 -> spine, y <= 2 -> spine.001, else spine.002.
    const int vertex_count = body.at("mesh").value("vertex_count", 0);
    ASSERT_GT(vertex_count, 0);
    std::vector<int> indices;
    for (int i = 0; i < vertex_count; ++i) {
        indices.push_back(i);
    }
    Mcp_client::Tool_result values = client.call_tool(
        "get_mesh_attribute_values",
        json{{"scene_name", scene}, {"node_name", "body"}, {"domain", "vertex"}, {"indices", indices}, {"attributes", {"vertex_joint_indices_0", "vertex_joint_weights_0"}}}
    );
    ASSERT_FALSE(values.is_error) << values.text;
    std::array<int, 3> per_joint{0, 0, 0};
    for (const json& element : values.payload.at("elements")) {
        const float y        = element.at("position")[1].get<float>();
        const int   expected = (y <= 1.0f + 1.0e-4f) ? 0 : (y <= 2.0f + 1.0e-4f) ? 1 : 2;
        const json& attributes = element.at("attributes");
        EXPECT_EQ(attributes.at("vertex_joint_indices_0").at("value")[0].get<int>(), expected) << "vertex at y = " << y;
        EXPECT_FLOAT_EQ(attributes.at("vertex_joint_weights_0").at("value")[0].get<float>(), 1.0f);
        ++per_joint[static_cast<std::size_t>(attributes.at("vertex_joint_indices_0").at("value")[0].get<int>())];
    }
    EXPECT_GT(per_joint[0], 0);
    EXPECT_GT(per_joint[1], 0);
    EXPECT_GT(per_joint[2], 0);

    // Back at rest the skinned bounds are the box; turning spine.002 by 90
    // degrees about +Z swings its part (y in [2, 3]) toward -X.
    set_rotation_z("spine.001", 0.0f);
    auto bounds = [&details]() -> json { return details("body").at("mesh").at("world_aabb"); };
    json rest_bounds = bounds();
    EXPECT_NEAR(rest_bounds.at("min")[0].get<float>(), -0.25f, 1.0e-3f);
    EXPECT_NEAR(rest_bounds.at("max")[1].get<float>(),  3.0f,  1.0e-3f);
    set_rotation_z("spine.002", 1.5707963f);
    json posed_bounds = bounds();
    EXPECT_LT(posed_bounds.at("min")[0].get<float>(), -0.9f) << "the top bone's vertices swing to -X";
    EXPECT_LT(posed_bounds.at("max")[1].get<float>(), 2.3f) << "nothing is left above the top bone's head";
    EXPECT_NEAR(posed_bounds.at("min")[1].get<float>(), 0.0f, 1.0e-3f) << "the lower bones' vertices stay";
    set_rotation_z("spine.002", 0.0f);

    // Bound now: R9 refuses structure and rest edits, a second bind is refused.
    Mcp_client::Tool_result extrude = call("extrude_bones", json{{"scene_name", scene}, {"bones", {"spine.002"}}});
    EXPECT_TRUE(extrude.is_error);
    EXPECT_NE(extrude.text.find("is a joint of skin 'body skin'"), std::string::npos) << extrude.text;
    Mcp_client::Tool_result rebind = call("bind_mesh_to_bones", json{{"scene_name", scene}, {"mesh", "body"}, {"bones", {"spine"}}});
    EXPECT_TRUE(rebind.is_error);
    EXPECT_NE(rebind.text.find("already has skin"), std::string::npos) << rebind.text;
    ASSERT_FALSE(call("create_shape", json{{"scene_name", scene}, {"shape", "box"}, {"name", "other"}, {"motion_mode", "none"}}).is_error);
    Mcp_client::Tool_result other = call("bind_mesh_to_bones", json{{"scene_name", scene}, {"mesh", "other"}, {"bones", {"spine.001"}}});
    EXPECT_TRUE(other.is_error);
    EXPECT_NE(other.text.find("already a joint of skin 'body skin'"), std::string::npos) << other.text;

    // Undo what came after the bind (the 'other' box, the pose edits), then
    // the bind itself: the plain box is back and the bones are free again.
    while (undo_depth() >= bound_depth) {
        client.call_tool("undo", json::object());
        advance_frames(client, 4);
    }
    EXPECT_EQ(undo_depth(), bound_depth - 1);
    const json unbound = details("body");
    ASSERT_TRUE(unbound.contains("mesh")) << "undo puts the original mesh back";
    EXPECT_FALSE(unbound.at("mesh").value("skinned", true)) << "undo removes the skin";
    EXPECT_FALSE(call("extrude_bones", json{{"scene_name", scene}, {"bones", {"spine.002"}}}).is_error) << "the bones are free again";

    client.call_tool("close_scene", json{{"scene_name", scene}});
    advance_frames(client, 4);
}

namespace {

// Double-precision helpers for the orientation checks of R13 / R16: the
// node details carry float values, and angles near zero need more than
// float's acos resolution.
using Dvec  = std::array<double, 3>;
using Dquat = std::array<double, 4>; // x, y, z, w

[[nodiscard]] auto dvec_of(const json& v) -> Dvec
{
    return Dvec{v[0].get<double>(), v[1].get<double>(), v[2].get<double>()};
}

[[nodiscard]] auto dquat_of(const json& q) -> Dquat
{
    return Dquat{q[0].get<double>(), q[1].get<double>(), q[2].get<double>(), q[3].get<double>()};
}

[[nodiscard]] auto dcross(const Dvec& a, const Dvec& b) -> Dvec
{
    return Dvec{(a[1] * b[2]) - (a[2] * b[1]), (a[2] * b[0]) - (a[0] * b[2]), (a[0] * b[1]) - (a[1] * b[0])};
}

[[nodiscard]] auto ddot(const Dvec& a, const Dvec& b) -> double
{
    return (a[0] * b[0]) + (a[1] * b[1]) + (a[2] * b[2]);
}

[[nodiscard]] auto dlength(const Dvec& a) -> double
{
    return std::sqrt(ddot(a, a));
}

[[nodiscard]] auto ddistance(const Dvec& a, const Dvec& b) -> double
{
    return dlength(Dvec{a[0] - b[0], a[1] - b[1], a[2] - b[2]});
}

[[nodiscard]] auto dnormalize(const Dvec& a) -> Dvec
{
    const double length = dlength(a);
    return Dvec{a[0] / length, a[1] / length, a[2] / length};
}

// v rotated by the unit quaternion q.
[[nodiscard]] auto drotate(const Dquat& q, const Dvec& v) -> Dvec
{
    const Dvec   u{q[0], q[1], q[2]};
    const double w = q[3];
    const Dvec   t = dcross(u, v);
    const Dvec   t2{2.0 * t[0], 2.0 * t[1], 2.0 * t[2]};
    const Dvec   c = dcross(u, t2);
    return Dvec{v[0] + (w * t2[0]) + c[0], v[1] + (w * t2[1]) + c[1], v[2] + (w * t2[2]) + c[2]};
}

// The angle between two directions in degrees, well conditioned near zero.
[[nodiscard]] auto dangle_deg(const Dvec& a, const Dvec& b) -> double
{
    return std::atan2(dlength(dcross(a, b)), ddot(a, b)) * (180.0 / 3.14159265358979323846);
}

// The angle between two rotations in degrees: the angle of conj(a) * b from
// its vector part and scalar part, well conditioned near zero (an angle from
// |dot(a, b)| alone is not, for quaternions read back as floats).
[[nodiscard]] auto dquat_angle_deg(const Dquat& a, const Dquat& b) -> double
{
    const Dvec   va{a[0], a[1], a[2]};
    const Dvec   vb{b[0], b[1], b[2]};
    const Dvec   c = dcross(va, vb);
    const double w = (a[3] * b[3]) + ddot(va, vb);
    const Dvec   v{(a[3] * vb[0]) - (b[3] * va[0]) - c[0], (a[3] * vb[1]) - (b[3] * va[1]) - c[1], (a[3] * vb[2]) - (b[3] * va[2]) - c[2]};
    return 2.0 * std::atan2(dlength(v), std::abs(w)) * (180.0 / 3.14159265358979323846);
}

// The mirror across the world X = 0 plane (the skeleton frame of a skeleton
// whose root is a child of an identity scene root node).
[[nodiscard]] auto dmirror(const Dvec& a) -> Dvec
{
    return Dvec{-a[0], a[1], a[2]};
}

// S * R(q) * S: (x, y, z, w) -> (x, -y, -z, w).
[[nodiscard]] auto dmirror_rotation(const Dquat& q) -> Dquat
{
    return Dquat{q[0], -q[1], -q[2], q[3]};
}

[[nodiscard]] auto parse_dvec(const std::string& text) -> Dvec
{
    Dvec v{};
    std::istringstream stream{text};
    stream >> v[0] >> v[1] >> v[2];
    return v;
}

[[nodiscard]] auto parse_dquat(const std::string& text) -> Dquat
{
    Dquat q{};
    std::istringstream stream{text};
    stream >> q[0] >> q[1] >> q[2] >> q[3];
    return q;
}

[[nodiscard]] auto create_new_scene(Mcp_client& client) -> std::string
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
    return scene;
}

// The node-detail accessors the R13 / R16 tests share, over one scene.
class Rig_probe
{
public:
    Rig_probe(Mcp_client& client, std::string scene) : m_client{client}, m_scene{std::move(scene)} {}

    [[nodiscard]] auto details(const std::string& name) const -> json
    {
        Mcp_client::Tool_result result = m_client.call_tool("get_node_details", json{{"scene_name", m_scene}, {"node_name", name}});
        return result.is_error ? json::object() : result.payload;
    }
    [[nodiscard]] auto exists     (const std::string& name) const -> bool        { return details(name).contains("id"); }
    [[nodiscard]] auto parent     (const std::string& name) const -> std::string { return details(name).value("parent", ""); }
    [[nodiscard]] auto world_t    (const std::string& name) const -> Dvec        { return dvec_of (details(name).at("world_transform").at("translation")); }
    [[nodiscard]] auto world_q    (const std::string& name) const -> Dquat       { return dquat_of(details(name).at("world_transform").at("rotation_xyzw")); }
    [[nodiscard]] auto local_t    (const std::string& name) const -> Dvec        { return dvec_of (details(name).at("local_transform").at("translation")); }
    [[nodiscard]] auto local_q    (const std::string& name) const -> Dquat       { return dquat_of(details(name).at("local_transform").at("rotation_xyzw")); }
    [[nodiscard]] auto property   (const std::string& name, const char* property_name) const -> json
    {
        return item_property(m_client, details(name).value("id", 0), property_name);
    }
    [[nodiscard]] auto property_vec(const std::string& name, const char* property_name) const -> Dvec
    {
        return parse_dvec(property(name, property_name).value("value", ""));
    }
    // The world position of the bone's tail: head + world rotation * Rig.tail
    // (unit scale).
    [[nodiscard]] auto world_tail(const std::string& name) const -> Dvec
    {
        const Dvec head = world_t(name);
        const Dvec tail = drotate(world_q(name), property_vec(name, "Rig.tail"));
        return Dvec{head[0] + tail[0], head[1] + tail[1], head[2] + tail[2]};
    }
    void set_property(const std::string& name, const char* property_name, const json& value) const
    {
        Mcp_client::Tool_result result = m_client.call_tool("set_item_property", json{{"item_id", details(name).value("id", 0)}, {"property", property_name}, {"value", value}});
        EXPECT_FALSE(result.is_error) << property_name << ": " << result.text;
        advance_frames(m_client, 2);
    }
    void set_local(const std::string& name, const Dvec& translation, const Dquat& rotation) const
    {
        Mcp_client::Tool_result result = m_client.call_tool(
            "set_node_transform",
            json{
                {"scene_name", m_scene}, {"node_name", name}, {"space", "local"},
                {"translation", {translation[0], translation[1], translation[2]}},
                {"rotation_xyzw", {rotation[0], rotation[1], rotation[2], rotation[3]}}
            }
        );
        EXPECT_FALSE(result.is_error) << result.text;
        advance_frames(m_client, 2);
    }
    [[nodiscard]] auto call(const char* tool, json args) const -> Mcp_client::Tool_result
    {
        args["scene_name"] = m_scene;
        Mcp_client::Tool_result result = m_client.call_tool(tool, args);
        advance_frames(m_client, 3);
        return result;
    }
    [[nodiscard]] auto undo_depth() const -> std::size_t
    {
        return m_client.call_tool("get_undo_redo_stack", json::object()).payload.at("undo").size();
    }
    void undo() const { m_client.call_tool("undo", json::object()); advance_frames(m_client, 4); }
    void redo() const { m_client.call_tool("redo", json::object()); advance_frames(m_client, 4); }

private:
    Mcp_client& m_client;
    std::string m_scene;
};

[[nodiscard]] auto normalized_quat(const Dquat& q) -> Dquat
{
    const double length = std::sqrt((q[0] * q[0]) + (q[1] * q[1]) + (q[2] * q[2]) + (q[3] * q[3]));
    return Dquat{q[0] / length, q[1] / length, q[2] / length, q[3] / length};
}

} // anonymous namespace

// R13 Symmetrize on an authored, unskinned arm: selecting the whole one-sided
// arm creates its mirror across the skeleton frame's X = 0 plane (the scene
// root's frame here, the root 'spine' being its child) in one undo step:
// flipped names, parents mapped, world heads and tails mirrored, the tail,
// the rest and the connected flag carried over mirrored, and the IK limits
// mapped (X range kept, Y and Z ranges negated and swapped).
TEST_F(Mcp_test, symmetrize_mirrors_an_authored_arm_in_one_undo_step)
{
    Mcp_client& client = Mcp_env::get().client();
    const std::string scene = create_new_scene(client);
    ASSERT_FALSE(scene.empty()) << "could not create a scene";
    const Rig_probe p{client, scene};
    constexpr double eps = 1.0e-5;

    ASSERT_FALSE(p.call("create_bone", json{{"name", "spine"}}).is_error);
    ASSERT_FALSE(p.call("create_bone", json{{"parent", "spine"}, {"name", "arm_L"}}).is_error);
    // Off the plane and turned about every axis, with its rest on the pose.
    const Dvec  arm_translation{0.3, 1.1, 0.2};
    const Dquat arm_rotation = normalized_quat(Dquat{0.1, 0.2, -0.55, 0.8});
    p.set_local("arm_L", arm_translation, arm_rotation);
    p.set_property("arm_L", "Rig.rest_translation", "0.3 1.1 0.2");
    {
        std::ostringstream text;
        text.precision(17);
        text << arm_rotation[0] << " " << arm_rotation[1] << " " << arm_rotation[2] << " " << arm_rotation[3];
        p.set_property("arm_L", "Rig.rest_rotation", text.str());
    }
    ASSERT_FALSE(p.call("extrude_bones", json{{"bones", {"arm_L"}}}).is_error);
    ASSERT_TRUE(p.exists("arm_L.001"));
    p.set_property("arm_L.001", "Ik.lock_x",     true);
    p.set_property("arm_L.001", "Ik.limit_y",    true);
    p.set_property("arm_L.001", "Ik.limit_z",    true);
    p.set_property("arm_L.001", "Ik.limit_min",  "-0.1 -0.2 -0.3");
    p.set_property("arm_L.001", "Ik.limit_max",  "0.4 0.5 0.6");
    p.set_property("arm_L.001", "Ik.pole_angle", "0.25");

    const Dvec head_l      = p.world_t   ("arm_L");
    const Dvec tail_l      = p.world_tail("arm_L");
    const Dvec head_l1     = p.world_t   ("arm_L.001");
    const Dvec tail_l1     = p.world_tail("arm_L.001");
    const Dvec limit_min_l = p.property_vec("arm_L.001", "Ik.limit_min");
    const Dvec limit_max_l = p.property_vec("arm_L.001", "Ik.limit_max");
    const Dvec rig_tail_l1 = p.property_vec("arm_L.001", "Rig.tail");

    // The child named first: the verb orders parents first itself.
    const std::size_t depth = p.undo_depth();
    Mcp_client::Tool_result mirrored = p.call("symmetrize_bones", json{{"bones", {"arm_L.001", "arm_L"}}});
    ASSERT_FALSE(mirrored.is_error) << mirrored.text;
    EXPECT_EQ(p.undo_depth(), depth + 1) << "symmetrize_bones is one undo step";
    EXPECT_EQ(mirrored.payload.at("created").size(), 2u);
    ASSERT_TRUE(p.exists("arm_R"));
    ASSERT_TRUE(p.exists("arm_R.001"));
    EXPECT_EQ(p.parent("arm_R"),     "spine")  << "the unsided parent is kept";
    EXPECT_EQ(p.parent("arm_R.001"), "arm_R")  << "the parent's mirror is the new parent";
    EXPECT_LT(ddistance(p.world_t   ("arm_R"),     dmirror(head_l)),  eps);
    EXPECT_LT(ddistance(p.world_tail("arm_R"),     dmirror(tail_l)),  eps);
    EXPECT_LT(ddistance(p.world_t   ("arm_R.001"), dmirror(head_l1)), eps);
    EXPECT_LT(ddistance(p.world_tail("arm_R.001"), dmirror(tail_l1)), eps);
    EXPECT_LT(dquat_angle_deg(p.world_q("arm_R"), dmirror_rotation(p.world_q("arm_L"))), 1.0e-3) << "world rotation S * R * S";
    EXPECT_LT(ddistance(p.property_vec("arm_R.001", "Rig.tail"), dmirror(rig_tail_l1)), eps) << "Rig.tail (x, y, z) -> (-x, y, z)";
    EXPECT_EQ(p.property("arm_R.001", "Rig.connected").value("value", ""), "true");
    EXPECT_LT(ddistance(p.property_vec("arm_R", "Rig.rest_translation"), Dvec{-0.3, 1.1, 0.2}), eps) << "the rest is mirrored";
    EXPECT_EQ(p.property("arm_R.001", "Ik.lock_x").value("value", ""),  "true");
    EXPECT_EQ(p.property("arm_R.001", "Ik.limit_y").value("value", ""), "true");
    const Dvec limit_min_r = p.property_vec("arm_R.001", "Ik.limit_min");
    const Dvec limit_max_r = p.property_vec("arm_R.001", "Ik.limit_max");
    EXPECT_LT(ddistance(limit_min_r, Dvec{limit_min_l[0], -limit_max_l[1], -limit_max_l[2]}), 1.0e-3) << "Y and Z ranges negated and swapped";
    EXPECT_LT(ddistance(limit_max_r, Dvec{limit_max_l[0], -limit_min_l[1], -limit_min_l[2]}), 1.0e-3) << "the X range is kept";
    const double pole_l = std::stod(p.property("arm_L.001", "Ik.pole_angle").value("value", "0"));
    const double pole_r = std::stod(p.property("arm_R.001", "Ik.pole_angle").value("value", "0"));
    EXPECT_NEAR(pole_r, -pole_l, 1.0e-4) << "the pole angle is negated";
    EXPECT_TRUE(p.details("arm_R").value("selected", false)) << "the new bones become the selection";

    p.undo();
    EXPECT_FALSE(p.exists("arm_R"))     << "undo removes the mirror bones";
    EXPECT_FALSE(p.exists("arm_R.001"));
    EXPECT_LT(ddistance(p.world_t("arm_L.001"), head_l1), eps) << "the source side is untouched";
    p.redo();
    ASSERT_TRUE(p.exists("arm_R.001"));
    EXPECT_LT(ddistance(p.world_t("arm_R.001"), dmirror(head_l1)), eps);

    // Both sides now exist: nothing to mirror, nothing queued.
    const std::size_t again_depth = p.undo_depth();
    Mcp_client::Tool_result again = p.call("symmetrize_bones", json{{"bones", {"arm_L"}}});
    ASSERT_FALSE(again.is_error) << again.text;
    EXPECT_TRUE(again.payload.at("created").empty());
    EXPECT_EQ(p.undo_depth(), again_depth);

    client.call_tool("close_scene", json{{"scene_name", scene}});
    advance_frames(client, 4);
}

// R16 on an authored chain: Recalculate Roll aims a bone's local X at world
// +Z (within 1e-4 degrees in the plane perpendicular to the bone) without
// moving its head-to-tail axis or its children; Align to Active gives a bone
// the active bone's direction and roll, head kept. Each one undo step whose
// undo restores the local transforms exactly.
TEST_F(Mcp_test, recalculate_roll_and_align_to_active_keep_axes_and_children)
{
    Mcp_client& client = Mcp_env::get().client();
    const std::string scene = create_new_scene(client);
    ASSERT_FALSE(scene.empty()) << "could not create a scene";
    const Rig_probe p{client, scene};
    constexpr double eps = 1.0e-5;

    ASSERT_FALSE(p.call("create_bone", json{{"name", "chain"}}).is_error);
    p.set_local("chain", Dvec{0.0, 0.0, 0.0}, normalized_quat(Dquat{0.2, 0.3, 0.1, 0.93}));
    for (const char* from : {"chain", "chain.001"}) {
        ASSERT_FALSE(p.call("extrude_bones", json{{"bones", {from}}}).is_error);
    }
    // A child that is not connected, off the tail and turned.
    ASSERT_FALSE(p.call("create_bone", json{{"parent", "chain.001"}, {"name", "side"}}).is_error);
    p.set_local("side", Dvec{0.2, 0.5, 0.1}, normalized_quat(Dquat{-0.3, 0.1, 0.4, 0.86}));
    ASSERT_TRUE(p.exists("chain.002"));
    ASSERT_TRUE(p.exists("side"));

    const Dvec  axis_before     = dnormalize(drotate(p.world_q("chain.001"), p.property_vec("chain.001", "Rig.tail")));
    const Dvec  tail_before     = p.world_tail("chain.001");
    const Dquat local_before    = p.local_q("chain.001");
    const Dvec  child_t_before  = p.world_t("chain.002");
    const Dquat child_q_before  = p.world_q("chain.002");
    const Dvec  side_t_before   = p.world_t("side");
    const Dquat side_q_before   = p.world_q("side");

    // Recalculate Roll: chain.001's local X toward world +Z.
    std::size_t depth = p.undo_depth();
    Mcp_client::Tool_result rolled = p.call("recalculate_bone_roll", json{{"bones", {"chain.001"}}, {"axis", "x"}, {"reference", "z"}});
    ASSERT_FALSE(rolled.is_error) << rolled.text;
    EXPECT_EQ(p.undo_depth(), depth + 1) << "recalculate_bone_roll is one undo step";
    const Dquat q          = p.world_q("chain.001");
    const Dvec  axis_after = dnormalize(drotate(q, p.property_vec("chain.001", "Rig.tail")));
    const Dvec  x_after    = drotate(q, Dvec{1.0, 0.0, 0.0});
    const Dvec  z_in_plane = Dvec{0.0 - (axis_after[2] * axis_after[0]), 0.0 - (axis_after[2] * axis_after[1]), 1.0 - (axis_after[2] * axis_after[2])};
    EXPECT_LT(dangle_deg(x_after, z_in_plane), 1.0e-4) << "local X points to world +Z within the plane perpendicular to the bone";
    EXPECT_LT(dangle_deg(axis_after, axis_before), 1.0e-4) << "the head-to-tail axis is unchanged";
    EXPECT_LT(ddistance(p.world_tail("chain.001"), tail_before), eps) << "the tail point stays put";
    EXPECT_LT(ddistance(p.world_t("chain.002"), child_t_before), eps) << "the connected child keeps its world transform";
    EXPECT_LT(dquat_angle_deg(p.world_q("chain.002"), child_q_before), 1.0e-3);
    EXPECT_LT(ddistance(p.world_t("side"), side_t_before), eps) << "the other child keeps its world transform";
    EXPECT_LT(dquat_angle_deg(p.world_q("side"), side_q_before), 1.0e-3);
    EXPECT_LT(dquat_angle_deg(parse_dquat(p.property("chain.001", "Rig.rest_rotation").value("value", "")), p.local_q("chain.001")), 1.0e-3)
        << "the rest turns with the bone (it was at rest)";
    p.undo();
    EXPECT_LT(dquat_angle_deg(p.local_q("chain.001"), local_before), 1.0e-6) << "undo restores the local rotation";
    EXPECT_LT(ddistance(p.world_t("side"), side_t_before), eps);

    // Align to Active: 'side' takes chain.002's direction and roll (both bone
    // axes are local +Y), its head stays.
    depth = p.undo_depth();
    Mcp_client::Tool_result aligned = p.call("align_bones", json{{"bones", {"side"}}, {"active", "chain.002"}});
    ASSERT_FALSE(aligned.is_error) << aligned.text;
    EXPECT_EQ(p.undo_depth(), depth + 1) << "align_bones is one undo step";
    EXPECT_LT(dquat_angle_deg(p.world_q("side"), p.world_q("chain.002")), 1.0e-3) << "same world frame as the active bone";
    EXPECT_LT(ddistance(p.world_t("side"), side_t_before), eps) << "the head stays";
    EXPECT_EQ(p.property("side", "Rig.tail").value("source", ""), "local") << "the tail is recorded";
    p.undo();
    EXPECT_LT(dquat_angle_deg(p.world_q("side"), side_q_before), 1.0e-4) << "undo restores the rotation";

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

// ---- Input event injection (doc/agents/mcp_ui_driving.md) ------------------

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

// ---- Gestures (doc/agents/mcp_ui_driving.md) -------------------------------

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

// Part A: ImGui introspection ------------------------------------------------

namespace {

// The Hierarchy window of the scene the test prepared, by the title
// Scene_root::make_browser_window() gives it ("Scene Hierarchy [N]").
[[nodiscard]] auto find_hierarchy_window(Mcp_client& client, std::string& out_title) -> bool
{
    Mcp_client::Tool_result windows = client.call_tool("get_imgui_windows", json::object());
    if (windows.is_error || !windows.payload.contains("windows")) {
        return false;
    }
    for (const json& window : windows.payload["windows"]) {
        const std::string name = window.value("name", "");
        if ((name.rfind("Scene Hierarchy", 0) == 0) && window.value("active", false)) {
            out_title = name;
            return true;
        }
    }
    return false;
}

} // anonymous namespace

// get_imgui_hosts reports the desktop host, and a frame in which nothing asked
// for a recording calls no item hook at all: hook_calls_total stands
// still over a run of frames.
TEST_F(Mcp_test, get_imgui_hosts_reports_the_desktop_host_and_records_nothing_unasked)
{
    Mcp_client& client = Mcp_env::get().client();

    Mcp_client::Tool_result hosts = client.call_tool("get_imgui_hosts", json::object());
    ASSERT_FALSE(hosts.is_error) << hosts.text;
    ASSERT_TRUE(hosts.payload.contains("hosts"));
    const json& host_list = hosts.payload["hosts"];
    ASSERT_FALSE(host_list.empty()) << "no ImGui hosts reported";

    std::string default_host;
    uint64_t    hook_calls_before = 0;
    for (const json& host : host_list) {
        EXPECT_GT(host.value("width",  0.0f), 0.0f) << host.dump();
        EXPECT_GT(host.value("height", 0.0f), 0.0f) << host.dump();
        if (host.value("default", false)) {
            default_host      = host.value("name", "");
            hook_calls_before = host.value("hook_calls_total", static_cast<uint64_t>(0));
        }
    }
    ASSERT_FALSE(default_host.empty()) << "no default ImGui host: " << host_list.dump();

    advance_frames(client, 10);

    Mcp_client::Tool_result after = client.call_tool("get_imgui_hosts", json::object());
    ASSERT_FALSE(after.is_error) << after.text;
    uint64_t hook_calls_after = 0;
    bool     found            = false;
    for (const json& host : after.payload["hosts"]) {
        if (host.value("name", "") == default_host) {
            hook_calls_after = host.value("hook_calls_total", static_cast<uint64_t>(0));
            found            = true;
        }
    }
    ASSERT_TRUE(found);
    EXPECT_EQ(hook_calls_after, hook_calls_before)
        << "item hooks ran in frames no query asked to record";

    Mcp_client::Tool_result unknown = client.call_tool("get_imgui_windows", json{{"host", "no such host"}});
    EXPECT_TRUE(unknown.is_error) << "an unknown host name was accepted";
}

// get_imgui_items on the Hierarchy window lists the scene's node rows. The
// rows draw their own text, so they are named for the recorder explicitly
// (erhe::imgui::set_item_debug_label) - this is what checks that.
TEST_F(Mcp_test, get_imgui_items_lists_the_hierarchy_rows)
{
    Mcp_env&    env    = Mcp_env::get();
    Mcp_client& client = env.client();

    std::string hierarchy_title;
    ASSERT_TRUE(find_hierarchy_window(client, hierarchy_title)) << "no Scene Hierarchy window is open";

    const std::string box_name = "imgui items test box";
    Mcp_client::Tool_result shape = client.call_tool("create_shape", json{
        {"scene_name",  env.scene_name()},
        {"shape",       "box"},
        {"name",        box_name},
        {"motion_mode", "none"}
    });
    ASSERT_FALSE(shape.is_error) << shape.text;
    ASSERT_TRUE(wait_until_idle(client, 10000)) << "create_shape did not settle";

    Mcp_client::Tool_result items = client.call_tool("get_imgui_items", json{
        {"window", hierarchy_title},
        {"limit",  500}
    });
    ASSERT_FALSE(items.is_error) << items.text;
    ASSERT_TRUE(items.payload.contains("items"));
    EXPECT_GT(items.payload.value("total", 0), 1) << items.payload.dump();

    bool found_box = false;
    for (const json& item : items.payload["items"]) {
        EXPECT_EQ(item.value("window", ""), hierarchy_title);
        if (item.value("display_label", "") == box_name) {
            found_box = true;
            EXPECT_GT(item.value("width",  0.0f), 0.0f) << item.dump();
            EXPECT_GT(item.value("height", 0.0f), 0.0f) << item.dump();
        }
    }
    EXPECT_TRUE(found_box) << "the Hierarchy row of '" << box_name << "' was not listed";

    client.call_tool("delete_nodes", json{{"scene_name", env.scene_name()}, {"names", json::array({box_name})}});
    advance_frames(client, 3);
}

// End to end: get_imgui_item_rect resolves a Hierarchy row to a rectangle,
// and a mouse_click at its center selects that node - the rectangles are in
// the same window pixels the input gestures take.
TEST_F(Mcp_test, imgui_item_rect_center_is_a_click_target)
{
    Mcp_env&    env    = Mcp_env::get();
    Mcp_client& client = env.client();

    std::string hierarchy_title;
    ASSERT_TRUE(find_hierarchy_window(client, hierarchy_title)) << "no Scene Hierarchy window is open";

    const std::string box_name = "imgui click test box";
    Mcp_client::Tool_result shape = client.call_tool("create_shape", json{
        {"scene_name",  env.scene_name()},
        {"shape",       "box"},
        {"name",        box_name},
        {"motion_mode", "none"}
    });
    ASSERT_FALSE(shape.is_error) << shape.text;
    ASSERT_TRUE(wait_until_idle(client, 10000)) << "create_shape did not settle";

    client.call_tool("select_items", json{{"scene_name", env.scene_name()}, {"paths", json::array()}});
    advance_frames(client, 2);

    Mcp_client::Tool_result rect = client.call_tool("get_imgui_item_rect", json{
        {"window", hierarchy_title},
        {"label",  box_name}
    });
    ASSERT_FALSE(rect.is_error) << rect.text;
    EXPECT_EQ(rect.payload.value("match_count", 0), 1) << rect.payload.dump();
    const float center_x = rect.payload.value("center_x", 0.0f);
    const float center_y = rect.payload.value("center_y", 0.0f);
    ASSERT_GT(center_x, 0.0f) << rect.payload.dump();
    ASSERT_GT(center_y, 0.0f) << rect.payload.dump();

    Mcp_client::Tool_result click = client.call_tool("mouse_click", json{{"x", center_x}, {"y", center_y}});
    ASSERT_FALSE(click.is_error) << click.text;
    advance_frames(client, 3);

    Mcp_client::Tool_result selection = client.call_tool("get_selection", json::object());
    ASSERT_FALSE(selection.is_error) << selection.text;
    const json& selected = selection.payload["items"];
    ASSERT_TRUE(selected.is_array());
    bool found = false;
    for (const json& item : selected) {
        if (item.value("name", "") == box_name) {
            found = true;
        }
    }
    EXPECT_TRUE(found) << "clicking the reported row rectangle did not select it: " << selected.dump();

    Mcp_client::Tool_result missing = client.call_tool("get_imgui_item_rect", json{
        {"window", hierarchy_title},
        {"label",  "no such item"}
    });
    EXPECT_TRUE(missing.is_error) << "an unknown label was accepted";

    client.call_tool("delete_nodes", json{{"scene_name", env.scene_name()}, {"names", json::array({box_name})}});
    advance_frames(client, 3);
}

// Ctrl+A over an item tree selects that tree's whole subtree and the editor
// answers the next call. The chord used to be tested per visible row of every
// item tree, and each item entered the selection through its own selection
// change, so one Ctrl+A ran select-all once per row and each select-all
// re-sorted, diffed and published the whole selection once per item: the
// editor never came back from it.
TEST_F(Mcp_test, key_press_ctrl_a_selects_the_focused_tree)
{
    Mcp_env&    env    = Mcp_env::get();
    Mcp_client& client = env.client();

    std::string hierarchy_title;
    ASSERT_TRUE(find_hierarchy_window(client, hierarchy_title)) << "no Scene Hierarchy window is open";

    const std::string box_name = "ctrl a test box";
    Mcp_client::Tool_result shape = client.call_tool("create_shape", json{
        {"scene_name",  env.scene_name()},
        {"shape",       "box"},
        {"name",        box_name},
        {"motion_mode", "none"}
    });
    ASSERT_FALSE(shape.is_error) << shape.text;
    ASSERT_TRUE(wait_until_idle(client, 10000)) << "create_shape did not settle";

    client.call_tool("select_items", json{{"scene_name", env.scene_name()}, {"paths", json::array()}});
    advance_frames(client, 2);

    // Clicking the box's Hierarchy row focuses that tree's window, which is
    // what routes the chord to it.
    Mcp_client::Tool_result rect = client.call_tool("get_imgui_item_rect", json{
        {"window", hierarchy_title},
        {"label",  box_name}
    });
    ASSERT_FALSE(rect.is_error) << rect.text;
    Mcp_client::Tool_result click = client.call_tool("mouse_click", json{
        {"x", rect.payload.value("center_x", 0.0f)},
        {"y", rect.payload.value("center_y", 0.0f)}
    });
    ASSERT_FALSE(click.is_error) << click.text;
    advance_frames(client, 3);

    Mcp_client::Tool_result before = client.call_tool("get_selection", json::object());
    ASSERT_FALSE(before.is_error) << before.text;
    const std::size_t selected_before = before.payload["items"].size();
    ASSERT_GE(selected_before, 1u) << "clicking the row did not select it: " << before.payload["items"].dump();

    Mcp_client::Tool_result key = client.call_tool("key_press", json{
        {"key",       "a"},
        {"modifiers", json::array({"ctrl"})}
    });
    ASSERT_FALSE(key.is_error) << key.text;
    advance_frames(client, 3);

    Mcp_client::Tool_result after = client.call_tool("get_selection", json::object());
    ASSERT_FALSE(after.is_error) << after.text;
    const json& selected = after.payload["items"];
    ASSERT_TRUE(selected.is_array());
    EXPECT_GT(selected.size(), selected_before) << "Ctrl+A did not select the tree: " << selected.dump();
    bool found = false;
    for (const json& item : selected) {
        if (item.value("name", "") == box_name) {
            found = true;
        }
    }
    EXPECT_TRUE(found) << "the focused tree's own item is not in the selection: " << selected.dump();

    client.call_tool("select_items", json{{"scene_name", env.scene_name()}, {"paths", json::array()}});
    client.call_tool("delete_nodes", json{{"scene_name", env.scene_name()}, {"names", json::array({box_name})}});
    advance_frames(client, 3);
}

// Part A actions --------------------------------------------------------------

// The menu path a user takes, by label only. A menu item exists only while its
// menu is open, so it is two clicks, and the item of the popup is reached
// without naming the window: ImGui names a menu's popup itself ("Window###
// Menu_00"), so the resolver searches every window and reports the one the
// match was found in.
//
// The item used is a window toggle, which the test puts back; Window > Open
// Four View is the same path and is verified by hand (the four view's own
// viewport windows would outlive this test and the editor saves the windows
// it has when it exits).
TEST_F(Mcp_test, imgui_click_opens_a_menu_item_by_label)
{
    Mcp_client& client = Mcp_env::get().client();

    const auto animation_window_active = [&]() -> bool {
        Mcp_client::Tool_result windows = client.call_tool("get_imgui_windows", json::object());
        EXPECT_FALSE(windows.is_error) << windows.text;
        for (const json& window : windows.payload["windows"]) {
            if (window.value("name", "") == "Animation") {
                return window.value("active", false);
            }
        }
        return false;
    };

    client.call_tool("set_window_visibility", json{{"title", "Animation"}, {"visible", false}});
    advance_frames(client, 3);
    ASSERT_FALSE(animation_window_active()) << "the Animation window was open before the menu was used";

    Mcp_client::Tool_result menu = client.call_tool("imgui_click", json{{"label", "Window"}});
    ASSERT_FALSE(menu.is_error) << menu.text;
    EXPECT_EQ(menu.payload["target"].value("display_label", ""), "Window") << menu.payload.dump();

    Mcp_client::Tool_result item = client.call_tool("imgui_click", json{{"label", "Animation"}});
    ASSERT_FALSE(item.is_error) << item.text;
    // The popup ImGui opened for the menu, found without the caller naming it.
    EXPECT_NE(item.payload["target"].value("window", "").find("Window"), std::string::npos)
        << "the menu item was not found in the Window menu's popup: " << item.payload["target"].dump();
    advance_frames(client, 3);

    EXPECT_TRUE(animation_window_active()) << "clicking the menu item by label did not open its window";

    client.call_tool("set_window_visibility", json{{"title", "Animation"}, {"visible", false}});
    advance_frames(client, 2);
}

// imgui_click focuses a text field and type_text edits it: the Operations
// window's filter narrows the operation list to what was typed, and Escape
// (which ImGui reverts an edit with) restores it.
TEST_F(Mcp_test, imgui_click_focuses_a_text_field_that_type_text_edits)
{
    Mcp_client& client = Mcp_env::get().client();

    const auto operations_item_count = [&]() -> int {
        Mcp_client::Tool_result items = client.call_tool("get_imgui_items", json{
            {"window", "Operations"},
            {"limit",  1}
        });
        EXPECT_FALSE(items.is_error) << items.text;
        return items.is_error ? -1 : items.payload.value("total", -1);
    };

    const int unfiltered = operations_item_count();
    ASSERT_GT(unfiltered, 5) << "the Operations window is not showing its operations";

    Mcp_client::Tool_result focus = client.call_tool("imgui_click", json{
        {"window", "Operations"},
        {"label",  "Filter"}
    });
    ASSERT_FALSE(focus.is_error) << focus.text;
    EXPECT_TRUE(focus.payload["target"]["status"].value("inputable", false))
        << "the item clicked is not a text field: " << focus.payload["target"].dump();

    Mcp_client::Tool_result typed = client.call_tool("type_text", json{{"text", "Ambo"}});
    ASSERT_FALSE(typed.is_error) << typed.text;

    const int filtered = operations_item_count();
    EXPECT_GT(filtered, 0);
    EXPECT_LT(filtered, unfiltered) << "typing into the filter did not narrow the Operations window";

    Mcp_client::Tool_result match = client.call_tool("get_imgui_item_rect", json{
        {"window", "Operations"},
        {"label",  "Ambo"}
    });
    EXPECT_FALSE(match.is_error) << "the operation the filter was typed for is not listed: " << match.text;

    Mcp_client::Tool_result escape = client.call_tool("key_press", json{{"key", "escape"}});
    ASSERT_FALSE(escape.is_error) << escape.text;
    EXPECT_EQ(operations_item_count(), unfiltered) << "Escape did not revert the filter edit";
}

// The two press/release pairs a double click is built of register as one.
// The probe is a tree node carrying ImGuiTreeNodeFlags_OpenOnDoubleClick (the
// Operation Stack window's stacks): Dear ImGui itself opens it on a double
// click and on nothing else, so a single click of the same shape leaving it
// closed is the control.
TEST_F(Mcp_test, imgui_click_double_is_read_as_a_double_click)
{
    Mcp_client& client = Mcp_env::get().client();

    Mcp_client::Tool_result shown = client.call_tool("set_window_visibility", json{
        {"title",   "Operation Stack"},
        {"visible", true}
    });
    ASSERT_FALSE(shown.is_error) << shown.text;
    advance_frames(client, 3);

    const auto is_opened = [&]() -> bool {
        Mcp_client::Tool_result rect = client.call_tool("get_imgui_item_rect", json{
            {"window", "Operation Stack"},
            {"label",  "Executed"}
        });
        EXPECT_FALSE(rect.is_error) << rect.text;
        return !rect.is_error && rect.payload["status"].value("opened", false);
    };
    ASSERT_FALSE(is_opened()) << "the tree node was already open";

    Mcp_client::Tool_result single = client.call_tool("imgui_click", json{
        {"window", "Operation Stack"},
        {"label",  "Executed"}
    });
    ASSERT_FALSE(single.is_error) << single.text;
    advance_frames(client, 3);
    EXPECT_FALSE(is_opened()) << "a single click opened a node that only opens on a double click";

    Mcp_client::Tool_result double_click = client.call_tool("imgui_click", json{
        {"window", "Operation Stack"},
        {"label",  "Executed"},
        {"double", true}
    });
    ASSERT_FALSE(double_click.is_error) << double_click.text;
    advance_frames(client, 3);
    EXPECT_TRUE(is_opened()) << "the two press/release pairs were not read as a double click";

    client.call_tool("set_window_visibility", json{{"title", "Operation Stack"}, {"visible", false}});
    advance_frames(client, 2);
}

// imgui_hover leaves the pointer on the item, and imgui_scroll turns the wheel
// over a window, which scrolls it.
TEST_F(Mcp_test, imgui_hover_and_scroll_act_on_the_resolved_target)
{
    Mcp_client& client = Mcp_env::get().client();

    Mcp_client::Tool_result hover = client.call_tool("imgui_hover", json{
        {"window", "Operations"},
        {"label",  "Merge"}
    });
    ASSERT_FALSE(hover.is_error) << hover.text;
    const float center_x = hover.payload["target"].value("center_x", 0.0f);
    const float center_y = hover.payload["target"].value("center_y", 0.0f);
    ASSERT_GT(center_x, 0.0f) << hover.payload.dump();

    Mcp_client::Tool_result state = client.call_tool("get_input_state", json::object());
    ASSERT_FALSE(state.is_error) << state.text;
    EXPECT_FLOAT_EQ(state.payload["pointer"].value("x", 0.0f), center_x);
    EXPECT_FLOAT_EQ(state.payload["pointer"].value("y", 0.0f), center_y);

    Mcp_client::Tool_result hovered = client.call_tool("get_imgui_item_rect", json{
        {"window", "Operations"},
        {"label",  "Merge"}
    });
    ASSERT_FALSE(hovered.is_error) << hovered.text;
    EXPECT_TRUE(hovered.payload["status"].value("hovered", false)) << "the item was not left hovered";
    const float y_before = hovered.payload.value("y", 0.0f);

    Mcp_client::Tool_result scroll = client.call_tool("imgui_scroll", json{
        {"window", "Operations"},
        {"dy",     -5.0f}
    });
    ASSERT_FALSE(scroll.is_error) << scroll.text;
    EXPECT_EQ(scroll.payload["target"].value("window", ""), "Operations") << scroll.payload.dump();

    Mcp_client::Tool_result after = client.call_tool("get_imgui_item_rect", json{
        {"window", "Operations"},
        {"label",  "Merge"}
    });
    ASSERT_FALSE(after.is_error) << after.text;
    EXPECT_LT(after.payload.value("y", 0.0f), y_before) << "the wheel did not scroll the window";

    // A rendertarget host is inspected, not driven, by these actions.
    Mcp_client::Tool_result rendertarget = client.call_tool("imgui_click", json{
        {"host",  "Rendertarget"},
        {"label", "Merge"}
    });
    EXPECT_TRUE(rendertarget.is_error) << "a non-desktop host was accepted by imgui_click";
}

// capture_screenshot.annotate_imgui_items reports the number -> item table and
// draws the same numbered rectangles into the PNG.
TEST_F(Mcp_test, capture_screenshot_annotates_imgui_items)
{
    Mcp_client& client = Mcp_env::get().client();

    Mcp_client::Tool_result plain = client.call_tool("capture_screenshot", json{
        {"path", "logs/mcp_test_screenshot.png"}
    });
    ASSERT_FALSE(plain.is_error) << plain.text;
    EXPECT_FALSE(plain.payload.contains("annotations")) << "an unannotated capture reported annotations";
    const int width  = plain.payload.value("width",  0);
    const int height = plain.payload.value("height", 0);
    ASSERT_GT(width,  0);
    ASSERT_GT(height, 0);

    Mcp_client::Tool_result annotated = client.call_tool("capture_screenshot", json{
        {"path",                 "logs/mcp_test_screenshot_annotated.png"},
        {"annotate_imgui_items", true},
        {"annotate_window",      "Operations"},
        {"annotate_limit",       12}
    });
    ASSERT_FALSE(annotated.is_error) << annotated.text;
    ASSERT_TRUE(annotated.payload.contains("annotations"));
    const json& table = annotated.payload["annotations"];
    ASSERT_FALSE(table.empty()) << "nothing was annotated";
    EXPECT_LE(table.size(), 12u);
    EXPECT_EQ(annotated.payload.value("annotation_count", 0), static_cast<int>(table.size()));

    int expected_number = 1;
    for (const json& entry : table) {
        EXPECT_EQ(entry.value("number", 0), expected_number) << entry.dump();
        ++expected_number;
        EXPECT_EQ(entry.value("window", ""), "Operations") << entry.dump();
        EXPECT_GT(entry.value("width",  0.0f), 0.0f) << entry.dump();
        EXPECT_GT(entry.value("height", 0.0f), 0.0f) << entry.dump();
        EXPECT_GE(entry.value("x", -1.0f), 0.0f) << entry.dump();
        EXPECT_LE(entry.value("x", 0.0f) + entry.value("width", 0.0f), static_cast<float>(width)) << entry.dump();
        EXPECT_LE(entry.value("y", 0.0f) + entry.value("height", 0.0f), static_cast<float>(height)) << entry.dump();
    }
}

// A Properties row is addressed by the name the row shows. The value widgets
// of a row are labelled "##..." and the name is drawn as table text, so the
// row names its widgets for the item recorder
// (Property_editor::show_entries -> erhe::imgui::set_recorded_item_labels);
// a vector row names one item per component, which makes "Translation.x" one
// drag field. Ctrl+click opens that field's text input, and what is typed
// there reaches the node.
TEST_F(Mcp_test, property_row_is_addressable_by_its_label)
{
    Mcp_env&    env    = Mcp_env::get();
    Mcp_client& client = env.client();

    const auto is_window_active = [&](const char* const name) -> bool {
        Mcp_client::Tool_result windows = client.call_tool("get_imgui_windows", json::object());
        EXPECT_FALSE(windows.is_error) << windows.text;
        for (const json& window : windows.payload["windows"]) {
            if (window.value("name", "") == name) {
                return window.value("active", false);
            }
        }
        return false;
    };

    const bool properties_was_open = is_window_active("Properties");
    Mcp_client::Tool_result shown = client.call_tool("set_window_visibility", json{
        {"title",   "Properties"},
        {"visible", true}
    });
    ASSERT_FALSE(shown.is_error) << shown.text;
    advance_frames(client, 3);
    ASSERT_TRUE(is_window_active("Properties")) << "the Properties window did not open";

    const std::string box_name = "property row test box";
    Mcp_client::Tool_result shape = client.call_tool("create_shape", json{
        {"scene_name",  env.scene_name()},
        {"shape",       "box"},
        {"name",        box_name},
        {"motion_mode", "none"}
    });
    ASSERT_FALSE(shape.is_error) << shape.text;
    ASSERT_TRUE(wait_until_idle(client, 10000)) << "create_shape did not settle";

    Mcp_client::Tool_result selected = client.call_tool("select_items", json{
        {"scene_name", env.scene_name()},
        {"paths",      json::array({box_name})}
    });
    ASSERT_FALSE(selected.is_error) << selected.text;
    advance_frames(client, 3);

    // The first record carrying the label is the node's own Translation row;
    // the read-only World section below it repeats the label, so the row is
    // taken in submission order rather than by index, which only counts the
    // items a query kept.
    const auto first_translation_x = [&]() -> json {
        Mcp_client::Tool_result items = client.call_tool("get_imgui_items", json{
            {"window",         "Properties"},
            {"label_contains", "Translation.x"},
            {"visible_only",   false},
            {"limit",          10}
        });
        EXPECT_FALSE(items.is_error) << items.text;
        if (items.is_error || items.payload["items"].empty()) {
            return json::object();
        }
        return items.payload["items"][0];
    };

    // Property groups start closed (Property_group_states) and an AI-driven
    // editor reads no user state, so the row exists only once its group
    // header has been clicked open.
    json row = first_translation_x();
    if (row.empty()) {
        Mcp_client::Tool_result open_group = client.call_tool("imgui_click", json{
            {"window", "Properties"},
            {"label",  "Local Transform"}
        });
        ASSERT_FALSE(open_group.is_error) << open_group.text;
        advance_frames(client, 3);
        row = first_translation_x();
    }
    ASSERT_FALSE(row.empty()) << "no Properties row is named 'Translation.x'";

    // Scroll the window to its top and then step down until the row is on
    // screen: only an unclipped item can be clicked.
    client.call_tool("imgui_scroll", json{{"window", "Properties"}, {"dy", 100.0f}});
    bool row_visible = false;
    for (int step = 0; (step < 60) && !row_visible; ++step) {
        row = first_translation_x();
        ASSERT_FALSE(row.empty());
        row_visible = row["status"].value("visible", false);
        if (!row_visible) {
            client.call_tool("imgui_scroll", json{{"window", "Properties"}, {"dy", -2.0f}});
        }
    }
    ASSERT_TRUE(row_visible) << "the Translation row never came on screen: " << row.dump();
    EXPECT_EQ(row.value("display_label", ""), "Translation.x") << row.dump();

    Mcp_client::Tool_result click = client.call_tool("mouse_click", json{
        {"x",         row.value("center_x", 0.0f)},
        {"y",         row.value("center_y", 0.0f)},
        {"modifiers", json::array({"ctrl"})}
    });
    ASSERT_FALSE(click.is_error) << click.text;

    Mcp_client::Tool_result typed = client.call_tool("type_text", json{{"text", "2.5"}});
    ASSERT_FALSE(typed.is_error) << typed.text;
    Mcp_client::Tool_result commit = client.call_tool("key_press", json{{"key", "enter"}});
    ASSERT_FALSE(commit.is_error) << commit.text;
    advance_frames(client, 3);

    Mcp_client::Tool_result properties = client.call_tool("get_item_properties", json{
        {"scene_name", env.scene_name()},
        {"item_name",  box_name}
    });
    ASSERT_FALSE(properties.is_error) << properties.text;
    std::string translation;
    for (const json& property : properties.payload["properties"]) {
        if (property.value("name", "") == "translation") {
            translation = property.value("value", "");
        }
    }
    ASSERT_FALSE(translation.empty()) << "the node reports no translation";
    EXPECT_EQ(translation.rfind("2.5", 0), 0u)
        << "typing into the row's text input did not reach the node: " << translation;

    client.call_tool("select_items", json{{"scene_name", env.scene_name()}, {"paths", json::array()}});
    client.call_tool("delete_nodes", json{{"scene_name", env.scene_name()}, {"names", json::array({box_name})}});
    if (!properties_was_open) {
        client.call_tool("set_window_visibility", json{{"title", "Properties"}, {"visible", false}});
    }
    advance_frames(client, 3);
}

// doc/agents/mcp_api_guidelines.md "Arguments are validated, never
// reinterpreted": a rotation_xyzw that is not a unit quaternion is refused
// (the node keeps its rotation, nothing is recorded for undo), while one that
// is only off by decimal rounding is accepted and normalized.
TEST_F(Mcp_test, set_node_transform_refuses_a_non_unit_rotation_and_normalizes_rounding)
{
    Mcp_client& client = Mcp_env::get().client();

    const std::string scene = create_new_scene(client);
    ASSERT_FALSE(scene.empty()) << "could not create a scene";
    Mcp_client::Tool_result created = client.call_tool("create_node", json{{"scene_name", scene}, {"name", "rotated"}});
    ASSERT_FALSE(created.is_error) << created.text;
    advance_frames(client, 2);

    auto local_rotation = [&client, &scene]() -> json {
        Mcp_client::Tool_result result = client.call_tool("get_node_details", json{{"scene_name", scene}, {"node_name", "rotated"}});
        EXPECT_FALSE(result.is_error) << result.text;
        return result.payload.at("local_transform").at("rotation_xyzw");
    };
    auto undo_depth = [&client]() -> std::size_t {
        return client.call_tool("get_undo_redo_stack", json::object()).payload.at("undo").size();
    };
    auto set_rotation = [&client, &scene](const json& xyzw) -> Mcp_client::Tool_result {
        Mcp_client::Tool_result result = client.call_tool(
            "set_node_transform",
            json{{"scene_name", scene}, {"node_name", "rotated"}, {"space", "local"}, {"rotation_xyzw", xyzw}}
        );
        advance_frames(client, 2);
        return result;
    };

    const json        rotation_before = local_rotation();
    const std::size_t depth_before    = undo_depth();
    for (const json& bad : {json{0.0f, 0.0f, 0.0f, 2.0f}, json{0.0f, 0.0f, 0.0f, 0.0f}, json{0.0f, 0.0f, 0.5f, 0.5f}}) {
        Mcp_client::Tool_result refused = set_rotation(bad);
        EXPECT_TRUE(refused.is_error) << "accepted non-unit rotation_xyzw " << bad.dump();
        EXPECT_NE(refused.text.find("unit quaternion"), std::string::npos) << refused.text;
    }
    EXPECT_EQ(local_rotation(), rotation_before) << "a refused rotation changed the node";
    EXPECT_EQ(undo_depth(), depth_before) << "a refused rotation was recorded for undo";

    // 90 degrees about Z, written with the 4-digit rounding of a hand-typed value.
    Mcp_client::Tool_result rounded = set_rotation(json{0.0f, 0.0f, 0.7071f, 0.7071f});
    ASSERT_FALSE(rounded.is_error) << rounded.text;
    const json  q      = local_rotation();
    const float length = std::sqrt(
        (q[0].get<float>() * q[0].get<float>()) + (q[1].get<float>() * q[1].get<float>()) +
        (q[2].get<float>() * q[2].get<float>()) + (q[3].get<float>() * q[3].get<float>())
    );
    EXPECT_NEAR(length, 1.0f, 1.0e-6f) << "the rounded rotation was not normalized: " << q.dump();
    EXPECT_NEAR(q[2].get<float>(), 0.70710678f, 1.0e-5f) << q.dump();

    client.call_tool("close_scene", json{{"scene_name", scene}});
    advance_frames(client, 3);
}
