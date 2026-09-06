// Mcp_server material editing tool (edit_material and its field / texture-slot helpers).
// Split out of mcp_server.cpp; shares helpers via mcp_server_shared.hpp.

#include "mcp/mcp_server.hpp"
#include "mcp/mcp_server_shared.hpp"

#include "app_context.hpp"
#include "app_scenes.hpp"
#include "assets/asset_manager.hpp"
#include "content_library/content_library.hpp"
#include "operations/compound_operation.hpp"
#include "operations/material_change_operation.hpp"
#include "operations/property_set_operation.hpp"
#include "operations/mesh_material_assign_operation.hpp"
#include "operations/operation.hpp"
#include "operations/operation_stack.hpp"
#include "preview/material_preview.hpp"
#include "scene/scene_root.hpp"
#include "texture_graph/graph_texture.hpp"

#include "erhe_graphics/device.hpp"
#include "erhe_graphics/sampler.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_item/item.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene.hpp"

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

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
    bool                                                               has_texture{false};
    std::shared_ptr<erhe::graphics::Texture>                           texture{};
    std::optional<erhe::primitive::Texgen_mode>                        texgen_mode{};
    std::optional<float>                                               rotation{};
    std::optional<glm::vec2>                                           offset{};
    std::optional<glm::vec2>                                           scale{};
    std::optional<std::array<erhe::graphics::Sampler_address_mode, 2>> wrap{};
    std::optional<erhe::graphics::Filter>                              min_filter{};
    std::optional<erhe::graphics::Filter>                              mag_filter{};

    [[nodiscard]] auto has_sampler_edit() const -> bool
    {
        return wrap.has_value() || min_filter.has_value() || mag_filter.has_value();
    }
};

[[nodiscard]] auto parse_address_mode(const json& ref, erhe::graphics::Sampler_address_mode& out, std::string& out_error) -> bool
{
    if (!ref.is_string()) {
        out_error = "wrap entries must be strings ('repeat', 'clamp_to_edge', 'mirrored_repeat')";
        return false;
    }
    const std::string s = ref.get<std::string>();
    if (s == "repeat")          { out = erhe::graphics::Sampler_address_mode::repeat;          return true; }
    if (s == "clamp_to_edge")   { out = erhe::graphics::Sampler_address_mode::clamp_to_edge;   return true; }
    if (s == "mirrored_repeat") { out = erhe::graphics::Sampler_address_mode::mirrored_repeat; return true; }
    out_error = "wrap must be one of 'repeat', 'clamp_to_edge', 'mirrored_repeat' (got '" + s + "')";
    return false;
}

[[nodiscard]] auto address_mode_name(const erhe::graphics::Sampler_address_mode mode) -> const char*
{
    switch (mode) {
        case erhe::graphics::Sampler_address_mode::repeat:          return "repeat";
        case erhe::graphics::Sampler_address_mode::clamp_to_edge:   return "clamp_to_edge";
        case erhe::graphics::Sampler_address_mode::mirrored_repeat: return "mirrored_repeat";
        default:                                                    return "?";
    }
}

[[nodiscard]] auto parse_filter(const json& ref, const char* key, erhe::graphics::Filter& out, std::string& out_error) -> bool
{
    if (!ref.is_string()) {
        out_error = std::string{key} + " must be a string ('nearest' or 'linear')";
        return false;
    }
    const std::string s = ref.get<std::string>();
    if (s == "nearest") { out = erhe::graphics::Filter::nearest; return true; }
    if (s == "linear")  { out = erhe::graphics::Filter::linear;  return true; }
    out_error = std::string{key} + " must be 'nearest' or 'linear' (got '" + s + "')";
    return false;
}

[[nodiscard]] auto filter_name(const erhe::graphics::Filter filter) -> const char*
{
    return (filter == erhe::graphics::Filter::linear) ? "linear" : "nearest";
}

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

    const auto tc_it = slot_json.find("texgen_mode");
    if (tc_it != slot_json.end()) {
        if (!tc_it->is_number_integer() && !tc_it->is_number_unsigned()) {
            out_error = "texgen_mode must be an integer";
            return false;
        }
        // erhe::primitive::Texgen_mode values: 0..2 = uv0..uv2, then
        // world_xy/xz/yz, node_xy/xz/yz, tangent. Reject out-of-range
        // values instead of wrapping the enum silently.
        constexpr std::int64_t k_max_texgen_mode = static_cast<std::int64_t>(erhe::primitive::Texgen_mode::tangent);
        const std::int64_t raw = tc_it->get<std::int64_t>();
        if (raw < 0 || raw > k_max_texgen_mode) {
            out_error = "texgen_mode must be in [0, " + std::to_string(k_max_texgen_mode) + "]";
            return false;
        }
        out_edit.texgen_mode = static_cast<erhe::primitive::Texgen_mode>(raw);
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

    const auto wrap_it = slot_json.find("wrap");
    if (wrap_it != slot_json.end()) {
        std::array<erhe::graphics::Sampler_address_mode, 2> wrap{};
        if (wrap_it->is_array()) {
            if (wrap_it->size() != 2) {
                out_error = "wrap must be a string or an array of 2 strings [u, v]";
                return false;
            }
            if (!parse_address_mode((*wrap_it)[0], wrap[0], out_error)) {
                return false;
            }
            if (!parse_address_mode((*wrap_it)[1], wrap[1], out_error)) {
                return false;
            }
        } else {
            // A single string applies to both axes.
            if (!parse_address_mode(*wrap_it, wrap[0], out_error)) {
                return false;
            }
            wrap[1] = wrap[0];
        }
        out_edit.wrap = wrap;
    }

    erhe::graphics::Filter filter_tmp{};
    const auto min_filter_it = slot_json.find("min_filter");
    if (min_filter_it != slot_json.end()) {
        if (!parse_filter(*min_filter_it, "min_filter", filter_tmp, out_error)) {
            return false;
        }
        out_edit.min_filter = filter_tmp;
    }
    const auto mag_filter_it = slot_json.find("mag_filter");
    if (mag_filter_it != slot_json.end()) {
        if (!parse_filter(*mag_filter_it, "mag_filter", filter_tmp, out_error)) {
            return false;
        }
        out_edit.mag_filter = filter_tmp;
    }
    return true;
}

void apply_slot_edit(const Slot_edit& edit, erhe::primitive::Material_texture_sampler& target)
{
    // Assigning (or nulling) a plain texture replaces any previous binding on
    // the slot, including a Graph_texture reference - the slot is a single
    // texture_reference.
    if (edit.has_texture)             target.texture_reference = edit.texture;
    if (edit.texgen_mode.has_value()) target.texgen_mode       = edit.texgen_mode.value();
    if (edit.rotation.has_value())    target.rotation          = edit.rotation.value();
    if (edit.offset.has_value())      target.offset            = edit.offset.value();
    if (edit.scale.has_value())       target.scale             = edit.scale.value();

    if (edit.wrap.has_value()) {
        target.sampler.wrap_u = edit.wrap.value()[0];
        target.sampler.wrap_v = edit.wrap.value()[1];
    }
    if (edit.min_filter.has_value()) {
        target.sampler.min_filter = edit.min_filter.value();
    }
    if (edit.mag_filter.has_value()) {
        target.sampler.mag_filter = edit.mag_filter.value();
    }
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
    if (edit.texgen_mode.has_value()) entry["texgen_mode"] = edit.texgen_mode.value();
    if (edit.rotation.has_value())    entry["rotation"]    = edit.rotation.value();
    if (edit.offset.has_value())      entry["offset"]      = {edit.offset->x, edit.offset->y};
    if (edit.scale.has_value())       entry["scale"]       = {edit.scale->x,  edit.scale->y};
    if (edit.wrap.has_value()) {
        entry["wrap"] = {address_mode_name(edit.wrap.value()[0]), address_mode_name(edit.wrap.value()[1])};
    }
    if (edit.min_filter.has_value()) entry["min_filter"] = filter_name(edit.min_filter.value());
    if (edit.mag_filter.has_value()) entry["mag_filter"] = filter_name(edit.mag_filter.value());
    return entry;
}

// Optional material fields shared by edit_material and create_material.
// Applies each present field of `args` to `after`, recording it in
// `applied`; returns an error message on invalid input. `library` is only
// needed for texture_samplers lookups.
[[nodiscard]] auto apply_material_fields(
    const json&                             args,
    const std::shared_ptr<Content_library>& library,
    erhe::primitive::Material_values&       after_values,
    erhe::primitive::Material_data&         after,
    json&                                   applied
) -> std::optional<std::string>
{
    glm::vec3   v3{};
    glm::vec2   v2{};
    float       f{};
    bool        b{};
    std::string field_err;

    auto clamp01 = [](float v) { return std::clamp(v, 0.0f, 1.0f); };
    auto clamp01_vec3 = [&](glm::vec3 v) { return glm::vec3{clamp01(v.x), clamp01(v.y), clamp01(v.z)}; };
    auto clamp01_vec2 = [&](glm::vec2 v) { return glm::vec2{clamp01(v.x), clamp01(v.y)}; };

    switch (try_read_vec3(args, "base_color", v3, field_err)) {
        case Field_status::Ok: {
            const glm::vec3 clamped = clamp01_vec3(v3);
            after_values.base_color = clamped;
            applied["base_color"] = {clamped.x, clamped.y, clamped.z};
            break;
        }
        case Field_status::Invalid:    return field_err;
        case Field_status::NotPresent: break;
    }
    switch (try_read_float(args, "opacity", f, field_err)) {
        case Field_status::Ok:         after_values.opacity = clamp01(f); applied["opacity"] = after_values.opacity; break;
        case Field_status::Invalid:    return field_err;
        case Field_status::NotPresent: break;
    }
    switch (try_read_vec2(args, "roughness", v2, field_err)) {
        case Field_status::Ok: {
            const glm::vec2 clamped = clamp01_vec2(v2);
            after_values.roughness = clamped;
            applied["roughness"] = {clamped.x, clamped.y};
            break;
        }
        case Field_status::Invalid:    return field_err;
        case Field_status::NotPresent: break;
    }
    switch (try_read_float(args, "metallic", f, field_err)) {
        case Field_status::Ok:         after_values.metallic = clamp01(f); applied["metallic"] = after_values.metallic; break;
        case Field_status::Invalid:    return field_err;
        case Field_status::NotPresent: break;
    }
    switch (try_read_float(args, "reflectance", f, field_err)) {
        case Field_status::Ok:         after_values.reflectance = clamp01(f); applied["reflectance"] = after_values.reflectance; break;
        case Field_status::Invalid:    return field_err;
        case Field_status::NotPresent: break;
    }
    switch (try_read_vec3(args, "emissive", v3, field_err)) {
        case Field_status::Ok: {
            // Emissive is HDR; floor at 0 but no upper clamp.
            const glm::vec3 clamped{std::max(0.0f, v3.x), std::max(0.0f, v3.y), std::max(0.0f, v3.z)};
            after_values.emissive = clamped;
            applied["emissive"] = {clamped.x, clamped.y, clamped.z};
            break;
        }
        case Field_status::Invalid:    return field_err;
        case Field_status::NotPresent: break;
    }
    switch (try_read_float(args, "ior", f, field_err)) {
        // Physically meaningful IORs sit in [1, 3] (vacuum .. diamond-ish);
        // clamp to that range, matching the Properties window slider.
        case Field_status::Ok:         after_values.ior = std::clamp(f, 1.0f, 3.0f); applied["ior"] = after_values.ior; break;
        case Field_status::Invalid:    return field_err;
        case Field_status::NotPresent: break;
    }
    switch (try_read_float(args, "transmission", f, field_err)) {
        case Field_status::Ok:         after_values.transmission = clamp01(f); applied["transmission"] = after_values.transmission; break;
        case Field_status::Invalid:    return field_err;
        case Field_status::NotPresent: break;
    }
    switch (try_read_float(args, "normal_texture_scale", f, field_err)) {
        case Field_status::Ok:         after_values.normal_texture_scale = f; applied["normal_texture_scale"] = f; break;
        case Field_status::Invalid:    return field_err;
        case Field_status::NotPresent: break;
    }
    switch (try_read_float(args, "occlusion_texture_strength", f, field_err)) {
        case Field_status::Ok:         after_values.occlusion_texture_strength = clamp01(f); applied["occlusion_texture_strength"] = after_values.occlusion_texture_strength; break;
        case Field_status::Invalid:    return field_err;
        case Field_status::NotPresent: break;
    }
    {
        const auto bxdf_it = args.find("bxdf_model");
        if (bxdf_it != args.end()) {
            if (!bxdf_it->is_string()) {
                return "bxdf_model must be a string";
            }
            const std::string s = bxdf_it->get<std::string>();
            if (s == "unlit") {
                after_values.bxdf_model = erhe::primitive::Bxdf_model::unlit;
            } else if (s == "isotropic_brdf") {
                after_values.bxdf_model = erhe::primitive::Bxdf_model::isotropic_brdf;
            } else if (s == "anisotropic_brdf") {
                after_values.bxdf_model = erhe::primitive::Bxdf_model::anisotropic_brdf;
            } else if (s == "anisotropic_slope") {
                after_values.bxdf_model = erhe::primitive::Bxdf_model::anisotropic_slope;
            } else if (s == "anisotropic_engine_ready") {
                after_values.bxdf_model = erhe::primitive::Bxdf_model::anisotropic_engine_ready;
            } else {
                return "bxdf_model must be one of 'unlit', 'isotropic_brdf', 'anisotropic_brdf', 'anisotropic_slope', 'anisotropic_engine_ready'";
            }
            applied["bxdf_model"] = s;
        }
    }
    {
        const auto blending_it = args.find("blending_mode");
        if (blending_it != args.end()) {
            if (!blending_it->is_string()) {
                return "blending_mode must be a string";
            }
            const std::string s = blending_it->get<std::string>();
            if (s == "opaque") {
                after_values.blending_mode = erhe::primitive::Material_blending_mode::opaque;
            } else if (s == "alpha_blend") {
                after_values.blending_mode = erhe::primitive::Material_blending_mode::alpha_blend;
            } else if (s == "multiply") {
                after_values.blending_mode = erhe::primitive::Material_blending_mode::multiply;
            } else if (s == "add") {
                after_values.blending_mode = erhe::primitive::Material_blending_mode::add;
            } else if (s == "subtract") {
                after_values.blending_mode = erhe::primitive::Material_blending_mode::subtract;
            } else if (s == "screen_door") {
                after_values.blending_mode = erhe::primitive::Material_blending_mode::screen_door;
            } else if (s == "alpha_test") {
                after_values.blending_mode = erhe::primitive::Material_blending_mode::alpha_test;
            } else {
                return "blending_mode must be one of 'opaque', 'alpha_blend', 'multiply', 'add', 'subtract', 'screen_door', 'alpha_test'";
            }
            applied["blending_mode"] = s;
        }
    }
    switch (try_read_float(args, "alpha_cutoff", f, field_err)) {
        case Field_status::Ok:         after_values.alpha_cutoff = clamp01(f); applied["alpha_cutoff"] = after_values.alpha_cutoff; break;
        case Field_status::Invalid:    return field_err;
        case Field_status::NotPresent: break;
    }
    if (try_read_bool(args, "use_circular_brushed_metal", b)) {
        after_values.use_circular_brushed_metal = b;
        applied["use_circular_brushed_metal"] = b;
    }
    if (try_read_bool(args, "use_aniso_control", b)) {
        after_values.use_aniso_control = b;
        applied["use_aniso_control"] = b;
    }

    const auto ts_it = args.find("texture_samplers");
    if (ts_it != args.end()) {
        if (!ts_it->is_object()) {
            return "texture_samplers must be an object";
        }

        if (!library || !library->textures) {
            return "Content library has no textures node (texture-sampler edits need a scene-hosted material)";
        }
        const auto& tex_list = library->get_all<erhe::graphics::Texture>();

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
                return std::string{slot.slot_name} + ": " + slot_error;
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

    return std::nullopt;
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
            for (const std::shared_ptr<erhe::primitive::Material>& mat : library->get_all<erhe::primitive::Material>()) {
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
        // R5.6: materials are not hosted; the manager records the defining
        // scene (null for loaded containers' materials, which live in no
        // scene - sampler edits then have no library, as before).
        if (m_context.asset_manager != nullptr) {
            const std::shared_ptr<Scene_root> defining_scene_root = m_context.asset_manager->get_defining_scene_root(*material);
            if (defining_scene_root) {
                library = defining_scene_root->get_content_library();
            }
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

        const auto& mat_list = library->get_all<erhe::primitive::Material>();
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

    const erhe::primitive::Material_values before_values = material->get_values();
    erhe::primitive::Material_values       after_values  = before_values;
    const erhe::primitive::Material_data   before        = material->data;
    erhe::primitive::Material_data         after         = before;

    json applied = json::object();
    const std::optional<std::string> field_error = apply_material_fields(args, library, after_values, after, applied);
    if (field_error.has_value()) {
        json r = make_text_content(field_error.value());
        r["isError"] = true;
        return r.dump();
    }

    if (applied.empty()) {
        json r = make_text_content("No editable material fields supplied");
        r["isError"] = true;
        return r.dump();
    }

    const bool values_changed   = !(after_values == before_values);
    const bool samplers_changed = !(after == before);
    if (!values_changed && !samplers_changed) {
        return make_json_content({
            {"name",     material->get_name()},
            {"id",       material->get_id()},
            {"applied",  applied},
            {"changed",  false}
        }).dump();
    }

    // Property fields as one Property_set_apply_operation (only the entries
    // that changed), texture slots as a Material_change_operation; both in
    // one undo step when both changed.
    Compound_operation::Parameters parameters;
    if (values_changed) {
        parameters.operations.push_back(
            std::make_shared<Property_set_apply_operation>(
                std::vector<std::shared_ptr<erhe::Item_base>>{material},
                erhe::property::Property_set::diff(
                    erhe::primitive::Material::to_property_set(before_values),
                    erhe::primitive::Material::to_property_set(after_values)
                )
            )
        );
    }
    if (samplers_changed) {
        parameters.operations.push_back(std::make_shared<Material_change_operation>(material, before, after));
    }
    if (parameters.operations.size() == 1) {
        m_context.operation_stack->queue(parameters.operations.front());
    } else {
        m_context.operation_stack->queue(std::make_shared<Compound_operation>(std::move(parameters)));
    }

    return make_json_content({
        {"name",    material->get_name()},
        {"id",      material->get_id()},
        {"applied", applied},
        {"changed", true}
    }).dump();
}

auto Mcp_server::action_create_material(const json& args) -> std::string
{
    const std::string scene_name = args.value("scene_name", "");
    const std::string name       = args.value("name", "");

    if (name.empty()) {
        json r = make_text_content("name is required");
        r["isError"] = true;
        return r.dump();
    }

    Scene_root* const scene_root = find_scene(scene_name);
    if (scene_root == nullptr) {
        json r = make_text_content("Scene not found: " + scene_name);
        r["isError"] = true;
        return r.dump();
    }
    const std::shared_ptr<Content_library> library = scene_root->get_content_library();
    if (!library || !library->materials) {
        json r = make_text_content("Scene has no material library: " + scene_name);
        r["isError"] = true;
        return r.dump();
    }

    // Refuse duplicate names: edit_material addresses materials by name, so
    // a second material with the same name would make both unaddressable.
    // The existing id is returned so the caller can reuse or rename.
    for (const auto& mat : library->get_all<erhe::primitive::Material>()) {
        if (mat->get_name() == name) {
            json r = make_text_content("Material name already exists: " + name);
            r["isError"]     = true;
            r["existing_id"] = mat->get_id();
            return r.dump();
        }
    }

    erhe::primitive::Material_values values{};
    erhe::primitive::Material_data   data{};
    json applied = json::object();
    const std::optional<std::string> field_error = apply_material_fields(args, library, values, data, applied);
    if (field_error.has_value()) {
        json r = make_text_content(field_error.value());
        r["isError"] = true;
        return r.dump();
    }

    std::shared_ptr<erhe::primitive::Material> material;
    {
        std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{library->mutex};
        material = library->make<erhe::primitive::Material>(
            erhe::primitive::Material_create_info{
                .name   = name,
                .values = values,
                .data   = data
            }
        );
    }
    if (!material) {
        json r = make_text_content("Material creation failed");
        r["isError"] = true;
        return r.dump();
    }

    // Like copy_library_item, library-item creation is not undoable.
    return make_json_content({
        {"name",    material->get_name()},
        {"id",      material->get_id()},
        {"scene",   scene_root->get_name()},
        {"applied", applied}
    }).dump();
}

// Assign a material to one mesh primitive - the MCP equivalent of dragging a
// material from the library onto a mesh (doc/draw_list_material_set_plan.md,
// phase 1). Two things make this a test surface rather than a convenience:
//
// - it goes through Mesh::set_primitive_material(), the one writer of a
//   primitive's material (R4), so every notification the editor raises for a
//   drag-drop is raised here too;
// - it renders the material preview once, exactly as an open Properties
//   window would. That is what reproduced the reported bug deterministically
//   while the bug existed: the preview's own material buffer update rewrote
//   the single mutable slot on the shared Material, and the draw-list flush
//   later in the same frame wrote cached records from it. Slots are per
//   Material_set now, so the preview cannot reach another root's records at
//   all - the step stays because the regression test must keep exercising the
//   path it was written against, rather than depending on which windows
//   happen to be open.
//
// Undoable, like the gesture: the assignment goes onto the operation stack
// through Mesh_material_assign_operation, so an MCP assignment can be undone
// with the same Ctrl+Z the user would press after a drag-drop. execute_now
// keeps the tool's contract that the result it reports is already applied.
auto Mcp_server::action_assign_mesh_material(const json& args) -> std::string
{
    const std::string scene_name      = args.value("scene_name", "");
    const std::size_t primitive_index = args.value("primitive_index", std::size_t{0});

    Scene_root* const scene_root = find_scene(scene_name);
    if (scene_root == nullptr) {
        return make_error_content("Scene not found: " + scene_name);
    }

    const std::shared_ptr<erhe::scene::Node> node = find_node_in_scene(*scene_root, args, "mesh_id", "mesh_name");
    if (!node) {
        return make_error_content("Mesh node not found in scene: " + scene_name);
    }
    const std::shared_ptr<erhe::scene::Mesh> mesh = erhe::scene::get_mesh(node.get());
    if (!mesh) {
        return make_error_content("Node has no mesh: " + node->get_name());
    }
    const std::vector<erhe::scene::Mesh_primitive>& primitives = mesh->get_primitives();
    if (primitive_index >= primitives.size()) {
        return make_error_content(
            "primitive_index " + std::to_string(primitive_index) + " out of range for mesh '" +
            mesh->get_name() + "' (" + std::to_string(primitives.size()) + " primitives)"
        );
    }

    // Material resolution mirrors edit_material: the id path reaches any
    // scene's library and the asset manager (cross-scene assignment is a real
    // gesture - dragging one scene's material onto another scene's mesh), the
    // name path looks in the target scene's own library.
    const std::size_t material_id   = args.value("material_id", std::size_t{0});
    const std::string material_name = args.value("material_name", "");
    std::shared_ptr<erhe::primitive::Material> material;
    if (material_id != 0) {
        material = find_material_by_id(material_id);
        if (!material) {
            return make_error_content("Material not found with id: " + std::to_string(material_id));
        }
    } else if (!material_name.empty()) {
        const std::shared_ptr<Content_library> library = scene_root->get_content_library();
        if (!library || !library->materials) {
            return make_error_content("Scene has no material library: " + scene_name);
        }
        std::vector<std::size_t> matching_ids;
        for (const std::shared_ptr<erhe::primitive::Material>& mat : library->get_all<erhe::primitive::Material>()) {
            if (mat->get_name() == material_name) {
                if (!material) {
                    material = mat;
                }
                matching_ids.push_back(mat->get_id());
            }
        }
        if (!material) {
            return make_error_content("Material not found: " + material_name);
        }
        if (matching_ids.size() > 1) {
            json r = make_text_content(
                "Material name '" + material_name + "' matches " +
                std::to_string(matching_ids.size()) + " materials"
            );
            r["isError"]       = true;
            r["candidate_ids"] = matching_ids;
            return r.dump();
        }
    } else {
        return make_error_content("material_name or material_id is required");
    }

    const std::shared_ptr<erhe::primitive::Material> previous = primitives[primitive_index].material;
    const bool changed = (previous != material);

    // Mesh::set_primitive_material stays the one writer (R4) - the operation
    // calls it, so everything downstream (the scene host hooks, the draw-list
    // re-register) is raised exactly as for a drag-drop.
    const std::shared_ptr<Mesh_material_assign_operation> operation =
        make_mesh_material_assign_operation(mesh, primitive_index, material);
    if (operation && (m_context.operation_stack != nullptr)) {
        m_context.operation_stack->execute_now(operation);
    }

    // Render the preview, the way an open Properties window would.
    bool preview_rendered = false;
    if (args.value("render_preview", true) &&
        (m_context.material_preview != nullptr) &&
        (m_context.graphics_device != nullptr) &&
        (m_context.current_command_buffer != nullptr))
    {
        // Take the preview off any external thumbnail target first, then give
        // it a small one of its own: Thumbnails renders into a caller-supplied
        // texture, and this must not scribble into whichever one was last used.
        m_context.material_preview->set_color_texture({});
        m_context.material_preview->resize(64, 64);
        m_context.material_preview->update_rendertarget(*m_context.graphics_device);
        m_context.material_preview->render_preview(material);
        preview_rendered = true;
    }

    // node_id / node_name echo what mesh_id / mesh_name addressed (the node
    // carrying the mesh, the same convention get_draw_lists uses); mesh is the
    // mesh item's own name, which can differ from the node's.
    json result = {
        {"scene",            scene_root->get_name()},
        {"node_name",        node->get_name()},
        {"node_id",          node->get_id()},
        {"mesh",             mesh->get_name()},
        {"primitive_index",  primitive_index},
        {"material",         material->get_name()},
        {"material_id",      material->get_id()},
        {"changed",          changed},
        {"preview_rendered", preview_rendered}
    };
    if (previous) {
        result["previous_material"]    = previous->get_name();
        result["previous_material_id"] = previous->get_id();
    }
    return make_json_content(result).dump();
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
