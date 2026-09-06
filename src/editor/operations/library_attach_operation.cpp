#include "operations/library_attach_operation.hpp"

#include "app_context.hpp"
#include "content_library/content_library.hpp"
#include "editor_log.hpp"
#include "operations/item_insert_remove_operation.hpp"
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

    const std::shared_ptr<erhe::Hierarchy> prim = std::dynamic_pointer_cast<erhe::Hierarchy>(item);
    ERHE_VERIFY(prim);
    const std::shared_ptr<erhe::Hierarchy> insert_parent = parent
        ? parent
        : std::static_pointer_cast<erhe::Hierarchy>(content_library->get_scope(Content_library::get_kind_type_bit(*item)));
    ERHE_VERIFY(insert_parent);

    return std::make_shared<Item_insert_remove_operation>(
        Item_insert_remove_operation::Parameters{
            .context = context,
            .item    = prim,
            .parent  = insert_parent,
            .mode    = Item_insert_remove_operation::Mode::insert
        }
    );
}


}
