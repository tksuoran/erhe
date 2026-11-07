#include "assets/local_mesh_items.hpp"

#include "erhe_primitive/prepared_primitive.hpp"
#include "erhe_primitive/primitive.hpp"
#include "erhe_primitive/triangle_soup.hpp"

#include <memory>

namespace editor {

auto prepare_shape_for_items(
    erhe::primitive::Primitive_render_shape&                shape,
    const Local_mesh_source                                 source,
    std::optional<erhe::primitive::Primitive_render_shape>& out_authoring_shape
) -> erhe::primitive::Prepare_primitive_result
{
    // A core loaded from an item holds its vertices in the GPU formats of that
    // item, which round some attributes: items are made from the source
    // content their key hashes, the authoring soup (doc/erhe/primitive.md "Authoring soup").
    if ((source == Local_mesh_source::soup) && shape.has_authoring_soup()) {
        const std::shared_ptr<erhe::primitive::Triangle_soup> authoring = shape.get_authoring_triangle_soup();
        if (!authoring) {
            erhe::primitive::Prepare_primitive_result result{};
            result.skip_reason = "the authoring soup cannot be read";
            return result;
        }
        out_authoring_shape.emplace(authoring);
        const std::optional<erhe::hash::Content_hash> content_hash = shape.get_content_hash();
        if (content_hash.has_value()) {
            out_authoring_shape->set_content_hash(content_hash.value());
        }
        return erhe::primitive::prepare_primitive_for_export(out_authoring_shape.value());
    }
    return (source == Local_mesh_source::soup)
        ? erhe::primitive::prepare_primitive_for_export(shape)
        : erhe::primitive::prepare_derived_primitive_for_export(shape);
}

auto make_local_mesh_items(
    erhe::primitive::Primitive_render_shape&            shape,
    const Local_mesh_source                             source,
    const std::optional<erhe::primitive::Lod_settings>& lod_settings,
    const erhe::gltf::Gltf_gpu_vertex_formats* const    gpu_vertex_formats,
    const Local_mesh_items_wanted                       wanted
) -> Local_mesh_items
{
    Local_mesh_items items{};
    std::optional<erhe::primitive::Primitive_render_shape> authoring_shape;
    erhe::primitive::Prepare_primitive_result result = prepare_shape_for_items(shape, source, authoring_shape);
    erhe::primitive::Primitive_render_shape& prepared_shape = authoring_shape.has_value() ? authoring_shape.value() : shape;
    if (!result.prepared.has_value()) {
        items.skip_reason = std::move(result.skip_reason);
        return items;
    }
    erhe::primitive::Prepared_primitive_export& prepared = result.prepared.value();
    if (wanted != Local_mesh_items_wanted::bvh) {
        if (lod_settings.has_value()) {
            static_cast<void>(erhe::primitive::make_export_lod_chain(prepared, prepared_shape, lod_settings.value()));
        }
        items.mesh = erhe::gltf::write_mesh_item(prepared, gpu_vertex_formats);
    }
    if (wanted != Local_mesh_items_wanted::mesh) {
        if (prepared.bvh.has_value()) {
            items.bvh = erhe::gltf::write_raytrace_bvh_item(prepared.bvh.value());
        } else {
            items.bvh_failure = "the preparation built no BVH";
        }
    }
    return items;
}

}
