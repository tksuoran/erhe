// Mcp_server asset-manager tools (Phase R1): query_asset_manager plus the
// acquire_asset / release_asset / unload_asset test hooks that drive the
// manager's debug holds, so usership bookkeeping and unload refusal /
// exclusivity verification are exercisable headless.
// Split out of mcp_server.cpp; shares helpers via mcp_server_shared.hpp.

#include "mcp/mcp_server_shared.hpp"

#include "assets/asset_key.hpp"
#include "assets/asset_manager.hpp"
#include "assets/asset_reference.hpp"

namespace editor {

using namespace mcp_server_detail;

namespace {

auto asset_key_to_json(const Asset_key& key) -> json
{
    return {
        {"scope", c_str(key.scope)},
        {"type",  c_str(key.type)},
        {"path",  key.path},
        {"uid",   key.uid},
        {"name",  key.name}
    };
}

// Builds an Asset_key from the shared tool arguments {scope, type, path,
// uid, name}. Returns false with error set when scope or type is missing /
// unknown.
auto parse_asset_key_args(const json& args, Asset_key& out_key, std::string& out_error) -> bool
{
    const std::string scope_text = args.value("scope", "");
    const std::string type_text  = args.value("type", "");
    out_key.scope = parse_asset_scope(scope_text);
    out_key.type  = parse_asset_type(type_text);
    out_key.path  = args.value("path", "");
    out_key.uid   = args.value("uid", "");
    out_key.name  = args.value("name", "");
    if (out_key.scope == Asset_scope::none) {
        out_error = "'scope' must be one of: builtin, scene_local, file";
        return false;
    }
    if (out_key.type == Asset_type::none) {
        out_error = "'type' must be one of: brush, material, animation, mesh";
        return false;
    }
    return true;
}

}

auto Mcp_server::query_asset_manager(const json& args) -> std::string
{
    static_cast<void>(args);
    Asset_manager* asset_manager = m_context.asset_manager;
    if (asset_manager == nullptr) {
        return make_error_content("Asset manager not available");
    }

    json assets = json::array();
    for (const Asset_info& info : asset_manager->inspect_assets()) {
        json entry = asset_key_to_json(info.key);
        entry["user_count"] = info.user_labels.size();
        entry["users"]      = info.user_labels;
        assets.push_back(std::move(entry));
    }

    json containers = json::array();
    for (const Asset_container_info& info : asset_manager->inspect_containers()) {
        containers.push_back({
            {"id",              info.id},
            {"path",            info.path},
            {"material_count",  info.material_count},
            {"animation_count", info.animation_count},
            {"errors",          info.errors},
            {"dirty",           info.dirty}
        });
    }

    json debug_holds = json::array();
    for (const auto& [hold_name, reference] : asset_manager->get_debug_holds()) {
        json entry = {
            {"hold_name", hold_name},
            {"key",       asset_key_to_json(reference.get_key())},
            {"state",     c_str(reference.get_state())}
        };
        const std::shared_ptr<erhe::Item_base>& item = reference.get();
        if (item) {
            entry["item_id"]   = item->get_id();
            entry["item_name"] = item->get_name();
        }
        debug_holds.push_back(std::move(entry));
    }

    const json result = {
        {"assets",      assets},
        {"containers",  containers},
        {"debug_holds", debug_holds}
    };
    return make_json_content(result).dump();
}

auto Mcp_server::action_acquire_asset(const json& args) -> std::string
{
    Asset_manager* asset_manager = m_context.asset_manager;
    if (asset_manager == nullptr) {
        return make_error_content("Asset manager not available");
    }
    const std::string hold_name = args.value("hold_name", "");
    if (hold_name.empty()) {
        return make_error_content("'hold_name' is required");
    }
    Asset_key   key;
    std::string error;
    if (!parse_asset_key_args(args, key, error)) {
        return make_error_content(error);
    }
    const std::shared_ptr<erhe::Item_base> item = asset_manager->debug_acquire(hold_name, key, error);
    if (!item) {
        return make_error_content("acquire failed: " + error);
    }
    const json result = {
        {"hold_name", hold_name},
        {"key",       asset_key_to_json(key)},
        {"item_id",   item->get_id()},
        {"item_name", item->get_name()},
        {"item_uid",  item->get_gltf_uid()},
        {"item_type", std::string{item->get_type_name()}}
    };
    return make_json_content(result).dump();
}

auto Mcp_server::action_release_asset(const json& args) -> std::string
{
    Asset_manager* asset_manager = m_context.asset_manager;
    if (asset_manager == nullptr) {
        return make_error_content("Asset manager not available");
    }
    const std::string hold_name = args.value("hold_name", "");
    if (hold_name.empty()) {
        return make_error_content("'hold_name' is required");
    }
    const bool released = asset_manager->debug_release(hold_name);
    if (!released) {
        return make_error_content("no debug hold named '" + hold_name + "'");
    }
    return make_text_content("released debug hold '" + hold_name + "'").dump();
}

auto Mcp_server::action_unload_asset(const json& args) -> std::string
{
    Asset_manager* asset_manager = m_context.asset_manager;
    if (asset_manager == nullptr) {
        return make_error_content("Asset manager not available");
    }
    Asset_key   key;
    std::string error;
    if (!parse_asset_key_args(args, key, error)) {
        return make_error_content(error);
    }
    const Unload_result unload_result = asset_manager->request_unload(key);
    json users = json::array();
    for (const Asset_user_info& user : unload_result.users) {
        users.push_back({
            {"label",      user.label},
            {"asset_name", user.asset_name}
        });
    }
    const json result = {
        {"ok",                   unload_result.ok},
        {"message",              unload_result.message},
        {"users",                users},
        {"undeclared_survivors", unload_result.undeclared_survivors}
    };
    if (!unload_result.ok) {
        json r = make_json_content(result);
        r["isError"] = true;
        return r.dump();
    }
    return make_json_content(result).dump();
}

}
