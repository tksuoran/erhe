#include "operations/library_attach_operation.hpp"

#include "app_context.hpp"
#include "content_library/content_library.hpp"
#include "editor_log.hpp"
#include "operations/compound_operation.hpp"
#include "operations/item_insert_remove_operation.hpp"
#include "operations/kind_scope_operation.hpp"
#include "operations/operation.hpp"

#include "erhe_item/hierarchy.hpp"
#include "erhe_item/item.hpp"
#include "erhe_item/scope.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_verify/verify.hpp"

#include <fmt/format.h>

#include <mutex>

namespace editor {

auto make_library_attach_operation(
    App_context&                                          context,
    const std::shared_ptr<Content_library>&               content_library,
    const std::shared_ptr<erhe::Item_base>&               item,
    const Gltf_source_reference&                          gltf_source,
    const std::shared_ptr<erhe::gltf::Gltf_image_source>& image_source,
    const std::optional<Asset_key>&                       asset_key,
    const std::shared_ptr<erhe::Hierarchy>&               parent
) -> std::shared_ptr<Operation>
{
    ERHE_VERIFY(content_library);
    ERHE_VERIFY(item);

    content_library->set_gltf_source(item, gltf_source);
    if (image_source) {
        content_library->set_image_source(item, image_source);
    }
    if (asset_key.has_value()) {
        content_library->set_asset_key(item, asset_key.value());
    }

    if (!parent) {
        return make_library_insert_operation(context, content_library, item);
    }

    const std::shared_ptr<erhe::Hierarchy> prim = std::dynamic_pointer_cast<erhe::Hierarchy>(item);
    ERHE_VERIFY(prim);

    return std::make_shared<Item_insert_remove_operation>(
        Item_insert_remove_operation::Parameters{
            .context = context,
            .item    = prim,
            .parent  = parent,
            .mode    = Item_insert_remove_operation::Mode::insert
        }
    );
}

auto make_library_insert_operation(
    App_context&                            context,
    const std::shared_ptr<Content_library>& content_library,
    const std::shared_ptr<erhe::Item_base>& item
) -> std::shared_ptr<Operation>
{
    ERHE_VERIFY(content_library);
    ERHE_VERIFY(item);

    const std::shared_ptr<erhe::Hierarchy> prim = std::dynamic_pointer_cast<erhe::Hierarchy>(item);
    ERHE_VERIFY(prim);

    const uint64_t kind_type_bit = Content_library::get_kind_type_bit(*item);
    const std::shared_ptr<erhe::Scope> existing_scope = content_library->get_existing_scope(kind_type_bit);
    const std::shared_ptr<erhe::Scope> scope = existing_scope
        ? existing_scope
        : content_library->make_kind_scope(kind_type_bit);
    ERHE_VERIFY(scope);

    const std::shared_ptr<Operation> insert = std::make_shared<Item_insert_remove_operation>(
        Item_insert_remove_operation::Parameters{
            .context = context,
            .item    = prim,
            .parent  = std::static_pointer_cast<erhe::Hierarchy>(scope),
            .mode    = Item_insert_remove_operation::Mode::insert
        }
    );
    // The question is whether the scope STANDS IN THE TREE, not whether the
    // object exists: a scope made for an operation that has not executed yet
    // is already the kind's, and the operations built against it each carry
    // the step that places it. Placing it twice and removing it twice are
    // both no-ops (see `Kind_scope_operation`), so whichever of them executes
    // first places it and whichever is undone last removes it.
    if (scope->get_parent().lock()) {
        return insert;
    }

    Compound_operation::Parameters parameters{};
    parameters.operations.push_back(
        std::make_shared<Kind_scope_operation>(
            Kind_scope_operation::Parameters{
                .scope  = scope,
                .parent = content_library->get_prim_root()
            }
        )
    );
    parameters.operations.push_back(insert);
    return std::make_shared<Compound_operation>(std::move(parameters));
}


}
