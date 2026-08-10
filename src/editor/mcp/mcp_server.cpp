// Mcp_server lifecycle, HTTP routing, JSON-RPC handling and tool dispatch.
// Split out of mcp_server.cpp; shares helpers via mcp_server_shared.hpp.

#include "mcp/mcp_server_shared.hpp"

#include "config/generated/editor_settings_config.hpp"
#include "config/generated/ray_trace_config.hpp"
#include "renderers/ray_trace_renderer.hpp"

#include "erhe_graphics/image_writer.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_imgui/imgui_window.hpp"
#include "erhe_imgui/imgui_windows.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <iterator>
#include <span>

namespace editor {

using namespace mcp_server_detail;

namespace {

// Name -> handler dispatch table type (the table itself lives inside
// Mcp_server::dispatch_tool_call(), which has access to the private
// handler members). A table, not an if/else-if chain: MSVC counts each
// else-if as a nested block and aborts with C1061 ("blocks nested too
// deeply") once the tool count passes its limit (~120).
using Tool_handler = auto (Mcp_server::*)(const nlohmann::json&) -> std::string;

class Tool_dispatch_entry
{
public:
    const char*  name;
    Tool_handler handler;
};

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
    m_auth_token = load_auth_token();
    if (m_auth_token.empty()) {
        log_mcp->warn(
            "MCP server: no bearer token loaded (write a secret to ~/.agents/erhe_mcp_token "
            "with mode 0600 to require Authorization: Bearer)"
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

    // Bind the preferred port, falling back through up to k_port_retry_count-1
    // successors (e.g. 8080..8099) when it is already in use. This matters on
    // Quest, where another service may already hold 8080; without the fallback
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
#if defined(_WIN32)
        // httplib's default socket options set SO_REUSEADDR, which on
        // Windows (unlike POSIX) lets bind() succeed on a port another
        // process is actively LISTENing on. The new socket is silently
        // shadowed (the first listener keeps receiving all connections),
        // so the fallback scan below never fires and this server logs a
        // port that a DIFFERENT editor instance is answering on.
        // SO_EXCLUSIVEADDRUSE makes bind() fail like POSIX would.
        m_http_server->set_socket_options(
            [](socket_t sock) {
                httplib::set_socket_opt(sock, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, 1);
            }
        );
#endif
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
                res.body = make_jsonrpc_error("null", -32001, "Unauthorized");
                return;
            }
        }

        json request;
        try {
            request = json::parse(req.body);
        } catch (const json::parse_error&) {
            res.body = make_jsonrpc_error("null", -32700, "Parse error");
            return;
        }

        // JSON-RPC allows the id to be a string, a number, or null. json::value
        // with a string default throws json::type_error on a numeric id, which
        // httplib turns into an opaque HTTP 500. Normalize any id type to the
        // string form used internally for the response echo.
        std::string id = "null";
        if (request.contains("id")) {
            const json& id_json = request.at("id");
            if (id_json.is_string()) {
                id = id_json.get<std::string>();
            } else if (id_json.is_number_integer()) {
                id = std::to_string(id_json.get<int64_t>());
            } else if (id_json.is_number_unsigned()) {
                id = std::to_string(id_json.get<uint64_t>());
            } else if (id_json.is_number_float()) {
                id = std::to_string(id_json.get<double>());
            }
        }
        const std::string method = request.value("method", "");

        if (method == "initialize") {
            res.body = handle_initialize(id);
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

    m_http_server->Get("/health", [](const httplib::Request&, httplib::Response& res) {
        res.set_header("Content-Type", "application/json");
        res.body = R"({"status":"ok"})";
    });
}

auto Mcp_server::handle_initialize(const std::string& id) -> std::string
{
    json result = {
        {"protocolVersion", "2024-11-05"},
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

auto Mcp_server::handle_tools_list(const std::string& id) -> std::string
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
    const std::string& id,
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

auto Mcp_server::handle_error(const std::string& id, int code, const std::string& message) -> std::string
{
    return make_jsonrpc_error(id, code, message);
}

auto Mcp_server::process_queued_requests() -> int
{
    std::vector<std::unique_ptr<Queued_request>> requests;
    {
        std::lock_guard<std::mutex> lock{m_queue_mutex};
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
                make_jsonrpc_error("dropped", -32000, "Request expired before processing: " + req->tool_name)
            );
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
            result = dispatch_tool_call(req->tool_name, req->arguments);
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

auto Mcp_server::dispatch_tool_call(const std::string& tool_name, const json& arguments) -> std::string
{
    // Member-function-local: the handlers are private members, so their
    // addresses can only be taken from inside the class. A table, not an
    // if/else-if chain: MSVC counts each else-if as a nested block and
    // aborts with C1061 once the tool count passes its limit (~120).
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
        { "get_scene_settings",             &Mcp_server::query_scene_settings                 },
        { "get_material_details",           &Mcp_server::query_material_details               },
        { "get_viewports",                  &Mcp_server::query_viewports                      },
        { "pick_at",                        &Mcp_server::query_pick_at                        },
        { "get_server_info",                &Mcp_server::query_server_info                    },
        { "set_window_visibility",          &Mcp_server::action_set_window_visibility         },
        { "get_frame_pacing_status",        &Mcp_server::query_frame_pacing_status            },
        { "get_frame_pacing_frames",        &Mcp_server::query_frame_pacing_frames            },
        { "set_frame_pacing_min_vsyncs",    &Mcp_server::action_set_frame_pacing_min_vsyncs   },
        { "set_frame_pacing_workload",      &Mcp_server::action_set_frame_pacing_workload     },
        { "set_frame_pacing_capture",       &Mcp_server::action_set_frame_pacing_capture      },
        { "get_selection",                  &Mcp_server::query_selection                      },
        { "get_undo_redo_stack",            &Mcp_server::query_undo_redo_stack                },
        { "clear_undo_history",             &Mcp_server::action_clear_undo_history            },
        { "get_async_status",               &Mcp_server::query_async_status                   },
        { "get_transform_update_stats",     &Mcp_server::query_transform_update_stats         },
        { "merge_static_subtree",           &Mcp_server::action_merge_static_subtree          },
        { "get_shadow_fit_debug",           &Mcp_server::query_shadow_fit_debug               },
        { "raycast",                        &Mcp_server::query_raycast                        },
        { "geometry_query",                 &Mcp_server::query_geometry_batch                 },
        { "select_items",                   &Mcp_server::action_select_items                  },
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
        { "set_node_transform",             &Mcp_server::action_set_node_transform            },
        { "place_brush",                    &Mcp_server::action_place_brush                   },
        { "place_brush_instances",          &Mcp_server::action_place_brush_instances         },
        { "create_shape",                   &Mcp_server::action_create_shape                  },
        { "create_node",                    &Mcp_server::action_create_node                   },
        { "create_light",                   &Mcp_server::action_create_light                  },
        { "edit_light",                     &Mcp_server::action_edit_light                    },
        { "edit_camera",                    &Mcp_server::action_edit_camera                   },
        { "toggle_physics",                 &Mcp_server::action_toggle_physics                },
        { "advance_time",                   &Mcp_server::action_advance_time                  },
        { "apply_physics_force",            &Mcp_server::action_apply_physics_force           },
        { "add_node_attachment",            &Mcp_server::action_add_node_attachment           },
        { "remove_node_attachment",         &Mcp_server::action_remove_node_attachment        },
        { "reparent_node",                  &Mcp_server::action_reparent_node                 },
        { "clipboard_copy_nodes",           &Mcp_server::action_clipboard_copy_nodes          },
        { "clipboard_paste",                &Mcp_server::action_clipboard_paste               },
        { "lock_items",                     &Mcp_server::action_lock_items                    },
        { "unlock_items",                   &Mcp_server::action_unlock_items                  },
        { "add_tags",                       &Mcp_server::action_add_tags                      },
        { "remove_tags",                    &Mcp_server::action_remove_tags                   },
        { "edit_material",                  &Mcp_server::action_edit_material                 },
        { "create_material",                &Mcp_server::action_create_material               },
        { "copy_library_item",              &Mcp_server::action_copy_library_item             },
        { "set_scene_settings",             &Mcp_server::action_set_scene_settings            },
        { "save_scene",                     &Mcp_server::action_save_scene                    },
        { "load_scene",                     &Mcp_server::action_load_scene                    },
        { "open_scene",                     &Mcp_server::action_open_scene                    },
        { "close_scene",                    &Mcp_server::action_close_scene                   },
        { "create_scene",                   &Mcp_server::action_create_scene                  },
        { "export_gltf",                    &Mcp_server::action_export_gltf                   },
        { "import_gltf",                    &Mcp_server::action_import_gltf                   },
        { "scan_gltf",                      &Mcp_server::query_scan_gltf                      },
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
        { "get_prefabs",                    &Mcp_server::query_prefabs                        },
        { "capture_screenshot",             &Mcp_server::action_capture_screenshot            },
        { "push_shader_debug",              &Mcp_server::action_push_shader_debug             },
        { "pop_shader_debug",               &Mcp_server::action_pop_shader_debug              },
        { "wake_physics_bodies",            &Mcp_server::action_wake_physics_bodies           },
        { "get_physics_items",              &Mcp_server::query_physics_items                  },
        { "get_physics_state",              &Mcp_server::query_get_physics_state              },
        { "create_physics_body",            &Mcp_server::action_create_physics_body           },
        { "edit_physics_body",              &Mcp_server::action_edit_physics_body             },
        { "create_physics_joint",           &Mcp_server::action_create_physics_joint          },
        { "edit_physics_joint",             &Mcp_server::action_edit_physics_joint            },
        { "create_physics_material",        &Mcp_server::action_create_physics_material       },
        { "edit_physics_material",          &Mcp_server::action_edit_physics_material         },
        { "create_collision_filter",        &Mcp_server::action_create_collision_filter       },
        { "edit_collision_filter",          &Mcp_server::action_edit_collision_filter         },
        { "create_physics_joint_settings",  &Mcp_server::action_create_physics_joint_settings },
        { "edit_physics_joint_settings",    &Mcp_server::action_edit_physics_joint_settings   },
        { "set_mesh_component_mode",        &Mcp_server::action_set_mesh_component_mode       },
        { "select_mesh_components",         &Mcp_server::action_select_mesh_components        },
        { "grow_mesh_selection",            &Mcp_server::action_grow_mesh_selection           },
        { "shrink_mesh_selection",          &Mcp_server::action_shrink_mesh_selection         },
        { "get_mesh_component_selection",   &Mcp_server::query_mesh_component_selection       },
        { "get_id_range_mapping",           &Mcp_server::query_id_range_mapping               },
        { "debug_region_select",            &Mcp_server::action_debug_region_select           },
        { "get_mesh_geometry_info",         &Mcp_server::query_mesh_geometry_info             },
        { "get_mesh_attribute_values",      &Mcp_server::query_mesh_attribute_values          },
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
        { "merge_faces",                    &Mcp_server::action_merge_faces                   },
        { "generate_texture_coordinates",   &Mcp_server::action_generate_texture_coordinates  },
        { "set_transform_reference_mode",   &Mcp_server::action_set_transform_reference_mode  },
        { "set_transform_mode",             &Mcp_server::action_set_transform_mode            },
        { "set_gizmo_visibility",           &Mcp_server::action_set_gizmo_visibility          },
        { "get_transform_state",            &Mcp_server::query_transform_state                },
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
        { "set_ray_trace",                  &Mcp_server::action_set_ray_trace                 },
    };
    for (const Tool_dispatch_entry& entry : c_tool_dispatch) {
        if (tool_name == entry.name) {
            return (this->*entry.handler)(arguments);
        }
    }
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
