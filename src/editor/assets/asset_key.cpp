#include "assets/asset_key.hpp"

#include "content_library/content_library.hpp"

#include "erhe_item/item.hpp"

#include <fmt/format.h>

#include <array>
#include <functional>

namespace editor {

auto c_str(const Asset_scope scope) -> const char*
{
    switch (scope) {
        case Asset_scope::scene_local: return "scene_local";
        case Asset_scope::builtin:     return "builtin";
        case Asset_scope::file:        return "file";
        case Asset_scope::none:
        default:                       return "none";
    }
}

auto c_str(const Asset_type type) -> const char*
{
    switch (type) {
        case Asset_type::brush:     return "brush";
        case Asset_type::material:  return "material";
        case Asset_type::animation: return "animation";
        case Asset_type::mesh:      return "mesh";
        case Asset_type::none:
        default:                    return "none";
    }
}

auto parse_asset_scope(const std::string_view text) -> Asset_scope
{
    if (text == "scene_local") { return Asset_scope::scene_local; }
    if (text == "builtin")     { return Asset_scope::builtin; }
    if (text == "file")        { return Asset_scope::file; }
    return Asset_scope::none;
}

auto parse_asset_type(const std::string_view text) -> Asset_type
{
    if (text == "brush")     { return Asset_type::brush; }
    if (text == "material")  { return Asset_type::material; }
    if (text == "animation") { return Asset_type::animation; }
    if (text == "mesh")      { return Asset_type::mesh; }
    return Asset_type::none;
}

auto Asset_key::is_empty() const -> bool
{
    return (scope == Asset_scope::none) && (type == Asset_type::none) && path.empty() && uid.empty() && name.empty();
}

auto Asset_key::describe() const -> std::string
{
    std::string description = fmt::format("{}:{}", c_str(scope), c_str(type));
    if (!path.empty()) {
        description += fmt::format(" path='{}'", path);
    }
    if (!uid.empty()) {
        description += fmt::format(" uid='{}'", uid);
    }
    if (!name.empty()) {
        description += fmt::format(" name='{}'", name);
    }
    return description;
}

auto Asset_key_hash::operator()(const Asset_key& key) const -> std::size_t
{
    const std::hash<std::string> string_hash{};
    std::size_t seed = static_cast<std::size_t>(key.scope) * 31u + static_cast<std::size_t>(key.type);
    const auto combine = [&seed](const std::size_t value) {
        seed ^= value + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2);
    };
    combine(string_hash(key.path));
    combine(string_hash(key.uid));
    combine(string_hash(key.name));
    return seed;
}

namespace {

const std::array<Asset_type_info, 4> c_asset_type_infos{
    Asset_type_info{Asset_type::brush,     "brush",     erhe::Item_type::brush,     &Content_library::brushes   },
    Asset_type_info{Asset_type::material,  "material",  erhe::Item_type::material,  &Content_library::materials },
    Asset_type_info{Asset_type::animation, "animation", erhe::Item_type::animation, &Content_library::animations},
    Asset_type_info{Asset_type::mesh,      "mesh",      erhe::Item_type::mesh,      nullptr                     }
};

}

auto get_asset_type_infos() -> std::span<const Asset_type_info>
{
    return c_asset_type_infos;
}

auto get_asset_type_info(const Asset_type type) -> const Asset_type_info*
{
    for (const Asset_type_info& info : c_asset_type_infos) {
        if (info.type == type) {
            return &info;
        }
    }
    return nullptr;
}

auto asset_type_from_item(const erhe::Item_base& item) -> Asset_type
{
    const uint64_t item_type = item.get_type();
    for (const Asset_type_info& info : c_asset_type_infos) {
        if ((item_type & info.item_type_bit) == info.item_type_bit) {
            return info.type;
        }
    }
    return Asset_type::none;
}

auto is_manager_owned_asset_type(const Asset_type type) -> bool
{
    return (type == Asset_type::brush) || (type == Asset_type::material) || (type == Asset_type::animation);
}

}
