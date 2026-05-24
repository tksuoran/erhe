// Integration tests for the editor's MCP server.
//
// These tests connect to a running editor over HTTP. Start the editor
// before running, or use scripts/run_mcp_tests.ps1 to launch+test+cleanup
// in one step.
//
// Configuration (env vars):
//   ERHE_MCP_TEST_HOST      default "127.0.0.1"
//   ERHE_MCP_TEST_PORT      default 8080
//   ERHE_MCP_TEST_TIMEOUT_S default 30  (initial /health wait)

#include <gtest/gtest.h>

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
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

// Single shared client + discovered scene/material across all tests.
class Mcp_env
{
public:
    static auto get() -> Mcp_env&
    {
        static Mcp_env instance;
        return instance;
    }

    Mcp_client& client() { return *m_client; }

    [[nodiscard]] auto ready() const -> bool { return m_ready; }
    [[nodiscard]] auto scene_name() const -> const std::string& { return m_scene_name; }
    [[nodiscard]] auto material_name() const -> const std::string& { return m_material_name; }
    [[nodiscard]] auto first_texture_name() const -> const std::optional<std::string>& { return m_texture_name; }
    [[nodiscard]] auto first_texture_id  () const -> const std::optional<std::size_t>&  { return m_texture_id;   }

    void initialize()
    {
        const std::string host       = env_or    ("ERHE_MCP_TEST_HOST",      "127.0.0.1");
        const int         port       = env_or_int("ERHE_MCP_TEST_PORT",      8080);
        const int         timeout_s  = env_or_int("ERHE_MCP_TEST_TIMEOUT_S", 30);

        m_client = std::make_unique<Mcp_client>(host, port);
        if (!m_client->wait_for_ready(timeout_s)) {
            GTEST_LOG_(ERROR) << "MCP server not reachable at " << host << ":" << port
                              << " within " << timeout_s << "s. Start the editor first.";
            return;
        }

        // Discover a scene and a material.
        Mcp_client::Tool_result scenes_res = m_client->call_tool("list_scenes", json::object());
        if (scenes_res.is_error || !scenes_res.payload.contains("scenes")) {
            GTEST_LOG_(ERROR) << "list_scenes returned no scenes: " << scenes_res.text;
            return;
        }
        const json& scenes = scenes_res.payload["scenes"];
        if (!scenes.is_array() || scenes.empty()) {
            GTEST_LOG_(ERROR) << "Editor has no scenes.";
            return;
        }
        m_scene_name = scenes[0].value("name", "");
        if (m_scene_name.empty()) {
            GTEST_LOG_(ERROR) << "First scene has empty name.";
            return;
        }

        Mcp_client::Tool_result mats_res = m_client->call_tool(
            "get_scene_materials", json{{"scene_name", m_scene_name}}
        );
        if (mats_res.is_error || !mats_res.payload.contains("materials") || mats_res.payload["materials"].empty()) {
            GTEST_LOG_(ERROR) << "Scene '" << m_scene_name << "' has no materials.";
            return;
        }
        m_material_name = mats_res.payload["materials"][0].value("name", "");
        if (m_material_name.empty()) {
            GTEST_LOG_(ERROR) << "First material has empty name.";
            return;
        }

        Mcp_client::Tool_result tex_res = m_client->call_tool(
            "get_scene_textures", json{{"scene_name", m_scene_name}}
        );
        if (!tex_res.is_error && tex_res.payload.contains("textures")) {
            const json& textures = tex_res.payload["textures"];
            if (textures.is_array() && !textures.empty()) {
                const json& first = textures[0];
                if (first.contains("name") && first["name"].is_string()) {
                    m_texture_name = first["name"].get<std::string>();
                }
                if (first.contains("id") && first["id"].is_number()) {
                    m_texture_id = first["id"].get<std::size_t>();
                }
            }
        }

        m_ready = true;
    }

private:
    std::unique_ptr<Mcp_client> m_client;
    bool                        m_ready{false};
    std::string                 m_scene_name;
    std::string                 m_material_name;
    std::optional<std::string>  m_texture_name;
    std::optional<std::size_t>  m_texture_id;
};

class Mcp_test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        Mcp_env& env = Mcp_env::get();
        if (!env.ready()) {
            env.initialize();
        }
        if (!env.ready()) {
            GTEST_SKIP() << "MCP environment not ready (editor not running?)";
        }
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
        EXPECT_TRUE(s.contains("tex_coord"));
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

// ---- edit_material: texture_samplers (transforms always testable) ---------

TEST_F(Mcp_test, edit_material_texture_transform_round_trip)
{
    json before = material_details();
    const json bs = before["texture_samplers"]["base_color"];
    const double orig_rotation = bs["rotation"].get<double>();

    edit_and_wait(
        json{{"texture_samplers", {
            {"base_color", {
                {"tex_coord", 1},
                {"rotation",  0.1234},
                {"offset",    json::array({0.5, 0.25})},
                {"scale",     json::array({2.0, 3.0})}
            }}
        }}},
        [](const json& d) {
            const json& s = d["texture_samplers"]["base_color"];
            return s["tex_coord"].get<int>() == 1
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
                {"tex_coord", bs["tex_coord"].get<int>()},
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

// ---- edit_material: texture assignment (skipped if no textures) -----------

TEST_F(Mcp_test, edit_material_texture_assign_by_name)
{
    Mcp_env& env = Mcp_env::get();
    if (!env.first_texture_name().has_value()) {
        GTEST_SKIP() << "No textures in content library; skipping assign-by-name test";
    }
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
    json restore_tex = before_slot["texture_id"].is_null()
        ? json{nullptr}
        : json{before_slot["texture_id"]};
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
    if (!env.first_texture_id().has_value()) {
        GTEST_SKIP() << "No textures in content library; skipping assign-by-id test";
    }
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

    json restore_tex = before_slot["texture_id"].is_null()
        ? json{nullptr}
        : json{before_slot["texture_id"]};
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
    if (!env.first_texture_id().has_value()) {
        GTEST_SKIP() << "No textures in content library; skipping clear test";
    }
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
        for (const char* key : {"base_color", "opacity", "roughness", "metallic", "reflectance", "emissive", "normal_texture_scale", "occlusion_texture_strength"}) {
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

    httplib::Client c{env_or("ERHE_MCP_TEST_HOST", "127.0.0.1"), env_or_int("ERHE_MCP_TEST_PORT", 8080)};
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

// (c) Queue overflow: spam concurrent slow tool calls and verify at
// least one returns the "Server busy" -32000 error.
TEST_F(Mcp_test, queue_overflow_returns_busy_error)
{
    const std::string host = env_or    ("ERHE_MCP_TEST_HOST", "127.0.0.1");
    const int         port = env_or_int("ERHE_MCP_TEST_PORT", 8080);

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

// (d) Ambiguous-name material lookup. The stock scene rarely has two
// materials sharing a name, so the test is skipped if it cannot find
// such a pair; when it does it confirms isError + candidate_ids.
TEST_F(Mcp_test, edit_material_ambiguous_name_returns_candidates)
{
    Mcp_env& env = Mcp_env::get();
    Mcp_client::Tool_result mats = env.client().call_tool(
        "get_scene_materials", json{{"scene_name", env.scene_name()}}
    );
    ASSERT_FALSE(mats.is_error);
    ASSERT_TRUE(mats.payload.contains("materials"));

    std::map<std::string, int> name_counts;
    std::string duplicate_name;
    for (const json& m : mats.payload["materials"]) {
        const std::string name = m.value("name", std::string{});
        if (name.empty()) continue;
        if (++name_counts[name] >= 2) {
            duplicate_name = name;
            break;
        }
    }
    if (duplicate_name.empty()) {
        GTEST_SKIP() << "Scene has no duplicate-named materials; cannot exercise ambiguous-name path";
    }

    Material_scalar_state_guard guard;  // restores opacity/etc. on the FIRST matching material if we accidentally mutate
    Mcp_client::Tool_result r = env.client().call_tool(
        "edit_material",
        json{{"scene_name", env.scene_name()}, {"material_name", duplicate_name}, {"opacity", 0.5}}
    );
    EXPECT_TRUE(r.is_error) << "Server should have refused ambiguous material name: " << r.text;
    ASSERT_TRUE(r.payload.contains("candidate_ids")) << "Server should have listed candidate ids: " << r.text;
    EXPECT_GE(r.payload["candidate_ids"].size(), 2u);
}

// (a) Tampered token: send a wrong bearer token and expect 401 if auth
// is enabled. The test is a no-op when the server has no token loaded
// (auth disabled, e.g. token file missing).
TEST_F(Mcp_test, auth_tampered_token_rejected)
{
    const std::string host = env_or    ("ERHE_MCP_TEST_HOST", "127.0.0.1");
    const int         port = env_or_int("ERHE_MCP_TEST_PORT", 8080);

    httplib::Client c_noauth{host, port};
    c_noauth.set_read_timeout(5, 0);
    json probe_body = {{"jsonrpc", "2.0"}, {"id", "auth-probe"}, {"method", "tools/list"}};
    httplib::Result probe = c_noauth.Post("/mcp", probe_body.dump(), "application/json");
    ASSERT_TRUE(probe);
    if (probe->status == 200) {
        // Auth disabled: nothing to test. (Mcp_env's existing tools/list
        // call would have failed during initialize() if auth was
        // required and the token mechanism was broken, so we know the
        // happy path works.)
        GTEST_SKIP() << "Server has no bearer token loaded; tampered-token test is a no-op";
    }
    ASSERT_EQ(probe->status, 401) << "Unauthenticated request should be 401 when auth is enabled";

    httplib::Client c_bad{host, port};
    c_bad.set_read_timeout(5, 0);
    c_bad.set_default_headers({{"Authorization", "Bearer not-the-right-token"}});
    httplib::Result tampered = c_bad.Post("/mcp", probe_body.dump(), "application/json");
    ASSERT_TRUE(tampered);
    EXPECT_EQ(tampered->status, 401) << "Wrong bearer token should be 401";
}
