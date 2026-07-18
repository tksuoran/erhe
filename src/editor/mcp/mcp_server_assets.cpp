// Mcp_server asset-manager tools (Phase R1): query_asset_manager plus the
// acquire_asset / release_asset / unload_asset test hooks that drive the
// manager's debug holds, so usership bookkeeping and unload refusal /
// exclusivity verification are exercisable headless. set_tool_asset (Phase
// R3) drives the tool-held Asset_references the same way.
// Split out of mcp_server.cpp; shares helpers via mcp_server_shared.hpp.

#include "mcp/mcp_server_shared.hpp"

#include "assets/asset_key.hpp"
#include "assets/asset_manager.hpp"
#include "assets/asset_reference.hpp"
#include "brushes/brush_tool.hpp"
#include "tools/material_paint_tool.hpp"
#include "windows/inventory_window.hpp"

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
        json entry = {
            {"id",              info.id},
            {"path",            info.path},
            {"brush_count",     info.brush_count},
            {"material_count",  info.material_count},
            {"animation_count", info.animation_count},
            {"errors",          info.errors},
            {"dirty",           info.dirty},
            {"open_as_scene",   info.open_as_scene},
            {"session",         info.session}
        };
        if (info.open_as_scene) {
            entry["scene_name"] = info.scene_name;
        }
        containers.push_back(std::move(entry));
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
    const Unload_result unload_result = asset_manager->request_unload(key, args.value("discard", false));
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

// Phase R3 verification hook: set / clear the Brush_tool active brush or the
// Material_paint_tool material, so the tools' Asset_reference userships are
// exercisable headless (the interactive entry points - hotbar clicks,
// material pick, the properties combo - all need mouse input).
auto Mcp_server::action_set_tool_asset(const json& args) -> std::string
{
    const std::string tool_text  = args.value("tool", "");
    const std::string scene_name = args.value("scene_name", "");
    const std::string name       = args.value("name", "");

    const bool is_brush_tool          = (tool_text == "brush");
    const bool is_material_paint_tool = (tool_text == "material_paint");
    if (!is_brush_tool && !is_material_paint_tool) {
        return make_error_content("'tool' must be one of: brush, material_paint");
    }
    if (is_brush_tool && (m_context.brush_tool == nullptr)) {
        return make_error_content("Brush tool not available");
    }
    if (is_material_paint_tool && (m_context.material_paint_tool == nullptr)) {
        return make_error_content("Material paint tool not available");
    }

    if (name.empty()) {
        if (is_brush_tool) {
            m_context.brush_tool->clear_active_brush();
            return make_text_content("cleared brush tool active brush").dump();
        }
        m_context.material_paint_tool->set_material({});
        return make_text_content("cleared material paint tool material").dump();
    }

    auto* sr = find_scene(scene_name);
    if (sr == nullptr) {
        return make_error_content("Scene not found: " + scene_name);
    }
    const std::shared_ptr<Content_library>& library = sr->get_content_library();

    if (is_brush_tool) {
        std::shared_ptr<Brush> brush;
        if (library && library->brushes) {
            for (const std::shared_ptr<Brush>& b : library->brushes->get_all<Brush>()) {
                if (b->get_name() == name) {
                    brush = b;
                    break;
                }
            }
        }
        if (!brush) {
            return make_error_content("Brush not found: " + name);
        }
        m_context.brush_tool->set_active_brush(brush);
        const json result = {
            {"tool",     "brush"},
            {"asset",    brush->get_name()},
            {"asset_id", brush->get_id()}
        };
        return make_json_content(result).dump();
    }

    std::shared_ptr<erhe::primitive::Material> material;
    if (library && library->materials) {
        for (const std::shared_ptr<erhe::primitive::Material>& m : library->materials->get_all<erhe::primitive::Material>()) {
            if (m->get_name() == name) {
                material = m;
                break;
            }
        }
    }
    if (!material) {
        return make_error_content("Material not found: " + name);
    }
    m_context.material_paint_tool->set_material(material);
    const json result = {
        {"tool",     "material_paint"},
        {"asset",    material->get_name()},
        {"asset_id", material->get_id()}
    };
    return make_json_content(result).dump();
}

auto Mcp_server::action_save_container(const json& args) -> std::string
{
    Asset_manager* asset_manager = m_context.asset_manager;
    if (asset_manager == nullptr) {
        return make_error_content("Asset manager not available");
    }
    const std::string path = args.value("path", "");
    if (path.empty()) {
        return make_error_content("Missing required 'path'");
    }
    std::string error;
    const bool ok = asset_manager->save_container(std::filesystem::path{path}, error);
    if (!ok) {
        return make_error_content(error);
    }
    return make_json_content({{"saved", true}, {"path", path}}).dump();
}

auto Mcp_server::action_set_inventory_slot(const json& args) -> std::string
{
    if (m_context.inventory_window == nullptr) {
        return make_error_content("Inventory window not available");
    }
    if (m_context.asset_manager == nullptr) {
        return make_error_content("Asset manager not available");
    }
    if (!args.contains("slot_index") || !args["slot_index"].is_number_integer()) {
        return make_error_content("Missing required 'slot_index'");
    }
    const int slot_index = args["slot_index"].get<int>();

    if (args.value("clear", false)) {
        if (!m_context.inventory_window->adopt_into_grid_slot(slot_index, {})) {
            return make_error_content("slot_index out of range");
        }
        return make_json_content({{"slot_index", slot_index}, {"cleared", true}}).dump();
    }

    if (!args.contains("item_id") || !args["item_id"].is_number_integer()) {
        return make_error_content("Missing required 'item_id' (or pass 'clear': true)");
    }
    const std::size_t item_id = static_cast<std::size_t>(args["item_id"].get<std::int64_t>());
    const std::shared_ptr<erhe::Item_base> item = m_context.asset_manager->find_loaded_by_id(item_id);
    if (!item) {
        return make_error_content(fmt::format("No manager-known asset with item id {}", item_id));
    }
    if (!m_context.inventory_window->adopt_into_grid_slot(slot_index, item)) {
        return make_error_content("slot_index out of range or item is not a brush / material");
    }
    // The written v4 slot key is exactly make_key of the adopted item -
    // returned here so key-flip legs verify scope / path / uid without
    // reading the autosaved config file.
    return make_json_content({
        {"slot_index", slot_index},
        {"item_name",  item->get_name()},
        {"item_id",    item->get_id()},
        {"key",        asset_key_to_json(m_context.asset_manager->make_key(*item))}
    }).dump();
}

}
