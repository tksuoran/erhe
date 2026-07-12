#include "erhe_gltf/gltf_item_flags.hpp"

#include "erhe_item/item.hpp"

namespace erhe::gltf {

namespace {

class Serialized_item_flag
{
public:
    uint64_t         bit;
    std::string_view name;
};

constexpr Serialized_item_flag c_persistent_item_flags[] = {
    { erhe::Item_flags::no_message,                "no_message"                },
    { erhe::Item_flags::no_transform_update,       "no_transform_update"       },
    { erhe::Item_flags::transform_world_normative, "transform_world_normative" },
    { erhe::Item_flags::show_in_ui,                "show_in_ui"                },
    { erhe::Item_flags::show_debug_visualizations, "show_debug_visualizations" },
    { erhe::Item_flags::shadow_cast,               "shadow_cast"               },
    { erhe::Item_flags::lock_viewport_selection,   "lock_viewport_selection"   },
    { erhe::Item_flags::lock_viewport_transform,   "lock_viewport_transform"   },
    { erhe::Item_flags::visible,                   "visible"                   },
    { erhe::Item_flags::invisible_parent,          "invisible_parent"          },
    { erhe::Item_flags::render_wireframe,          "render_wireframe"          },
    { erhe::Item_flags::render_bounding_volume,    "render_bounding_volume"    },
    { erhe::Item_flags::content,                   "content"                   },
    { erhe::Item_flags::id,                        "id"                        },
    { erhe::Item_flags::tool,                      "tool"                      },
    { erhe::Item_flags::brush,                     "brush"                     },
    { erhe::Item_flags::controller,                "controller"                },
    { erhe::Item_flags::rendertarget,              "rendertarget"              },
    { erhe::Item_flags::expand,                    "expand"                    },
    { erhe::Item_flags::lock_edit,                 "lock_edit"                 },
    { erhe::Item_flags::show_in_developer_ui,      "show_in_developer_ui"      },
    { erhe::Item_flags::exclude_from_prefab,       "exclude_from_prefab"       }
};

} // anonymous namespace

auto persistent_item_flags_to_json(const uint64_t flag_bits) -> std::string
{
    std::string out{"["};
    const char* separator = "";
    for (const Serialized_item_flag& flag : c_persistent_item_flags) {
        if ((flag_bits & flag.bit) != 0) {
            out += separator;
            out += '"';
            out += flag.name;
            out += '"';
            separator = ",";
        }
    }
    out += "]";
    return out;
}

auto persistent_item_flag_from_name(const std::string_view name) -> uint64_t
{
    for (const Serialized_item_flag& flag : c_persistent_item_flags) {
        if (name == flag.name) {
            return flag.bit;
        }
    }
    return 0;
}

void apply_persistent_item_flags(erhe::Item_base& item, const uint64_t listed_bits)
{
    for (const Serialized_item_flag& flag : c_persistent_item_flags) {
        if ((listed_bits & flag.bit) != 0) {
            item.enable_flag_bits(flag.bit);
        } else {
            item.disable_flag_bits(flag.bit);
        }
    }
}

}
