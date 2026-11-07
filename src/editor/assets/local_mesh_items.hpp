#pragma once

#include "assets/item_keys.hpp"

#include "erhe_gltf/gltf.hpp"
#include "erhe_primitive/lod.hpp"
#include "erhe_primitive/prepared_primitive.hpp"
#include "erhe_primitive/primitive.hpp"

#include <optional>
#include <string>

namespace editor {

// Which items make_local_mesh_items() makes.
enum class Local_mesh_items_wanted : unsigned int
{
    mesh_and_bvh = 0,
    mesh,
    bvh
};

// The items of one shape; an empty optional for one not wanted or not made.
class Local_mesh_items
{
public:
    std::optional<erhe::gltf::Gltf_mesh_item>         mesh;
    std::optional<erhe::gltf::Gltf_raytrace_bvh_item> bvh;
    std::string                                       skip_reason; // the shape does not prepare: no item
    std::string                                       bvh_failure; // a BVH item was wanted and not made
};

// The preparation the items of a shape are made from: of its source content -
// a soup (its authoring soup when it has one, then out_authoring_shape holds
// that soup), or the triangles derived from its Geometry (the caller holds that
// Geometry's build lock). Any thread.
[[nodiscard]] auto prepare_shape_for_items(
    erhe::primitive::Primitive_render_shape&                shape,
    Local_mesh_source                                       source,
    std::optional<erhe::primitive::Primitive_render_shape>& out_authoring_shape
) -> erhe::primitive::Prepare_primitive_result;

// The Local-space mesh item and the Raytrace BVH item of one primitive shape,
// the one implementation of Export's Ensure and of the editor's own loading
// (doc/editor/prepared_assets.md "Export", "The editor's own loading"): the
// preparation of its source (a soup, or the triangles derived from its
// Geometry - the caller then holds that Geometry's build lock), the LOD chain
// of the request, write_mesh_item() and write_raytrace_bvh_item(). Any thread.
[[nodiscard]] auto make_local_mesh_items(
    erhe::primitive::Primitive_render_shape&            shape,
    Local_mesh_source                                   source,
    const std::optional<erhe::primitive::Lod_settings>& lod_settings,
    const erhe::gltf::Gltf_gpu_vertex_formats*          gpu_vertex_formats,
    Local_mesh_items_wanted                             wanted
) -> Local_mesh_items;

}
