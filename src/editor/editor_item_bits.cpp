#include "editor_item_bits.hpp"

#include "erhe_item/item.hpp"

namespace editor {

namespace {

// Labels as the library's c_bit_labels; persistent names as the ERHE_*
// extensions have always serialized them (doc/editor/gltf_scene_roundtrip.md
// phase 3), nullptr for the transient bits.
constexpr erhe::Item_flag_info c_editor_item_flags[] = {
    { Editor_item_flags::tool,                           "Tool",                           "tool"                      },
    { Editor_item_flags::brush,                          "Brush",                          "brush"                     },
    { Editor_item_flags::controller,                     "Controller",                     "controller"                },
    { Editor_item_flags::rendertarget,                   "Rendertarget",                   "rendertarget"              },
    { Editor_item_flags::expand,                         "Expand",                         "expand"                    },
    { Editor_item_flags::show_in_developer_ui,           "Show In Developer UI",           "show_in_developer_ui"      },
    { Editor_item_flags::show_debug_visualizations,      "Show Debug",                     "show_debug_visualizations" },
    { Editor_item_flags::lock_viewport_selection,        "Lock Selection",                 "lock_viewport_selection"   },
    { Editor_item_flags::lock_viewport_transform,        "Lock Transform",                 "lock_viewport_transform"   },
    { Editor_item_flags::invisible_parent,               "Invisible Parent",               "invisible_parent"          },
    { Editor_item_flags::render_wireframe,               "Render Wireframe",               "render_wireframe"          },
    { Editor_item_flags::render_bounding_volume,         "Render Bounding Volume",         "render_bounding_volume"    },
    { Editor_item_flags::hovered_in_viewport,            "Hovered in Viewport",            nullptr                     },
    { Editor_item_flags::hovered_in_item_tree,           "Hovered in Item Tree",           nullptr                     },
    { Editor_item_flags::descendant_hovered_in_viewport, "Descendant Hovered in Viewport", nullptr                     },
    { Editor_item_flags::hovered_in_graph,               "Hovered in Graph",               nullptr                     },
    { Editor_item_flags::child_hovered_in_graph,         "Child Hovered in Graph",         nullptr                     },
    { Editor_item_flags::ancestor_hovered_in_graph,      "Ancestor Hovered in Graph",      nullptr                     },
    { Editor_item_flags::affects_shadow,                 "Affects Shadow",                 nullptr                     },
    { Editor_item_flags::bone_proxy,                     "Bone Proxy",                     nullptr                     },
    { Editor_item_flags::active_item,                    "Active Item",                    nullptr                     },
    { Editor_item_flags::view_anchored,                  "View Anchored",                  nullptr                     }
};

// The C++ class names (the item trees name their drag and drop payloads by them).
constexpr erhe::Item_type_info c_editor_item_types[] = {
    { Editor_item_types::index_brush,                  "Brush"                  },
    { Editor_item_types::index_composer,               "Composer"               },
    { Editor_item_types::index_grid,                   "Grid"                   },
    { Editor_item_types::index_composition_pass,       "Composition_pass"       },
    { Editor_item_types::index_rendertarget,           "Rendertarget"           },
    { Editor_item_types::index_asset_folder,           "Asset_folder"           },
    { Editor_item_types::index_asset_file_gltf,        "Asset_file_gltf"        },
    { Editor_item_types::index_asset_file_geogram,     "Asset_file_geogram"     },
    { Editor_item_types::index_asset_file_other,       "Asset_file_other"       },
    { Editor_item_types::index_content_library_folder, "Content_library_folder" },
    { Editor_item_types::index_content_library_node,   "Content_library_node"   },
    { Editor_item_types::index_joint,                  "Joint"                  },
    { Editor_item_types::index_raytrace,               "Raytrace"               },
    { Editor_item_types::index_render_style,           "Render_style"           },
    { Editor_item_types::index_graph,                  "Graph"                  },
    { Editor_item_types::index_graph_link,             "Graph_link"             },
    { Editor_item_types::index_asset_file_scene,       "Asset_file_scene"       },
    { Editor_item_types::index_graph_texture,          "Graph_texture"          },
    { Editor_item_types::index_graph_mesh,             "Graph_mesh"             },
    { Editor_item_types::index_asset_file_texture,     "Asset_file_texture"     },
    { Editor_item_types::index_style,                  "Style"                  },
    { Editor_item_types::index_asset_file_usd,         "Asset_file_usd"         }
};

// A persistent editor flag bit as a bridged boolean of every item.
[[nodiscard]] auto register_flag_property(
    const std::string_view name,
    const uint64_t         bit,
    const std::string_view label,
    const std::string_view group,
    const std::string_view tooltip,
    const bool             developer_only
) -> erhe::property::Property<bool>
{
    return erhe::Item_base::register_flag_bit_property(
        name, erhe::Item_base::property_owner_type(), bit,
        erhe::property::Property_ui{.group = group, .tooltip = tooltip, .developer_only = developer_only, .label = label}
    );
}

} // anonymous namespace

const erhe::property::Property<bool> Editor_item_properties::lock_viewport_transform_property = register_flag_property(
    "lock_viewport_transform", Editor_item_flags::lock_viewport_transform, "Transform", "Locks", "Viewport transform tools leave the item alone", false
);
const erhe::property::Property<bool> Editor_item_properties::lock_viewport_selection_property = register_flag_property(
    "lock_viewport_selection", Editor_item_flags::lock_viewport_selection, "Selection", "Locks", "Viewport picking skips the item", false
);
const erhe::property::Property<bool> Editor_item_properties::show_debug_visualizations_property = register_flag_property(
    "show_debug_visualizations", Editor_item_flags::show_debug_visualizations, "Show Debug Visualizations", "", "", false
);
const erhe::property::Property<bool> Editor_item_properties::show_in_developer_ui_property = register_flag_property(
    "show_in_developer_ui", Editor_item_flags::show_in_developer_ui, "Show In Developer UI", "", "", true
);

void register_editor_item_bits()
{
    erhe::Item_flags::register_application_flags(
        c_editor_item_flags,
        Editor_item_flags::transient,
        Editor_item_flags::purpose_guide_when_set
    );
    erhe::Item_type::register_application_types(c_editor_item_types);
}

} // namespace editor
