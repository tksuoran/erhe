// Mcp_server lifecycle, HTTP routing, JSON-RPC handling and tool dispatch.
// Split out of mcp_server.cpp; shares helpers via mcp_server_shared.hpp.

#include "mcp/mcp_server.hpp"
#include "mcp/mcp_server_shared.hpp"

#include "app_context.hpp"
#include "app_scenes.hpp"
#include "config/generated/editor_settings_config.hpp"
#include "config/generated/ddgi_config.hpp"
#include "config/generated/indirect_diffuse_bounces.hpp"
#include "config/generated/indirect_diffuse_source.hpp"
#include "config/generated/radiance_cascades_config.hpp"
#include "config/generated/radiance_cascades_direction_jitter.hpp"
#include "config/generated/radiance_cascades_probe_overlay.hpp"
#include "config/generated/radiance_cascades_merge_mode.hpp"
#include "config/generated/ray_trace_config.hpp"
#include "editor_log.hpp"
#include "operations/operation_stack.hpp"
#include "renderers/ddgi_renderer.hpp"
#include "renderers/indirect_diffuse.hpp"
#include "renderers/radiance_cascades_renderer.hpp"
#include "renderers/ray_trace_renderer.hpp"
#include "scene/scene_image_capture.hpp"
#include "scene/scene_root.hpp"

#include "erhe_commands/commands.hpp"
#include "erhe_graphics/gpu_timer.hpp"
#include "erhe_graphics/image_writer.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_imgui/imgui_window.hpp"
#include "erhe_imgui/imgui_windows.hpp"
#include "erhe_scene/scene.hpp"

#include <httplib.h>
#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <future>
#include <iterator>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <vector>

namespace editor {

using namespace mcp_server_detail;

namespace {


} // anonymous namespace

Mcp_server::Mcp_server(
    erhe::commands::Commands& commands,
    App_context&              context,
    int                       port
)
    : m_commands{commands}
    , m_context {context}
    , m_port    {port}
{
    // ERHE_MCP_PORT overrides the construction-time port (the preferred port
    // for the fallback scan in start()). Lets an operator run several editors
    // on known ports, or move the server without a rebuild.
    const char* const port_env = std::getenv("ERHE_MCP_PORT");
    if (port_env != nullptr) {
        char* end = nullptr;
        const long value = std::strtol(port_env, &end, 10);
        if ((end != port_env) && (*end == '\0') && (value >= 1) && (value <= 65535)) {
            m_port = static_cast<int>(value);
            log_mcp->info("MCP server: preferred port {} from ERHE_MCP_PORT", m_port);
        } else {
            log_mcp->warn("MCP server: ignoring invalid ERHE_MCP_PORT value '{}'", port_env);
        }
    }

    m_auth_token = load_auth_token();
    if (m_auth_token.empty()) {
        log_mcp->warn(
            "MCP server: no bearer token loaded (write a secret to ~/.agents/erhe_mcp_token "
            "with mode 0600, or name a token file in ERHE_MCP_TOKEN_FILE, to require Authorization: Bearer)"
        );
    } else {
        log_mcp->info("MCP server: bearer-token auth enabled");
    }
}

Mcp_server::~Mcp_server() noexcept
{
    // ~thread on a still-joinable handle calls std::terminate; the
    // join itself can throw (system_error on EDEADLK / EINVAL).
    // Catch and log so destruction is noexcept in practice and any
    // platform-level join failure leaves a breadcrumb instead of
    // crashing the editor at shutdown.
    try {
        stop();
    } catch (const std::system_error& e) {
        log_mcp->error("MCP server: ~Mcp_server caught system_error during stop: {}", e.what());
    } catch (const std::exception& e) {
        log_mcp->error("MCP server: ~Mcp_server caught exception during stop: {}", e.what());
    } catch (...) {
        log_mcp->error("MCP server: ~Mcp_server caught unknown exception during stop");
    }
}

void Mcp_server::start()
{
    std::lock_guard<std::mutex> lock{m_lifecycle_mutex};

    if (m_running.load() || m_server_thread.joinable() || m_http_server) {
        return;
    }

    log_mcp->info("MCP server: starting on port {}", m_port);

    refresh_tool_list();
    validate_tool_list_against_dispatch();

    // Bind the preferred port, falling back through up to k_port_retry_count-1
    // successors (e.g. 3743..3762) when it is already in use. This matters on
    // Quest, where another service may already hold the port; without the fallback
    // the editor came up with no reachable MCP endpoint. Each FAILED
    // httplib::Server::bind_to_port() permanently decommissions that Server
    // instance (it sets is_decommissioned, after which every later bind on the
    // same object returns -1 regardless of port), so each attempt must use a
    // FRESH Server. Binding is non-blocking, so it runs here on the caller
    // thread; only the blocking accept loop (listen_after_bind) runs on
    // m_server_thread.
    constexpr int k_port_retry_count = 20;
    const int     preferred_port     = m_port;
    int           bound_port         = -1;
    for (int candidate = preferred_port; candidate < (preferred_port + k_port_retry_count); ++candidate) {
        m_http_server = std::make_unique<httplib::Server>();
        m_http_server->set_payload_max_length(k_max_payload_bytes);
        // The port scan below relies on bind() FAILING on a port another
        // editor is LISTENing on; httplib's default socket options break
        // that on every platform. On Windows they set SO_REUSEADDR, which
        // (unlike POSIX) lets bind() succeed on an actively listening port:
        // the new socket is silently shadowed and this server logs a port a
        // DIFFERENT editor is answering on; SO_EXCLUSIVEADDRUSE makes bind()
        // fail. On POSIX they set SO_REUSEPORT, under which two listeners
        // of the same user share the port and the kernel splits incoming
        // connections between them (seen on macOS: two fixture editors both
        // logged "listening on 3774" and served alternate requests);
        // SO_REUSEADDR alone keeps TIME_WAIT reuse and fails on a listener.
        m_http_server->set_socket_options(
            [](socket_t sock) {
#if defined(_WIN32)
                httplib::set_socket_opt(sock, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, 1);
#else
                httplib::set_socket_opt(sock, SOL_SOCKET, SO_REUSEADDR, 1);
#endif
            }
        );
        setup_routes();
        if (m_http_server->bind_to_port("127.0.0.1", candidate)) {
            bound_port = candidate;
            break;
        }
        m_http_server.reset(); // decommissioned by the failed bind; retry with a fresh Server
    }

    if (bound_port < 0) {
        log_mcp->error(
            "MCP server: failed to bind any port in [{}, {}) - all in use?",
            preferred_port, preferred_port + k_port_retry_count
        );
        return;
    }

    m_port = bound_port;
    if (bound_port != preferred_port) {
        log_mcp->warn("MCP server: port {} unavailable; bound to {} instead", preferred_port, bound_port);
    }

    m_running.store(true);
    m_server_thread = std::thread{&Mcp_server::server_thread_main, this};
}

void Mcp_server::stop()
{
    std::lock_guard<std::mutex> lock{m_lifecycle_mutex};

    // Worker thread flips m_running to false on its way out of
    // httplib::listen() (server_thread_main), so checking m_running
    // alone would miss the post-listen window where the thread is
    // still joinable. Check both the thread and the server pointer.
    if (!m_server_thread.joinable() && !m_http_server) {
        return;
    }

    log_mcp->info("MCP server: stopping");

    m_running.store(false);
    if (m_http_server) {
        m_http_server->stop();
    }
    if (m_server_thread.joinable()) {
        m_server_thread.join();
    }
    m_http_server.reset();

    log_mcp->info("MCP server: stopped");
}

auto Mcp_server::is_running() const -> bool
{
    return m_running.load();
}

void Mcp_server::server_thread_main()
{
    // m_http_server is already bound to m_port by start(); enter the blocking
    // accept loop. stop() unblocks this via m_http_server->stop().
    log_mcp->info("MCP server: listening on 127.0.0.1:{} (pid {}, built {})", m_port, get_process_id(), c_mcp_build_timestamp);

    if (!m_http_server->listen_after_bind()) {
        if (m_running.load()) {
            log_mcp->error("MCP server: listen_after_bind() failed on port {}", m_port);
        }
    }

    m_running.store(false);
    log_mcp->info("MCP server: thread exiting");
}

void Mcp_server::setup_routes()
{
    m_http_server->Post("/mcp", [this](const httplib::Request& req, httplib::Response& res) {
        res.set_header("Content-Type", "application/json");

        // Auth check first. When m_auth_token is empty (token file not
        // present or unreadable) auth is disabled and any request is
        // accepted; the operator gets a startup warning so disabled
        // auth is loud rather than silent.
        if (!m_auth_token.empty()) {
            const std::optional<std::string> presented = bearer_token_from(req);
            if (!presented.has_value() || !constant_time_equal(*presented, m_auth_token)) {
                res.status = 401;
                res.set_header("WWW-Authenticate", "Bearer realm=\"erhe-mcp\"");
                res.body = make_jsonrpc_error(nullptr, -32001, "Unauthorized");
                return;
            }
        }

        json request;
        try {
            request = json::parse(req.body);
        } catch (const json::parse_error&) {
            res.body = make_jsonrpc_error(nullptr, -32700, "Parse error");
            return;
        }

        const std::string method = request.value("method", "");

        // A request without an id is a JSON-RPC notification (the MCP client
        // sends notifications/initialized after the initialize handshake, and
        // may send notifications/cancelled). Notifications must not receive a
        // response body; acknowledge with 202 Accepted per MCP streamable
        // HTTP. Nothing here acts on notifications yet.
        if (!request.contains("id")) {
            log_mcp->debug("MCP server: notification '{}' acknowledged", method);
            res.status = 202;
            res.body.clear();
            return;
        }

        // JSON-RPC allows the id to be a string, a number, or null, and the
        // response must echo it with the same type. Anything else (array,
        // object, bool) is invalid; echo null for those rather than throwing
        // (json::type_error would surface as an opaque HTTP 500).
        json id = request.at("id");
        if (!id.is_string() && !id.is_number() && !id.is_null()) {
            id = nullptr;
        }

        if (method == "initialize") {
            res.body = handle_initialize(id, request.value("params", json::object()));
        } else if (method == "tools/list") {
            res.body = handle_tools_list(id);
        } else if (method == "tools/call") {
            const auto& params    = request.value("params", json::object());
            const std::string tool_name = params.value("name", "");
            const json arguments = params.value("arguments", json::object());
            if (tool_name.empty()) {
                res.body = make_jsonrpc_error(id, -32602, "Missing tool name in params.name");
            } else {
                res.body = handle_tools_call(id, tool_name, arguments);
            }
        } else {
            res.body = make_jsonrpc_error(id, -32601, "Method not found: " + method);
        }
    });

    // Readiness, not liveness: 200 once the main loop drains the request
    // queue (see m_serving), 503 while the editor is still starting up.
    m_http_server->Get("/health", [this](const httplib::Request&, httplib::Response& res) {
        bool serving = false;
        {
            std::lock_guard<std::mutex> lock{m_queue_mutex};
            serving = m_serving;
        }
        res.set_header("Content-Type", "application/json");
        if (serving) {
            res.body = R"({"status":"ok"})";
        } else {
            res.status = 503;
            res.body   = R"({"status":"starting"})";
        }
    });
}

auto Mcp_server::handle_initialize(const json& id, const json& params) -> std::string
{
    // Version negotiation per the MCP lifecycle spec: if the client requests
    // a protocol revision the server supports, echo it back; otherwise answer
    // with the latest revision the server supports and let the client decide
    // whether to proceed. All three revisions listed here are equivalent for
    // this server's feature set (tools over plain HTTP POST responses).
    static const char* const k_supported_protocol_versions[] = {
        "2025-06-18",
        "2025-03-26",
        "2024-11-05"
    };
    std::string protocol_version = k_supported_protocol_versions[0];
    const std::string requested_version = params.value("protocolVersion", "");
    for (const char* supported : k_supported_protocol_versions) {
        if (requested_version == supported) {
            protocol_version = requested_version;
            break;
        }
    }

    json result = {
        {"protocolVersion", protocol_version},
        {"capabilities", {
            {"tools", json::object()}
        }},
        {"serverInfo", {
            {"name",    "erhe-editor"},
            {"version", "0.2.0"},
            {"pid",     get_process_id()},
            {"build",   c_mcp_build_timestamp}
        }}
    };
    return make_jsonrpc_response(id, result);
}

auto Mcp_server::handle_tools_list(const json& id) -> std::string
{
    refresh_tool_list();

    json tools = json::array();
    {
        std::lock_guard<std::mutex> lock{m_tools_mutex};
        for (const auto& tool : m_tool_infos) {
            json tool_json = {
                {"name",        tool.name},
                {"description", tool.description},
                {"inputSchema", tool.input_schema}
            };
            tools.push_back(tool_json);
        }
    }

    json result = {{"tools", tools}};
    return make_jsonrpc_response(id, result);
}

auto Mcp_server::handle_tools_call(
    const json&        id,
    const std::string& tool_name,
    const json&        arguments
) -> std::string
{
    auto queued = std::make_unique<Queued_request>();
    queued->tool_name = tool_name;
    queued->arguments = arguments;
    std::future<std::string> result_future = queued->result_promise.get_future();

    {
        std::lock_guard<std::mutex> lock{m_queue_mutex};
        if (m_request_queue.size() >= k_max_queue_depth) {
            // Drop without enqueuing so process_queued_requests cannot
            // pop a stale entry whose HTTP client has already timed
            // out. -32000 is "server error" in JSON-RPC's reserved
            // range; the message is what differentiates it from other
            // -32000s.
            return make_jsonrpc_error(id, -32000, "Server busy: request queue full");
        }
        m_request_queue.push_back(std::move(queued));
    }

    const auto status = result_future.wait_for(k_request_timeout);
    if (status == std::future_status::timeout) {
        return make_jsonrpc_error(id, -32000, "Request timed out: " + tool_name);
    }

    std::string result_json;
    try {
        result_json = result_future.get();
    } catch (const std::future_error& e) {
        // The queued request's promise was destroyed without being
        // settled. This can happen if Mcp_server is being torn down
        // while HTTP handler threads are still waiting (the queue
        // unique_ptrs are freed before this thread observes ready).
        // Translate the broken_promise into a clean JSON-RPC error
        // so the exception cannot escape into httplib's dispatcher.
        log_mcp->warn("MCP server: future_error in handle_tools_call: {}", e.what());
        return make_jsonrpc_error(id, -32000, "Internal: request abandoned: " + tool_name);
    }
    json result = json::parse(result_json, nullptr, false);
    if (result.is_discarded()) {
        return make_jsonrpc_error(id, -32000, "Internal error processing: " + tool_name);
    }
    return make_jsonrpc_response(id, result);
}

auto Mcp_server::handle_error(const json& id, int code, const std::string& message) -> std::string
{
    return make_jsonrpc_error(id, code, message);
}

auto Mcp_server::process_queued_requests() -> int
{
    std::vector<std::unique_ptr<Queued_request>> requests;
    {
        std::lock_guard<std::mutex> lock{m_queue_mutex};
        m_serving = true;
        requests.swap(m_request_queue);
    }

    // Requests deferred on the previous pass run first (they are older, and
    // the frame they were waiting for has rendered in between).
    if (!m_deferred_requests.empty()) {
        m_deferred_requests.insert(
            m_deferred_requests.end(),
            std::make_move_iterator(requests.begin()),
            std::make_move_iterator(requests.end())
        );
        requests.swap(m_deferred_requests);
        m_deferred_requests.clear();
    }

    // A capture whose request was dropped on an earlier pass: release it once
    // its recorded copy has retired.
    release_abandoned_scene_image_capture();

    const auto now = std::chrono::steady_clock::now();
    int count = 0;
    for (auto& req : requests) {
        // Drop entries whose HTTP client has already given up (wait_for
        // returned timeout in handle_tools_call). Without this guard
        // a slow editor frame would still mutate editor state for a
        // request the operator already considers failed. We still
        // settle the promise so the future destructor does not abort.
        if ((now - req->enqueued_at) >= k_request_timeout) {
            req->result_promise.set_value(
                make_jsonrpc_error(nullptr, -32000, "Request expired before processing: " + req->tool_name)
            );
            if (m_selection_drag_steps.has_value() && (m_selection_drag_steps->request == req.get())) {
                // The steps stop; the scripted drag stays held until a
                // drag_selection release.
                m_selection_drag_steps.reset();
                log_mcp->warn("MCP server: drag_selection expired mid-drag; the drag is held - release it with action 'release'");
            }
            if (m_physics_drag_steps.has_value() && (m_physics_drag_steps->request == req.get())) {
                m_physics_drag_steps.reset();
                log_mcp->warn("MCP server: physics_drag expired mid-drag; the drag is held - release it with action 'release'");
            }
            if (m_input_gesture_steps.request == req.get()) {
                // The remaining events are dropped; what was already injected
                // has been dispatched, so a held button stays held until the
                // caller injects its release.
                log_mcp->warn(
                    "MCP server: inject_input_events expired after {} of {} events; the rest are dropped",
                    m_input_gesture_steps.injected,
                    m_input_gesture_steps.events.size()
                );
                m_input_gesture_steps.clear();
            }
            if ((m_reference_query_request == req.get()) && (m_context.ddgi_renderer != nullptr)) {
                // Stop tracing chunks nobody will read.
                m_reference_query_request = nullptr;
                m_context.ddgi_renderer->cancel_reference_query();
                log_mcp->warn("MCP server: reference_indirect_diffuse expired; its remaining chunks are cancelled");
            }
            if (m_rc_texels_request == req.get()) {
                // A recorded copy still completes; a request arriving while
                // it is in flight reads it (its counters say when it was
                // taken).
                m_rc_texels_request = nullptr;
            }
            if (m_scene_image_capture && (m_scene_image_request == req.get())) {
                m_scene_image_request = nullptr;
                log_mcp->warn("MCP server: render_scene_image expired; its capture is released once its GPU copy has retired");
                release_abandoned_scene_image_capture();
            }
            log_mcp->warn("MCP server: dropped expired '{}' before processing", req->tool_name);
            continue;
        }

        // Per-request exception boundary. process_queued_requests() runs on the
        // main thread, so a handler that throws would skip the set_value() below,
        // break the waiting HTTP thread's promise (observed as
        // future_error "broken promise"), and - fatally - escape up the main
        // thread into the crash handler, taking down the whole editor. A single
        // bad tool call must instead become a JSON-RPC tool error. The throw is
        // logged loudly so the offending handler can still be tracked down.
        std::string result;
        try {
            m_current_request = req.get();
            result = dispatch_tool_call(req->tool_name, req->arguments);
            m_current_request = nullptr;
        } catch (const std::exception& e) {
            log_mcp->error("MCP server: handler for '{}' threw: {}", req->tool_name, e.what());
            result = make_error_content(std::string{"Handler '"} + req->tool_name + "' threw an exception: " + e.what());
        } catch (...) {
            log_mcp->error("MCP server: handler for '{}' threw a non-standard exception", req->tool_name);
            result = make_error_content(std::string{"Handler '"} + req->tool_name + "' threw a non-standard exception");
        }

        if (m_defer_current_request) {
            // The handler needs a rendered frame before it can answer (see
            // the m_deferred_requests member comment): park the request,
            // promise unsettled, and re-run it on the next frame's pass.
            m_defer_current_request = false;
            m_deferred_requests.push_back(std::move(req));
            continue;
        }

        req->result_promise.set_value(std::move(result));
        ++count;
        log_mcp->info("MCP server: processed '{}'", req->tool_name);
    }

    return count;
}

auto Mcp_server::get_dispatch_table() -> std::span<const Mcp_server::Tool_dispatch_entry>
{
    // Member-function-local: the handlers are private members, so their
    // addresses can only be taken from inside the class.
    static constexpr Tool_dispatch_entry c_tool_dispatch[] = {
        { "batch",                          &Mcp_server::action_batch                         },
        { "list_scenes",                    &Mcp_server::query_list_scenes                    },
        { "get_scene_nodes",                &Mcp_server::query_scene_nodes                    },
        { "get_composition_passes",         &Mcp_server::query_composition_passes             },
        { "get_node_details",               &Mcp_server::query_node_details                   },
        { "get_scene_cameras",              &Mcp_server::query_scene_cameras                  },
        { "get_scene_lights",               &Mcp_server::query_scene_lights                   },
        { "get_scene_materials",            &Mcp_server::query_scene_materials                },
        { "get_scene_textures",             &Mcp_server::query_scene_textures                 },
        { "get_scene_brushes",              &Mcp_server::query_scene_brushes                  },
        { "get_brush_geometry_states",      &Mcp_server::query_brush_geometry_states          },
        { "request_brush_geometry",         &Mcp_server::action_request_brush_geometry        },
        { "get_scene_settings",             &Mcp_server::query_scene_settings                 },
        { "get_scene_variants",             &Mcp_server::query_scene_variants                 },
        { "get_material_details",           &Mcp_server::query_material_details               },
        { "get_viewports",                  &Mcp_server::query_viewports                      },
        { "pick_at",                        &Mcp_server::query_pick_at                        },
        { "get_server_info",                &Mcp_server::query_server_info                    },
        { "set_window_visibility",          &Mcp_server::action_set_window_visibility         },
        { "get_frame_pacing_status",        &Mcp_server::query_frame_pacing_status            },
        { "get_gpu_timers",                 &Mcp_server::query_gpu_timers                     },
        { "get_frame_pacing_frames",        &Mcp_server::query_frame_pacing_frames            },
        { "set_frame_pacing_min_vsyncs",    &Mcp_server::action_set_frame_pacing_min_vsyncs   },
        { "set_frame_pacing_workload",      &Mcp_server::action_set_frame_pacing_workload     },
        { "set_frame_pacing_capture",       &Mcp_server::action_set_frame_pacing_capture      },
        { "get_selection",                  &Mcp_server::query_selection                      },
        { "get_undo_redo_stack",            &Mcp_server::query_undo_redo_stack                },
        { "clear_undo_history",             &Mcp_server::action_clear_undo_history            },
        { "undo",                           &Mcp_server::action_undo                          },
        { "redo",                           &Mcp_server::action_redo                          },
        { "request_exit",                   &Mcp_server::action_request_exit                  },
        { "reset_editor_state",             &Mcp_server::action_reset_editor_state            },
        { "get_async_status",               &Mcp_server::query_async_status                   },
        { "get_transform_update_stats",     &Mcp_server::query_transform_update_stats         },
        { "merge_static_subtree",           &Mcp_server::action_merge_static_subtree          },
        { "get_shadow_fit_debug",           &Mcp_server::query_shadow_fit_debug               },
        { "raycast",                        &Mcp_server::query_raycast                        },
        { "geometry_query",                 &Mcp_server::query_geometry_batch                 },
        { "select_items",                   &Mcp_server::action_select_items                  },
        { "set_active_item",                &Mcp_server::action_set_active_item                },
        { "attach_selection_to_active",     &Mcp_server::action_attach_selection_to_active    },
        { "delete_nodes",                   &Mcp_server::action_delete_nodes                  },
        { "set_item_flags",                 &Mcp_server::action_set_item_flags                },
        { "lightmap_bake_gbuffer",          &Mcp_server::action_lightmap_bake_gbuffer         },
        { "lightmap_bake_direct",           &Mcp_server::action_lightmap_bake_direct          },
        { "lightmap_set_baking",            &Mcp_server::action_lightmap_set_baking           },
        { "lightmap_bake_to_disk",          &Mcp_server::action_lightmap_bake_to_disk         },
        { "lightmap_save_all_tiles",        &Mcp_server::action_lightmap_save_all_tiles      },
        { "lightmap_clear_tiles",           &Mcp_server::action_lightmap_clear_tiles         },
        { "lightmap_prepare_tiles",         &Mcp_server::action_lightmap_prepare_tiles        },
        { "lightmap_revert_tiles",          &Mcp_server::action_lightmap_revert_tiles         },
        { "lightmap_prepare_cancel",        &Mcp_server::action_lightmap_prepare_cancel       },
        { "lightmap_set_render",            &Mcp_server::action_lightmap_set_render           },
        { "lightmap_frame_selection",       &Mcp_server::action_lightmap_frame_selection      },
        { "lightmap_get_tiles",             &Mcp_server::query_lightmap_tiles                 },
        { "lightmap_subdivide_tile",        &Mcp_server::action_lightmap_subdivide_tile       },
        { "lightmap_merge_tile",            &Mcp_server::action_lightmap_merge_tile           },
        { "lightmap_reorder_charts",        &Mcp_server::action_lightmap_reorder_charts       },
        { "get_active_scene",               &Mcp_server::query_active_scene                   },
        { "set_active_scene",               &Mcp_server::action_set_active_scene              },
        { "transform_selection",            &Mcp_server::action_transform_selection           },
        { "drag_selection",                 &Mcp_server::action_drag_selection                },
        { "physics_drag",                   &Mcp_server::action_physics_drag                  },
        { "set_node_transform",             &Mcp_server::action_set_node_transform            },
        { "ik_drag",                        &Mcp_server::action_ik_drag                       },
        { "select_bones",                   &Mcp_server::action_select_bones                  },
        { "flip_bone_names",                &Mcp_server::action_flip_bone_names               },
        { "clear_pose",                     &Mcp_server::action_clear_pose                    },
        { "copy_pose",                      &Mcp_server::action_copy_pose                     },
        { "paste_pose",                     &Mcp_server::action_paste_pose                    },
        { "create_bone",                    &Mcp_server::action_create_bone                   },
        { "extrude_bones",                  &Mcp_server::action_extrude_bones                 },
        { "subdivide_bones",                &Mcp_server::action_subdivide_bones               },
        { "delete_bones",                   &Mcp_server::action_delete_bones                  },
        { "symmetrize_bones",               &Mcp_server::action_symmetrize_bones              },
        { "recalculate_bone_roll",          &Mcp_server::action_recalculate_bone_roll         },
        { "align_bones",                    &Mcp_server::action_align_bones                   },
        { "bind_mesh_to_bones",             &Mcp_server::action_bind_mesh_to_bones            },
        { "place_brush",                    &Mcp_server::action_place_brush                   },
        { "place_brush_instances",          &Mcp_server::action_place_brush_instances         },
        { "create_shape",                   &Mcp_server::action_create_shape                  },
        { "create_node",                    &Mcp_server::action_create_node                   },
        { "create_light",                   &Mcp_server::action_create_light                  },
        { "create_skin",                    &Mcp_server::action_create_skin                   },
        { "edit_light",                     &Mcp_server::action_edit_light                    },
        { "edit_camera",                    &Mcp_server::action_edit_camera                   },
        { "toggle_physics",                 &Mcp_server::action_toggle_physics                },
        { "advance_time",                   &Mcp_server::action_advance_time                  },
        { "set_log_levels",                 &Mcp_server::action_set_log_levels                },
        { "apply_physics_force",            &Mcp_server::action_apply_physics_force           },
        { "create_child_prim",              &Mcp_server::action_create_child_prim             },
        { "reparent_item",                  &Mcp_server::action_reparent_item                 },
        { "create_scope",                   &Mcp_server::action_create_scope                  },
        { "clipboard_copy_nodes",           &Mcp_server::action_clipboard_copy_nodes          },
        { "clipboard_paste",                &Mcp_server::action_clipboard_paste               },
        { "lock_items",                     &Mcp_server::action_lock_items                    },
        { "unlock_items",                   &Mcp_server::action_unlock_items                  },
        { "add_tags",                       &Mcp_server::action_add_tags                      },
        { "remove_tags",                    &Mcp_server::action_remove_tags                   },
        { "edit_material",                  &Mcp_server::action_edit_material                 },
        { "create_material",                &Mcp_server::action_create_material               },
        { "assign_mesh_material",           &Mcp_server::action_assign_mesh_material          },
        { "copy_library_item",              &Mcp_server::action_copy_library_item             },
        { "set_scene_settings",             &Mcp_server::action_set_scene_settings            },
        { "set_graphics_settings",          &Mcp_server::action_set_graphics_settings         },
        { "set_graphics_preset",            &Mcp_server::action_set_graphics_preset           },
        { "select_variant",                 &Mcp_server::action_select_variant                },
        { "save_scene",                     &Mcp_server::action_save_scene                    },
        { "load_scene",                     &Mcp_server::action_load_scene                    },
        { "open_scene",                     &Mcp_server::action_open_scene                    },
        { "frame_scene",                    &Mcp_server::action_frame_scene                   },
        { "close_scene",                    &Mcp_server::action_close_scene                   },
        { "create_scene",                   &Mcp_server::action_create_scene                  },
        { "export_gltf",                    &Mcp_server::action_export_gltf                   },
        { "import_gltf",                    &Mcp_server::action_import_gltf                   },
        { "scan_gltf",                      &Mcp_server::query_scan_gltf                      },
        { "import_usd",                     &Mcp_server::action_import_usd                    },
        { "describe_usd_file",              &Mcp_server::query_describe_usd_file              },
        { "query_asset_manager",            &Mcp_server::query_asset_manager                  },
        { "acquire_asset",                  &Mcp_server::action_acquire_asset                 },
        { "release_asset",                  &Mcp_server::action_release_asset                 },
        { "unload_asset",                   &Mcp_server::action_unload_asset                  },
        { "set_tool_asset",                 &Mcp_server::action_set_tool_asset                },
        { "set_inventory_slot",             &Mcp_server::action_set_inventory_slot            },
        { "save_container",                 &Mcp_server::action_save_container                },
        { "load_asset_file",                &Mcp_server::action_load_asset_file               },
        { "reference_asset_into_scene",     &Mcp_server::action_reference_asset_into_scene    },
        { "make_asset_external",            &Mcp_server::action_make_asset_external           },
        { "make_asset_internal",            &Mcp_server::action_make_asset_internal           },
        { "instantiate_prefab",             &Mcp_server::action_instantiate_prefab            },
        { "reload_prefab",                  &Mcp_server::action_reload_prefab                 },
        { "set_prefab_template_property",   &Mcp_server::action_set_prefab_template_property   },
        { "get_prefabs",                    &Mcp_server::query_prefabs                        },
        { "capture_screenshot",             &Mcp_server::action_capture_screenshot            },
        { "render_scene_image",             &Mcp_server::action_render_scene_image            },
        { "request_renderdoc_capture",      &Mcp_server::action_request_renderdoc_capture     },
        { "push_shader_debug",              &Mcp_server::action_push_shader_debug             },
        { "pop_shader_debug",               &Mcp_server::action_pop_shader_debug              },
        { "wake_physics_bodies",            &Mcp_server::action_wake_physics_bodies           },
        { "get_physics_items",              &Mcp_server::query_physics_items                  },
        { "get_draw_lists",                 &Mcp_server::query_draw_lists                     },
        { "set_draw_lists_enabled",         &Mcp_server::action_set_draw_lists_enabled        },
        { "reset_composition_pass_stats",   &Mcp_server::action_reset_composition_pass_stats  },
        { "get_physics_state",              &Mcp_server::query_get_physics_state              },
        { "create_physics_body",            &Mcp_server::action_create_physics_body           },
        { "edit_physics_body",              &Mcp_server::action_edit_physics_body             },
        { "create_joint",                   &Mcp_server::action_create_joint                  },
        { "edit_joint",                     &Mcp_server::action_edit_joint                    },
        { "create_physics_material",        &Mcp_server::action_create_physics_material       },
        { "edit_physics_material",          &Mcp_server::action_edit_physics_material         },
        { "create_collision_filter",        &Mcp_server::action_create_collision_filter       },
        { "edit_collision_filter",          &Mcp_server::action_edit_collision_filter         },
        { "create_physics_joint_settings",  &Mcp_server::action_create_physics_joint_settings },
        { "edit_physics_joint_settings",    &Mcp_server::action_edit_physics_joint_settings   },
        { "set_joint_constraint_visualization", &Mcp_server::action_set_joint_constraint_visualization },
        { "get_joint_constraint_state",     &Mcp_server::query_joint_constraint_state         },
        { "set_mesh_component_mode",        &Mcp_server::action_set_mesh_component_mode       },
        { "select_mesh_components",         &Mcp_server::action_select_mesh_components        },
        { "grow_mesh_selection",            &Mcp_server::action_grow_mesh_selection           },
        { "shrink_mesh_selection",          &Mcp_server::action_shrink_mesh_selection         },
        { "select_all_mesh_components",     &Mcp_server::action_select_all_mesh_components    },
        { "invert_mesh_selection",          &Mcp_server::action_invert_mesh_selection         },
        { "select_linked_mesh_components",  &Mcp_server::action_select_linked_mesh_components },
        { "select_mesh_loop",               &Mcp_server::action_select_mesh_loop              },
        { "get_mesh_component_selection",   &Mcp_server::query_mesh_component_selection       },
        { "get_id_range_mapping",           &Mcp_server::query_id_range_mapping               },
        { "debug_region_select",            &Mcp_server::action_debug_region_select           },
        { "get_mesh_geometry_info",         &Mcp_server::query_mesh_geometry_info             },
        { "get_mesh_attribute_values",      &Mcp_server::query_mesh_attribute_values          },
        { "set_mesh_attribute_values",      &Mcp_server::action_set_mesh_attribute_values     },
        { "get_mesh_buffer_info",           &Mcp_server::query_mesh_buffer_info               },
        { "get_mesh_buffer_data",           &Mcp_server::query_mesh_buffer_data               },
        { "clear_mesh_component_selection", &Mcp_server::action_clear_mesh_component_selection},
        { "set_edge_sharpness",             &Mcp_server::action_set_edge_sharpness            },
        { "catmull_clark",                  &Mcp_server::action_catmull_clark                 },
        { "align_components",               &Mcp_server::action_align_components              },
        { "add_joint",                      &Mcp_server::action_add_joint                     },
        { "flip_joint",                     &Mcp_server::action_flip_joint                    },
        { "remesh",                         &Mcp_server::action_remesh                        },
        { "decimate",                       &Mcp_server::action_decimate                      },
        { "smooth",                         &Mcp_server::action_smooth                        },
        { "chamfer",                        &Mcp_server::action_chamfer3                      },
        { "csg",                            &Mcp_server::action_csg                           },
        { "lattice_deform",                 &Mcp_server::action_lattice_deform                },
        { "project_texcoords",              &Mcp_server::action_project_texcoords             },
        { "merge_faces",                    &Mcp_server::action_merge_faces                   },
        { "delete_mesh_components",         &Mcp_server::action_delete_mesh_components        },
        { "dissolve_mesh_components",       &Mcp_server::action_dissolve_mesh_components      },
        { "dissolve_limited",               &Mcp_server::action_dissolve_limited              },
        { "merge_mesh_vertices",            &Mcp_server::action_merge_mesh_vertices           },
        { "merge_mesh_by_distance",         &Mcp_server::action_merge_mesh_by_distance        },
        { "subdivide_mesh_edges",           &Mcp_server::action_subdivide_mesh_edges          },
        { "generate_texture_coordinates",   &Mcp_server::action_generate_texture_coordinates  },
        { "set_transform_reference_mode",   &Mcp_server::action_set_transform_reference_mode  },
        { "set_transform_mode",             &Mcp_server::action_set_transform_mode            },
        { "slide_mesh_components",          &Mcp_server::action_slide_mesh_components         },
        { "cancel_component_edit",          &Mcp_server::action_cancel_component_edit         },
        { "loop_cut_mesh",                  &Mcp_server::action_loop_cut_mesh                 },
        { "inset_mesh_faces",               &Mcp_server::action_inset_mesh_faces              },
        { "bevel_mesh_edges",               &Mcp_server::action_bevel_mesh_edges              },
        { "knife_cut_mesh",                 &Mcp_server::action_knife_cut_mesh                },
        { "split_mesh_components",          &Mcp_server::action_split_mesh_components         },
        { "rip_mesh_vertices",              &Mcp_server::action_rip_mesh_vertices             },
        { "separate_mesh_selection",        &Mcp_server::action_separate_mesh_selection       },
        { "fill_mesh_selection",            &Mcp_server::action_fill_mesh_selection           },
        { "connect_mesh_vertices",          &Mcp_server::action_connect_mesh_vertices         },
        { "bridge_mesh_loops",              &Mcp_server::action_bridge_mesh_loops             },
        { "flip_mesh_facets",               &Mcp_server::action_flip_mesh_facets              },
        { "recalculate_mesh_normals",       &Mcp_server::action_recalculate_mesh_normals      },
        { "smooth_mesh_vertices",           &Mcp_server::action_smooth_mesh_vertices          },
        { "set_gizmo_visibility",           &Mcp_server::action_set_gizmo_visibility          },
        { "get_transform_state",            &Mcp_server::query_transform_state                },
        { "get_editor_references",          &Mcp_server::query_editor_references              },
        { "get_memory_usage",               &Mcp_server::query_memory_usage                   },
        { "free_undone_loads",              &Mcp_server::action_free_undone_loads             },
        { "debug_set_item_tree_hover",      &Mcp_server::action_debug_set_item_tree_hover     },
        { "debug_set_transform_hover",      &Mcp_server::action_debug_set_transform_hover     },
        { "inject_input_events",            &Mcp_server::action_inject_input_events           },
        { "mouse_click",                    &Mcp_server::action_mouse_click                   },
        { "mouse_drag",                     &Mcp_server::action_mouse_drag                    },
        { "mouse_release",                  &Mcp_server::action_mouse_release                 },
        { "mouse_wheel",                    &Mcp_server::action_mouse_wheel                   },
        { "key_press",                      &Mcp_server::action_key_press                     },
        { "type_text",                      &Mcp_server::action_type_text                     },
        { "get_input_state",                &Mcp_server::query_input_state                    },
        { "list_input_bindings",            &Mcp_server::query_input_bindings                 },
        { "set_command_bindings",           &Mcp_server::action_set_command_bindings          },
        { "reset_command_bindings",         &Mcp_server::action_reset_command_bindings        },
        { "get_transform_handles",          &Mcp_server::query_transform_handles              },
        { "get_transform_rotation",         &Mcp_server::query_transform_rotation             },
        { "get_geometry_spreadsheet",       &Mcp_server::query_geometry_spreadsheet           },
        { "get_imgui_hosts",                &Mcp_server::query_imgui_hosts                    },
        { "get_imgui_windows",              &Mcp_server::query_imgui_windows                  },
        { "get_imgui_items",                &Mcp_server::query_imgui_items                    },
        { "get_imgui_item_rect",            &Mcp_server::query_imgui_item_rect                },
        { "imgui_click",                    &Mcp_server::action_imgui_click                   },
        { "imgui_hover",                    &Mcp_server::action_imgui_hover                   },
        { "imgui_scroll",                   &Mcp_server::action_imgui_scroll                  },
        { "open_four_view",                 &Mcp_server::action_open_four_view                },
        { "get_four_views",                 &Mcp_server::query_four_views                     },
        { "get_geometry_graph",             &Mcp_server::query_geometry_graph                 },
        { "set_geometry_graph_target",      &Mcp_server::action_set_geometry_graph_target     },
        { "geometry_graph_add_node",        &Mcp_server::action_geometry_graph_add_node       },
        { "geometry_graph_remove_node",     &Mcp_server::action_geometry_graph_remove_node    },
        { "geometry_graph_set_parameter",   &Mcp_server::action_geometry_graph_set_parameter  },
        { "geometry_graph_set_display_flags", &Mcp_server::action_geometry_graph_set_display_flags },
        { "geometry_graph_set_node_previews", &Mcp_server::action_geometry_graph_set_node_previews },
        { "geometry_graph_connect",         &Mcp_server::action_geometry_graph_connect        },
        { "geometry_graph_disconnect",      &Mcp_server::action_geometry_graph_disconnect     },
        { "geometry_graph_set_link_mid_points", &Mcp_server::action_geometry_graph_set_link_mid_points },
        { "geometry_graph_set_link_curve",  &Mcp_server::action_geometry_graph_set_link_curve },
        { "geometry_graph_set_view",        &Mcp_server::action_geometry_graph_set_view       },
        { "texture_graph_set_view",         &Mcp_server::action_texture_graph_set_view        },
        { "texture_graph_add_all",          &Mcp_server::action_texture_graph_add_all         },
        { "geometry_graph_select_nodes",    &Mcp_server::action_geometry_graph_select_nodes   },
        { "geometry_graph_set_node_layout", &Mcp_server::action_geometry_graph_set_node_layout},
        { "create_graph_texture",           &Mcp_server::action_create_graph_texture          },
        { "set_material_texture_source",    &Mcp_server::action_set_material_texture_source   },
        { "get_graph_textures",             &Mcp_server::query_graph_textures                 },
        { "get_scene_node_graphs",          &Mcp_server::query_scene_node_graphs              },
        { "create_graph_mesh",              &Mcp_server::action_create_graph_mesh             },
        { "set_node_graph_mesh",            &Mcp_server::action_set_node_graph_mesh           },
        { "get_graph_meshes",               &Mcp_server::query_graph_meshes                   },
        { "get_texture_graph",              &Mcp_server::query_texture_graph                  },
        { "set_texture_graph_target",       &Mcp_server::action_set_texture_graph_target      },
        { "texture_graph_add_node",         &Mcp_server::action_texture_graph_add_node        },
        { "texture_graph_remove_node",      &Mcp_server::action_texture_graph_remove_node     },
        { "texture_graph_set_parameter",    &Mcp_server::action_texture_graph_set_parameter   },
        { "texture_graph_connect",          &Mcp_server::action_texture_graph_connect         },
        { "texture_graph_disconnect",       &Mcp_server::action_texture_graph_disconnect      },
        { "texture_graph_export_png",       &Mcp_server::action_texture_graph_export_png      },
        { "texture_graph_export_material",  &Mcp_server::action_texture_graph_export_material },
        { "texture_graph_select_nodes",     &Mcp_server::action_texture_graph_select_nodes    },
        { "texture_graph_set_node_layout",  &Mcp_server::action_texture_graph_set_node_layout },
        { "open_geometry_graph_window",     &Mcp_server::action_open_geometry_graph_window    },
        { "open_texture_graph_window",      &Mcp_server::action_open_texture_graph_window     },
        { "open_properties_window",         &Mcp_server::action_open_properties_window        },
        { "get_scene_animations",           &Mcp_server::query_scene_animations               },
        { "set_animation_target",           &Mcp_server::action_set_animation_target          },
        { "animation_playback",             &Mcp_server::action_animation_playback            },
        { "animation_edit_keyframe",        &Mcp_server::action_animation_edit_keyframe       },
        { "animation_create_key",           &Mcp_server::action_animation_create_key          },
        { "animation_delete_key",           &Mcp_server::action_animation_delete_key          },
        { "get_item_properties",            &Mcp_server::query_item_properties                },
        { "get_addable_item_properties",    &Mcp_server::query_addable_item_properties        },
        { "set_item_property",              &Mcp_server::action_set_item_property             },
        { "set_item_style",                 &Mcp_server::action_set_item_style                },
        { "create_style",                   &Mcp_server::action_create_style                  },
        { "clear_item_style",               &Mcp_server::action_clear_item_style              },
        { "set_ray_trace",                  &Mcp_server::action_set_ray_trace                 },
        { "set_ddgi",                       &Mcp_server::action_set_ddgi                      },
        { "set_indirect_diffuse",           &Mcp_server::action_set_indirect_diffuse          },
        { "set_radiance_cascades",          &Mcp_server::action_set_radiance_cascades         },
        { "get_indirect_diffuse_stats",     &Mcp_server::query_indirect_diffuse_stats         },
        { "get_radiance_cascades_texels",   &Mcp_server::query_radiance_cascades_texels       },
        { "sample_indirect_diffuse",        &Mcp_server::query_sample_indirect_diffuse        },
        { "reference_indirect_diffuse",     &Mcp_server::query_reference_indirect_diffuse     },
    };
    return c_tool_dispatch;
}

auto Mcp_server::dispatch_tool_call(const std::string& tool_name, const json& arguments) -> std::string
{
    for (const Tool_dispatch_entry& entry : get_dispatch_table()) {
        if (tool_name == entry.name) {
            return (this->*entry.handler)(arguments);
        }
    }
    // Not a statically dispatched tool: the remaining tools are the editor
    // commands refresh_tool_list() appends at runtime.
    return execute_command(tool_name);
}

auto Mcp_server::action_batch(const json& args) -> std::string
{
    const auto calls_it = args.find("calls");
    if ((calls_it == args.end()) || !calls_it->is_array() || calls_it->empty()) {
        json r = make_text_content("calls must be a non-empty array of {tool, arguments} objects");
        r["isError"] = true;
        return r.dump();
    }
    constexpr std::size_t k_max_batch_calls = 1024;
    if (calls_it->size() > k_max_batch_calls) {
        json r = make_text_content("Batch too large (max " + std::to_string(k_max_batch_calls) + " calls)");
        r["isError"] = true;
        return r.dump();
    }

    // Validate every entry's shape up front so a malformed entry cannot
    // leave half a batch applied.
    for (const json& entry : *calls_it) {
        if (!entry.is_object() || !entry.contains("tool") || !entry["tool"].is_string()) {
            json r = make_text_content("Each batch entry must be an object with a string 'tool'");
            r["isError"] = true;
            return r.dump();
        }
        const std::string tool = entry["tool"].get<std::string>();
        if (tool == "batch") {
            json r = make_text_content("batch cannot be nested");
            r["isError"] = true;
            return r.dump();
        }
        // render_scene_image spans several frames (render, then readback) and
        // owns rendergraph nodes meanwhile; a batch answers within one pass.
        if (tool == "render_scene_image") {
            json r = make_text_content("render_scene_image needs several rendered frames and cannot run inside a batch; call it on its own");
            r["isError"] = true;
            return r.dump();
        }
        if (entry.contains("arguments") && !entry["arguments"].is_object()) {
            json r = make_text_content("Batch entry 'arguments' must be an object when present");
            r["isError"] = true;
            return r.dump();
        }
    }

    // All sub-call operations become ONE undo entry. The group is closed
    // before returning on every path; sub-call exceptions are caught below.
    Operation_stack* const operation_stack = m_context.operation_stack;
    if (operation_stack != nullptr) {
        operation_stack->begin_group();
    }

    json        results     = json::array();
    std::size_t error_count = 0;
    for (const json& entry : *calls_it) {
        const std::string tool      = entry["tool"].get<std::string>();
        const json        arguments = entry.value("arguments", json::object());

        std::string sub_result;
        try {
            sub_result = dispatch_tool_call(tool, arguments);
        } catch (const std::exception& e) {
            log_mcp->error("MCP server: batch handler for '{}' threw: {}", tool, e.what());
            sub_result = make_error_content(std::string{"Handler '"} + tool + "' threw an exception: " + e.what());
        } catch (...) {
            log_mcp->error("MCP server: batch handler for '{}' threw a non-standard exception", tool);
            sub_result = make_error_content(std::string{"Handler '"} + tool + "' threw a non-standard exception");
        }
        if (m_defer_current_request) {
            // Deferral re-runs a whole queued request on the next frame;
            // that cannot be honored for one call inside a batch.
            m_defer_current_request = false;
            sub_result = make_error_content("Tool '" + tool + "' needs a rendered frame and cannot run inside a batch; call it on its own");
        }

        // Sub-results are the handlers' MCP content envelopes; unwrap the
        // text payload (JSON when the handler produced JSON) so the batch
        // response nests cleanly.
        json parsed  = json::parse(sub_result, nullptr, false);
        bool is_error = true;
        json payload  = sub_result;
        if (parsed.is_object()) {
            is_error = parsed.value("isError", false);
            const auto content_it = parsed.find("content");
            if ((content_it != parsed.end()) && content_it->is_array() && !content_it->empty() && content_it->front().contains("text")) {
                const std::string text = content_it->front()["text"].get<std::string>();
                json text_parsed = json::parse(text, nullptr, false);
                payload = text_parsed.is_discarded() ? json(text) : text_parsed;
            }
        }
        if (is_error) {
            ++error_count;
        }
        results.push_back({
            {"tool",   tool},
            {"ok",     !is_error},
            {"result", payload}
        });
    }

    const std::size_t grouped = (operation_stack != nullptr) ? operation_stack->end_group() : 0;

    json r = make_json_content({
        {"count",              results.size()},
        {"error_count",        error_count},
        {"grouped_operations", grouped},
        {"results",            results}
    });
    if (error_count > 0) {
        // The successful sub-calls stay applied; the full per-call results
        // are in the content text either way.
        r["isError"] = true;
    }
    return r.dump();
}

auto Mcp_server::action_set_ray_trace(const json& args) -> std::string
{
    // GPU ray tracing (issue #233): toggle Ray_trace_renderer without the
    // ImGui checkbox so the headless verify loop can exercise it; optionally
    // show the Ray Trace window so capture_screenshot sees the output.
    Ray_trace_renderer* renderer = m_context.ray_trace_renderer;
    if (renderer == nullptr) {
        return make_error_content("Ray trace renderer not available");
    }
    if (args.contains("enabled")) {
        renderer->set_enabled(args.value("enabled", false));
    }
    // Optional Ray_trace_config edits (same knobs as the Ray Trace window;
    // autosaved with the editor settings).
    if (m_context.editor_settings != nullptr) {
        Ray_trace_config& config = m_context.editor_settings->ray_trace;
        if (args.contains("downscale")) {
            config.downscale = std::clamp(args.value("downscale", 1.0f), 1.0f, 8.0f);
        }
        if (args.contains("max_rays")) {
            config.max_rays = std::clamp(args.value("max_rays", 24), 1, 1024);
        }
        if (args.contains("max_bounces")) {
            config.max_bounces = std::clamp(args.value("max_bounces", 8), 0, 12);
        }
    }
    if (args.value("show_window", false) && (m_context.imgui_windows != nullptr)) {
        for (erhe::imgui::Imgui_window* window : m_context.imgui_windows->get_windows()) {
            if (window->get_ini_label() == "ray_trace") {
                window->show_window();
            }
        }
    }
    json result{
        {"supported",      renderer->is_supported()},
        {"enabled",        renderer->is_enabled()},
        {"instance_count", renderer->get_instance_count()}
    };
    if (m_context.editor_settings != nullptr) {
        const Ray_trace_config& config = m_context.editor_settings->ray_trace;
        result["downscale"]   = config.downscale;
        result["max_rays"]    = config.max_rays;
        result["max_bounces"] = config.max_bounces;
    }
    const std::string save_path = args.value("save_path", "");
    if (!save_path.empty()) {
        std::vector<uint8_t> pixels;
        if (!renderer->read_output_rgba8(pixels)) {
            return make_error_content("Ray trace output readback failed (renderer not supported, or no output texture)");
        }
        // The output texture stores LINEAR tonemapped color (the in-editor
        // display path encodes at swapchain write, like every viewport
        // texture). Encode to sRGB here so the diagnostic PNG is viewable
        // in external tools.
        {
            std::array<uint8_t, 256> linear_to_srgb_lut{};
            for (std::size_t i = 0; i < linear_to_srgb_lut.size(); ++i) {
                const float x = static_cast<float>(i) / 255.0f;
                const float y = (x <= 0.0031308f) ? (12.92f * x) : (1.055f * std::pow(x, 1.0f / 2.4f) - 0.055f);
                linear_to_srgb_lut[i] = static_cast<uint8_t>(std::lround(std::clamp(y, 0.0f, 1.0f) * 255.0f));
            }
            for (std::size_t i = 0, end = pixels.size(); i + 3 < end; i += 4) {
                pixels[i + 0] = linear_to_srgb_lut[pixels[i + 0]];
                pixels[i + 1] = linear_to_srgb_lut[pixels[i + 1]];
                pixels[i + 2] = linear_to_srgb_lut[pixels[i + 2]];
                // alpha unchanged
            }
        }
        const std::shared_ptr<erhe::graphics::Texture> texture = renderer->get_output_texture();
        const int width      = texture->get_width();
        const int height     = texture->get_height();
        const int row_stride = width * 4;
        std::unique_ptr<erhe::graphics::Image_writer> writer = erhe::graphics::Image_writer::create();
        const std::span<const std::byte> data{reinterpret_cast<const std::byte*>(pixels.data()), pixels.size()};
        if (!writer->write_png(std::filesystem::path{save_path}, width, height, row_stride, texture->get_pixelformat(), data)) {
            return make_error_content("Failed to write PNG '" + save_path + "' (image writer backend may be disabled)");
        }
        result["saved_path"] = save_path;
        result["width"]      = width;
        result["height"]     = height;
    }
    return make_json_content(result).dump();
}

auto Mcp_server::action_set_ddgi(const json& args) -> std::string
{
    // Dynamic diffuse global illumination (doc/editor/ddgi.md): toggle and
    // tune DDGI without the ImGui widgets, and read back what the renderer
    // made of the settings, so the headless verify loop can exercise it.
    Ddgi_renderer* renderer = m_context.ddgi_renderer;
    if (renderer == nullptr) {
        return make_error_content("DDGI renderer not available");
    }
    if (m_context.editor_settings != nullptr) {
        Ddgi_config& config = m_context.editor_settings->ddgi;
        // 'enabled' is shorthand for the source selection
        // (set_indirect_diffuse): true selects DDGI, false returns a DDGI
        // selection to ambient and leaves any other source alone.
        if (args.contains("enabled")) {
            const Indirect_diffuse_source source = m_context.editor_settings->indirect_diffuse_source;
            if (args.value("enabled", false)) {
                set_indirect_diffuse_source(m_context, Indirect_diffuse_source::ddgi);
            } else if (source == Indirect_diffuse_source::ddgi) {
                set_indirect_diffuse_source(m_context, Indirect_diffuse_source::ambient);
            }
        }
        if (args.contains("probe_spacing_m")) {
            config.probe_spacing_m = std::clamp(args.value("probe_spacing_m", 1.5f), 0.01f, 64.0f);
        }
        if (args.contains("volume_padding_m")) {
            config.volume_padding_m = std::max(0.0f, args.value("volume_padding_m", 1.0f));
        }
        if (args.contains("max_probes")) {
            config.max_probes = std::clamp(args.value("max_probes", 4096), 8, 262144);
        }
        if (args.contains("rays_per_probe")) {
            config.rays_per_probe = std::clamp(args.value("rays_per_probe", 128), 8, 1024);
        }
        if (args.contains("hysteresis")) {
            config.hysteresis = std::clamp(args.value("hysteresis", 0.97f), 0.0f, 0.999f);
        }
        if (args.contains("intensity")) {
            config.intensity = std::max(0.0f, args.value("intensity", 1.0f));
        }
        if (args.contains("debug_draw_probes")) {
            config.debug_draw_probes = args.value("debug_draw_probes", false);
        }
        if (args.contains("bounces")) {
            const json& value = args["bounces"];
            Indirect_diffuse_bounces parsed{};
            if (!value.is_string() || !from_string(value.get<std::string>(), parsed)) {
                return make_error_content("set_ddgi: 'bounces' must be \"single\" or \"multi\"");
            }
            config.bounces = parsed;
        }
    }
    if (args.value("show_window", false) && (m_context.imgui_windows != nullptr)) {
        for (erhe::imgui::Imgui_window* window : m_context.imgui_windows->get_windows()) {
            if (window->get_ini_label() == "ddgi") {
                window->show_window();
            }
        }
    }
    const Ddgi_renderer::Grid& grid = renderer->get_grid();
    json result{
        {"supported",         renderer->is_supported()},
        {"active",            renderer->is_active()},
        {"probe_counts",      json::array({grid.counts.x, grid.counts.y, grid.counts.z})},
        {"probe_count",       grid.get_probe_count()},
        {"probe_spacing",     json::array({grid.spacing.x, grid.spacing.y, grid.spacing.z})},
        {"grid_origin",       json::array({grid.origin.x, grid.origin.y, grid.origin.z})},
        {"rays_per_probe",    renderer->get_rays_per_probe()},
        {"irradiance_texels", renderer->get_irradiance_texels()},
        {"distance_texels",   renderer->get_distance_texels()},
        {"texture_bytes",     renderer->get_texture_byte_count()}
    };
    if (m_context.editor_settings != nullptr) {
        const Ddgi_config& config = m_context.editor_settings->ddgi;
        result["enabled"]    = (m_context.editor_settings->indirect_diffuse_source == Indirect_diffuse_source::ddgi);
        result["hysteresis"] = config.hysteresis;
        result["intensity"]         = config.intensity;
        result["debug_draw_probes"] = config.debug_draw_probes;
        result["bounces"]           = std::string{to_string(config.bounces)};
    }
    return make_json_content(result).dump();
}

namespace {

// The radiance cascades layout, memory and trace / merge cost
// (doc/editor/radiance_cascades.md "MCP").
[[nodiscard]] auto radiance_cascades_stats_json(const Radiance_cascades_renderer& renderer) -> json
{
    const Radiance_cascades_layout& layout = renderer.get_layout();
    json cascades = json::array();
    for (int i = 0; i < layout.cascade_count; ++i) {
        const Radiance_cascade& cascade = layout.cascades[static_cast<std::size_t>(i)];
        cascades.push_back(json{
            {"index",         i},
            {"grid_origin",   json::array({cascade.grid.origin.x,  cascade.grid.origin.y,  cascade.grid.origin.z })},
            {"grid_spacing",  json::array({cascade.grid.spacing.x, cascade.grid.spacing.y, cascade.grid.spacing.z})},
            {"grid_counts",   json::array({cascade.grid.counts.x,  cascade.grid.counts.y,  cascade.grid.counts.z })},
            {"probe_count",   cascade.get_probe_count()},
            {"tile_texels",   cascade.tile_texels},
            {"interval",      json::array({cascade.interval_start, cascade.interval_end})},
            {"texels",        cascade.get_texel_count()},
            {"atlas_size",    json::array({cascade.get_atlas_width(), cascade.get_atlas_height()})},
            {"texture_bytes", renderer.get_cascade_texture_byte_count(i)}
        });
    }
    const Radiance_cascades_renderer::Stats stats = renderer.get_stats();
    // The probe overlay's last retired copy (doc/editor/radiance_cascades.md
    // "Probe overlay"); readback_count stays put while the overlay is off.
    const Radiance_cascades_renderer::Probe_overlay_summary& overlay = renderer.get_probe_overlay_summary();
    const json probe_overlay = json{
        {"readback_count", stats.probe_overlay_readback_count},
        {"cascade",        overlay.cascade},
        {"probe_count",    overlay.probe_count},
        {"active",         overlay.active},
        {"inside",         overlay.inside},
        {"unclassified",   overlay.unclassified},
        {"irradiance",     overlay.irradiance},
        {"update_count",   overlay.update_count}
    };
    const auto pass_time_json = [](const Radiance_cascades_renderer::Pass_time& time) -> json {
        return json{
            {"last_ms",    time.last_ms},
            {"average_ms", time.average_ms}
        };
    };
    // The probe field the reduce pass writes: cascade 0's grid in the DDGI
    // atlas format (doc/editor/radiance_cascades.md "Reduce").
    json field = nullptr;
    if (renderer.has_field()) {
        const erhe::scene_renderer::Ddgi_parameters parameters = renderer.get_forward_parameters();
        field = json{
            {"grid_origin",       json::array({parameters.grid_origin.x,  parameters.grid_origin.y,  parameters.grid_origin.z })},
            {"grid_spacing",      json::array({parameters.grid_spacing.x, parameters.grid_spacing.y, parameters.grid_spacing.z})},
            {"grid_counts",       json::array({parameters.grid_counts.x,  parameters.grid_counts.y,  parameters.grid_counts.z })},
            {"irradiance_texels", parameters.irradiance_texels},
            {"distance_texels",   parameters.distance_texels},
            {"depth_sharpness",   parameters.depth_sharpness},
            {"tiles_per_row",     parameters.tiles_per_row}
        };
    }
    return json{
        {"supported",                renderer.is_supported()},
        {"active",                   renderer.is_active()},
        {"has_field",                renderer.has_field()},
        {"merge_mode",               std::string{to_string(renderer.get_merge_mode())}},
        {"cascade_count",            layout.cascade_count},
        {"r0",                       layout.r0},
        {"probe_count",              layout.get_total_probes()},
        {"texels",                   layout.get_total_texels()},
        {"texture_bytes",            renderer.get_texture_byte_count()},
        {"fit_count",                renderer.get_fit_count()},
        {"cascades",                 cascades},
        {"update_count",             stats.update_count},
        {"timing_sample_count",      stats.timing_sample_count},
        {"completed_sweeps",         stats.completed_sweeps},
        {"texels_per_update",        stats.texels_per_update},
        {"rays_per_update",          stats.rays_per_update},
        {"neighbour_rays_per_update", stats.neighbour_rays_per_update},
        {"gpu_ms",                   json{
            {"trace",           pass_time_json(stats.trace)},
            {"neighbour_trace", pass_time_json(stats.neighbour_trace)},
            {"merge",           pass_time_json(stats.merge)},
            {"reduce",          pass_time_json(stats.reduce)}
        }},
        {"gpu_ms_total",             pass_time_json(stats.total)},
        {"timing_history_size",      Radiance_cascades_renderer::c_timing_history_size},
        {"ms_per_million_rays",      stats.ms_per_million_rays},
        {"updates_per_full_refresh", stats.updates_per_full_refresh},
        {"full_refresh_ms",          stats.full_refresh_ms},
        {"visibility",               json{{"last_ms", stats.visibility_last_ms}, {"update_count", stats.visibility_update_count}}},
        {"history_reset_count",      stats.history_reset_count},
        {"probe_overlay",            probe_overlay},
        {"field",                    field}
    };
}

void show_window_by_ini_label(App_context& context, const std::string_view ini_label)
{
    if (context.imgui_windows == nullptr) {
        return;
    }
    for (erhe::imgui::Imgui_window* window : context.imgui_windows->get_windows()) {
        if (window->get_ini_label() == ini_label) {
            window->show_window();
        }
    }
}

} // anonymous namespace

auto Mcp_server::action_set_indirect_diffuse(const json& args) -> std::string
{
    // Select the producer of the indirect diffuse field
    // (doc/editor/radiance_cascades.md "Source selection"), the MCP
    // counterpart of the Settings window's Indirect Diffuse combo.
    if (m_context.editor_settings == nullptr) {
        return make_error_content("set_indirect_diffuse: editor settings not available");
    }
    if (args.contains("source")) {
        const json& value = args["source"];
        Indirect_diffuse_source parsed{};
        if (!value.is_string() || !from_string(value.get<std::string>(), parsed)) {
            return make_error_content("set_indirect_diffuse: 'source' must be one of \"ambient\", \"ddgi\", \"radiance_cascades\"");
        }
        set_indirect_diffuse_source(m_context, parsed);
    }
    const Indirect_diffuse_source source = m_context.editor_settings->indirect_diffuse_source;
    if (args.value("show_window", false)) {
        if (source == Indirect_diffuse_source::ddgi) {
            show_window_by_ini_label(m_context, "ddgi");
        } else if (source == Indirect_diffuse_source::radiance_cascades) {
            show_window_by_ini_label(m_context, "radiance_cascades");
        }
    }
    const Ddgi_renderer*              ddgi = m_context.ddgi_renderer;
    const Radiance_cascades_renderer* rc   = m_context.radiance_cascades_renderer;
    return make_json_content(json{
        {"source",                      std::string{to_string(source)}},
        {"ddgi_supported",              (ddgi != nullptr) && ddgi->is_supported()},
        {"radiance_cascades_supported", (rc   != nullptr) && rc->is_supported()}
    }).dump();
}

auto Mcp_server::action_set_radiance_cascades(const json& args) -> std::string
{
    // Tune radiance cascades without the ImGui widgets
    // (doc/editor/radiance_cascades.md "MCP"). The renderer refits on its
    // next tick; the result reports the layout of the last fit, so read
    // get_indirect_diffuse_stats a frame later for the new one.
    Radiance_cascades_renderer* renderer = m_context.radiance_cascades_renderer;
    if (renderer == nullptr) {
        return make_error_content("Radiance cascades renderer not available");
    }
    if (m_context.editor_settings != nullptr) {
        Radiance_cascades_config& config = m_context.editor_settings->radiance_cascades;
        if (args.contains("probe_spacing_m")) {
            config.probe_spacing_m = std::clamp(args.value("probe_spacing_m", 0.5f), 0.01f, 64.0f);
        }
        if (args.contains("volume_padding_m")) {
            config.volume_padding_m = std::max(0.0f, args.value("volume_padding_m", 1.0f));
        }
        if (args.contains("max_probes_cascade0")) {
            config.max_probes_cascade0 = std::clamp(args.value("max_probes_cascade0", 65536), 8, 1048576);
        }
        if (args.contains("max_cascades")) {
            config.max_cascades = std::clamp(args.value("max_cascades", 8), 1, c_max_radiance_cascades);
        }
        if (args.contains("cascade0_tile_texels")) {
            config.cascade0_tile_texels = std::clamp(args.value("cascade0_tile_texels", 4), 1, 64);
        }
        if (args.contains("interval_scale")) {
            config.interval_scale = std::clamp(args.value("interval_scale", 1.0f), 1.0f, 64.0f);
        }
        if (args.contains("texels_per_frame")) {
            config.texels_per_frame = std::max(1, args.value("texels_per_frame", 65536));
        }
        if (args.contains("hysteresis")) {
            config.hysteresis = std::clamp(args.value("hysteresis", 0.9f), 0.0f, 0.999f);
        }
        if (args.contains("merge_mode")) {
            const json& value = args["merge_mode"];
            Radiance_cascades_merge_mode parsed{};
            if (!value.is_string() || !from_string(value.get<std::string>(), parsed)) {
                return make_error_content("set_radiance_cascades: 'merge_mode' must be \"interpolate\", \"visibility_masked\" or \"per_neighbour_trace\"");
            }
            config.merge_mode = parsed;
            renderer->set_merge_mode(parsed);
        }
        if (args.contains("debug_cascade_mask")) {
            config.debug_cascade_mask = std::clamp(args.value("debug_cascade_mask", 0), 0, (1 << (Radiance_cascades_renderer::c_sky_mask_bit + 1)) - 1);
        }
        if (args.contains("direction_jitter")) {
            const json& value = args["direction_jitter"];
            Radiance_cascades_direction_jitter parsed{};
            if (!value.is_string() || !from_string(value.get<std::string>(), parsed)) {
                return make_error_content("set_radiance_cascades: 'direction_jitter' must be \"none\" or \"footprint\"");
            }
            config.direction_jitter = parsed;
        }
        if (args.contains("bounces")) {
            const json& value = args["bounces"];
            Indirect_diffuse_bounces parsed{};
            if (!value.is_string() || !from_string(value.get<std::string>(), parsed)) {
                return make_error_content("set_radiance_cascades: 'bounces' must be \"single\" or \"multi\"");
            }
            config.bounces = parsed;
        }
        if (args.contains("debug_draw_probes")) {
            const json& value = args["debug_draw_probes"];
            Radiance_cascades_probe_overlay parsed{};
            if (!value.is_string() || !from_string(value.get<std::string>(), parsed)) {
                return make_error_content("set_radiance_cascades: 'debug_draw_probes' must be \"none\", \"state\" or \"state_and_irradiance\"");
            }
            config.debug_draw_probes = parsed;
        }
        if (args.contains("debug_draw_cascade")) {
            config.debug_draw_cascade = std::clamp(args.value("debug_draw_cascade", 0), 0, c_max_radiance_cascades - 1);
        }
    }
    if (args.value("show_window", false)) {
        show_window_by_ini_label(m_context, "radiance_cascades");
    }
    json result = radiance_cascades_stats_json(*renderer);
    if (m_context.editor_settings != nullptr) {
        const Radiance_cascades_config& config = m_context.editor_settings->radiance_cascades;
        result["config"] = json{
            {"probe_spacing_m",      config.probe_spacing_m},
            {"volume_padding_m",     config.volume_padding_m},
            {"max_probes_cascade0",  config.max_probes_cascade0},
            {"max_cascades",         config.max_cascades},
            {"cascade0_tile_texels", config.cascade0_tile_texels},
            {"interval_scale",       config.interval_scale},
            {"texels_per_frame",     config.texels_per_frame},
            {"hysteresis",           config.hysteresis},
            {"debug_cascade_mask",   config.debug_cascade_mask},
            {"merge_mode",           std::string{to_string(config.merge_mode)}},
            {"direction_jitter",     std::string{to_string(config.direction_jitter)}},
            {"bounces",              std::string{to_string(config.bounces)}},
            {"debug_draw_probes",    std::string{to_string(config.debug_draw_probes)}},
            {"debug_draw_cascade",   config.debug_draw_cascade}
        };
        result["source"] = std::string{to_string(m_context.editor_settings->indirect_diffuse_source)};
    }
    return make_json_content(result).dump();
}

auto Mcp_server::query_indirect_diffuse_stats(const json& args) -> std::string
{
    // Measured cost of the indirect diffuse field (doc/editor/ddgi.md
    // "Performance", doc/plans/radiance_cascades.md section 8), plus the
    // probe relocation / classification state ("Probe state"). Read-only;
    // optional 'probes' selects probes whose offset and state to return.
    Ddgi_renderer* renderer = m_context.ddgi_renderer;
    const Indirect_diffuse_source source = (m_context.editor_settings != nullptr)
        ? m_context.editor_settings->indirect_diffuse_source
        : Indirect_diffuse_source::ambient;
    json result{
        {"source", std::string{to_string(source)}}
    };
    if (m_context.radiance_cascades_renderer != nullptr) {
        result["radiance_cascades"] = radiance_cascades_stats_json(*m_context.radiance_cascades_renderer);
    }
    if (renderer == nullptr) {
        return make_json_content(result).dump();
    }
    const Ddgi_renderer::Grid&  grid  = renderer->get_grid();
    const Ddgi_renderer::Stats stats = renderer->get_stats();
    const auto pass_time_json = [](const Ddgi_renderer::Pass_time& pass_time) -> json {
        return json{
            {"last_ms",    pass_time.last_ms},
            {"average_ms", pass_time.average_ms}
        };
    };
    json passes = json::object();
    for (std::size_t i = 0; i < c_ddgi_pass_count; ++i) {
        passes[c_str(static_cast<Ddgi_pass>(i))] = pass_time_json(stats.passes[i]);
    }
    result["ddgi"] = json{
        {"supported",                renderer->is_supported()},
        {"active",                   renderer->is_active()},
        {"grid_origin",              json::array({grid.origin.x,  grid.origin.y,  grid.origin.z })},
        {"grid_spacing",             json::array({grid.spacing.x, grid.spacing.y, grid.spacing.z})},
        {"grid_counts",              json::array({grid.counts.x,  grid.counts.y,  grid.counts.z })},
        {"probe_count",              grid.get_probe_count()},
        {"rays_per_probe",           renderer->get_rays_per_probe()},
        {"probes_per_update",        renderer->get_probes_per_update()},
        {"rays_per_update",          stats.rays_per_update},
        {"gpu_ms",                   passes},
        {"gpu_ms_total",             pass_time_json(stats.total)},
        {"timing_history_size",      Ddgi_renderer::c_timing_history_size},
        {"ms_per_million_rays",      stats.ms_per_million_rays},
        {"updates_per_full_refresh", stats.updates_per_full_refresh},
        {"full_refresh_ms",          stats.full_refresh_ms},
        {"texture_bytes",            renderer->get_texture_byte_count()},
        {"update_count",             stats.update_count},
        {"timing_sample_count",      stats.timing_sample_count},
        {"history_reset_count",      stats.history_reset_count}
    };

    // Probe state: report the last retired copy, and ask for a fresh one.
    json probe_states = nullptr;
    if (renderer->is_active()) {
        const bool valid = renderer->poll_probe_states();
        renderer->request_probe_states();
        if (valid) {
            const Ddgi_renderer::Probe_state_summary& summary = renderer->get_probe_state_summary();
            probe_states = json{
                {"update_count",            summary.update_count},
                {"active",                  summary.active},
                {"inactive",                summary.inactive},
                {"relocated",               summary.relocated},
                {"max_offset_over_spacing", summary.max_offset_over_spacing}
            };
            if (args.contains("probes") && args["probes"].is_array()) {
                const std::span<const glm::vec4> states = renderer->get_probe_states();
                json probe_data = json::array();
                for (const json& entry : args["probes"]) {
                    if (!entry.is_array() || (entry.size() != 3) || !entry[0].is_number_integer() || !entry[1].is_number_integer() || !entry[2].is_number_integer()) {
                        return make_error_content("get_indirect_diffuse_stats: 'probes' entries must be [x, y, z] integer grid coordinates");
                    }
                    const glm::ivec3 coords{entry[0].get<int>(), entry[1].get<int>(), entry[2].get<int>()};
                    if (glm::any(glm::lessThan(coords, glm::ivec3{0})) || glm::any(glm::greaterThanEqual(coords, grid.counts))) {
                        return make_error_content("get_indirect_diffuse_stats: probe coordinates outside the grid");
                    }
                    const std::size_t index = static_cast<std::size_t>(coords.x + (grid.counts.x * (coords.y + (grid.counts.y * coords.z))));
                    const glm::vec4   value = states[index];
                    probe_data.push_back(json{
                        {"coords", json::array({coords.x, coords.y, coords.z})},
                        {"offset", json::array({value.x, value.y, value.z})},
                        {"state",  value.w}
                    });
                }
                result["ddgi"]["probe_data"] = probe_data;
            }
        }
    }
    result["ddgi"]["probe_states"] = probe_states;
    return make_json_content(result).dump();
}

namespace {

// A finite [x, y, z] array, or nullopt.
[[nodiscard]] auto parse_finite_vec3(const json& value) -> std::optional<glm::vec3>
{
    if (!value.is_array() || (value.size() != 3)) {
        return std::nullopt;
    }
    glm::vec3 result{0.0f};
    for (int i = 0; i < 3; ++i) {
        if (!value[i].is_number()) {
            return std::nullopt;
        }
        result[i] = value[i].get<float>();
        if (!std::isfinite(result[i])) {
            return std::nullopt;
        }
    }
    return result;
}

[[nodiscard]] auto vec3_json(const glm::vec3& value) -> json
{
    return json::array({value.x, value.y, value.z});
}

} // anonymous namespace

namespace {

// One requested raw texel of get_radiance_cascades_texels.
class Rc_texel_address
{
public:
    int        cascade{0};
    glm::ivec3 probe  {0};
    glm::ivec2 texel  {0};
};

// Parses and range-checks the 'texels' argument against a layout. Returns
// an error message, empty on success.
[[nodiscard]] auto parse_rc_texel_addresses(
    const json&                     texels,
    const Radiance_cascades_layout& layout,
    std::vector<Rc_texel_address>&  out
) -> std::string
{
    out.clear();
    if (texels.is_null()) {
        return {};
    }
    if (!texels.is_array()) {
        return "'texels' must be an array of {cascade, probe:[x,y,z], texel:[u,v]}";
    }
    if (texels.size() > 4096) {
        return "at most 4096 texels per call";
    }
    const auto is_int_array = [](const json& value, const std::size_t size) -> bool {
        if (!value.is_array() || (value.size() != size)) {
            return false;
        }
        for (const json& element : value) {
            if (!element.is_number_integer()) {
                return false;
            }
        }
        return true;
    };
    for (std::size_t i = 0; i < texels.size(); ++i) {
        const json& entry = texels[i];
        if (
            !entry.is_object() ||
            !entry.contains("cascade") || !entry["cascade"].is_number_integer() ||
            !entry.contains("probe")   || !is_int_array(entry["probe"], 3) ||
            !entry.contains("texel")   || !is_int_array(entry["texel"], 2)
        ) {
            return fmt::format("texels[{}] needs integer 'cascade', 'probe' [x, y, z] and 'texel' [u, v]", i);
        }
        Rc_texel_address address{};
        address.cascade = entry["cascade"].get<int>();
        address.probe   = glm::ivec3{entry["probe"][0].get<int>(), entry["probe"][1].get<int>(), entry["probe"][2].get<int>()};
        address.texel   = glm::ivec2{entry["texel"][0].get<int>(), entry["texel"][1].get<int>()};
        if ((address.cascade < 0) || (address.cascade >= layout.cascade_count)) {
            return fmt::format("texels[{}].cascade outside [0, {})", i, layout.cascade_count);
        }
        const Radiance_cascade& cascade = layout.cascades[static_cast<std::size_t>(address.cascade)];
        if (glm::any(glm::lessThan(address.probe, glm::ivec3{0})) || glm::any(glm::greaterThanEqual(address.probe, cascade.grid.counts))) {
            return fmt::format("texels[{}].probe outside the cascade {} grid", i, address.cascade);
        }
        if (glm::any(glm::lessThan(address.texel, glm::ivec2{0})) || glm::any(glm::greaterThanEqual(address.texel, glm::ivec2{cascade.tile_texels}))) {
            return fmt::format("texels[{}].texel outside the {} x {} tile", i, cascade.tile_texels, cascade.tile_texels);
        }
        out.push_back(address);
    }
    return {};
}

// One requested probe field texel of get_radiance_cascades_texels.
enum class Rc_field_atlas : unsigned int
{
    irradiance = 0,
    distance   = 1
};
class Rc_field_texel_address
{
public:
    Rc_field_atlas atlas{Rc_field_atlas::irradiance};
    glm::ivec3     probe{0};
    glm::ivec2     texel{0};
};

// Parses and range-checks the 'field_texels' argument against the field
// parameters. Returns an error message, empty on success.
[[nodiscard]] auto parse_rc_field_texel_addresses(
    const json&                                  field_texels,
    const erhe::scene_renderer::Ddgi_parameters& parameters,
    std::vector<Rc_field_texel_address>&         out
) -> std::string
{
    out.clear();
    if (field_texels.is_null()) {
        return {};
    }
    if (!field_texels.is_array()) {
        return "'field_texels' must be an array of {probe:[x,y,z], texel:[u,v], atlas}";
    }
    if (field_texels.size() > 4096) {
        return "at most 4096 field texels per call";
    }
    if (!parameters.is_valid()) {
        return "no probe field (the reduce has not run yet)";
    }
    for (std::size_t i = 0; i < field_texels.size(); ++i) {
        const json& entry = field_texels[i];
        const bool ok =
            entry.is_object() &&
            entry.contains("probe") && entry["probe"].is_array() && (entry["probe"].size() == 3) &&
            entry["probe"][0].is_number_integer() && entry["probe"][1].is_number_integer() && entry["probe"][2].is_number_integer() &&
            entry.contains("texel") && entry["texel"].is_array() && (entry["texel"].size() == 2) &&
            entry["texel"][0].is_number_integer() && entry["texel"][1].is_number_integer();
        if (!ok) {
            return fmt::format("field_texels[{}] needs integer 'probe' [x, y, z] and 'texel' [u, v]", i);
        }
        Rc_field_texel_address address{};
        const std::string atlas = entry.value("atlas", std::string{"irradiance"});
        if (atlas == "irradiance") {
            address.atlas = Rc_field_atlas::irradiance;
        } else if (atlas == "distance") {
            address.atlas = Rc_field_atlas::distance;
        } else {
            return fmt::format("field_texels[{}].atlas must be \"irradiance\" or \"distance\"", i);
        }
        address.probe = glm::ivec3{entry["probe"][0].get<int>(), entry["probe"][1].get<int>(), entry["probe"][2].get<int>()};
        address.texel = glm::ivec2{entry["texel"][0].get<int>(), entry["texel"][1].get<int>()};
        if (glm::any(glm::lessThan(address.probe, glm::ivec3{0})) || glm::any(glm::greaterThanEqual(address.probe, parameters.grid_counts))) {
            return fmt::format("field_texels[{}].probe outside the cascade 0 grid", i);
        }
        const int texels = (address.atlas == Rc_field_atlas::irradiance) ? parameters.irradiance_texels : parameters.distance_texels;
        if (glm::any(glm::lessThan(address.texel, glm::ivec2{0})) || glm::any(glm::greaterThanEqual(address.texel, glm::ivec2{texels}))) {
            return fmt::format("field_texels[{}].texel outside the {} x {} tile interior", i, texels, texels);
        }
        out.push_back(address);
    }
    return {};
}

} // anonymous namespace

auto Mcp_server::query_radiance_cascades_texels(const json& args) -> std::string
{
    // Raw and merged radiance interval texels and per-cascade summaries
    // (doc/editor/radiance_cascades.md "MCP"): a copy of every raw and
    // merged atlas is requested after the next trace and merge and read
    // back once its frame retired.
    // Nothing is copied unless this tool asks. Same deferral flow as
    // sample_indirect_diffuse.
    Radiance_cascades_renderer* renderer = m_context.radiance_cascades_renderer;

    const bool continuation =
        (m_rc_texels_request != nullptr) &&
        (m_rc_texels_request == m_current_request) &&
        (m_rc_texels_enqueued_at == m_current_request->enqueued_at);
    if (continuation) {
        const Rc_readback_state state = (renderer != nullptr) ? renderer->poll_texel_readback() : Rc_readback_state::idle;
        if (state == Rc_readback_state::complete) {
            m_rc_texels_request = nullptr;
            const Radiance_cascades_layout& layout = renderer->get_readback_layout();
            std::vector<Rc_texel_address> addresses;
            const std::string error = parse_rc_texel_addresses(m_rc_texels_args, layout, addresses);
            if (!error.empty()) {
                return make_error_content("get_radiance_cascades_texels: the layout changed before the copy: " + error);
            }
            json cascades = json::array();
            for (int i = 0; i < layout.cascade_count; ++i) {
                const Radiance_cascades_renderer::Cascade_summary& summary = renderer->get_readback_summary(i);
                json entry{
                    {"index",             i},
                    {"texel_count",       summary.texel_count},
                    {"mean_radiance",     vec3_json(summary.mean_radiance)},
                    {"beta_one_fraction", summary.beta_one_fraction},
                    {"mean_merged_radiance", vec3_json(summary.mean_merged_radiance)},
                    {"mean_merged_beta",     summary.mean_merged_beta}
                };
                if (renderer->readback_has_probe_states()) {
                    entry["inside_probe_count"]     = summary.inside_probe_count;
                    entry["upper_visible_fraction"] = summary.upper_visible_fraction;
                }
                if (i == 0) {
                    entry["backface_fraction"]    = summary.backface_fraction;
                    entry["backface_probe_count"] = summary.backface_probe_count;
                }
                cascades.push_back(std::move(entry));
            }
            std::vector<Rc_field_texel_address> field_addresses;
            if (renderer->readback_has_field()) {
                const std::string field_error = parse_rc_field_texel_addresses(m_rc_field_texels_args, renderer->get_readback_field_parameters(), field_addresses);
                if (!field_error.empty()) {
                    return make_error_content("get_radiance_cascades_texels: the field changed before the copy: " + field_error);
                }
            } else if (!m_rc_field_texels_args.is_null()) {
                return make_error_content("get_radiance_cascades_texels: no probe field was copied");
            }
            json texels = json::array();
            for (const Rc_texel_address& address : addresses) {
                const Radiance_cascade& cascade   = layout.cascades[static_cast<std::size_t>(address.cascade)];
                const glm::vec4         value     = renderer->read_raw_texel   (address.cascade, address.probe, address.texel);
                const glm::vec4         merged    = renderer->read_merged_texel(address.cascade, address.probe, address.texel);

                const glm::vec3         position  = cascade.grid.origin + (glm::vec3{address.probe} * cascade.grid.spacing);
                const glm::vec3         direction = get_texel_direction(address.texel, cascade.tile_texels);
                json entry{
                    {"cascade",        address.cascade},
                    {"probe",          json::array({address.probe.x, address.probe.y, address.probe.z})},
                    {"texel",          json::array({address.texel.x, address.texel.y})},
                    {"probe_position", vec3_json(position)},
                    {"direction",      vec3_json(direction)},
                    {"interval",       json::array({cascade.interval_start, cascade.interval_end})},
                    {"radiance",       vec3_json(glm::vec3{value})},
                    {"beta",           value.a},
                    {"merged_radiance", vec3_json(glm::vec3{merged})},
                    {"merged_beta",     merged.a}
                };
                if (renderer->readback_has_probe_states()) {
                    const uint32_t probe_state = renderer->read_probe_state(address.cascade, address.probe);
                    entry["probe_inside"]       = (probe_state & Radiance_cascades_renderer::c_state_inside) != 0u;
                    entry["upper_visible_mask"] = probe_state & Radiance_cascades_renderer::c_state_upper_visible_mask;
                }
                if (address.cascade == 0) {
                    // Cascade 0 is merged at cascade 1's angular resolution:
                    // the merged value of each nested cascade 1 direction
                    // (merged_radiance / merged_beta are their mean).
                    json children = json::array();
                    for (int child = 0; child < (c_merged_cascade0_block * c_merged_cascade0_block); ++child) {
                        const glm::vec4 child_value = renderer->read_merged_child_texel(address.probe, address.texel, child);
                        children.push_back(json::array({child_value.r, child_value.g, child_value.b, child_value.a}));
                    }
                    entry["merged_children"] = std::move(children);
                    // The blended hit distance statistics; signed_distance
                    // is the mean distance, negative when most traces hit a
                    // backface (exact without direction jitter).
                    const glm::vec4 statistics = renderer->read_distance_statistics(address.probe, address.texel);
                    entry["signed_distance"]   = (statistics.z > 0.5f) ? -statistics.x : statistics.x;
                    entry["distance_mean_squared"] = statistics.y;
                    entry["backface_fraction"]     = statistics.z;
                }
                texels.push_back(std::move(entry));
            }
            json result{
                {"update_count",     renderer->get_readback_update_count()},
                {"completed_sweeps", renderer->get_readback_sweep_count()},
                {"cascades",         std::move(cascades)},
                {"texels",           std::move(texels)}
            };
            if (renderer->readback_has_field()) {
                const erhe::scene_renderer::Ddgi_parameters& parameters = renderer->get_readback_field_parameters();
                json field_texels = json::array();
                for (const Rc_field_texel_address& address : field_addresses) {
                    const bool      irradiance = (address.atlas == Rc_field_atlas::irradiance);
                    const int       tile_texels = irradiance ? parameters.irradiance_texels : parameters.distance_texels;
                    const glm::vec4 probe_data = renderer->read_field_probe_data(address.probe);
                    json entry{
                        {"atlas",       irradiance ? "irradiance" : "distance"},
                        {"probe",       json::array({address.probe.x, address.probe.y, address.probe.z})},
                        {"texel",       json::array({address.texel.x, address.texel.y})},
                        {"direction",   vec3_json(get_texel_direction(address.texel, tile_texels))},
                        {"probe_state", probe_data.w}
                    };
                    if (irradiance) {
                        entry["irradiance"] = vec3_json(renderer->read_field_irradiance_texel(address.probe, address.texel));
                    } else {
                        const glm::vec2 moments = renderer->read_field_distance_texel(address.probe, address.texel);
                        entry["moments"] = json::array({moments.x, moments.y});
                    }
                    field_texels.push_back(std::move(entry));
                }
                result["field"] = json{
                    {"grid_counts",       json::array({parameters.grid_counts.x, parameters.grid_counts.y, parameters.grid_counts.z})},
                    {"irradiance_texels", parameters.irradiance_texels},
                    {"distance_texels",   parameters.distance_texels},
                    {"depth_sharpness",   parameters.depth_sharpness},
                    {"tiles_per_row",     parameters.tiles_per_row}
                };
                result["field_texels"] = std::move(field_texels);
            }
            return make_json_content(result).dump();
        }
        if ((state == Rc_readback_state::requested) || (state == Rc_readback_state::in_flight)) {
            if ((state == Rc_readback_state::requested) && !renderer->is_active()) {
                m_rc_texels_request = nullptr;
                return make_error_content("get_radiance_cascades_texels: radiance cascades became inactive before the copy was recorded");
            }
            m_defer_current_request = true;
            return {};
        }
        m_rc_texels_request = nullptr;
        return make_error_content("get_radiance_cascades_texels: the readback was dropped");
    }

    if ((renderer == nullptr) || !renderer->is_active()) {
        return make_error_content("get_radiance_cascades_texels: radiance cascades are not active (select them with set_indirect_diffuse; needs ray query and scene content)");
    }
    const json texels_arg = args.contains("texels") ? args["texels"] : json{};
    std::vector<Rc_texel_address> addresses;
    const std::string error = parse_rc_texel_addresses(texels_arg, renderer->get_layout(), addresses);
    if (!error.empty()) {
        return make_error_content("get_radiance_cascades_texels: " + error);
    }
    const json field_texels_arg = args.contains("field_texels") ? args["field_texels"] : json{};
    std::vector<Rc_field_texel_address> field_addresses;
    const std::string field_error = parse_rc_field_texel_addresses(field_texels_arg, renderer->get_forward_parameters(), field_addresses);
    if (!field_error.empty()) {
        return make_error_content("get_radiance_cascades_texels: " + field_error);
    }
    renderer->request_texel_readback();
    m_rc_field_texels_args  = field_texels_arg;
    m_rc_texels_args        = texels_arg;
    m_rc_texels_request     = m_current_request;
    m_rc_texels_enqueued_at = m_current_request->enqueued_at;
    m_defer_current_request = true;
    return {};
}

auto Mcp_server::query_sample_indirect_diffuse(const json& args) -> std::string
{
    // Linear float evaluation of the indirect diffuse field at world points
    // (doc/editor/ddgi.md "Irradiance queries"): the forward pass's own
    // ddgi_sample_irradiance(), run by Ddgi_renderer's ddgi_sample.comp over
    // the selected producer's published field (get_indirect_diffuse_field(),
    // DDGI or radiance cascades) and read back once the frame that recorded
    // it has retired. The first pass queues the points and defers; later
    // passes of the same request poll.
    Ddgi_renderer* renderer = m_context.ddgi_renderer;

    const bool continuation =
        (m_irradiance_query_request != nullptr) &&
        (m_irradiance_query_request == m_current_request) &&
        (m_irradiance_query_enqueued_at == m_current_request->enqueued_at);
    if (continuation) {
        const Irradiance_query_state state = (renderer != nullptr) ? renderer->poll_irradiance_query() : Irradiance_query_state::idle;
        if (state == Irradiance_query_state::complete) {
            m_irradiance_query_request = nullptr;
            const std::span<const glm::vec3> results = renderer->get_irradiance_query_results();
            json samples = json::array();
            for (const glm::vec3& irradiance : results) {
                samples.push_back(json{{"irradiance", vec3_json(irradiance)}});
            }
            json result = m_irradiance_query_header;
            result["update_count"] = renderer->get_irradiance_query_update_count();
            result["samples"]      = std::move(samples);
            return make_json_content(result).dump();
        }
        if ((state == Irradiance_query_state::queued) || (state == Irradiance_query_state::in_flight)) {
            if ((state == Irradiance_query_state::queued) && !get_indirect_diffuse_field(m_context).is_valid()) {
                renderer->cancel_irradiance_query();
                m_irradiance_query_request = nullptr;
                return make_error_content("sample_indirect_diffuse: the probe field went away before the query was recorded");
            }
            m_defer_current_request = true;
            return {};
        }
        m_irradiance_query_request = nullptr;
        return make_error_content("sample_indirect_diffuse: the query was dropped");
    }

    // Arguments.
    const auto samples_it = args.find("samples");
    if ((samples_it == args.end()) || !samples_it->is_array() || samples_it->empty()) {
        return make_error_content("sample_indirect_diffuse: 'samples' must be a non-empty array of {position:[x,y,z], normal:[x,y,z]}");
    }
    if (samples_it->size() > Ddgi_renderer::c_max_irradiance_query_points) {
        return make_error_content(fmt::format(
            "sample_indirect_diffuse: {} samples requested, at most {} per call",
            samples_it->size(), Ddgi_renderer::c_max_irradiance_query_points
        ));
    }
    std::optional<glm::vec3> view_position{};
    if (args.contains("view_position")) {
        view_position = parse_finite_vec3(args["view_position"]);
        if (!view_position.has_value()) {
            return make_error_content("sample_indirect_diffuse: 'view_position' must be a finite [x, y, z]");
        }
    }
    std::vector<Irradiance_query_point> points;
    points.reserve(samples_it->size());
    for (std::size_t i = 0; i < samples_it->size(); ++i) {
        const json& sample = (*samples_it)[i];
        const std::optional<glm::vec3> position = sample.is_object() ? parse_finite_vec3(sample.value("position", json{})) : std::nullopt;
        const std::optional<glm::vec3> normal   = sample.is_object() ? parse_finite_vec3(sample.value("normal",   json{})) : std::nullopt;
        if (!position.has_value() || !normal.has_value()) {
            return make_error_content(fmt::format("sample_indirect_diffuse: samples[{}] needs finite 'position' and 'normal' [x, y, z] arrays", i));
        }
        const float normal_length = glm::length(normal.value());
        if (!(normal_length > 1.0e-6f)) {
            return make_error_content(fmt::format("sample_indirect_diffuse: samples[{}].normal has zero length", i));
        }
        Irradiance_query_point point{};
        point.position = position.value();
        point.normal   = normal.value() / normal_length;
        // standard.frag's V points from the surface toward the viewer.
        // Without a view position the viewer sits on the normal (V = N).
        point.view_direction = point.normal;
        if (view_position.has_value()) {
            const glm::vec3 to_viewer = view_position.value() - point.position;
            const float     distance  = glm::length(to_viewer);
            if (distance > 1.0e-6f) {
                point.view_direction = to_viewer / distance;
            }
        }
        points.push_back(point);
    }

    json header{
        {"point_count",        points.size()},
        {"view_direction",     view_position.has_value() ? "toward_view_position" : "normal"},
        {"intensity_included", true}
    };

    // No active field: the forward pass shades with the flat scene ambient,
    // which is also what the shader returns without a volume.
    if ((renderer == nullptr) || !get_indirect_diffuse_field(m_context).is_valid()) {
        const std::shared_ptr<Scene_root> scene_root = (m_context.app_scenes != nullptr)
            ? m_context.app_scenes->get_single_scene_root()
            : std::shared_ptr<Scene_root>{};
        if (!scene_root) {
            return make_error_content("sample_indirect_diffuse: no scene");
        }
        const glm::vec3 ambient = scene_root->get_scene().get_ambient_light();
        json samples = json::array();
        for (std::size_t i = 0; i < points.size(); ++i) {
            samples.push_back(json{{"irradiance", vec3_json(ambient)}});
        }
        header["source"]       = "ambient";
        header["update_count"] = 0;
        header["samples"]      = std::move(samples);
        return make_json_content(header).dump();
    }

    if (!renderer->begin_irradiance_query(std::span<const Irradiance_query_point>{points})) {
        // An earlier query (from a request that has since expired) is still
        // queued or in flight; it completes within a few frames. Wait for
        // it and try again on the next pass.
        static_cast<void>(renderer->poll_irradiance_query());
        m_defer_current_request = true;
        return {};
    }
    // The field's producer; the sampling parameters (intensity included)
    // are the DDGI settings for both producers.
    header["source"]    = (m_context.editor_settings != nullptr) ? std::string{to_string(m_context.editor_settings->indirect_diffuse_source)} : std::string{"ddgi"};
    header["intensity"] = (m_context.editor_settings != nullptr) ? m_context.editor_settings->ddgi.intensity : 1.0f;
    m_irradiance_query_header      = std::move(header);
    m_irradiance_query_request     = m_current_request;
    m_irradiance_query_enqueued_at = m_current_request->enqueued_at;
    m_defer_current_request        = true;
    return {};
}

auto Mcp_server::query_reference_indirect_diffuse(const json& args) -> std::string
{
    // Ground-truth irradiance at world points (doc/editor/ddgi.md "Reference
    // irradiance"): cosine-distributed rays from each point, each carrying
    // exactly what a DDGI probe ray carries (erhe_ddgi_ray.glsl), traced by
    // Ddgi_renderer's ddgi_reference.comp in per-frame chunks. Same deferral
    // flow as sample_indirect_diffuse.
    Ddgi_renderer* renderer = m_context.ddgi_renderer;

    const bool continuation =
        (m_reference_query_request != nullptr) &&
        (m_reference_query_request == m_current_request) &&
        (m_reference_query_enqueued_at == m_current_request->enqueued_at);
    if (continuation) {
        const Irradiance_query_state state = (renderer != nullptr) ? renderer->poll_reference_query() : Irradiance_query_state::idle;
        if (state == Irradiance_query_state::complete) {
            m_reference_query_request = nullptr;
            json samples = json::array();
            for (const Reference_irradiance_sample& sample : renderer->get_reference_query_results()) {
                samples.push_back(json{
                    {"irradiance",        vec3_json(sample.irradiance)},
                    {"standard_error",    sample.standard_error},
                    {"sky_fraction",      sample.sky_fraction},
                    {"backface_fraction", sample.backface_fraction}
                });
            }
            json result = m_reference_query_header;
            result["intensity"] = renderer->get_reference_query_intensity();
            result["samples"]   = std::move(samples);
            return make_json_content(result).dump();
        }
        if ((state == Irradiance_query_state::queued) || (state == Irradiance_query_state::in_flight)) {
            m_defer_current_request = true;
            return {};
        }
        m_reference_query_request = nullptr;
        if (state == Irradiance_query_state::failed) {
            return make_error_content("reference_indirect_diffuse: " + renderer->get_reference_query_error());
        }
        return make_error_content("reference_indirect_diffuse: the query was dropped");
    }

    if ((renderer == nullptr) || !renderer->is_supported()) {
        return make_error_content("reference_indirect_diffuse: needs GPU ray query support (Device_info::use_ray_query), which this device / backend does not have");
    }

    // Arguments.
    const auto samples_it = args.find("samples");
    if ((samples_it == args.end()) || !samples_it->is_array() || samples_it->empty()) {
        return make_error_content("reference_indirect_diffuse: 'samples' must be a non-empty array of {position:[x,y,z], normal:[x,y,z]}");
    }
    if (samples_it->size() > Ddgi_renderer::c_max_irradiance_query_points) {
        return make_error_content(fmt::format(
            "reference_indirect_diffuse: {} samples requested, at most {} per call",
            samples_it->size(), Ddgi_renderer::c_max_irradiance_query_points
        ));
    }
    Reference_query_settings settings{};
    if (args.contains("rays_per_point")) {
        const json& value = args["rays_per_point"];
        if (!value.is_number_integer() || (value.get<int64_t>() < 1) || (value.get<int64_t>() > Ddgi_renderer::c_max_reference_rays_per_point)) {
            return make_error_content(fmt::format(
                "reference_indirect_diffuse: 'rays_per_point' must be an integer in [1, {}]",
                Ddgi_renderer::c_max_reference_rays_per_point
            ));
        }
        settings.rays_per_point = value.get<int>();
    }
    if (args.contains("seed")) {
        const json& value = args["seed"];
        if (!value.is_number_integer() || (value.get<int64_t>() < 0) || (value.get<int64_t>() > int64_t{0xffffffff})) {
            return make_error_content("reference_indirect_diffuse: 'seed' must be an integer in [0, 4294967295]");
        }
        settings.seed = static_cast<uint32_t>(value.get<int64_t>());
    }
    if (args.contains("normal_bias")) {
        const json& value = args["normal_bias"];
        if (!value.is_number() || !std::isfinite(value.get<float>()) || (value.get<float>() < 0.0f)) {
            return make_error_content("reference_indirect_diffuse: 'normal_bias' must be a finite number >= 0");
        }
        settings.normal_bias = value.get<float>();
    }
    const int64_t total_rays = static_cast<int64_t>(samples_it->size()) * static_cast<int64_t>(settings.rays_per_point);
    if (total_rays > Ddgi_renderer::c_max_reference_rays_per_query) {
        return make_error_content(fmt::format(
            "reference_indirect_diffuse: {} samples x {} rays = {} rays, at most {} per call; split the samples over several calls",
            samples_it->size(), settings.rays_per_point, total_rays, Ddgi_renderer::c_max_reference_rays_per_query
        ));
    }
    std::vector<Irradiance_query_point> points;
    points.reserve(samples_it->size());
    for (std::size_t i = 0; i < samples_it->size(); ++i) {
        const json& sample = (*samples_it)[i];
        const std::optional<glm::vec3> position = sample.is_object() ? parse_finite_vec3(sample.value("position", json{})) : std::nullopt;
        const std::optional<glm::vec3> normal   = sample.is_object() ? parse_finite_vec3(sample.value("normal",   json{})) : std::nullopt;
        if (!position.has_value() || !normal.has_value()) {
            return make_error_content(fmt::format("reference_indirect_diffuse: samples[{}] needs finite 'position' and 'normal' [x, y, z] arrays", i));
        }
        const float normal_length = glm::length(normal.value());
        if (!(normal_length > 1.0e-6f)) {
            return make_error_content(fmt::format("reference_indirect_diffuse: samples[{}].normal has zero length", i));
        }
        Irradiance_query_point point{};
        point.position       = position.value();
        point.normal         = normal.value() / normal_length;
        point.view_direction = point.normal; // unused: irradiance is view independent
        points.push_back(point);
    }

    const std::shared_ptr<Scene_root> scene_root = (m_context.app_scenes != nullptr)
        ? m_context.app_scenes->get_single_scene_root()
        : std::shared_ptr<Scene_root>{};
    if (!scene_root) {
        return make_error_content("reference_indirect_diffuse: needs exactly one open scene (the one DDGI traces)");
    }

    if (!renderer->begin_reference_query(std::span<const Irradiance_query_point>{points}, settings)) {
        // An earlier query (from a request that has since expired) has
        // chunks in flight; it retires within a few frames.
        static_cast<void>(renderer->poll_reference_query());
        m_defer_current_request = true;
        return {};
    }
    m_reference_query_header = json{
        {"source",          "reference"},
        {"convention",      "irradiance = intensity x cosine-weighted mean incident radiance (E / pi): the quantity ddgi_sample_irradiance returns, same units, intensity included the same way"},
        {"intensity_included", true},
        {"point_count",     points.size()},
        {"rays_per_point",  settings.rays_per_point},
        {"seed",            settings.seed},
        {"normal_bias",     settings.normal_bias},
        {"ddgi_enabled",    renderer->is_active()},
        {"standard_error",  "of the luminance (0.2126 R + 0.7152 G + 0.0722 B) of irradiance"}
    };
    m_reference_query_request     = m_current_request;
    m_reference_query_enqueued_at = m_current_request->enqueued_at;
    m_defer_current_request       = true;
    return {};
}

auto Mcp_server::find_scene(const std::string& name) -> Scene_root*
{
    if (!m_context.app_scenes) {
        return nullptr;
    }
    for (const auto& sr : m_context.app_scenes->get_scene_roots()) {
        if (sr->get_name() == name) {
            return sr.get();
        }
    }
    return nullptr;
}

auto Mcp_server::execute_command(const std::string& tool_name) -> std::string
{
    const auto& registered_commands = m_commands.get_commands();
    for (auto* command : registered_commands) {
        if (tool_name == command->get_name()) {
            const bool success = command->try_call();
            if (success) {
                return make_text_content("Command executed successfully: " + tool_name).dump();
            } else {
                json r = make_text_content("Command failed: " + tool_name);
                r["isError"] = true;
                return r.dump();
            }
        }
    }
    json r = make_text_content("Command not found: " + tool_name);
    r["isError"] = true;
    return r.dump();
}


} // namespace editor
