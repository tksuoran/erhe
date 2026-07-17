// Mcp_server material editing tool (edit_material and its field / texture-slot helpers).
// Split out of mcp_server.cpp; shares helpers via mcp_server_shared.hpp.

#include "mcp/mcp_server_shared.hpp"

#include "assets/asset_manager.hpp"

namespace editor {

using namespace mcp_server_detail;

namespace {

// Read helpers return a tri-state so the caller can distinguish "the
// field was not in the JSON" from "the field was present but invalid
// (wrong type, NaN, Inf)". The Invalid branch carries a human-readable
// error in out_error so it can flow directly into a JSON-RPC error
// response.
enum class Field_status
{
    NotPresent,
    Ok,
    Invalid
};

[[nodiscard]] auto try_read_vec3(const json& obj, const char* key, glm::vec3& out, std::string& out_error) -> Field_status
{
    const auto it = obj.find(key);
    if (it == obj.end()) {
        return Field_status::NotPresent;
    }
    if (!it->is_array() || it->size() < 3) {
        out_error = std::string{key} + " must be an array of 3 finite numbers";
        return Field_status::Invalid;
    }
    const json& a = *it;
    if (!a[0].is_number() || !a[1].is_number() || !a[2].is_number()) {
        out_error = std::string{key} + " entries must be numbers";
        return Field_status::Invalid;
    }
    const float x = a[0].get<float>();
    const float y = a[1].get<float>();
    const float z = a[2].get<float>();
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) {
        out_error = std::string{key} + " entries must be finite (got NaN or Inf)";
        return Field_status::Invalid;
    }
    out.x = x;
    out.y = y;
    out.z = z;
    return Field_status::Ok;
}

[[nodiscard]] auto try_read_vec2(const json& obj, const char* key, glm::vec2& out, std::string& out_error) -> Field_status
{
    const auto it = obj.find(key);
    if (it == obj.end()) {
        return Field_status::NotPresent;
    }
    if (it->is_number()) {
        const float v = it->get<float>();
        if (!std::isfinite(v)) {
            out_error = std::string{key} + " must be finite (got NaN or Inf)";
            return Field_status::Invalid;
        }
        out.x = v;
        out.y = v;
        return Field_status::Ok;
    }
    if (it->is_array() && it->size() >= 2 && (*it)[0].is_number() && (*it)[1].is_number()) {
        const float x = (*it)[0].get<float>();
        const float y = (*it)[1].get<float>();
        if (!std::isfinite(x) || !std::isfinite(y)) {
            out_error = std::string{key} + " entries must be finite (got NaN or Inf)";
            return Field_status::Invalid;
        }
        out.x = x;
        out.y = y;
        return Field_status::Ok;
    }
    out_error = std::string{key} + " must be a number or an array of 2 finite numbers";
    return Field_status::Invalid;
}

[[nodiscard]] auto try_read_float(const json& obj, const char* key, float& out, std::string& out_error) -> Field_status
{
    const auto it = obj.find(key);
    if (it == obj.end()) {
        return Field_status::NotPresent;
    }
    if (!it->is_number()) {
        out_error = std::string{key} + " must be a number";
        return Field_status::Invalid;
    }
    const float v = it->get<float>();
    if (!std::isfinite(v)) {
        out_error = std::string{key} + " must be finite (got NaN or Inf)";
        return Field_status::Invalid;
    }
    out = v;
    return Field_status::Ok;
}

[[nodiscard]] auto try_read_bool(const json& obj, const char* key, bool& out) -> bool
{
    const auto it = obj.find(key);
    if (it == obj.end() || !it->is_boolean()) {
        return false;
    }
    out = it->get<bool>();
    return true;
}

class Slot_edit
{
public:
    bool                                     has_texture{false};
    std::shared_ptr<erhe::graphics::Texture> texture{};
    std::optional<uint32_t>                  tex_coord{};
    std::optional<float>                     rotation{};
    std::optional<glm::vec2>                 offset{};
    std::optional<glm::vec2>                 scale{};
};

[[nodiscard]] auto find_texture_in_library(
    const std::vector<std::shared_ptr<erhe::graphics::Texture>>& tex_list,
    const json&                                                  ref,
    std::shared_ptr<erhe::graphics::Texture>&                    out_texture,
    std::string&                                                 out_error
) -> bool
{
    if (ref.is_number_integer() || ref.is_number_unsigned()) {
        const std::size_t target_id = ref.get<std::size_t>();
        for (const auto& tex : tex_list) {
            if (tex->get_id() == target_id) {
                out_texture = tex;
                return true;
            }
        }
        out_error = "Texture id not found in content library: " + std::to_string(target_id);
        return false;
    }
    if (ref.is_string()) {
        const std::string target_name = ref.get<std::string>();
        std::shared_ptr<erhe::graphics::Texture> first_match;
        std::vector<std::size_t> matching_ids;
        for (const auto& tex : tex_list) {
            if (tex->get_name() == target_name) {
                if (!first_match) {
                    first_match = tex;
                }
                matching_ids.push_back(tex->get_id());
            }
        }
        if (!first_match) {
            out_error = "Texture name not found in content library: " + target_name;
            return false;
        }
        if (matching_ids.size() > 1) {
            out_error = "Texture name '" + target_name + "' matches " +
                        std::to_string(matching_ids.size()) + " textures (ids:";
            for (const std::size_t id : matching_ids) {
                out_error += " " + std::to_string(id);
            }
            out_error += "); reference by id to disambiguate";
            return false;
        }
        out_texture = first_match;
        return true;
    }
    out_error = "Texture reference must be a string (name), integer (id), or null (clear)";
    return false;
}

[[nodiscard]] auto parse_slot_edit(
    const json&                                                  slot_json,
    const std::vector<std::shared_ptr<erhe::graphics::Texture>>& tex_list,
    Slot_edit&                                                   out_edit,
    std::string&                                                 out_error
) -> bool
{
    if (!slot_json.is_object()) {
        out_error = "Texture slot entry must be an object";
        return false;
    }

    const auto tex_it = slot_json.find("texture");
    if (tex_it != slot_json.end()) {
        out_edit.has_texture = true;
        if (tex_it->is_null()) {
            out_edit.texture.reset();
        } else if (!find_texture_in_library(tex_list, *tex_it, out_edit.texture, out_error)) {
            return false;
        }
    }

    const auto tc_it = slot_json.find("tex_coord");
    if (tc_it != slot_json.end()) {
        if (!tc_it->is_number_integer() && !tc_it->is_number_unsigned()) {
            out_error = "tex_coord must be an integer";
            return false;
        }
        // Forward-looking field: Material_texture_sampler::tex_coord
        // is stored on the material, but the current standard shader
        // reads v_texcoord (vertex stream a_texcoord_0 only). Accept
        // 0 or 1 so a future multi-UV shader does not need a config
        // bump, and reject larger values explicitly instead of
        // wrapping uint32_t silently.
        constexpr std::int64_t k_max_tex_coord = 1;
        const std::int64_t raw = tc_it->get<std::int64_t>();
        if (raw < 0 || raw > k_max_tex_coord) {
            out_error = "tex_coord must be in [0, " + std::to_string(k_max_tex_coord) + "]";
            return false;
        }
        out_edit.tex_coord = static_cast<uint32_t>(raw);
    }

    float f_tmp{};
    switch (try_read_float(slot_json, "rotation", f_tmp, out_error)) {
        case Field_status::Ok:         out_edit.rotation = f_tmp; break;
        case Field_status::Invalid:    return false;
        case Field_status::NotPresent: break;
    }
    glm::vec2 v2_tmp{};
    switch (try_read_vec2(slot_json, "offset", v2_tmp, out_error)) {
        case Field_status::Ok:         out_edit.offset = v2_tmp; break;
        case Field_status::Invalid:    return false;
        case Field_status::NotPresent: break;
    }
    switch (try_read_vec2(slot_json, "scale", v2_tmp, out_error)) {
        case Field_status::Ok:         out_edit.scale = v2_tmp; break;
        case Field_status::Invalid:    return false;
        case Field_status::NotPresent: break;
    }
    return true;
}

void apply_slot_edit(const Slot_edit& edit, erhe::primitive::Material_texture_sampler& target)
{
    // Assigning (or nulling) a plain texture replaces any previous binding on
    // the slot, including a Graph_texture reference - the slot is a single
    // texture_reference.
    if (edit.has_texture)         target.texture_reference = edit.texture;
    if (edit.tex_coord.has_value()) target.tex_coord = edit.tex_coord.value();
    if (edit.rotation.has_value()) target.rotation  = edit.rotation.value();
    if (edit.offset.has_value())   target.offset    = edit.offset.value();
    if (edit.scale.has_value())    target.scale     = edit.scale.value();
}

[[nodiscard]] auto slot_edit_summary(const Slot_edit& edit) -> json
{
    json entry = json::object();
    if (edit.has_texture) {
        if (edit.texture) {
            entry["texture_id"]   = edit.texture->get_id();
            entry["texture_name"] = edit.texture->get_name();
        } else {
            entry["texture_id"]   = nullptr;
            entry["texture_name"] = nullptr;
        }
    }
    if (edit.tex_coord.has_value()) entry["tex_coord"] = edit.tex_coord.value();
    if (edit.rotation.has_value())  entry["rotation"]  = edit.rotation.value();
    if (edit.offset.has_value())    entry["offset"]    = {edit.offset->x, edit.offset->y};
    if (edit.scale.has_value())     entry["scale"]     = {edit.scale->x,  edit.scale->y};
    return entry;
}

} // anonymous namespace

auto Mcp_server::find_material_by_id(const std::size_t material_id) -> std::shared_ptr<erhe::primitive::Material>
{
    if (m_context.app_scenes != nullptr) {
        for (const std::shared_ptr<Scene_root>& scene_root : m_context.app_scenes->get_scene_roots()) {
            const std::shared_ptr<Content_library> library = scene_root->get_content_library();
            if (!library || !library->materials) {
                continue;
            }
            for (const std::shared_ptr<erhe::primitive::Material>& mat : library->materials->get_all<erhe::primitive::Material>()) {
                if (mat->get_id() == material_id) {
                    return mat;
                }
            }
        }
    }
    if (m_context.asset_manager != nullptr) {
        return std::dynamic_pointer_cast<erhe::primitive::Material>(m_context.asset_manager->find_loaded_by_id(material_id));
    }
    return {};
}

auto Mcp_server::action_edit_material(const json& args) -> std::string
{
    if (m_context.operation_stack == nullptr) {
        json r = make_text_content("Operation stack not available");
        r["isError"] = true;
        return r.dump();
    }

    const std::string scene_name    = args.value("scene_name", "");
    const std::string material_name = args.value("material_name", "");
    const std::size_t material_id   = args.value("material_id", std::size_t{0});

    std::shared_ptr<erhe::primitive::Material> material;
    std::shared_ptr<Content_library>           library;
    if (material_id != 0) {
        // The id path (unique item ids): reaches materials in any scene's
        // library AND the asset manager's loaded containers, which live in
        // no scene. Texture-sampler edits need a scene library for texture
        // lookup, so they resolve against the material's hosting scene when
        // it has one.
        material = find_material_by_id(material_id);
        if (!material) {
            json r = make_text_content("Material not found with id: " + std::to_string(material_id));
            r["isError"] = true;
            return r.dump();
        }
        erhe::Item_host* const item_host = material->get_item_host();
        if ((m_context.app_scenes != nullptr) && (item_host != nullptr) && m_context.app_scenes->is_host_registered(item_host)) {
            library = static_cast<Scene_root*>(item_host)->get_content_library();
        }
    } else {
        Scene_root* sr = find_scene(scene_name);
        if (sr == nullptr) {
            json r = make_text_content("Scene not found: " + scene_name);
            r["isError"] = true;
            return r.dump();
        }

        library = sr->get_content_library();
        if (!library || !library->materials) {
            json r = make_text_content("No materials in scene: " + scene_name);
            r["isError"] = true;
            return r.dump();
        }

        const auto& mat_list = library->materials->get_all<erhe::primitive::Material>();
        std::vector<std::size_t> matching_ids;
        for (const auto& mat : mat_list) {
            if (mat->get_name() == material_name) {
                if (!material) {
                    material = mat;
                }
                matching_ids.push_back(mat->get_id());
            }
        }
        if (!material) {
            json r = make_text_content("Material not found: " + material_name);
            r["isError"] = true;
            return r.dump();
        }
        if (matching_ids.size() > 1) {
            // Ambiguous: refuse to mutate. Return the candidate ids so the
            // caller can re-issue with a disambiguating material_id.
            json r = make_text_content(
                "Material name '" + material_name + "' matches " +
                std::to_string(matching_ids.size()) + " materials"
            );
            r["isError"]      = true;
            r["candidate_ids"] = matching_ids;
            return r.dump();
        }
    }

    if (material->is_lock_edit()) {
        json r = make_text_content("Material is locked: " + material_name);
        r["isError"] = true;
        return r.dump();
    }

    const erhe::primitive::Material_data before = material->data;
    erhe::primitive::Material_data       after  = before;

    json applied = json::object();

    glm::vec3   v3{};
    glm::vec2   v2{};
    float       f{};
    bool        b{};
    std::string field_err;

    auto reject = [](const std::string& msg) -> std::string {
        json r = make_text_content(msg);
        r["isError"] = true;
        return r.dump();
    };

    auto clamp01 = [](float v) { return std::clamp(v, 0.0f, 1.0f); };
    auto clamp01_vec3 = [&](glm::vec3 v) { return glm::vec3{clamp01(v.x), clamp01(v.y), clamp01(v.z)}; };
    auto clamp01_vec2 = [&](glm::vec2 v) { return glm::vec2{clamp01(v.x), clamp01(v.y)}; };

    switch (try_read_vec3(args, "base_color", v3, field_err)) {
        case Field_status::Ok: {
            const glm::vec3 clamped = clamp01_vec3(v3);
            after.base_color = clamped;
            applied["base_color"] = {clamped.x, clamped.y, clamped.z};
            break;
        }
        case Field_status::Invalid:    return reject(field_err);
        case Field_status::NotPresent: break;
    }
    switch (try_read_float(args, "opacity", f, field_err)) {
        case Field_status::Ok:         after.opacity = clamp01(f); applied["opacity"] = after.opacity; break;
        case Field_status::Invalid:    return reject(field_err);
        case Field_status::NotPresent: break;
    }
    switch (try_read_vec2(args, "roughness", v2, field_err)) {
        case Field_status::Ok: {
            const glm::vec2 clamped = clamp01_vec2(v2);
            after.roughness = clamped;
            applied["roughness"] = {clamped.x, clamped.y};
            break;
        }
        case Field_status::Invalid:    return reject(field_err);
        case Field_status::NotPresent: break;
    }
    switch (try_read_float(args, "metallic", f, field_err)) {
        case Field_status::Ok:         after.metallic = clamp01(f); applied["metallic"] = after.metallic; break;
        case Field_status::Invalid:    return reject(field_err);
        case Field_status::NotPresent: break;
    }
    switch (try_read_float(args, "reflectance", f, field_err)) {
        case Field_status::Ok:         after.reflectance = clamp01(f); applied["reflectance"] = after.reflectance; break;
        case Field_status::Invalid:    return reject(field_err);
        case Field_status::NotPresent: break;
    }
    switch (try_read_vec3(args, "emissive", v3, field_err)) {
        case Field_status::Ok: {
            // Emissive is HDR; floor at 0 but no upper clamp.
            const glm::vec3 clamped{std::max(0.0f, v3.x), std::max(0.0f, v3.y), std::max(0.0f, v3.z)};
            after.emissive = clamped;
            applied["emissive"] = {clamped.x, clamped.y, clamped.z};
            break;
        }
        case Field_status::Invalid:    return reject(field_err);
        case Field_status::NotPresent: break;
    }
    switch (try_read_float(args, "normal_texture_scale", f, field_err)) {
        case Field_status::Ok:         after.normal_texture_scale = f; applied["normal_texture_scale"] = f; break;
        case Field_status::Invalid:    return reject(field_err);
        case Field_status::NotPresent: break;
    }
    switch (try_read_float(args, "occlusion_texture_strength", f, field_err)) {
        case Field_status::Ok:         after.occlusion_texture_strength = clamp01(f); applied["occlusion_texture_strength"] = after.occlusion_texture_strength; break;
        case Field_status::Invalid:    return reject(field_err);
        case Field_status::NotPresent: break;
    }
    {
        const auto bxdf_it = args.find("bxdf_model");
        if (bxdf_it != args.end()) {
            if (!bxdf_it->is_string()) {
                json r = make_text_content("bxdf_model must be a string");
                r["isError"] = true;
                return r.dump();
            }
            const std::string s = bxdf_it->get<std::string>();
            if (s == "unlit") {
                after.bxdf_model = erhe::primitive::Bxdf_model::unlit;
            } else if (s == "isotropic_brdf") {
                after.bxdf_model = erhe::primitive::Bxdf_model::isotropic_brdf;
            } else if (s == "anisotropic_brdf") {
                after.bxdf_model = erhe::primitive::Bxdf_model::anisotropic_brdf;
            } else if (s == "anisotropic_slope") {
                after.bxdf_model = erhe::primitive::Bxdf_model::anisotropic_slope;
            } else if (s == "anisotropic_engine_ready") {
                after.bxdf_model = erhe::primitive::Bxdf_model::anisotropic_engine_ready;
            } else {
                json r = make_text_content("bxdf_model must be one of 'unlit', 'isotropic_brdf', 'anisotropic_brdf', 'anisotropic_slope', 'anisotropic_engine_ready'");
                r["isError"] = true;
                return r.dump();
            }
            applied["bxdf_model"] = s;
        }
    }
    if (try_read_bool(args, "use_circular_brushed_metal", b)) {
        after.use_circular_brushed_metal = b;
        applied["use_circular_brushed_metal"] = b;
    }
    if (try_read_bool(args, "use_aniso_control", b)) {
        after.use_aniso_control = b;
        applied["use_aniso_control"] = b;
    }

    const auto ts_it = args.find("texture_samplers");
    if (ts_it != args.end()) {
        if (!ts_it->is_object()) {
            json r = make_text_content("texture_samplers must be an object");
            r["isError"] = true;
            return r.dump();
        }

        if (!library || !library->textures) {
            json r = make_text_content("Content library has no textures node (texture-sampler edits need a scene-hosted material)");
            r["isError"] = true;
            return r.dump();
        }
        const auto& tex_list = library->textures->get_all<erhe::graphics::Texture>();

        struct Named_slot
        {
            const char*                                slot_name;
            erhe::primitive::Material_texture_sampler* target;
        };
        const Named_slot slots[] = {
            {"base_color",         &after.texture_samplers.base_color},
            {"metallic_roughness", &after.texture_samplers.metallic_roughness},
            {"normal",             &after.texture_samplers.normal},
            {"occlusion",          &after.texture_samplers.occlusion},
            {"emissive",           &after.texture_samplers.emissive}
        };

        std::vector<std::pair<const Named_slot*, Slot_edit>> parsed_edits;
        parsed_edits.reserve(std::size(slots));

        for (const Named_slot& slot : slots) {
            const auto slot_it = ts_it->find(slot.slot_name);
            if (slot_it == ts_it->end()) {
                continue;
            }
            Slot_edit  edit{};
            std::string slot_error{};
            if (!parse_slot_edit(*slot_it, tex_list, edit, slot_error)) {
                json r = make_text_content(std::string{slot.slot_name} + ": " + slot_error);
                r["isError"] = true;
                return r.dump();
            }
            parsed_edits.emplace_back(&slot, std::move(edit));
        }

        json applied_textures = json::object();
        for (auto& [slot, edit] : parsed_edits) {
            apply_slot_edit(edit, *slot->target);
            applied_textures[slot->slot_name] = slot_edit_summary(edit);
        }
        if (!applied_textures.empty()) {
            applied["texture_samplers"] = applied_textures;
        }
    }

    if (applied.empty()) {
        json r = make_text_content("No editable material fields supplied");
        r["isError"] = true;
        return r.dump();
    }

    if (after == before) {
        return make_json_content({
            {"name",     material->get_name()},
            {"id",       material->get_id()},
            {"applied",  applied},
            {"changed",  false}
        }).dump();
    }

    m_context.operation_stack->queue(
        std::make_shared<Material_change_operation>(material, before, after)
    );

    return make_json_content({
        {"name",    material->get_name()},
        {"id",      material->get_id()},
        {"applied", applied},
        {"changed", true}
    }).dump();
}

auto Mcp_server::action_copy_library_item(const json& args) -> std::string
{
    const std::string item_type    = args.value("item_type", "material");
    const std::string item_name    = args.value("item_name", "");
    const std::string source_scene = args.value("source_scene", "");
    const std::string target_scene = args.value("target_scene", "");

    Scene_root* const source = find_scene(source_scene);
    if (source == nullptr) {
        json r = make_text_content("Source scene not found: " + source_scene);
        r["isError"] = true;
        return r.dump();
    }
    Scene_root* const target = find_scene(target_scene);
    if (target == nullptr) {
        json r = make_text_content("Target scene not found: " + target_scene);
        r["isError"] = true;
        return r.dump();
    }
    if (source == target) {
        json r = make_text_content("Source and target are the same scene");
        r["isError"] = true;
        return r.dump();
    }

    const std::shared_ptr<Content_library> source_library = source->get_content_library();
    const std::shared_ptr<Content_library> target_library = target->get_content_library();
    if (!source_library || !target_library) {
        json r = make_text_content("Scene has no content library");
        r["isError"] = true;
        return r.dump();
    }

    const auto pick_folder = [](Content_library& library, const std::string& type) -> std::shared_ptr<Content_library_node> {
        if (type == "material")         return library.materials;
        if (type == "brush")            return library.brushes;
        if (type == "physics_material") return library.physics_materials;
        if (type == "collision_filter") return library.collision_filters;
        if (type == "physics_joint")    return library.physics_joints;
        return {};
    };
    const std::shared_ptr<Content_library_node> source_folder = pick_folder(*source_library.get(), item_type);
    const std::shared_ptr<Content_library_node> target_folder = pick_folder(*target_library.get(), item_type);
    if (!source_folder || !target_folder) {
        json r = make_text_content(
            "Unsupported item_type '" + item_type + "' - supported: material, brush, physics_material, "
            "collision_filter, physics_joint (textures and graph assets cannot be copied across scenes)"
        );
        r["isError"] = true;
        return r.dump();
    }

    std::shared_ptr<erhe::Item_base> found{};
    std::size_t                      match_count = 0;
    {
        std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{source_library->mutex};
        source_folder->for_each<Content_library_node>(
            [&found, &match_count, &item_name](Content_library_node& node) -> bool {
                if (node.item && (node.item->get_name() == item_name)) {
                    if (!found) {
                        found = node.item;
                    }
                    ++match_count;
                }
                return true;
            }
        );
    }
    if (!found) {
        json r = make_text_content("Item not found in " + source_scene + ": " + item_name);
        r["isError"] = true;
        return r.dump();
    }
    if (match_count > 1) {
        json r = make_text_content(
            "Item name '" + item_name + "' matches " + std::to_string(match_count) + " items in " + source_scene
        );
        r["isError"] = true;
        return r.dump();
    }

    // Copy, never alias: each library owns its items (item host = the
    // owning scene). Brushes get a payload-sharing copy; other types clone.
    std::shared_ptr<erhe::Item_base> copy{};
    {
        std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{target_library->mutex};
        copy = copy_library_item_to_library(found, *target_library.get());
    }
    if (!copy) {
        json r = make_text_content(
            std::string{found->get_type_name()} + " '" + item_name + "' is not copyable"
        );
        r["isError"] = true;
        return r.dump();
    }

    return make_json_content({
        {"name",         copy->get_name()},
        {"id",           copy->get_id()},
        {"type",         std::string{copy->get_type_name()}},
        {"target_scene", target->get_name()}
    }).dump();
}


} // namespace editor
