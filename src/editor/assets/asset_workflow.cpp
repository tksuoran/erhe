#include "assets/asset_workflow.hpp"

#include "app_context.hpp"
#include "assets/asset_manager.hpp"
#include "brushes/brush.hpp"
#include "content_library/content_library.hpp"
#include "editor_log.hpp"
#include "graphics/texture_file_loader.hpp"
#include "operations/library_attach_operation.hpp"
#include "operations/operation_stack.hpp"
#include "parsers/gltf.hpp"
#include "scene/scene_root.hpp"

#include "erhe_file/file.hpp"
#include "erhe_gltf/gltf.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_verify/verify.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_scene/xform.hpp"

#include <fmt/format.h>

namespace editor {

auto write_material_container_file(
    const std::vector<std::shared_ptr<erhe::primitive::Material>>& materials,
    const std::filesystem::path&                                   path,
    const Gltf_image_source_provider&                              image_source_provider,
    std::string&                                                   out_error
) -> bool
{
    // Free root node with no children: the container carries materials
    // only, one empty scene keeps the file valid glTF.
    const std::shared_ptr<erhe::scene::Node> root_node = std::make_shared<erhe::scene::Xform>("asset container");
    const erhe::gltf::Gltf_export_arguments export_arguments{
        .root_node             = *root_node,
        .binary                = path.extension() != std::filesystem::path{".gltf"},
        .image_source_provider = image_source_provider,
        .extra_materials       = materials,
    };
    const std::string gltf = erhe::gltf::export_gltf(export_arguments);
    if (gltf.empty()) {
        out_error = fmt::format("material container export produced no data for '{}'", erhe::file::to_string(path));
        return false;
    }
    if (!erhe::file::write_file(path, gltf)) {
        out_error = fmt::format("failed to write material container '{}'", erhe::file::to_string(path));
        return false;
    }
    return true;
}

auto write_material_container_file(
    const std::vector<std::shared_ptr<erhe::primitive::Material>>& materials,
    const std::filesystem::path&                                   path,
    const std::shared_ptr<Content_library>&                        image_source_library,
    std::string&                                                   out_error
) -> bool
{
    return write_material_container_file(materials, path, make_gltf_image_source_provider(image_source_library), out_error);
}

auto make_gltf_data_image_source_provider(const erhe::gltf::Gltf_data& gltf_data) -> Gltf_image_source_provider
{
    using Source_map = std::unordered_map<const erhe::graphics::Texture*, std::shared_ptr<const erhe::gltf::Gltf_image_source>>;
    std::shared_ptr<Source_map> sources = std::make_shared<Source_map>();
    for (std::size_t i = 0, end = gltf_data.images.size(); i < end; ++i) {
        if (gltf_data.images[i] && (i < gltf_data.image_sources.size()) && gltf_data.image_sources[i]) {
            (*sources)[gltf_data.images[i].get()] = gltf_data.image_sources[i];
        }
    }
    return [sources](const erhe::graphics::Texture* texture) -> std::shared_ptr<const erhe::gltf::Gltf_image_source> {
        const auto it = sources->find(texture);
        return (it != sources->end()) ? it->second : std::shared_ptr<const erhe::gltf::Gltf_image_source>{};
    };
}

auto make_material_external(
    App_context&                                      context,
    Scene_root&                                       scene_root,
    const std::shared_ptr<erhe::primitive::Material>& material,
    const std::filesystem::path&                      path,
    std::string&                                      out_error
) -> bool
{
    if (context.asset_manager == nullptr) {
        out_error = "asset manager not available";
        return false;
    }
    ERHE_VERIFY(material);
    Asset_manager& asset_manager = *context.asset_manager;
    if (!scene_root.is_asset_definition(*material)) {
        out_error = fmt::format(
            "material '{}' is not a definition of scene '{}' (already a reference, or defined elsewhere)",
            material->get_name(), scene_root.get_name()
        );
        return false;
    }
    if (!asset_manager.can_bind_container_path(path, out_error)) {
        return false;
    }
    if (!path.parent_path().empty()) {
        static_cast<void>(erhe::file::ensure_directory_exists(path.parent_path()));
    }
    // Write first (the export stamps the uid the file key needs), then
    // re-home; a failed re-home leaves a harmless file on disk.
    if (!write_material_container_file({material}, path, scene_root.get_content_library(), out_error)) {
        return false;
    }
    if (!asset_manager.rehome_definition_to_container(material, path, out_error)) {
        return false;
    }
    // The material stays a resource prim of this scene; what changed is where
    // its DEFINITION lives, which rehome_definition_to_container recorded and
    // Scene_root::is_asset_definition now answers. Record the file key on the
    // library so the R6 exporter writes a proxy for it on the next scene save.
    const std::shared_ptr<Content_library> library = scene_root.get_content_library();
    if (library) {
        std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{library->mutex};
        if (library->has_item(*material)) {
            library->set_asset_key(material, asset_manager.make_key(*material));
        }
    }
    log_asset->info(
        "make-external: material '{}' of scene '{}' now lives in '{}'; the scene references it (save the scene to persist the reference)",
        material->get_name(), scene_root.get_name(), erhe::file::to_string(path)
    );
    return true;
}

auto make_material_internal(
    App_context&                                      context,
    Scene_root&                                       scene_root,
    const std::shared_ptr<erhe::primitive::Material>& material,
    std::string&                                      out_error
) -> std::shared_ptr<erhe::primitive::Material>
{
    if (context.asset_manager == nullptr) {
        out_error = "asset manager not available";
        return {};
    }
    ERHE_VERIFY(material);
    const std::shared_ptr<Content_library> library = scene_root.get_content_library();
    if (!library) {
        out_error = fmt::format("scene '{}' has no material library", scene_root.get_name());
        return {};
    }
    bool is_external_definition = false;
    {
        std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{library->mutex};
        is_external_definition = library->has_item(*material) && !scene_root.is_asset_definition(*material);
    }
    if (!is_external_definition) {
        out_error = fmt::format(
            "material '{}' is not defined by another container - make-internal de-links external definitions only",
            material->get_name(), scene_root.get_name()
        );
        return {};
    }

    // Scene-owned copy through the manager (single creation funnel). The
    // Item_base copy constructor intentionally does not copy the gltf uid:
    // the copy is a NEW asset, stamped on its first export.
    const std::shared_ptr<erhe::primitive::Material> copy =
        context.asset_manager->create<erhe::primitive::Material>(scene_root, *material);

    // De-link: every mesh primitive of THIS scene that used the shared
    // object now uses the copy. Other holders (slots, tools, other scenes)
    // deliberately keep the shared object.
    std::size_t swapped_count = 0;
    scene_root.get_scene().for_each_node([&](const std::shared_ptr<erhe::scene::Node>& node) {
        if (!node) {
            return true;
        }
        const std::shared_ptr<erhe::scene::Mesh> mesh = erhe::scene::get_mesh(node.get());
        if (!mesh) {
            return true;
        }
        const std::vector<erhe::scene::Mesh_primitive>& mesh_primitives = mesh->get_primitives();
        for (std::size_t i = 0, end = mesh_primitives.size(); i < end; ++i) {
            if (mesh_primitives[i].material == material) {
                mesh->set_primitive_material(i, copy);
                ++swapped_count;
            }
        }
        return true;
    });
    {
        std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{library->mutex};
        // Brushes hold their own material references; a brush pointing at
        // the shared object is left alone (placing it keeps sharing).
        for (const std::shared_ptr<Brush>& brush : library->get_all<Brush>()) {
            if (brush && (brush->get_material() == material)) {
                log_asset->warn(
                    "make-internal: brush '{}' still references the shared material '{}' (brush materials are not swapped)",
                    brush->get_name(), material->get_name()
                );
            }
        }
        static_cast<void>(library->remove(material));
        library->add(copy);
    }
    log_asset->info(
        "make-internal: scene '{}' now owns material '{}' (a copy; {} mesh primitives swapped); the shared object is de-linked",
        scene_root.get_name(), copy->get_name(), swapped_count
    );
    return copy;
}

auto reference_material_into_scene(
    App_context&     context,
    Scene_root&      scene_root,
    const Asset_key& key,
    std::string&     out_error
) -> std::shared_ptr<erhe::primitive::Material>
{
    if ((context.asset_manager == nullptr) || (context.operation_stack == nullptr)) {
        out_error = "asset manager / operation stack not available";
        return {};
    }
    Asset_manager& asset_manager = *context.asset_manager;
    const std::shared_ptr<erhe::Item_base> item = asset_manager.acquire(key, out_error);
    if (!item) {
        return {};
    }
    const std::shared_ptr<erhe::primitive::Material> material = std::dynamic_pointer_cast<erhe::primitive::Material>(item);
    if (!material) {
        out_error = fmt::format("{} resolved to a {}, not a material", key.describe(), item->get_type_name());
        return {};
    }
    // Plan resolution 11: only assets of path-bound containers are
    // cross-scene referenceable (a durable file key exists).
    if (!asset_manager.is_cross_scene_referenceable(*material)) {
        out_error = fmt::format(
            "material '{}' is defined in a session-only (never saved) scene - save that scene first",
            material->get_name()
        );
        return {};
    }
    const std::shared_ptr<Content_library> library = scene_root.get_content_library();
    if (!library) {
        out_error = fmt::format("scene '{}' has no material library", scene_root.get_name());
        return {};
    }
    {
        std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{library->mutex};
        if (library->has_item(*material)) {
            return material; // already listed
        }
    }
    const Asset_key stored_key = asset_manager.make_key(*material); // authoritative: file scope + uid self-heal
    // The material becomes a resource prim of this scene; its DEFINITION
    // stays in the container the key names, which is what makes the next
    // scene save write an R6 proxy for it (Scene_root::is_asset_definition).
    context.operation_stack->queue(
        make_library_attach_operation(
            context,
            library,
            material,
            Gltf_source_reference{
                .gltf_path  = stored_key.path,
                .item_name  = material->get_name(),
                .item_index = -1, // referenced by identity, not by position
                .item_type  = "material",
            },
            std::shared_ptr<erhe::gltf::Gltf_image_source>{},
            stored_key
        )
    );
    log_asset->info(
        "reference-into-scene: material '{}' ({}) listed in scene '{}'; its definition stays in the container",
        material->get_name(), stored_key.describe(), scene_root.get_name()
    );
    return material;
}

void import_texture_into_scene(
    App_context&                       context,
    const std::shared_ptr<Scene_root>& scene_root,
    const std::filesystem::path&       path
)
{
    if (!scene_root || (context.texture_file_loader == nullptr) || (context.operation_stack == nullptr)) {
        return;
    }
    const std::shared_ptr<Content_library> library = scene_root->get_content_library();
    if (!library) {
        log_asset->warn("import texture: scene '{}' has no texture library", scene_root->get_name());
        return;
    }
    // The load outlives this call; the scene may be closed before it lands.
    const std::weak_ptr<Scene_root> weak_scene_root = scene_root;
    App_context* const              context_ptr     = &context;
    const std::string               path_string     = path.generic_string();
    context.texture_file_loader->load_async(
        path,
        [context_ptr, weak_scene_root, path_string](const std::shared_ptr<erhe::graphics::Texture>& texture) {
            if (!texture) {
                log_asset->warn("import texture: '{}' could not be loaded", path_string);
                return;
            }
            const std::shared_ptr<Scene_root> target = weak_scene_root.lock();
            if (!target) {
                return; // scene closed while the image was loading
            }
            const std::shared_ptr<Content_library> target_library = target->get_content_library();
            if (!target_library) {
                return;
            }
            context_ptr->operation_stack->queue(
                make_library_attach_operation(
                    *context_ptr,
                    target_library,
                    texture,
                    Gltf_source_reference{
                        .gltf_path  = path_string,
                        .item_name  = texture->get_name(),
                        .item_index = -1, // a standalone file, not an index into a glTF
                        .item_type  = "texture",
                    }
                )
            );
            log_asset->info(
                "import texture: '{}' listed in scene '{}'",
                path_string, target->get_name()
            );
        }
    );
}

}
