#pragma once

#include "content_library/content_library.hpp"
#include "operations/operation.hpp"
#include "scene/generated/gltf_source_reference.hpp"

#include "erhe_profile/profile.hpp"

#include <fmt/format.h>

#include <memory>
#include <mutex>

namespace editor {

template <typename T>
class Content_library_attach_operation : public Operation
{
public:
    // is_reference: attach as a reference entry (an item owned elsewhere,
    // e.g. a prefab template resource shared by every instancing scene) that
    // never claims the item's Item_host. The default attaches an owning
    // entry hosted by the library's owner.
    Content_library_attach_operation(
        std::shared_ptr<Content_library>               content_library,
        std::shared_ptr<Content_library_node>          sublibrary,
        std::shared_ptr<T>                             item,
        Gltf_source_reference                          gltf_source,
        std::shared_ptr<erhe::gltf::Gltf_image_source> image_source = {},
        bool                                           is_reference = false,
        std::optional<Asset_key>                       asset_key    = {}
    )
        : m_content_library{std::move(content_library)}
        , m_sublibrary     {std::move(sublibrary)}
        , m_item           {std::move(item)}
        , m_gltf_source    {std::move(gltf_source)}
        , m_image_source   {std::move(image_source)}
        , m_is_reference   {is_reference}
        , m_asset_key      {std::move(asset_key)}
    {
        set_description(
            fmt::format(
                "[{}] Content_library_attach {} '{}'",
                get_serial(),
                m_gltf_source.item_type,
                m_gltf_source.item_name
            )
        );
    }

    void execute(App_context&) override
    {
        std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_content_library->mutex};
        m_sublibrary->add(m_item, m_gltf_source, m_image_source, m_is_reference, m_asset_key);
    }

    void undo(App_context&) override
    {
        std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_content_library->mutex};
        m_sublibrary->remove(m_item);
    }

private:
    std::shared_ptr<Content_library>               m_content_library;
    std::shared_ptr<Content_library_node>          m_sublibrary;
    std::shared_ptr<T>                             m_item;
    Gltf_source_reference                          m_gltf_source;
    std::shared_ptr<erhe::gltf::Gltf_image_source> m_image_source;
    bool                                           m_is_reference{false};
    std::optional<Asset_key>                       m_asset_key;
};

}
